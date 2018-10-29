/***********************************************************************************
* Demodulation mode definitions
*
* Copyright 2018 Frank DD4WH, Louis McCarthy AI0LM
* 
* GNU GPL LICENSE v3 (See LICENSE file)
************************************************************************************/
#ifndef __DEMOD_H_
#define __DEMOD_H_

// out of the nine implemented AM detectors, only
// two proved to be of acceptable quality:
// AM2 and AM_ME2
// however, SAM outperforms all demodulators ;-)
#define       DEMOD_MIN           0
#define       DEMOD_MAX           6

#define       DEMOD_USB           0
#define       DEMOD_LSB           1
#define       DEMOD_AM2           2
#define       DEMOD_AM_ME2        26
#define       DEMOD_SAM          3 // synchronous AM demodulation
#define       DEMOD_SAM_USB      27 // synchronous AM demodulation
#define       DEMOD_SAM_LSB      28 // synchronous AM demodulation
#define       DEMOD_SAM_STEREO   4 // SAM, with pseude-stereo effect
#define       DEMOD_IQ           5
#define       DEMOD_WFM          6
#define       DEMOD_DCF77        29 // set the clock with the time signal station DCF77
#define       DEMOD_AUTOTUNE     10 // AM demodulation with autotune function
#define       DEMOD_STEREO_DSB   17 // double sideband: SSB without eliminating the opposite sideband
#define       DEMOD_DSB          18 // double sideband: SSB without eliminating the opposite sideband
#define       DEMOD_STEREO_LSB   19
#define       DEMOD_AM_USB       20 // AM demodulation with lower sideband suppressed
#define       DEMOD_AM_LSB       21 // AM demodulation with upper sideband suppressed
#define       DEMOD_STEREO_USB   22

#endif
