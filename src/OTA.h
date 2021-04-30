#pragma once

// define a struct to carry the RC data with derivatives

struct rcPlusDerivatives_s {
    unsigned int header : 8;
    unsigned int ch0    : 10;
    unsigned int ch1    : 10;
    unsigned int ch2    : 10;
    unsigned int ch3    : 10;
    int ch0Velocity     : 10;
    int ch0Acceleration : 10;
    int ch1Velocity     : 10;
    int ch1Acceleration : 10;
    unsigned int sw0     : 2;
    unsigned int swIndex : 3;
    unsigned int swValue : 2;
} __attribute__ ((__packed__));

typedef rcPlusDerivatives_s rcPlusDerivatives_t;

#define RCD_BUFFERSIZE (sizeof(rcPlusDerivatives_t)+1) // plus one for the 8 bit CRC