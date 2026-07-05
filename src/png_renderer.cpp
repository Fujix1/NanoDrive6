#include "png_renderer.h"

#include <FS.h>
#include <PNGdec.h>
#include <SD.h>
#include <new>

#include "disp.h"
#include "esp_heap_caps.h"

static PNG* png = nullptr;
static void* pngMem = nullptr;
static File pngFile;
static String lastPNGPath = "";
static String pngErrorMessage = "";

static constexpr int kPngLargeSourceWidth = 340;
static constexpr int kPngLargeSourceHeight = 508;
static constexpr size_t kPngFullSpriteSafeBytes = 192 * 1024;
static constexpr int kPngWorkMaxWidth = ((LCD_W + 1) * 3) / 2;
static constexpr int kPngWorkMaxHeight = (127 * 3) / 2;

struct PngRenderLayout {
  float zoomX;
  float zoomY;
  u16_t background;
};

struct PngDecodeTarget {
  bool scaled = false;
  int srcW = 0;
  int srcH = 0;
  int dstW = 0;
  int dstH = 0;
};

struct PngScaleAccumulator {
  int dstY = -1;
  uint32_t r[kPngWorkMaxWidth] = {};
  uint32_t g[kPngWorkMaxWidth] = {};
  uint32_t b[kPngWorkMaxWidth] = {};
  uint32_t weight[kPngWorkMaxWidth] = {};
};

static PngDecodeTarget pngDecodeTarget;
static PngScaleAccumulator pngScaleAccumulator;
static LGFX_Sprite* sprPng = nullptr;
static LGFX_Sprite* sprPngResized = nullptr;
alignas(LGFX_Sprite) static uint8_t sprPngStorage[sizeof(LGFX_Sprite)];
alignas(LGFX_Sprite) static uint8_t sprPngResizedStorage[sizeof(LGFX_Sprite)];

static int roundToInt(float value) {
  return (value >= 0.0f) ? (int)(value + 0.5f) : (int)(value - 0.5f);
}

static int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

static u16_t swap16(u16_t value) {
  return (u16_t)((value << 8) | (value >> 8));
}

static void unpackPNGPixel(u16_t pixel, uint32_t* r, uint32_t* g, uint32_t* b) {
  const u16_t native = swap16(pixel);
  *r = (native >> 11) & 0x1f;
  *g = (native >> 5) & 0x3f;
  *b = native & 0x1f;
}

static u16_t packPNGPixel(uint32_t r, uint32_t g, uint32_t b) {
  const u16_t native = (u16_t)(((r & 0x1f) << 11) | ((g & 0x3f) << 5) | (b & 0x1f));
  return swap16(native);
}

static PngRenderLayout getPNGRenderLayout(int width, int height) {
  if (width == 640 && height == 400) {
    return {(float)(LCD_W + 1) / width, 0.2646f, TFT_BLACK};
  }
  if (width >= height) {
    return {(float)(LCD_W + 1) / width, 127.0f / height, C_DARK};
  }
  return {94.5f / width, 127.0f / height, C_DARK};
}

static bool shouldCapPNGSprite(int width, int height) {
  const size_t spriteBytes = (size_t)width * height * 2;
  return width >= kPngLargeSourceWidth || height >= kPngLargeSourceHeight ||
         spriteBytes > kPngFullSpriteSafeBytes;
}

static void getPNGDecodeSize(int srcW, int srcH, const PngRenderLayout& layout, int* dstW,
                             int* dstH) {
  const int finalW = clampInt(roundToInt(srcW * layout.zoomX), 1, kPngWorkMaxWidth);
  const int finalH = clampInt(roundToInt(srcH * layout.zoomY), 1, kPngWorkMaxHeight);
  *dstW = clampInt(finalW * 2, 1, kPngWorkMaxWidth);
  *dstH = clampInt(finalH * 2, 1, kPngWorkMaxHeight);
  if (*dstW > srcW) *dstW = srcW;
  if (*dstH > srcH) *dstH = srcH;
}

static void resetPNGScaleAccumulator(int width) {
  pngScaleAccumulator.dstY = -1;
  for (int x = 0; x < width; x++) {
    pngScaleAccumulator.r[x] = 0;
    pngScaleAccumulator.g[x] = 0;
    pngScaleAccumulator.b[x] = 0;
    pngScaleAccumulator.weight[x] = 0;
  }
}

static void clearPNGScaleAccumulatorLine(int width) {
  for (int x = 0; x < width; x++) {
    pngScaleAccumulator.r[x] = 0;
    pngScaleAccumulator.g[x] = 0;
    pngScaleAccumulator.b[x] = 0;
    pngScaleAccumulator.weight[x] = 0;
  }
}

static void filterPNGLineHorizontal(const u16_t* srcLine, int srcW, int dstW, u16_t* dstLine) {
  for (int x = 0; x < dstW; x++) {
    const int dstLeft = x * srcW;
    const int dstRight = (x + 1) * srcW;
    const int firstSrcX = dstLeft / dstW;
    const int lastSrcX = (dstRight - 1) / dstW;
    uint32_t sumR = 0;
    uint32_t sumG = 0;
    uint32_t sumB = 0;
    uint32_t totalWeight = 0;

    for (int srcX = firstSrcX; srcX <= lastSrcX; srcX++) {
      const int srcLeft = srcX * dstW;
      const int srcRight = (srcX + 1) * dstW;
      int weight = dstRight < srcRight ? dstRight : srcRight;
      weight -= dstLeft > srcLeft ? dstLeft : srcLeft;
      if (weight <= 0) continue;

      uint32_t r;
      uint32_t g;
      uint32_t b;
      unpackPNGPixel(srcLine[srcX], &r, &g, &b);
      sumR += r * weight;
      sumG += g * weight;
      sumB += b * weight;
      totalWeight += weight;
    }

    if (totalWeight == 0) {
      dstLine[x] = srcLine[firstSrcX];
    } else {
      dstLine[x] = packPNGPixel((sumR + totalWeight / 2) / totalWeight,
                                (sumG + totalWeight / 2) / totalWeight,
                                (sumB + totalWeight / 2) / totalWeight);
    }
  }
}

static void flushPNGScaleAccumulatorLine(int width) {
  if (pngScaleAccumulator.dstY < 0) return;

  u16_t scaledLineBuffer[kPngWorkMaxWidth];
  for (int x = 0; x < width; x++) {
    const uint32_t weight = pngScaleAccumulator.weight[x];
    if (weight == 0) {
      scaledLineBuffer[x] = 0;
    } else {
      scaledLineBuffer[x] =
          packPNGPixel((pngScaleAccumulator.r[x] + weight / 2) / weight,
                       (pngScaleAccumulator.g[x] + weight / 2) / weight,
                       (pngScaleAccumulator.b[x] + weight / 2) / weight);
    }
  }
  sprPng->pushImage(0, pngScaleAccumulator.dstY, width, 1, scaledLineBuffer);
  clearPNGScaleAccumulatorLine(width);
  pngScaleAccumulator.dstY = -1;
}

static void pushAreaScaledPNGLine(PNGDRAW* pDraw, const u16_t* lineBuffer) {
  u16_t filteredLineBuffer[kPngWorkMaxWidth];
  filterPNGLineHorizontal(lineBuffer, pDraw->iWidth, pngDecodeTarget.dstW, filteredLineBuffer);

  const int srcTop = pDraw->y * pngDecodeTarget.dstH;
  const int srcBottom = (pDraw->y + 1) * pngDecodeTarget.dstH;
  int firstDstY = srcTop / pngDecodeTarget.srcH;
  int lastDstY = (srcBottom - 1) / pngDecodeTarget.srcH;
  if (firstDstY < 0) firstDstY = 0;
  if (lastDstY >= pngDecodeTarget.dstH) lastDstY = pngDecodeTarget.dstH - 1;

  for (int dstY = firstDstY; dstY <= lastDstY; dstY++) {
    if (pngScaleAccumulator.dstY != dstY) {
      flushPNGScaleAccumulatorLine(pngDecodeTarget.dstW);
      pngScaleAccumulator.dstY = dstY;
    }

    const int dstTop = dstY * pngDecodeTarget.srcH;
    const int dstBottom = (dstY + 1) * pngDecodeTarget.srcH;
    int weight = srcBottom < dstBottom ? srcBottom : dstBottom;
    weight -= srcTop > dstTop ? srcTop : dstTop;
    if (weight <= 0) continue;

    for (int x = 0; x < pngDecodeTarget.dstW; x++) {
      uint32_t r;
      uint32_t g;
      uint32_t b;
      unpackPNGPixel(filteredLineBuffer[x], &r, &g, &b);
      pngScaleAccumulator.r[x] += r * weight;
      pngScaleAccumulator.g[x] += g * weight;
      pngScaleAccumulator.b[x] += b * weight;
      pngScaleAccumulator.weight[x] += weight;
    }

    if (srcBottom >= dstBottom) {
      flushPNGScaleAccumulatorLine(pngDecodeTarget.dstW);
    }
  }
}

static void beginScaledPNGDecode(int srcW, int srcH, int dstW, int dstH) {
  pngDecodeTarget.scaled = true;
  pngDecodeTarget.srcW = srcW;
  pngDecodeTarget.srcH = srcH;
  pngDecodeTarget.dstW = dstW;
  pngDecodeTarget.dstH = dstH;
  resetPNGScaleAccumulator(dstW);
}

static void endScaledPNGDecode() {
  if (pngDecodeTarget.scaled) {
    flushPNGScaleAccumulatorLine(pngDecodeTarget.dstW);
  }
  pngDecodeTarget = PngDecodeTarget();
  resetPNGScaleAccumulator(kPngWorkMaxWidth);
}

static void setPNGError(String message) {
  pngErrorMessage = message;
}

static void* myOpen(const char* filename, int32_t* size) {
  pngFile = SD.open(filename);
  *size = pngFile.size();
  return &pngFile;
}

static void myClose(void* handle) {
  if (pngFile) pngFile.close();
}

static int32_t myRead(PNGFILE* handle, u8_t* buffer, int32_t length) {
  if (!pngFile) return 0;
  return pngFile.read(buffer, length);
}

static int32_t mySeek(PNGFILE* handle, int32_t position) {
  if (!pngFile) return 0;
  return pngFile.seek(position);
}

static void pngDraw(PNGDRAW* pDraw) {
  if (png == nullptr || sprPng == nullptr) return;
  u16_t lineBuffer[MAX_PNG_WIDTH];
  png->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);

  if (!pngDecodeTarget.scaled) {
    sprPng->pushImage(0, pDraw->y, pDraw->iWidth, 1, lineBuffer);
    return;
  }

  pushAreaScaledPNGLine(pDraw, lineBuffer);
}

bool initPNGRenderer() {
  if (sprPng == nullptr) {
    sprPng = new (sprPngStorage) LGFX_Sprite(&lcd);
  }
  if (sprPngResized == nullptr) {
    sprPngResized = new (sprPngResizedStorage) LGFX_Sprite(&lcd);
  }

  if (png == nullptr) {
    pngMem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pngMem == nullptr) {
      Serial.println("ERROR: Failed to allocate PNG object in PSRAM.");
      return false;
    }
    png = new (pngMem) PNG();
  }

  if (sprPngResized->width() == 0 || sprPngResized->height() == 0) {
    sprPngResized->setPsram(true);
    if (sprPngResized->createSprite(LCD_W + 1, 125) == nullptr) {
      Serial.println("ERROR: Failed to allocate PNG resized sprite.");
      return false;
    }
  }
  return true;
}

bool loadPNG(String path, bool AA) {
  pngErrorMessage = "";
  if (png == nullptr || sprPng == nullptr || sprPngResized == nullptr) {
    setPNGError("PNG renderer is not initialized.");
    return false;
  }
  if (path == "") {
    return false;
  }

  if (lastPNGPath == path) {
    return true;
  }
  if (!SD.exists(path)) {
    return false;
  }

  int16_t rc = png->open(path.c_str(), myOpen, myClose, myRead, mySeek, pngDraw);
  if (rc != PNG_SUCCESS) {
    setPNGError(String("PNG file error:\n") + path);
    sprPng->deleteSprite();
    lastPNGPath = "";
    png->close();
    return false;
  }

  const int pngWidth = png->getWidth();
  const int pngHeight = png->getHeight();
  const PngRenderLayout layout = getPNGRenderLayout(pngWidth, pngHeight);

  if (pngWidth > MAX_PNG_WIDTH) {
    setPNGError(String("PNG width is too large.\nMax width is ") + String(MAX_PNG_WIDTH) +
                " px\nFound " + String(pngWidth) + " px\n" + path);
    sprPng->deleteSprite();
    lastPNGPath = "";
    png->close();
    return false;
  }

  sprPng->setPsram(true);
  sprPng->deleteSprite();
  bool useCappedSprite = shouldCapPNGSprite(pngWidth, pngHeight);
  int decodeWidth = pngWidth;
  int decodeHeight = pngHeight;
  if (useCappedSprite) {
    getPNGDecodeSize(pngWidth, pngHeight, layout, &decodeWidth, &decodeHeight);
  }

  void* spriteBuffer = sprPng->createSprite(decodeWidth, decodeHeight);
  if (spriteBuffer == nullptr && !useCappedSprite) {
    useCappedSprite = true;
    getPNGDecodeSize(pngWidth, pngHeight, layout, &decodeWidth, &decodeHeight);
    sprPng->deleteSprite();
    spriteBuffer = sprPng->createSprite(decodeWidth, decodeHeight);
  }
  if (spriteBuffer == nullptr) {
    setPNGError(String("PNG sprite alloc error:\n") + String(decodeWidth) + "x" +
                String(decodeHeight) + "\n" + path);
    sprPng->deleteSprite();
    lastPNGPath = "";
    png->close();
    return false;
  }

  sprPng->fillSprite(layout.background);
  if (useCappedSprite) {
    beginScaledPNGDecode(pngWidth, pngHeight, decodeWidth, decodeHeight);
  }
  rc = png->decode(NULL, 0);
  endScaledPNGDecode();
  if (rc != PNG_SUCCESS) {
    setPNGError(String("PNG decode error: ") + String(rc) + "\n" + path);
    sprPng->deleteSprite();
    lastPNGPath = "";
    png->close();
    return false;
  }

  sprPngResized->fillSprite(layout.background);
  const float w = useCappedSprite ? layout.zoomX * pngWidth / decodeWidth : layout.zoomX;
  const float h = useCappedSprite ? layout.zoomY * pngHeight / decodeHeight : layout.zoomY;
  if (AA) {
    sprPng->pushRotateZoomWithAA(sprPngResized, 84.5, 63, 0, w, h);
  } else {
    sprPng->pushRotateZoom(sprPngResized, 84.5, 63, 0, w, h);
  }
  sprPng->deleteSprite();
  lastPNGPath = path;
  png->close();
  return true;
}

LGFX_Sprite& getPNGSprite() {
  return *sprPngResized;
}

const String& getPNGErrorMessage() {
  return pngErrorMessage;
}
