/***********************************************************************************
* This is the adc driver / audio codec driver
*
* Copyright 2018 Frank DD4WH, Louis McCarthy AI0LM
* 
* GNU GPL LICENSE v3 (See LICENSE file)
************************************************************************************/

#include "ADC.h"

ADC::ADC() {
  SAMPLE_RATE = SAMPLE_RATE_96K;
  IF_FREQ = SR[SAMPLE_RATE].rate / 4;
}

ADC::~ADC() {
  
}

