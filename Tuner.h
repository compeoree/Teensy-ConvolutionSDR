/***********************************************************************************
* This is the frequency adjustment driver
*
* Copyright 2018 Frank DD4WH, Louis McCarthy AI0LM
* 
* GNU GPL LICENSE v3 (See LICENSE file)
************************************************************************************/
#ifndef __TUNER_H_
#define __TUNER_H_

#include <arm_math.h>
//#include <arm_const_structs.h>

class Tuner {
  private:
  public:
    uint8_t autotune_flag = 0;

    Tuner();
    ~Tuner();
};

#endif
