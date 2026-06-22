#ifndef PNG_RENDERER_H
#define PNG_RENDERER_H

#include <Arduino.h>
#include <LovyanGFX.h>

bool initPNGRenderer();
bool loadPNG(String path, bool AA);
LGFX_Sprite& getPNGSprite();
const String& getPNGErrorMessage();

#endif
