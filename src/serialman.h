#ifndef SERIALMAN_H
#define SERIALMAN_H
#include <Arduino.h>

u8_t getSerial();

class SerialMan {
 public:
  SerialMan();
  void init();
  void startSerialTask();

 private:
};

extern SerialMan serialMan;

#endif
