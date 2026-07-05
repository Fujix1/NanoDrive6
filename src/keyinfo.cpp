#include "keyinfo.h"

keyboard::keyboard() {
  keyinfoMutex = xSemaphoreCreateMutex();
  for (int i = 0; i < DEVICE_COUNT; i++) {
    for (int j = 0; j < MAX_CHANNELS; j++) {
      keyInfo[i][j] = (struct NoteInfo){0, 0};
    }
  }
  for (int i = 0; i < 16; i++) {
    trackPan[i] = (i < 7) ? PAN_CENTER : PAN_MUTE;
    trackKeyOn[i] = false;
    trackLevel[i] = 0;
  }
}

void keyboard::reset() {
  if (xSemaphoreTake(keyinfoMutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < DEVICE_COUNT; i++) {
      for (int j = 0; j < device_channels[i]; j++) {
        keyInfo[i][j] = (struct NoteInfo){0, 0};
      }
    }
    for (int i = 0; i < 16; i++) {
      trackPan[i] = (i < 7) ? PAN_CENTER : PAN_MUTE;
      trackKeyOn[i] = false;
      trackLevel[i] = 0;
    }
    xSemaphoreGive(keyinfoMutex);
    Serial.printf("Key Info Reset.\n");
  }
}

void keyboard::set(t_device device, uint8_t ch, NoteInfo ni) {
  if (xSemaphoreTake(keyinfoMutex, portMAX_DELAY) == pdTRUE) {
    keyInfo[device][ch] = (struct NoteInfo)ni;
    xSemaphoreGive(keyinfoMutex);
  }
}

keyboard KeyBoard = keyboard();
