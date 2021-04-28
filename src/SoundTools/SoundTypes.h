#pragma once
#include <bits/stdc++.h>

namespace sound_tools {

#define INT24_MAX 0x7FFFFF

/**
 * @brief 24bit integer which is used for I2S sound processing. It works only on little endian machines!
 * 
 */
class int24_t {

  public:

    int24_t(uint8_t *ptr){
        value[0]=ptr[0];
        value[1]=ptr[1];
        value[2]=ptr[2];
    }

    int24_t(int16_t in){
      value[2] = in > 0 ? 0 : 0xFF;
      value[1] = (in >> 8) & 0xFF;
      value[0] = in & 0xFF;
    }

    int24_t(int32_t in){
      value[2] = (in >> 16) & 0xFF;;
      value[1] = (in >> 8) & 0xFF;
      value[0] = in & 0xFF;
    }

    operator int32_t()  {
        uint8_t tmp[4];
        tmp[0] = value[0];
        tmp[1] = value[1];
        tmp[2] = value[2];
        tmp[3] = value[2] & 0x80  ? 0xFF : 0;
        int32_t *ptr = (int32_t *)&tmp;
        return *ptr;
    }

    operator float()  {
        return static_cast<int32_t>(*this);
    }

    /// provides value between -32767 and 32767
    int16_t scale16()  {
        return static_cast<float>(*this) * INT16_MAX / INT24_MAX ; 
    }

    /// provides value between -2,147,483,647 and 2,147,483,647
    int16_t scale32()  {
        return static_cast<float>(*this)  * INT32_MAX / INT24_MAX; 
    }

    /// provides value between -1.0 and 1.0
    float scaleFloat()  {
        return static_cast<float>(*this)  / INT24_MAX ; 
    }

  private:
    uint8_t value[3]; 
};

}