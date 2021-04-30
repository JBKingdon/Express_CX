/** ----------------------------------------------------------------------------------------
 *  1AUD Filter
 * 
 *  Designed by Chris Thompson
 * 
 *  Implementation by James Kingdon
 * 
 *  The 1AUD filter is a dynamic filter that builds on the 1 Euro filter.
 *  Compared to 1E the 1AUD uses stacked PT1 filters as input to the derivative, enabling
 *  the use of higher derivative filter cut-off frequencies (and therefore faster tracking 
 *  of D), and stacked PT1 filters for the main filter, providing stronger (2nd order) 
 *  filtering of the input signal. Slew limiting of the input is also implemented.
 * 
 *  This software is released under the MIT license:
 * 
 *  Copyright 2020 C Thompson, J Kingdon
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of this 
 *  software and associated documentation files (the "Software"), to deal in the Software 
 *  without restriction, including without limitation the rights to use, copy, modify, merge, 
 *  publish, distribute, sublicense, and/or sell copies of the Software, and to permit 
 *  persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all copies or 
 *  substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
 *  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR 
 *  PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE 
 *  FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, 
 *  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
 *  THE SOFTWARE.
 * ----------------------------------------------------------------------------------------
*/

// #include <Arduino.h>
#include "1AUDfilterInt.h"

#include <stdio.h>
#include <stdlib.h>

DoublePT1filterInt::DoublePT1filterInt()
{
    current1 = 0;
    current2 = 0;
}

void DoublePT1filterInt::setInitial(const int32_t x)
{
    current1 = x;
    current2 = x;
}

int32_t DoublePT1filterInt::update(const int32_t x)
{
    // factor of 2^K_SHIFT_BITS to compensate for resolution multiplier on the stored k

    // Note that the cast to long is required to prevent overflow in the intermediate value
    // TODO since the cast didn't seem to add too much time to the calculation, try using longs throughout
    current1 = current1 + (((long)k * (x - current1)) >> K_SHIFT_BITS);
    current2 = current2 + (((long)k * (current1 - current2)) >> K_SHIFT_BITS);
    return current2;
}

int32_t DoublePT1filterInt::getCurrent()
{
    return current2;
}

void DoublePT1filterInt::setK(const uint32_t newK)
{
    k = newK;
}

uint32_t DoublePT1filterInt::getK()
{
    return k;
}

// ------------------------------------------------------------------------------------------------

/**
 *  Note that the cutoff frequencies must be less than sampleRate/(2Pi) in order for the filter
 * to remain stable
 */
OneAUDfilterInt::OneAUDfilterInt(const uint32_t _minFreq, const uint32_t _maxFreq, const uint32_t _inverseBeta, const uint32_t _sampleRate, 
                                 const uint32_t _dCutoff, const uint32_t _maxSlew, const int32_t initialValue)
{
    // save the params
    minFreq = _minFreq;
    maxFreq = _maxFreq;
    inverseBeta = _inverseBeta;
    sampleRate = _sampleRate;
    maxSlewPerSecond = _maxSlew;
    dCutoff = _dCutoff;

    // calculate derived values
    // kScale = FAST2PI / _sampleRate; kscale is just an optimisation, leave it out for now
    maxSlewPerSample = _maxSlew / _sampleRate; // we can probably live with integer slew per sample

    // other setup
    prevInput = initialValue;
    const uint32_t scaledIV = initialValue * PT_SCALE;
    dFilt.setInitial(scaledIV);
    oFilt.setInitial(scaledIV);
    prevSmoothed = scaledIV;

    // set k for the derivative (with a scaling factor to allow for storing in an int)
    const uint32_t k = dCutoff * TWO_PI_SHIFTED / sampleRate;

    // Serial.printf("d k %u\n", k);
    dFilt.setK(k);

    // Set k for the output filter
    const uint32_t kOut = minFreq * TWO_PI_SHIFTED / sampleRate;
    // Serial.printf("kOut %u\n", kOut);
    oFilt.setK(kOut);

    // TODO need to validate that maxFreq gives a stable value of k for the given sample rate
}

// reset the filter with new tunable parameters
void OneAUDfilterInt::setNewFilterParams(const uint32_t _minFreq, const uint32_t _maxFreq, const uint32_t _inverseBeta)
{
    // save the params
    minFreq = _minFreq;
    maxFreq = _maxFreq;
    inverseBeta = _inverseBeta;

    // Set k for the output filter
    const uint32_t kOut = minFreq * TWO_PI_SHIFTED / sampleRate;
    // Serial.printf("kOut %u\n", kOut);
    oFilt.setK(kOut);

    // TODO need to validate that maxFreq gives a stable value of k for the given sample rate
}


void OneAUDfilterInt::setSampleRate(const uint32_t newSampleRate)
{
    sampleRate = newSampleRate;
    maxSlewPerSample = maxSlewPerSecond / sampleRate;

    uint32_t kD = dCutoff * TWO_PI_SHIFTED / sampleRate;
    // make sure kD is reasonable TODO figure this out again
    // if (kD >= 1000) {
    //     kD = 990;
    // }
    dFilt.setK(kD);

    // The output PT1s get a new K at each update, so no need to set here
}

int32_t slewLimitHelper(const int32_t x, const int32_t prevX, const int32_t maxSlew)
{
    const int32_t s = x - prevX;

    if (s > maxSlew) {
        return prevX + maxSlew;
    } else if ((-s) > maxSlew) {
        return prevX - maxSlew;
    }

    return x;
}


// calculate a modified value of x that respects the slew limit
int32_t OneAUDfilterInt::slewLimit(const int32_t x)
{
    // static uint32_t avAbsSlew = 0;

    // printf("max sps %lu\n",  maxSlewPerSample);

    if (maxSlewPerSample == 0) {
        return x;
    }

    return slewLimitHelper(x, prevInput, maxSlewPerSample);

    // const int32_t s = x - prevInput;

    // // avAbsSlew -= avAbsSlew/8;
    // // if (s>0) {
    // //     avAbsSlew += s/8;
    // // } else {
    // //     avAbsSlew += (-s)/8;
    // // }

    // if (s > (int32_t)maxSlewPerSample) {
    //     // Serial.printf("%d > %d (av %d)\n", s, maxSlewPerSample, avAbsSlew/8);
    //     return prevInput + maxSlewPerSample;
    // } else if ((-s) > (int32_t)maxSlewPerSample) {
    //     // Serial.printf("%d < -%d (av %d)\n", s, maxSlewPerSample, avAbsSlew/8);
    //     return prevInput - maxSlewPerSample;
    // }

    // return x;
}

// update the filter with a new value
// scaling is crucial to retain precision while avoiding overflow
// TODO figure out the range of input values that don't overflow
// XXX TESTING returning dx instead of the new current value
int32_t OneAUDfilterInt::update(const int32_t newValue)
{
    // slew limit the input
    const int32_t limitedNew = slewLimit(newValue);

    // apply the derivative filter to the input
    const int32_t df = dFilt.update(limitedNew * PT_SCALE);

    // get differential of filtered input
    const int32_t dx = (df - prevSmoothed) * sampleRate;
    const int32_t absDx = dx >= 0 ? dx : -dx;
    
    // printf("dx, abdsDx %d %d\n", dx, absDx);

    // update prevSmoothed
    prevSmoothed = df;

    // ## adjust cutoff upwards using dx and Beta
    uint32_t fMain = (minFreq) + (absDx / (inverseBeta * PT_SCALE)); 
    // Serial.printf("dx %d fMain %d ", absDx, fMain);
    if (fMain > (maxFreq)) {
        fMain = maxFreq;
    }

    // printf("%d\n", fMain/PT_SCALE);

    // ## get the k value for the cutoff 
    // kCutoff = 2*PI*cutoff/sampleRate. 
    const uint32_t kMain = fMain * TWO_PI_SHIFTED / sampleRate;
    oFilt.setK(kMain);

    // printf("kMain %u\n", kMain);

    // apply the main filter to the input
    const int32_t mf = oFilt.update(limitedNew * PT_SCALE);

    // update the previous value
    prevInput = limitedNew; // XXX originally newValue, but that allows the fixed point math to overflow at startup

    return mf / PT_SCALE;
    // return dx / PT_SCALE;
}

// get the current filtered value
int32_t OneAUDfilterInt::getCurrent()
{
    return oFilt.getCurrent() / PT_SCALE;
}

float OneAUDfilterInt::getCutoff()
{
    return (float(oFilt.getK()) * sampleRate) / TWO_PI_SHIFTED;
}


// ------------------------------------------------------------------------------------------------


DifferentiatingFilter::DifferentiatingFilter(const uint32_t inputcutOffHz, const uint32_t outputcutOffHz, const uint32_t sampleRateHz)
{
    const uint32_t kInput = inputcutOffHz * TWO_PI_SHIFTED / sampleRateHz;
    const uint32_t kOutput = outputcutOffHz * TWO_PI_SHIFTED / sampleRateHz;

    inputFilter.setK(kInput);
    outputFilter.setK(kOutput);    

    sampleRate = sampleRateHz;
    invocationCount = 0;
}

// int32_t DifferentiatingFilter::update(const int32_t x)
// {
//     #define DX_SCALE 64; // chosen to avoid overflowing the filter

//     int32_t inputMaxSlew = 1000 * PT_SCALE;
//     int32_t outputMaxSlew = 4000 * PT_SCALE;

//     if (invocationCount < 8192) {
//         // scale down the skew limits while the filter is initialising
//         inputMaxSlew  /= 1024 - (invocationCount/8);
//         outputMaxSlew /= 1024 - (invocationCount/8);
//         invocationCount++;
//     }

//     const int32_t prevSmoothed = inputFilter.getCurrent();

//     const int32_t limitedX = slewLimitHelper(x * PT_SCALE, prevSmoothed, inputMaxSlew); // last value is max slew per sample

//     // printf("lx %ld\n", limitedX);

//     const int32_t newSmoothed = inputFilter.update(limitedX);

//     const int32_t dX = (newSmoothed - prevSmoothed) * DX_SCALE;

//     const int32_t prevDX = outputFilter.getCurrent();
//     const int32_t limitedDX = slewLimitHelper(dX, prevDX, outputMaxSlew);

//     return outputFilter.update(limitedDX) / PT_SCALE;
// }


int32_t DifferentiatingFilter::update(const int32_t x)
{
    // #define DX_SCALE 16384;

    int32_t outputMaxSlew = 20000;

    if (invocationCount < 8192) {
        // scale down the skew limits while the filter is initialising
        outputMaxSlew /= 1024 - (invocationCount/8);
        invocationCount++;
    }

    const int32_t dX = x;

    const int32_t prevDX = outputFilter.getCurrent();
    const int32_t limitedDX = slewLimitHelper(dX, prevDX, outputMaxSlew);

    return outputFilter.update(limitedDX);
}


int32_t DifferentiatingFilter::getCurrent()
{
    return outputFilter.getCurrent();
}

int32_t DifferentiatingFilter::getInfilter()
{
    return inputFilter.getCurrent() / PT_SCALE;
}
