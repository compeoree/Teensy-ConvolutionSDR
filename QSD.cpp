/***********************************************************************************
* This is the hardware driver for the quadrature RF front end and bandpass filters
*
* Copyright 2018 Frank DD4WH, Louis McCarthy
* 
* GNU GPL LICENSE v3 (See LICENSE file)
************************************************************************************/
#include "QSD.h"

QSD::QSD() {
  //audioADC();// = new ADC();
}

QSD::~QSD() {
}

void QSD::initQSD() {
  pinMode(Band1, OUTPUT);
  pinMode(Band2, OUTPUT);
  pinMode(Band3, OUTPUT);
  pinMode(Band4, OUTPUT);
  pinMode(Band5, OUTPUT);
}

void QSD::setfreq () {
  // NEVER USE AUDIONOINTERRUPTS HERE: that introduces annoying clicking noise with every frequency change
  //   hilfsf = (bands[band].freq +  IF_FREQ) * 10000000 * MASTER_CLK_MULT * SI5351_FREQ_MULT;
  hilfsf = (bands[band].freq +  audioADC.IF_FREQ * SI5351_FREQ_MULT) * 1000000000 * MASTER_CLK_MULT; // SI5351_FREQ_MULT is 100ULL;
  hilfsf = hilfsf / calibration_factor;
  si5351.set_freq(hilfsf, Si_5351_clock);
  if (band[bands].mode == DEMOD_AUTOTUNE)
  {
    tuner.autotune_flag = 1;
  }
  //FrequencyBarText();

  // LPF switching follows here
  // Five filter banks there:
  // longwave LPF 295kHz, mediumwave I LPF 955kHz, mediumwave II LPF 2MHz, tropical bands LPF 5.4MHz, others LPF LPF 30MHz
  // LW: Band5
  // MW: Band3 (up to 955kHz)
  // MW: Band1 (up tp 1996kHz)
  // SW: Band2 (up to 5400kHz)
  // SW: Band4 (up up up)
  //
  // LOWPASS 955KHZ
  if (((bands[band].freq + audioADC.IF_FREQ * SI5351_FREQ_MULT) < 955001 * SI5351_FREQ_MULT) && ((bands[band].freq + audioADC.IF_FREQ * SI5351_FREQ_MULT) > 300001 * SI5351_FREQ_MULT)) {
    digitalWrite (Band3, HIGH); //Serial.println ("Band3");
    digitalWrite (Band1, LOW); digitalWrite (Band2, LOW); digitalWrite (Band4, LOW); digitalWrite (Band5, LOW);
  } // end if

  // LOWPASS 2MHZ
  if (((bands[band].freq + audioADC.IF_FREQ * SI5351_FREQ_MULT) > 955000 * SI5351_FREQ_MULT) && ((bands[band].freq + audioADC.IF_FREQ * SI5351_FREQ_MULT) < 1996001 * SI5351_FREQ_MULT)) {
    digitalWrite (Band1, HIGH);//Serial.println ("Band1");
    digitalWrite (Band5, LOW); digitalWrite (Band3, LOW); digitalWrite (Band4, LOW); digitalWrite (Band2, LOW);
  } // end if

  //LOWPASS 5.4MHZ
  if (((bands[band].freq + audioADC.IF_FREQ * SI5351_FREQ_MULT) > 1996000 * SI5351_FREQ_MULT) && ((bands[band].freq + audioADC.IF_FREQ * SI5351_FREQ_MULT) < 5400001 * SI5351_FREQ_MULT)) {
    digitalWrite (Band2, HIGH);//Serial.println ("Band2");
    digitalWrite (Band4, LOW); digitalWrite (Band3, LOW); digitalWrite (Band1, LOW); digitalWrite (Band5, LOW);
  } // end if

  // LOWPASS 30MHZ --> OK
  if ((bands[band].freq + audioADC.IF_FREQ * SI5351_FREQ_MULT) > 5400000 * SI5351_FREQ_MULT) {
    // && ((bands[band].freq + IF_FREQ) < 12500001)) {
    digitalWrite (Band4, HIGH);//Serial.println ("Band4");
    digitalWrite (Band1, LOW); digitalWrite (Band3, LOW); digitalWrite (Band2, LOW); digitalWrite (Band5, LOW);
  } // end if
  // I took out the 12.5MHz lowpass and inserted the 30MHz instead - I have to live with 3rd harmonic images in the range 5.4 - 12Mhz now
  // maybe this is more important than the 5.4 - 2Mhz filter ?? Maybe swap them sometime, because I only got five filter relays . . .

  // this is the brandnew longwave LPF (cutoff ca. 295kHz) --> OK
  if ((bands[band].freq - audioADC.IF_FREQ * SI5351_FREQ_MULT) < 300000 * SI5351_FREQ_MULT) {
    digitalWrite (Band5, HIGH);//Serial.println ("Band5");
    digitalWrite (Band2, LOW); digitalWrite (Band3, LOW); digitalWrite (Band4, LOW); digitalWrite (Band1, LOW);
  } // end if
}
