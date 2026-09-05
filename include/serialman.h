#ifndef SERIALMAN_H
#define SERIALMAN_H
#include <Arduino.h>

u8_t getSerial();

class SerialMan {
 public:
  SerialMan();
  void init();
  void startSerialTask();
  void startTrackMaskTask();
  void changeYM2612Clock();
  void changeSN76489Clock();
  void setTrackMetadata(const String& title, const String& system, const String& composer,
                        const String& date, const String& path, String type);

 private:
  int YM2612Clock = 0, SN76489Clock = 0;
};

extern SerialMan serialMan;

#endif
