#ifndef FM_H
#define FM_H
#include <Arduino.h>

#include "SI5351.hpp"

// GPIO Assignment
#define D0 9
#define D1 10
#define D2 11
#define D3 12
#define D4 13
#define D5 14
#define D6 21
#define D7 47

#define A0 18
#define A1 8
#define WR 40
#define CS0 38
#define CS1 39
#define CS2 43
#define IC 48

#define A1_HIGH (gpio_set_level((gpio_num_t)A1, 1))
#define A1_LOW (gpio_set_level((gpio_num_t)A1, 0))
#define A0_HIGH (gpio_set_level((gpio_num_t)A0, 1))
#define A0_LOW (gpio_set_level((gpio_num_t)A0, 0))
#define WR_HIGH (gpio_set_level((gpio_num_t)WR, 1))
#define WR_LOW (gpio_set_level((gpio_num_t)WR, 0))
#define CS0_HIGH (gpio_set_level((gpio_num_t)CS0, 1))
#define CS0_LOW (gpio_set_level((gpio_num_t)CS0, 0))
#define CS1_HIGH (gpio_set_level((gpio_num_t)CS1, 1))
#define CS1_LOW (gpio_set_level((gpio_num_t)CS1, 0))
#define CS2_HIGH (gpio_set_level((gpio_num_t)CS2, 1))
#define CS2_LOW (gpio_set_level((gpio_num_t)CS2, 0))
#define IC_HIGH (gpio_set_level((gpio_num_t)IC, 1))
#define IC_LOW (gpio_set_level((gpio_num_t)IC, 0))

class FMChip {
 public:
  void begin();
  void reset();
  void setRegister(byte addr, byte value, int chipno);
  void setRegisterOPM(byte addr, byte value, uint8_t chipno);
  void setRegisterOPL3(byte port, byte addr, byte data, int chipno);
  void setYM2612(byte port, byte addr, byte data, uint8_t chipno);
  void setYM2612DAC(byte data, uint8_t chipno);
  void write(byte data, byte chipno, si5351Freq_t freq);
  void writeRaw(byte data, byte chipno, si5351Freq_t freq);

 private:
  u8_t _psgFrqLowByte = 0;
};

extern FMChip FM;
#endif