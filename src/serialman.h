#ifndef SERIALMAN_H
#define SERIALMAN_H
#include <Arduino.h>

u8_t getSerial();

class SerialMan {
 public:
  SerialMan();
  void init();
  void startSerialTask();
  void changeYM2612Clock();
  void changeSN76489Clock();

 private:
  int YM2612Clock = 0, SN76489Clock = 0;
};

extern SerialMan serialMan;

#endif
