/***********************************************************************************
* This is the adc driver / audio codec driver
*
* Copyright 2018 Frank DD4WH, Louis McCarthy AI0LM
* 
* GNU GPL LICENSE v3 (See LICENSE file)
************************************************************************************/
#ifndef __ADC_H_
#define __ADC_H_

#include <Audio.h>
#include <arm_math.h>
#include <arm_const_structs.h>

#define SAMPLE_RATE_MIN               6
#define SAMPLE_RATE_MAX               11

#define SAMPLE_RATE_8K                0
#define SAMPLE_RATE_11K               1
#define SAMPLE_RATE_16K               2
#define SAMPLE_RATE_22K               3
#define SAMPLE_RATE_32K               4
#define SAMPLE_RATE_44K               5
#define SAMPLE_RATE_48K               6
#define SAMPLE_RATE_88K               7
#define SAMPLE_RATE_96K               8
#define SAMPLE_RATE_100K              9
#define SAMPLE_RATE_176K              10
#define SAMPLE_RATE_192K              11

typedef struct SR_Descriptor
{
  const uint8_t SR_n;
  const uint32_t rate;
  const char* const text;
  const char* const f1;
  const char* const f2;
  const char* const f3;
  const char* const f4;
  const float32_t x_factor;
  const uint8_t x_offset;
} SR_Desc;

class ADC {
  private:
  public:
    const SR_Descriptor SR [12] = {
      //   SR_n , rate, text, f1, f2, f3, f4, x_factor = pixels per f1 kHz in spectrum display
      {  SAMPLE_RATE_8K, 8000,  "  8k", " 1", " 2", " 3", " 4", 64.0, 11}, // not OK
      {  SAMPLE_RATE_11K, 11025, " 11k", " 1", " 2", " 3", " 4", 43.1, 17}, // not OK
      {  SAMPLE_RATE_16K, 16000, " 16k",  " 4", " 4", " 8", "12", 64.0, 1}, // OK
      {  SAMPLE_RATE_22K, 22050, " 22k",  " 5", " 5", "10", "15", 58.05, 6}, // OK
      {  SAMPLE_RATE_32K, 32000,  " 32k", " 5", " 5", "10", "15", 40.0, 24}, // OK, one more indicator?
      {  SAMPLE_RATE_44K, 44100,  " 44k", "10", "10", "20", "30", 58.05, 6}, // OK
      {  SAMPLE_RATE_48K, 48000,  " 48k", "10", "10", "20", "30", 53.33, 11}, // OK
      {  SAMPLE_RATE_88K, 88200,  " 88k", "20", "20", "40", "60", 58.05, 6}, // OK
      {  SAMPLE_RATE_96K, 96000,  " 96k", "20", "20", "40", "60", 53.33, 12}, // OK
      {  SAMPLE_RATE_100K, 100000,  "100k", "20", "20", "40", "60", 53.33, 12}, // NOT OK
      {  SAMPLE_RATE_176K, 176400,  "176k", "40", "40", "80", "120", 58.05, 6}, // OK
      {  SAMPLE_RATE_192K, 192000,  "192k", "40", "40", "80", "120", 53.33, 12} // not OK
    };
    
    uint8_t SAMPLE_RATE;
    int32_t IF_FREQ;  // intermediate frequency

    ADC();
    ~ADC();
};
#endif
