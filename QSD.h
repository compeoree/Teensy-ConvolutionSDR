/***********************************************************************************
* This is the hardware driver for the quadrature RF front end and bandpass filters
*
* Copyright 2018 Frank DD4WH, Louis McCarthy
* 
* GNU GPL LICENSE v3 (See LICENSE file)
************************************************************************************/
#ifndef __QSD_H_
#define __QSD_H_

#include <WString.h>
#include <si5351.h>

#include "Demod.h"
#include "ADC.h"
#include "Tuner.h"

#define JORIS   0
#define ELEKTOR 1

#define MASTER_CLK_MULT  4  // QSD frontend requires 4x clock

#define NUM_BANDS  16

#define BAND_LW     0
#define BAND_MW     1
#define BAND_120M   2
#define BAND_90M    3
#define BAND_75M    4
#define BAND_60M    5
#define BAND_49M    6
#define BAND_41M    7
#define BAND_31M    8
#define BAND_25M    9
#define BAND_22M   10
#define BAND_19M   11
#define BAND_16M   12
#define BAND_15M   13
#define BAND_13M   14
#define BAND_11M   15

#define FIRST_BAND BAND_LW
#define LAST_BAND  BAND_13M
#define STARTUP_BAND BAND_MW

#if QSD_TYPE == JORIS
  #define Si_5351_clock  SI5351_CLK2
  #define Si_5351_crystal 27000000
#endif

#if QSD_TYPE == ELEKTOR
  #define Si_5351_clock  SI5351_CLK1
  #define Si_5351_crystal 25000000
#endif

struct band {
  unsigned long long freq; // frequency in Hz
  String name; // name of band
  int mode;
  int FHiCut;
  int FLoCut;
  int RFgain;
};

class QSD {
  private:
  public:
    // Bandpass filter I/O
    const uint8_t Band1 = 31; // band selection pins for LPF relays, used with 2N7000: HIGH means LPF is activated
    const uint8_t Band2 = 30; // always use only one LPF with HIGH, all others have to be LOW
    const uint8_t Band3 = 27;
    const uint8_t Band4 = 29; // 29: > 5.4MHz
    const uint8_t Band5 = 26; // LW
    
    Si5351 si5351;
    ADC audioADC;
    Tuner tuner;

    #if QSD_TYPE == JORIS
      long calibration_constant = -8000;
    #endif
    
    #if QSD_TYPE == ELEKTOR
      long calibration_constant = 108000;
    #endif

    int band = STARTUP_BAND;
    unsigned long long calibration_factor = 1000000000; // 10002285;
    unsigned long long hilfsf;
    
    // f, band, mode, bandwidth, RFgain
    struct band bands[NUM_BANDS] = {
      {22500000, "LW", DEMOD_SAM, 3600, -3600, 0},
      {63900000, "MW",  DEMOD_SAM, 3600, -3600, 0},
      {248500000, "120M",  DEMOD_SAM, 3600, -3600, 0},
      {350000000, "90M",  DEMOD_LSB, 3600, -3600, 6},
      {390500000, "75M",  DEMOD_SAM, 3600, -3600, 4},
      {502500000, "60M",  DEMOD_SAM, 3600, -3600, 7},
      {593200000, "49M",  DEMOD_SAM, 3600, -3600, 0},
      {712000000, "41M",  DEMOD_SAM, 3600, -3600, 0},
      {942000000, "31M",  DEMOD_SAM, 3600, -3600, 0},
      {1173500000, "25M", DEMOD_SAM, 3600, -3600, 2},
      {1357000000, "22M", DEMOD_SAM, 3600, -3600, 2},
      {1514000000, "19M", DEMOD_SAM, 3600, -3600, 4},
      {1748000000, "16M", DEMOD_SAM, 3600, -3600, 5},
      {3146866600, "15M", DEMOD_WFM, 3600, -3600, 21},
      {2145000000, "13M", DEMOD_SAM, 3600, -3600, 6},
      {2567000000, "11M", DEMOD_SAM, 3600, -3600, 6}
    };


    QSD();
    ~QSD();
    void initQSD();
    void setfreq();
};

#endif
