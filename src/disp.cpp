#include "disp.h"

#include "input.h"
#include "pics.h"
#include "png_renderer.h"

static bool _stopTimerDrawing = true;  // タイマーによる描画更新を止める

const uint8_t* Panel_ST7789_ND::getInitCommands(uint8_t listno) const {
  // ND6.1以降の液晶では現行の明るめガンマ値が必要。
  // TCA8418が見つからない旧ND6だけ、LovyanGFX upstream相当の旧液晶向け初期値を使う。
  static constexpr uint8_t oldList0[] = {
      // LovyanGFX upstream ST7789 defaults for the older Nano Drive 6 LCD module.
      CMD_GCTRL,
      1,
      0x35,
      CMD_VCOMS,
      1,
      0x28,
      CMD_LCMCTRL,
      1,
      0x0C,
      CMD_VDVVRHEN,
      2,
      0x01,
      0xFF,
      CMD_VRHS,
      1,
      0x10,
      CMD_VDVSET,
      1,
      0x20,
      CMD_PWCTRL1,
      2,
      0xa4,
      0xa1,
      CMD_RAMCTRL,
      2,
      0x00,
      0xC0,
      CMD_PVGAMCTRL,
      14,
      0xd0,
      0x00,
      0x02,
      0x07,
      0x0a,
      0x28,
      0x32,
      0x44,
      0x42,
      0x06,
      0x0e,
      0x12,
      0x14,
      0x17,
      CMD_NVGAMCTRL,
      14,
      0xd0,
      0x00,
      0x02,
      0x07,
      0x0a,
      0x28,
      0x31,
      0x54,
      0x47,
      0x0e,
      0x1c,
      0x17,
      0x1b,
      0x1e,
      CMD_SLPOUT,
      0 + CMD_INIT_DELAY,
      130,
      CMD_IDMOFF,
      0,
      CMD_DISPON,
      0,
      0xFF,
      0xFF,
  };

  if (ND::version == nd_v60) {
    switch (listno) {
      case 0:
        return oldList0;
      default:
        return nullptr;
    }
  }

  return lgfx::Panel_ST7789::getInitCommands(listno);
}

LGFX::LGFX(void) {
  {                                     // バス制御の設定を行います。
    auto cfg = _bus_instance.config();  // バス設定用の構造体を取得します。

    // SPIバスの設定
    cfg.spi_host = SPI2_HOST;  // 使用するSPIを選択  ESP32-S2,C3 : SPI2_HOST or
                               // SPI3_HOST / ESP32 : VSPI_HOST or HSPI_HOST
    // ※ ESP-IDFバージョンアップに伴い、VSPI_HOST ,
    // HSPI_HOSTの記述は非推奨になるため、エラーが出る場合は代わりにSPI2_HOST ,
    // SPI3_HOSTを使用してください。
    cfg.spi_mode = 3;                   // SPI通信モードを設定 (0 ~ 3)
    cfg.freq_write = 80000000;          // 送信時のSPIクロック (最大80MHz,
                                        // 80MHzを整数で割った値に丸められます)
    cfg.freq_read = 40000000;           // 受信時のSPIクロック
    cfg.spi_3wire = true;               // 受信をMOSIピンで行う場合はtrueを設定
    cfg.use_lock = true;                // トランザクションロックを使用する場合はtrueを設定
    cfg.dma_channel = SPI_DMA_CH_AUTO;  // 使用するDMAチャンネルを設定 (0=DMA不使用 / 1=1ch /
                                        // 2=ch / SPI_DMA_CH_AUTO=自動設定)
    // ※
    // ESP-IDFバージョンアップに伴い、DMAチャンネルはSPI_DMA_CH_AUTO(自動設定)が推奨になりました。1ch,2chの指定は非推奨になります。
    cfg.pin_sclk = LCD_CLK;                  // SPIのSCLKピン番号を設定
    cfg.pin_mosi = LCD_MOSI;                 // SPIのMOSIピン番号を設定
    cfg.pin_miso = -1;                       // SPIのMISOピン番号を設定 (-1 = disable)
    cfg.pin_dc = LCD_DC;                     // SPIのD/Cピン番号を設定  (-1 = disable)
    _bus_instance.config(cfg);               // 設定値をバスに反映します。
    _panel_instance.setBus(&_bus_instance);  // バスをパネルにセットします。
  }

  {                                       // 表示パネル制御の設定を行います。
    auto cfg = _panel_instance.config();  // 表示パネル設定用の構造体を取得します。

    cfg.pin_cs = -1;        // CSが接続されているピン番号   (-1 = disable)
    cfg.pin_rst = LCD_RST;  // RSTが接続されているピン番号  (-1 = disable)
    cfg.pin_busy = -1;      // BUSYが接続されているピン番号 (-1 = disable)

    cfg.panel_width = 170;     // 実際に表示可能な幅
    cfg.panel_height = 320;    // 実際に表示可能な高さ
    cfg.offset_x = 35;         // パネルのX方向オフセット量
    cfg.offset_y = 0;          // パネルのY方向オフセット量
    cfg.offset_rotation = 2;   // 回転方向の値のオフセット 0~7 (4~7は上下反転)
    cfg.dummy_read_pixel = 8;  // ピクセル読出し前のダミーリードのビット数
    cfg.dummy_read_bits = 1;   // ピクセル以外のデータ読出し前のダミーリードのビット数
    cfg.readable = false;      // データ読出しが可能な場合 trueに設定
    cfg.invert = true;         // パネルの明暗が反転してしまう場合 trueに設定
    cfg.rgb_order = false;     // パネルの赤と青が入れ替わってしまう場合 trueに設定
    cfg.dlen_16bit = false;    // 16bitパラレルやSPIでデータ長を16bit単位で送信するパネルの場合
                               // trueに設定
    cfg.bus_shared = false;    // SDカードとバスを共有している場合
                               // trueに設定(drawJpgFile等でバス制御を行います)

    _panel_instance.config(cfg);
  }
  setPanel(&_panel_instance);  // 使用するパネルをセットします。
}

LGFX lcd;
Disp disp;
PlayerWindow playerWindow;
VisualWindow visualWindow;
static LGFX_Sprite frameBuffer(&lcd);
static LGFX_Sprite keyboardBuffer(&lcd);
static LGFX_Sprite keyboardBufferSub(&lcd);
static LGFX_Sprite panMarkerCenter(&lcd);
static LGFX_Sprite panMarkerLeft(&lcd);
static LGFX_Sprite panMarkerRight(&lcd);
static LGFX_Sprite panMarkerMute(&lcd);

static OpenFontRender render;
static TimerHandle_t hDispTimer;
static TaskHandle_t hDispUpdateTask;
static int currentPage = 0;

#ifndef ND_VISUAL_ROTATE_180
#define ND_VISUAL_ROTATE_180 0
#endif

static constexpr bool kVisualRotate180 = ND_VISUAL_ROTATE_180 != 0;
static constexpr uint8_t kLabelSpriteCount = 3;  // XGM2, XGM1, VGM
static constexpr uint16_t kLabelSpriteWidth = labelsWidth;
static constexpr uint16_t kLabelSpriteHeight = 41;
static constexpr uint16_t kLabelVgmX = 155;
static constexpr uint16_t kLabelVgmY = 223;
static constexpr uint16_t kLabelXgm1X = 155;
static constexpr uint16_t kLabelXgm1Y = 179;
static constexpr uint16_t kLabelXgm2X = 155;
static constexpr uint16_t kLabelXgm2Y = 135;
static constexpr uint8_t kLevelSpriteCount = 16;  // level 0 .. 15
static constexpr uint16_t kLevelSpriteWidth = levelsWidth;
static constexpr uint16_t kLevelSpriteHeight = 17;
static constexpr uint8_t kNumberSpriteCount = 11;  // "--", 9 .. 0
static constexpr uint16_t kNumberSpriteWidth = numbersWidth;
static constexpr uint16_t kNumberSpriteHeight = 8;
static constexpr uint16_t kLevelWaterfallAccelQ8 = 16;         // 落下加速度
static constexpr uint16_t kLevelWaterfallInitialSpeedQ8 = 32;  // 落下開始初速
static constexpr uint8_t kPeakHoldDelayFrames = 10;            // ピークホールドフレーム数
static constexpr uint8_t kPeakLineX0 = 29;
static constexpr uint8_t kPeakLineYTop = 2;
static constexpr uint8_t kPeakLineYBottom = 14;
static constexpr uint16_t kNoteDrawX = 115;        // キー表示 X
static constexpr uint16_t kNoteDrawBottomY = 263;  // キー表示 Y 下端
static constexpr int kLevelDrawX = 67;         // レベルメータX
static constexpr int kLevelDrawBottomY = 262;  // レベルメータY下端
static constexpr uint8_t kFmTrackCount = 6;
static constexpr uint8_t kNoteTrackCount = 16;
static constexpr uint8_t kPanTrackCount = 16;   // Track 1-6: FM, Track 7: PCM, 9-16: PSG
static constexpr uint8_t kLevelTrackCount = 16;  // Track 1-6: FM, 7: PCM, 9-16: SN76489
static constexpr uint8_t kPcmTrack = 6;          // UI Track 7
static constexpr uint8_t kUnusedTrack = 7;       // UI Track 8
static constexpr uint8_t kSn0ToneTrackFirst = 8;   // UI Track 9
static constexpr uint8_t kSn0NoiseTrack = 11;      // UI Track 12
static constexpr uint8_t kSn1ToneTrackFirst = 12;  // UI Track 13
static constexpr uint8_t kSn1NoiseTrack = 15;      // UI Track 16
static uint16_t levelSprite[kLevelSpriteCount][kLevelSpriteWidth * kLevelSpriteHeight];
static uint16_t levelWorkBuffer[kLevelSpriteWidth * kLevelSpriteHeight];
static uint16_t numberSprite[kNumberSpriteCount][kNumberSpriteWidth * kNumberSpriteHeight];
static uint16_t numberWorkBuffer[kNumberSpriteWidth * (kNumberSpriteHeight * 2)];
static uint16_t labelSprite[kLabelSpriteCount][kLabelSpriteWidth * kLabelSpriteHeight];
static uint16_t visualRowBuffer[LCD_W];
static int8_t lastTrackPan[kPanTrackCount];
static int8_t lastTrackLevel[kLevelTrackCount];
static int8_t lastTrackPeak[kLevelTrackCount];
static int16_t lastTrackNote[kNoteTrackCount];
static uint8_t heldTrackNote[kNoteTrackCount];
static uint16_t trackLevelDisplayQ8[kLevelTrackCount] = {0};
static uint16_t trackLevelFallSpeedQ8[kLevelTrackCount] = {0};
static uint16_t trackPeakDisplayQ8[kLevelTrackCount] = {0};
static uint16_t trackPeakFallSpeedQ8[kLevelTrackCount] = {0};
static uint8_t trackPeakHoldFrames[kLevelTrackCount] = {0};

static Label lblTitle = Label(0, 28, LCD_W, C_ACCENT_LIGHT, C_BASEBG, 20, SCROLL_SPEED_TITLE, Align::TopCenter);
static Label lblGame = Label(0, 53, LCD_W, C_LIGHTGRAY, C_BASEBG, 15, SCROLL_SPEED_GAME, Align::TopCenter);
static Label lblAuthor = Label(28, 233, LCD_W - 28, C_GRAY, C_BASEBG, 16, SCROLL_SPEED_AUTHOR, Align::TopLeft);
static Label lblSystem = Label(28, 211, LCD_W - 28, C_GRAY, C_BASEBG, 16, SCROLL_SPEED_AUTHOR, Align::TopLeft);
static RotatedLabel lblSongTitle =
    RotatedLabel(133, 0, 268, TFT_WHITE, C_BASEBG, 17, SCROLL_SPEED_TITLE, Align::TopLeft);

static SemaphoreHandle_t spFrameBuffer;  // 描画用セマフォ

static int visualX(int x, int w) {
  return kVisualRotate180 ? LCD_W - x - w : x;
}

static int visualY(int y, int h) {
  return kVisualRotate180 ? LCD_H - y - h : y;
}

static void copyVisualImage(uint16_t* dst, const uint16_t* src, int w, int h) {
  if (!kVisualRotate180) {
    memcpy(dst, src, sizeof(uint16_t) * w * h);
    return;
  }

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      dst[y * w + x] = src[(h - 1 - y) * w + (w - 1 - x)];
    }
  }
}

static void pushVisualImage(LGFX_Sprite& target, int x, int y, int w, int h, const uint16_t* src) {
  target.pushImage(visualX(x, w), visualY(y, h), w, h, src);
}

static void pushPreparedVisualImage(LGFX_Sprite& target, int x, int y, int w, int h,
                                    const uint16_t* src) {
  if (!kVisualRotate180) {
    target.pushImage(x, y, w, h, src);
    return;
  }

  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      visualRowBuffer[col] = src[(h - 1 - row) * w + (w - 1 - col)];
    }
    target.pushImage(x, y + row, w, 1, visualRowBuffer);
  }
}

static void pushVisualSpriteToFrameBuffer(LGFX_Sprite& sprite, int x, int y) {
  sprite.pushSprite(&frameBuffer, visualX(x, sprite.width()), visualY(y, sprite.height()));
}

static void pushVisualSpriteToLcd(LGFX_Sprite& sprite, int x, int y) {
  sprite.pushSprite(visualX(x, sprite.width()), visualY(y, sprite.height()));
}

static void createVisualShuffleIconSprite(LGFX_Sprite& target, uint16_t color) {
  static constexpr int kShuffleIconSize = 18;

  LGFX_Sprite source(&lcd);
  source.setPsram(false);
  source.createSprite(kShuffleIconSize, kShuffleIconSize);
  if (source.width() == 0) {
    return;
  }

  target.deleteSprite();
  target.setPsram(false);
  target.createSprite(kShuffleIconSize, kShuffleIconSize);
  if (target.width() == 0) {
    source.deleteSprite();
    return;
  }

  source.fillSprite(TFT_BLACK);
  render.setDrawer(source);
  render.loadFont(fontMain, sizeof(fontMain));
  render.setFontSize(15);
  render.setAlignment(Align::TopCenter);
  render.setFontColor(color, TFT_BLACK);
  render.setCursor(kShuffleIconSize / 2, 1);
  render.printf("丂");
  render.unloadFont();

  target.fillSprite(TFT_BLACK);
  source.setPivot(kShuffleIconSize / 2.0f, kShuffleIconSize / 2.0f);
  const float angle = kVisualRotate180 ? 90.0f : -90.0f;
  source.pushRotateZoom(&target, kShuffleIconSize / 2.0f, kShuffleIconSize / 2.0f, angle,
                        1.0f, 1.0f, TFT_BLACK);
  source.deleteSprite();
}

static void pushVisualBufferToLcd(int x, int y, int w, int h, const uint16_t* data) {
  lcd.pushImage(visualX(x, w), visualY(y, h), w, h, data);
}

static void fillVisualRect(LGFX_Sprite& sprite, int x, int y, int w, int h) {
  if (kVisualRotate180) {
    sprite.fillRect(sprite.width() - x - w, sprite.height() - y - h, w, h);
  } else {
    sprite.fillRect(x, y, w, h);
  }
}

static void cutPanMarkerSprite(LGFX_Sprite& sprite, int markerIndex) {
  const int markerHeight = panmarkersHeight / 4;
  sprite.setPsram(false);
  sprite.createSprite(panmarkersWidth, markerHeight);

  if (kVisualRotate180) {
    for (int y = 0; y < markerHeight; y++) {
      const int srcY = markerIndex * markerHeight + (markerHeight - 1 - y);
      for (int x = 0; x < panmarkersWidth; x++) {
        visualRowBuffer[x] = panmarkers[srcY * panmarkersWidth + (panmarkersWidth - 1 - x)];
      }
      sprite.pushImage(0, y, panmarkersWidth, 1, visualRowBuffer);
    }
  } else {
    for (int y = 0; y < markerHeight; y++) {
      const int srcY = markerIndex * markerHeight + y;
      const uint16_t* src = &panmarkers[srcY * panmarkersWidth];
      sprite.pushImage(0, y, panmarkersWidth, 1, src);
    }
  }
}

// レベルメータ画像切り出し
static void cutLevelSprite(uint8_t levelIndex) {
  if (levelIndex >= kLevelSpriteCount) {
    return;
  }

  const uint16_t srcY = (uint16_t)(levelIndex * kLevelSpriteHeight);
  uint16_t* dst = levelSprite[levelIndex];

  if (kVisualRotate180) {
    for (uint16_t y = 0; y < kLevelSpriteHeight; y++) {
      for (uint16_t x = 0; x < kLevelSpriteWidth; x++) {
        dst[y * kLevelSpriteWidth + x] =
            levels[(srcY + (kLevelSpriteHeight - 1 - y)) * levelsWidth +
                   (kLevelSpriteWidth - 1 - x)];
      }
    }
  } else {
    for (uint16_t y = 0; y < kLevelSpriteHeight; y++) {
      const uint16_t* src = &levels[(srcY + y) * levelsWidth];
      memcpy(&dst[y * kLevelSpriteWidth], src, sizeof(uint16_t) * kLevelSpriteWidth);
    }
  }
}

// 数字画像切り出し
static void cutNumberSprite(uint8_t glyphIndex) {
  if (glyphIndex >= kNumberSpriteCount) {
    return;
  }

  const uint16_t srcY = (uint16_t)(glyphIndex * kNumberSpriteHeight);
  uint16_t* dst = numberSprite[glyphIndex];

  if (kVisualRotate180) {
    for (uint16_t y = 0; y < kNumberSpriteHeight; y++) {
      for (uint16_t x = 0; x < kNumberSpriteWidth; x++) {
        dst[y * kNumberSpriteWidth + x] =
            numbers[(srcY + (kNumberSpriteHeight - 1 - y)) * numbersWidth +
                    (kNumberSpriteWidth - 1 - x)];
      }
    }
  } else {
    for (uint16_t y = 0; y < kNumberSpriteHeight; y++) {
      const uint16_t* src = &numbers[(srcY + y) * numbersWidth];
      memcpy(&dst[y * kNumberSpriteWidth], src, sizeof(uint16_t) * kNumberSpriteWidth);
    }
  }
}

// ラベル画像切り出し
static void cutLabelSprite(uint8_t labelIndex) {
  if (labelIndex >= kLabelSpriteCount) {
    return;
  }

  const uint16_t* src = &labels[labelIndex * kLabelSpriteWidth * kLabelSpriteHeight];
  copyVisualImage(labelSprite[labelIndex], src, kLabelSpriteWidth, kLabelSpriteHeight);
}

// ラベルの描画
static void drawLabelClip(LGFX_Sprite& target, uint8_t labelIndex, int x, int y) {
  if (labelIndex >= kLabelSpriteCount) {
    return;
  }

  target.pushImage(visualX(x, kLabelSpriteWidth), visualY(y, kLabelSpriteHeight), kLabelSpriteWidth,
                   kLabelSpriteHeight, labelSprite[labelIndex]);
}

static uint8_t getNumberGlyphIndex(char c) {
  if (c == '-') {
    return 0;
  }
  if (c >= '0' && c <= '9') {
    return (uint8_t)(10 - (c - '0'));
  }
  return 0;
}

static bool isNoteDisplayTrack(uint8_t trackNo) {
  return trackNo != kPcmTrack && trackNo != kUnusedTrack && trackNo != kSn0NoiseTrack &&
         trackNo != kSn1NoiseTrack;
}

static uint8_t noteInfoToDisplayNoteNo(t_device device, const NoteInfo& ni) {
  if (ni.octave <= 0 || ni.octave > 8 || ni.note < 0 || ni.note > 11) {
    return 0xff;
  }

  int noteNo = -1;
  switch (device) {
    case YM2612_KEY:
      noteNo = ni.octave * 12 + ni.note - 3;
      break;
    case SN76489_0_KEY:
    case SN76489_1_KEY:
    case SN76489_MIX_KEY:
      noteNo = ni.octave * 12 + ni.note;
      break;
    default:
      return 0xff;
  }

  if (noteNo < 0 || noteNo > 96) {
    return 0xff;
  }
  return (uint8_t)noteNo;
}

static uint8_t updateTrackLevelWaterfall(uint8_t trackNo, uint8_t targetLevel) {
  if (trackNo >= kLevelTrackCount) {
    return 0;
  }
  if (targetLevel >= kLevelSpriteCount) {
    targetLevel = kLevelSpriteCount - 1;
  }

  const uint16_t targetQ8 = (uint16_t)(targetLevel << 8);
  uint16_t& displayQ8 = trackLevelDisplayQ8[trackNo];
  uint16_t& fallSpeedQ8 = trackLevelFallSpeedQ8[trackNo];

  if (displayQ8 <= targetQ8) {
    displayQ8 = targetQ8;
    fallSpeedQ8 = 0;
  } else {
    if (fallSpeedQ8 == 0) {
      fallSpeedQ8 = kLevelWaterfallInitialSpeedQ8;
    }
    fallSpeedQ8 = (uint16_t)(fallSpeedQ8 + kLevelWaterfallAccelQ8);
    if (displayQ8 > fallSpeedQ8) {
      displayQ8 = (uint16_t)(displayQ8 - fallSpeedQ8);
    } else {
      displayQ8 = 0;
    }
    if (displayQ8 < targetQ8) {
      displayQ8 = targetQ8;
      fallSpeedQ8 = 0;
    }
  }

  return (uint8_t)(displayQ8 >> 8);
}

static uint8_t updateTrackPeakHold(uint8_t trackNo) {
  if (trackNo >= kLevelTrackCount) {
    return 0;
  }

  const uint16_t currentQ8 = trackLevelDisplayQ8[trackNo];
  uint16_t& peakQ8 = trackPeakDisplayQ8[trackNo];
  uint16_t& fallSpeedQ8 = trackPeakFallSpeedQ8[trackNo];
  uint8_t& holdFrames = trackPeakHoldFrames[trackNo];

  if (peakQ8 <= currentQ8) {
    peakQ8 = currentQ8;
    fallSpeedQ8 = 0;
    holdFrames = kPeakHoldDelayFrames;
  } else if (holdFrames > 0) {
    holdFrames--;
  } else {
    if (fallSpeedQ8 == 0) {
      fallSpeedQ8 = kLevelWaterfallInitialSpeedQ8;
    }
    fallSpeedQ8 = (uint16_t)(fallSpeedQ8 + kLevelWaterfallAccelQ8);
    if (peakQ8 > fallSpeedQ8) {
      peakQ8 = (uint16_t)(peakQ8 - fallSpeedQ8);
    } else {
      peakQ8 = 0;
    }
  }

  return (uint8_t)(peakQ8 >> 8);
}

void redrawOnCore0Task(void* pvParameters) {
  playerWindow.redraw();
  vTaskDelete(NULL);
}

void redrawOnCore0() { xTaskCreateUniversal(redrawOnCore0Task, "task", 8192, NULL, 1, NULL, PRO_CPU_NUM); }

//---------------------------------------------------------------------------
// Scrolling label class
Label::Label(const int16_t x, const int16_t y, const int16_t w, const uint16_t fontColor, const uint16_t bgColor,
             const uint16_t fontSize, const float scrollSpeed, const Align textAlign) {
  _x = x;
  _y = y;
  _labelWidth = w;
  _fontColor = fontColor;
  _bgColor = bgColor;
  _fontSize = fontSize;
  _scrollSpeed = scrollSpeed;
  _textAlign = textAlign;
}

void Label::setCaption(const String newCaption) {
  if (_caption != newCaption) {
    _caption = newCaption;

    OpenFontRender ofr;

    ofr.setUseRenderTask(false);

    ofr.setDrawer(_sprite);
    ofr.loadFont(fontMain, sizeof(fontMain));

    ofr.setFontSize(_fontSize);
    ofr.setFontColor(_fontColor, _bgColor);
    _textWidth = ofr.getTextWidth(_caption.c_str());
    _devWidth = ofr.getTextWidth(TITLE_DEVIDER) + ofr.getTextWidth("/");
    _sprite.deleteSprite();
    _sprite.setPsram(true);

    if (_textWidth > _labelWidth) {
      _caption += TITLE_DEVIDER;
      _isScrolling = true;
      _sprite.createSprite(_textWidth + _devWidth, _fontSize);

      ofr.setAlignment(Align::TopLeft);
      ofr.setCursor(0, 0);
    } else {
      _isScrolling = false;
      _sprite.createSprite(_labelWidth, _fontSize);
      ofr.setAlignment(_textAlign);
      if (_textAlign == Align::TopCenter) {
        ofr.setCursor(_labelWidth / 2, 0);
      } else {
        ofr.setCursor(0, 0);
      }
    }

    _sprite.fillSprite(_bgColor);
    ofr.printf(_caption.c_str());
    ofr.unloadFont();
  }

  _sprite.pushSprite(&frameBuffer, _x, _y);
  _n = 0;
  _scrollCount = 0;
  _startTick = millis();
  _enabled = true;
}

void Label::update() {
  if (!_enabled) return;

  // スクロール回数チェック
  if (ndConfig.get(CFG_SCROLL) == SCROLL_INFINITE || _scrollCount < ndConfig.get(CFG_SCROLL)) {
    if (_isScrolling && (millis() - _startTick >= SCROLL_DELAY)) {
      lcd.setClipRect(_x, _y, _labelWidth, _sprite.height());
      _sprite.pushSprite(&lcd, _x - (int32_t)_n, _y);
      _sprite.pushSprite(&lcd, _x + _textWidth + _devWidth - (int32_t)_n, _y);
      if (_n < _textWidth + _devWidth - _scrollSpeed) {
        _n += _scrollSpeed;
      } else {
        _n = 0;
        _startTick = millis();
        _scrollCount++;
      }
      lcd.setClipRect(0, 0, LCD_W, LCD_H);
    }
  }
}

void Label::setEnabled(bool state) { _enabled = state; }

//---------------------------------------------------------------------------
// Rotated scrolling label class
RotatedLabel::RotatedLabel(const int16_t x, const int16_t y, const int16_t h,
                           const uint16_t fontColor, const uint16_t bgColor,
                           const uint16_t fontSize, const float scrollSpeed,
                           const Align textAlign) {
  _x = x;
  _y = y;
  _labelHeight = h;
  _fontColor = fontColor;
  _bgColor = bgColor;
  _fontSize = fontSize;
  _scrollSpeed = scrollSpeed;
  _textAlign = textAlign;
}

void RotatedLabel::setCaption(String newCaption) {
  if (_caption != newCaption) {
    _caption = newCaption;

    OpenFontRender ofr;
    LGFX_Sprite source(&lcd);
    String renderCaption = _caption;

    ofr.setUseRenderTask(false);
    ofr.loadFont(fontMain, sizeof(fontMain));
    ofr.setFontSize(_fontSize);
    _textWidth = ofr.getTextWidth(_caption.c_str());
    _devWidth = ofr.getTextWidth(TITLE_DEVIDER) + ofr.getTextWidth("/");

    _sprite.deleteSprite();
    _sprite.setPsram(true);
    source.setPsram(true);

    if (_textWidth > _labelHeight) {
      renderCaption += TITLE_DEVIDER;
      _isScrolling = true;
      source.createSprite(_textWidth + _devWidth, _fontSize);
      _sprite.createSprite(_fontSize, _textWidth + _devWidth);
    } else {
      _isScrolling = false;
      source.createSprite(_textWidth > 0 ? _textWidth : 1, _fontSize);
      _sprite.createSprite(_fontSize, _labelHeight > 0 ? _labelHeight : 1);
    }

    source.fillSprite(_bgColor);
    _sprite.fillSprite(_bgColor);

    ofr.setDrawer(source);
    ofr.setFontColor(_fontColor, _bgColor);
    ofr.setAlignment(Align::TopLeft);
    ofr.setCursor(0, 0);
    ofr.printf(renderCaption.c_str());
    ofr.unloadFont();

    const int32_t rotatedHeight = source.width();
    int32_t dstY = 0;
    if (!_isScrolling && _labelHeight > (uint32_t)rotatedHeight) {
      dstY = _labelHeight - rotatedHeight;
    }

    for (int32_t srcY = 0; srcY < source.height(); srcY++) {
      for (int32_t srcX = 0; srcX < source.width(); srcX++) {
        const int32_t dstX = srcY;
        const int32_t dstYPos = dstY + source.width() - 1 - srcX;
        if (dstX >= 0 && dstX < _sprite.width() && dstYPos >= 0 &&
            dstYPos < _sprite.height()) {
          const uint16_t color = source.readPixel(srcX, srcY);
          if (kVisualRotate180) {
            _sprite.drawPixel(_sprite.width() - 1 - dstX, _sprite.height() - 1 - dstYPos, color);
          } else {
            _sprite.drawPixel(dstX, dstYPos, color);
          }
        }
      }
    }

    source.deleteSprite();
  }

  const int32_t initialY = kVisualRotate180
                               ? visualY(_y, _labelHeight)
                               : (int32_t)_y + (int32_t)_labelHeight - _sprite.height();
  const int32_t drawX = visualX(_x, _sprite.width());
  const int32_t clipY = visualY(_y, _labelHeight);
  frameBuffer.setClipRect(drawX, clipY, _sprite.width(), _labelHeight);
  _sprite.pushSprite(&frameBuffer, drawX, initialY);
  frameBuffer.clearClipRect();
  _n = 0;
  _lastDrawOffset = -1;
  _scrollCount = 0;
  _startTick = millis();
  _enabled = true;
}

void RotatedLabel::update() {
  if (!_enabled || !_isScrolling) return;

  const int32_t scrollLimit = ndConfig.get(CFG_SCROLL);
  if (scrollLimit != SCROLL_INFINITE && _scrollCount >= scrollLimit) return;

  uint32_t now = millis();
  if (now - _startTick < SCROLL_DELAY) return;

  const int32_t loopHeight = _sprite.height();
  const int32_t drawOffset = (int32_t)_n;
  const int32_t baseY = kVisualRotate180 ? visualY(_y, _labelHeight)
                                         : (int32_t)_y + (int32_t)_labelHeight - _sprite.height();
  const int32_t drawX = visualX(_x, _sprite.width());
  const int32_t clipY = visualY(_y, _labelHeight);
  if (drawOffset != _lastDrawOffset) {
    lcd.setClipRect(drawX, clipY, _sprite.width(), _labelHeight);
    const int32_t firstY = kVisualRotate180 ? baseY - drawOffset : baseY + drawOffset;
    _sprite.pushSprite(&lcd, drawX, firstY);

    if (drawOffset > loopHeight - (int32_t)_labelHeight) {
      const int32_t secondY =
          kVisualRotate180 ? firstY + loopHeight : baseY + drawOffset - loopHeight;
      _sprite.pushSprite(&lcd, drawX, secondY);
    }
    lcd.clearClipRect();
    _lastDrawOffset = drawOffset;
  }

  if (_n < loopHeight - _scrollSpeed) {
    _n += _scrollSpeed;
  } else {
    _n = 0;
    _lastDrawOffset = -1;
    _startTick = now;
    _scrollCount++;
  }
}

void RotatedLabel::setEnabled(bool state) { _enabled = state; }

//---------------------------------------------------------------------------
// Draw header info
static LGFX_Sprite sprHeader(&lcd);
static constexpr int HEADER_TIME_W = 50;
static constexpr int HEADER_TIME_H = 14;
static constexpr int HEADER_TIME_X = LCD_W / 2 - HEADER_TIME_W / 2;
static constexpr int HEADER_TIME_Y = 4;

static void formatTimestamp(char* buffer, size_t size, int64_t sec) {
  const bool negative = sec < 0;
  uint64_t absSec = negative ? static_cast<uint64_t>(-sec) : static_cast<uint64_t>(sec);
  snprintf(buffer, size, "%s%d:%02d", negative ? "-" : "", (int)(absSec / 60), (int)(absSec % 60));
}

static void drawHeaderTime(LGFX_Sprite& target, int64_t sec, int x, int y, bool visible = true) {
  target.fillRect(x, y, HEADER_TIME_W, HEADER_TIME_H, C_HEADER);
  render.setDrawer(target);
  render.setAlignment(Align::TopCenter);
  render.loadFont(nimbusBold, sizeof(nimbusBold));
  render.setFontSize(14);
  render.setFontColor(TFT_WHITE);
  if (visible) {
    char timestamp[24];
    formatTimestamp(timestamp, sizeof(timestamp), sec);
    render.setCursor(x + HEADER_TIME_W / 2, y);
    render.printf("%s", timestamp);
  }
  render.unloadFont();
}

void PlayerWindow::updateHeader(int64_t sec, bool visible, uint32_t ticksToWait) {
  if (xSemaphoreTake(spFrameBuffer, ticksToWait) == pdTRUE) {
    sprHeader.createSprite(HEADER_TIME_W, HEADER_TIME_H);
    sprHeader.fillSprite(C_HEADER);
    drawHeaderTime(sprHeader, sec, 0, 0, visible);
    sprHeader.pushSprite(HEADER_TIME_X, HEADER_TIME_Y);
    sprHeader.deleteSprite();
    xSemaphoreGive(spFrameBuffer);
  }
}

void PlayerWindow::updateHeaderBlocking(int64_t sec) {
  updateHeader(sec, true, portMAX_DELAY);
}
//---------------------------------------------------------------------------
static bool tryLockDrawing() {
  return xSemaphoreTake(spFrameBuffer, 0) == pdTRUE;
}

static void lockDrawing() {
  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);
}

static void unlockDrawing() {
  xSemaphoreGive(spFrameBuffer);
}

static void setSerialModeLabels() {
  if (ndConfig.get(CFG_LANG) == LANG_JA) {
    lblTitle.setCaption("シリアルモード");
    lblGame.setCaption("ベータ版");
    lblAuthor.setCaption("--");
    lblSystem.setCaption("メガドライブ");
  } else {
    lblTitle.setCaption("Serial Mode");
    lblGame.setCaption("Beta Version");
    lblAuthor.setCaption("--");
    lblSystem.setCaption("Mega Drive / Genesis");
  }
}

static void dispUpdateWorker() {
  switch (disp.currentView) {
    case ViewMode::Player: {  // プレイヤーのとき
      if (tryLockDrawing()) {
        lcd.startWrite();
        lblTitle.update();
        lblGame.update();
        lblAuthor.update();
        lblSystem.update();
        lcd.setClipRect(0, 0, LCD_W, LCD_H);
        lcd.endWrite();
        unlockDrawing();
      }
      int64_t sec = playerWindow.dispData.time;
      if (ND::canPlay == false || ND::isPaused) {
        if (ND::isPaused) {
          // カウントダウン中だけは点滅させず、残り秒数を常時表示する。
          const bool visible = isPlayHoldCountdownActive() || ((millis() / 500) & 1) == 0;
          playerWindow.updateHeader(sec, visible, 0);
        }
        return;
      }

      sec = vgm.getCurrentTime();
      if (playerWindow.dispData.time != sec) {
        playerWindow.updateHeader(sec, true, 0);
        playerWindow.dispData.time = sec;
      }
      break;
    }
    case ViewMode::Visual: {  // ビジュアルモードのとき
      int64_t sec = playerWindow.dispData.time;
      if (ND::canPlay == false || ND::isPaused) {
        if (ND::isPaused) {
          visualWindow.updateLabels();
          // カウントダウン中だけは点滅させず、残り秒数を常時表示する。
          const bool visible = isPlayHoldCountdownActive() || ((millis() / 500) & 1) == 0;
          visualWindow.drawTimestamp(sec, visible);
        }
        return;
      }
      visualWindow.update();
      sec = vgm.getCurrentTime();
      if (playerWindow.dispData.time != sec) {
        visualWindow.drawTimestamp(sec);
        playerWindow.dispData.time = sec;
      }
      break;
    }
    default:
      break;
  }
}

void dispUpdateTask(void* param) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (ulTaskNotifyTake(pdTRUE, 0) > 0) {
      // queue collapse: 最新状態だけ描画
    }
    if (_stopTimerDrawing) {
      continue;
    }
    dispUpdateWorker();
  }
}

void dispTimerHandler(TimerHandle_t timer) {
  if (hDispUpdateTask != nullptr) {
    xTaskNotifyGive(hDispUpdateTask);
  }
}

// 背景描画
void PlayerWindow::drawBG() {
  frameBuffer.fillSprite(C_BASEBG);
  frameBuffer.fillRect(0, 0, LCD_W, 19, C_HEADER);

  // frameBuffer.fillRect(0, 75, LCD_W, 125, C_DARK);
  frameBuffer.fillRoundRect(1, 279, LCD_W - 2, 40, 2, C_DARK);
  frameBuffer.pushImage(8, 211, ICONS_WIDTH, ICONS_HEIGHT, icons);
  frameBuffer.fillRoundRect(6, 283, 17, 14, 2, C_FOOTER_ACTIVE);
  frameBuffer.fillRoundRect(6, 301, 17, 14, 2, C_FOOTER_ACTIVE);
}

// プレーヤー描画
void PlayerWindow::redraw() {
  if (ndConfig.currentMode == MODE_SERIAL) {
    serialModeDraw();
    return;
  }

  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);
  _stopTimerDrawing = true;
  playerWindow.drawBG();
  render.setUseRenderTask(false);
  render.setDrawer(frameBuffer);
  render.setAlignment(Align::TopLeft);

  render.loadFont(fontMain, sizeof(fontMain));
  render.setFontSize(16);
  render.setFontColor(C_GRAY, C_BASEBG);
  render.setCursor(27, 256);
  render.printf(dispData.date.c_str());

  const bool serialMode = ndConfig.currentMode == MODE_SERIAL;

  // シャッフルアイコン
  if (!serialMode && ndConfig.get(CFG_SHUFFLE) != TRANDOM_NO) {
    render.setCursor(3, 2);
    render.setFontSize(16);
    render.setFontColor(C_LIGHTGRAY, C_HEADER);
    render.printf("丂");
  }

  render.unloadFont();

  render.loadFont(nimbusBold, sizeof(nimbusBold));
  render.setFontSize(13);
  render.setFontColor(C_YELLOW, C_DARK);
  render.setCursor(27, 284);
  render.printf(dispData.chip0.c_str());
  render.setCursor(27, 303);
  render.printf(dispData.chip1.c_str());

  render.setFontColor(C_LIGHTGRAY, C_FOOTER_INACTIVE);
  render.setCursor(11, 284);
  render.printf("1");
  render.setCursor(11, 303);
  render.printf("2");

  if (!serialMode && dispData.no != 0 && dispData.maxFiles != 0) {
    render.setFontSize(14);
    render.setFontColor(C_GRAY, C_HEADER);
    render.setCursor(167, 4);
    render.setAlignment(Align::TopRight);
    render.printf("%02d/%02d", dispData.no, dispData.maxFiles);
  }

  if (!serialMode) {
    render.setFontSize(13);
    if (ndFile.accessMode == ACCESS_CACHE) {
      render.setFontColor(C_ACCENT_LIGHT, C_HEADER);
    } else {
      render.setFontColor(C_ORANGE, C_HEADER);
    }

    // シャッフルアイコン分ずらす
    if (ndConfig.get(CFG_SHUFFLE) != TRANDOM_NO) {
      render.setCursor(22, 4);
    } else {
      render.setCursor(4, 4);
    }
    render.setAlignment(Align::TopLeft);
    render.printf(dispData.type.c_str());
  }
  render.unloadFont();

  drawHeaderTime(frameBuffer, dispData.time, HEADER_TIME_X, HEADER_TIME_Y);

  if (serialMode) {
    setSerialModeLabels();
  } else if (ndConfig.get(CFG_LANG) == LANG_JA) {
    lblTitle.setCaption(dispData.trackJp);
    lblGame.setCaption(dispData.gameJp);
    lblAuthor.setCaption(dispData.authorJp);
    lblSystem.setCaption(dispData.systemJp);
  } else {
    lblTitle.setCaption(dispData.trackEn);
    lblGame.setCaption(dispData.gameEn);
    lblAuthor.setCaption(dispData.authorEn);
    lblSystem.setCaption(dispData.systemEn);
  }

  // Snapshot
  // 1) snap/[finemame].png
  // 2) snap/[songno].png
  // 3) ***.png

  if (!serialMode) {
    String fileName = ndFile.getCurrentFileName();
    fileName = fileName.substring(0, fileName.length() - 4);

    String dirPath = ndFile.getCurrentDirPath();
    String filePngName = ndFile.getCurrentFilePngName();
    String dirPngName = ndFile.getCurrentDirPngName();
    if (filePngName != "" && openPNG(dirPath + "/snap", filePngName, true, true) == false) {
      openPNG(dirPath, dirPngName, true, true);
    } else if (filePngName == "" && openPNG(dirPath + "/snap", fileName + ".png", true, true) == false) {
      if (openPNG(dirPath + "/snap", String(dispData.no) + ".png", true, true) == false) {
        openPNG(dirPath, dirPngName, true, true);
      }
    }
  }

  frameBuffer.pushSprite(0, 0);
  _stopTimerDrawing = false;
  xSemaphoreGive(spFrameBuffer);
}

// シリアルモード描画
void serialModeDraw() {
  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);
  _stopTimerDrawing = true;
  playerWindow.drawBG();
  render.setUseRenderTask(false);
  render.setDrawer(frameBuffer);

  frameBuffer.pushImage(170 - 2 - USB_ICON_WIDTH, 2, USB_ICON_WIDTH, USB_ICON_HEIGHT,
                        usb_icon);  // usb icon

  render.setAlignment(Align::TopLeft);

  render.loadFont(nimbusBold, sizeof(nimbusBold));
  render.setFontSize(13);
  render.setFontColor(C_YELLOW, C_DARK);
  render.setCursor(27, 284);

  if (ND::freq[0] != SI5351_UNDEFINED) {
    char buf[7];
    dtostrf((double)ND::freq[0] / 1000000.0, 1, 4, buf);
    String st = "YM2612 @ " + String(buf).substring(0, 5) + " MHz";
    render.printf(st.c_str());
  } else {
    render.printf("YM2612 @ -- MHz");
  }
  render.setCursor(27, 303);
  if (ND::freq[1] != SI5351_UNDEFINED) {
    char buf[7];
    dtostrf((double)ND::freq[1] / 1000000.0, 1, 4, buf);
    String st = "SN76489 @ " + String(buf).substring(0, 5) + " MHz";
    render.printf(st.c_str());
  } else {
    render.printf("SN76489 @ -- MHz");
  }

  render.setFontColor(C_LIGHTGRAY, C_FOOTER_INACTIVE);
  render.setCursor(11, 284);
  render.printf("1");
  render.setCursor(11, 303);
  render.printf("2");

  render.unloadFont();

  render.loadFont(fontMain, sizeof(fontMain));
  render.setFontSize(16);
  render.setFontColor(C_GRAY, C_BASEBG);
  render.setCursor(28, 256);
  render.printf("--");
  render.unloadFont();

  setSerialModeLabels();

  frameBuffer.pushSprite(0, 0);
  xSemaphoreGive(spFrameBuffer);
}

void PlayerWindow::updateDisp(tDispData data) {
  //
  dispData = data;
  dispData.time = 0;

  if (disp.currentView == ViewMode::Player) {
    redraw();
  } else if (disp.currentView == ViewMode::Visual) {
    visualWindow.draw();
  }
}

// Init Display
bool initDisp() {
  lcd.init();
  lcd.setRotation(0);
  lcd.fillScreen(C_BASEBG);

  // 再描画用セマフォ
  spFrameBuffer = xSemaphoreCreateBinary();
  xSemaphoreGive(spFrameBuffer);

  // フレームバッファスプライト作成
  frameBuffer.setPsram(true);
  frameBuffer.createSprite(LCD_W, LCD_H);
  visualWindow.init();

  if (!initPNGRenderer()) {
    return false;
  }

  _stopTimerDrawing = true;

  hDispUpdateTask = NULL;
  BaseType_t taskCreated =
      xTaskCreatePinnedToCore(dispUpdateTask, "dispUpdate", DISP_UPDATE_TASK_STACK, NULL,
                              DISP_UPDATE_TASK_PRIORITY, &hDispUpdateTask, DISP_UPDATE_TASK_CORE);
  if (taskCreated != pdPASS || hDispUpdateTask == NULL) {
    Serial.println("ERROR: dispUpdateTask create failed.");
    return false;
  }

  // タイマー生成
  hDispTimer = xTimerCreate("DISP_TIMER", DISP_TIMER_INTERVAL, pdTRUE, NULL, dispTimerHandler);
  xTimerStart(hDispTimer, 0);

  return true;
}

void startTimer() { xTimerStart(hDispTimer, 0); }
void stopTimer() { xTimerStop(hDispTimer, 0); }

/**
 * @brief PNGファイルを開いて配置する
 *
 * @param dirName   ディレクトリ名 /無し
 * @param fileName  ファイル名
 * @param AA        アンチエイリアス
 * @param toSprite  フレームバッファに配置か直接描画
 * @return true
 * @return false
 */

bool openPNG(String dirName, String fileName, bool AA = false, bool toSprite = true) {
  if (dirName == "" || fileName == "") {
    return false;
  }
  String path = dirName + "/" + fileName;

  if (!loadPNG(path, AA)) {
    const String& error = getPNGErrorMessage();
    if (error != "") {
      frameBuffer.setFont(&fonts::Font2);
      frameBuffer.setCursor(0, 77);
      frameBuffer.printf("%s", error.c_str());
    }
    return false;
  }

  LGFX_Sprite& pngSprite = getPNGSprite();
  if (toSprite) {
    pngSprite.pushSprite(&frameBuffer, 0, 75);
  } else {
    pngSprite.pushSprite(&lcd, 0, 75);
  }

  return true;
}

//---------------------------------------------------------------------------
// 設定画面クラスなど

//---------------------------------------------------------------------------
// Player / Config view management
void PlayerWindow::eventHandler(event ev) {
  if (ev == event::Option) cfgWindow.show();
}

void PlayerWindow::show() {
  disp.currentView = ViewMode::Player;
  disp.lastView = ViewMode::Player;
  ndConfig.saveLastView(LAST_VIEW_PLAYER);
  redraw();
}

//---------------------------------------------------------------------------
// Panel class

Panel::Panel(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t itemHeight,
             IPanelRenderer* renderer)
    : x(x), y(y), width(width), height(height), _itemHeight(itemHeight), _renderer(renderer) {
  _scrollbarBackground.createSprite(SCROLLBAR_WIDTH, height);
  _scrollbar.createSprite(SCROLLBAR_WIDTH, height);
  _scrollbarBackground.fillSprite(C_HIGHGRAY);
  _scrollbarBackground.fillTriangle(SCROLLBAR_WIDTH / 2, 3, SCROLLBAR_WIDTH - 2,
                                    SCROLLBAR_WIDTH - 3, 2, SCROLLBAR_WIDTH - 3, C_MID);
  _scrollbarBackground.fillTriangle(SCROLLBAR_WIDTH / 2, height - 3, SCROLLBAR_WIDTH - 2,
                                    height - SCROLLBAR_WIDTH + 3, 2,
                                    height - SCROLLBAR_WIDTH + 3, C_MID);
}

uint16_t Panel::itemWidth() const { return width - SCROLLBAR_WIDTH; }

// アイテムカウントを更新
void Panel::setItemCount(int newCount) {
  if (_itemCount != newCount) _needsFullRedraw = true;
  _itemCount = newCount;
  _innerHeight = _itemCount * _itemHeight;
}

// アイテムを見える場所に持ってくる
void Panel::ensureVisible() {
  const int itemTop = currentIndex * _itemHeight;
  const int itemBottom = itemTop + _itemHeight;
  if (itemTop < scrollTop) {
    scrollTop = itemTop;
  } else if (itemBottom > scrollTop + height) {
    scrollTop = itemBottom - height;
  }
  int maxScroll = _innerHeight - height;
  if (maxScroll < 0) maxScroll = 0;
  if (scrollTop < 0) scrollTop = 0;
  if (scrollTop > maxScroll) scrollTop = maxScroll;
}

void Panel::invalidate() { _needsFullRedraw = true; }

// スクロールバー描画
void Panel::drawScrollbar() {
  // スクロールバー背景描画
  _scrollbarBackground.pushSprite(&_scrollbar, 0, 0);
  if (_innerHeight > height) {
    const uint16_t maxBarHeight = height - INDICATOR_HEIGHT * 2;
    const uint16_t barHeight = maxBarHeight * height / _innerHeight;
    const uint16_t barTop = maxBarHeight * scrollTop / _innerHeight;
    _scrollbar.fillRoundRect(PADDING, INDICATOR_HEIGHT + barTop,
                             SCROLLBAR_WIDTH - PADDING * 2, barHeight,
                             (SCROLLBAR_WIDTH - PADDING * 2) / 2, C_MID);
  }
}

void Panel::redrawItem(LGFX_Sprite& targetBuffer, int index) {
  if (_renderer == nullptr || index < 0 || index >= _itemCount) return;
  int firstIndex = scrollTop / _itemHeight;
  int lastIndex = (scrollTop + height - 1) / _itemHeight;
  if (firstIndex < 0) firstIndex = 0;
  if (lastIndex >= _itemCount) lastIndex = _itemCount - 1;
  if (index < firstIndex || index > lastIndex) return;

  const int drawY = y + index * _itemHeight - scrollTop;
  _renderer->onDrawItem(targetBuffer, index, x, drawY, itemWidth(), index == currentIndex);
}

void Panel::redrawScrollEdgeItems(LGFX_Sprite& targetBuffer, int scrollDelta) {
  if (scrollDelta == 0 || _itemCount <= 0) return;
  int firstIndex = scrollTop / _itemHeight;
  int lastIndex = (scrollTop + height - 1) / _itemHeight;
  const int redrawCount = (abs(scrollDelta) + _itemHeight - 1) / _itemHeight + 1;
  if (firstIndex < 0) firstIndex = 0;
  if (lastIndex >= _itemCount) lastIndex = _itemCount - 1;

  if (scrollDelta > 0) {
    for (int i = 0; i < redrawCount; i++) redrawItem(targetBuffer, lastIndex - i);
  } else {
    for (int i = 0; i < redrawCount; i++) redrawItem(targetBuffer, firstIndex + i);
  }
}

void Panel::drawVisibleItems(LGFX_Sprite& targetBuffer) {
  if (_renderer == nullptr) return;
  int firstIndex = scrollTop / _itemHeight;
  int lastIndex = (scrollTop + height - 1) / _itemHeight;
  if (firstIndex < 0) firstIndex = 0;
  if (lastIndex >= _itemCount) lastIndex = _itemCount - 1;
  for (int index = firstIndex; index <= lastIndex; index++) {
    const int drawY = y + index * _itemHeight - scrollTop;
    _renderer->onDrawItem(targetBuffer, index, x, drawY, itemWidth(), index == currentIndex);
  }
}

// フレームバッファに内容を描画
void Panel::update(LGFX_Sprite& targetBuffer) {
  const int scrollDelta = scrollTop - _prevScrollTop;
  const bool reuseScrolledContent = !_needsFullRedraw && _itemCount == _prevItemCount &&
                                    scrollDelta != 0 && abs(scrollDelta) < height;
  const bool fullRedraw = _needsFullRedraw || _itemCount != _prevItemCount ||
                          (scrollTop != _prevScrollTop && !reuseScrolledContent);
  const int contentWidth = itemWidth();

  targetBuffer.setClipRect(x, y, contentWidth, height);
  if (fullRedraw) targetBuffer.fillRect(x, y, contentWidth, height, TFT_WHITE);

  if (fullRedraw) {
    drawVisibleItems(targetBuffer);
  } else if (reuseScrolledContent) {
    if (scrollDelta > 0) {
      targetBuffer.copyRect(x, y, contentWidth, height - scrollDelta, x, y + scrollDelta);
      targetBuffer.fillRect(x, y + height - scrollDelta, contentWidth, scrollDelta, TFT_WHITE);
    } else {
      const int shift = -scrollDelta;
      targetBuffer.copyRect(x, y + shift, contentWidth, height - shift, x, y);
      targetBuffer.fillRect(x, y, contentWidth, shift, TFT_WHITE);
    }
    redrawScrollEdgeItems(targetBuffer, scrollDelta);
    if (_prevCurrentIndex != currentIndex) {
      redrawItem(targetBuffer, _prevCurrentIndex);
      redrawItem(targetBuffer, currentIndex);
    }
  } else if (currentIndex != _prevCurrentIndex) {
    redrawItem(targetBuffer, _prevCurrentIndex);
    redrawItem(targetBuffer, currentIndex);
  }
  targetBuffer.clearClipRect();

  // スクロールバーをバッファに描画
  drawScrollbar();

  // スクロールバースプライト転送
  _scrollbar.pushSprite(&targetBuffer, x + width - SCROLLBAR_WIDTH, y);
  _prevCurrentIndex = currentIndex;
  _prevScrollTop = scrollTop;
  _prevItemCount = _itemCount;
  _needsFullRedraw = false;
}

//---------------------------------------------------------------------------
// 設定画面クラス
// 項目描画用
ConfigPanelRenderer configRenderer;
static Panel pnlConfig(0, 26, LCD_W, 264, CFG_ITEM_HEIGHT, &configRenderer);

void ConfigPanelRenderer::onDrawItem(LGFX_Sprite& target, int itemIndex, int x, int y,
                                     int width, bool selected) {
  cfgWindow.drawItem(target, itemIndex, x, y, width, selected);
}

void CFGWindow::init() {
  _sprite.createSprite(LCD_W, CFG_ITEM_HEIGHT);
  _sprFooter.createSprite(LCD_W, 23);

  // ヘッダーの初期化
  initHeaders();
}

// ヘッダ部初期化
void CFGWindow::initHeaders() {
  // スプライトが既に初期化されているかチェック
  if (_sprHeaderJP.width() > 0 && _sprHeaderEN.width() > 0) return;
  if (spFrameBuffer == nullptr || xSemaphoreTake(spFrameBuffer, portMAX_DELAY) != pdTRUE) return;

  OpenFontRender render;
  render.setUseRenderTask(false);

  // 日本語ヘッダー作成
  _sprHeaderJP.setPsram(true);
  _sprHeaderJP.createSprite(LCD_W, 26);
  _sprHeaderJP.fillSprite(C_HEADER);
  _sprHeaderJP.pushImage(146, 3, CFG_ICON_WIDTH, CFG_ICON_HEIGHT, cfgIcon);
  render.setDrawer(_sprHeaderJP);
  render.setAlignment(Align::TopLeft);
  render.loadFont(fontMain, sizeof(fontMain));
  render.setFontSize(17);
  render.setFontColor(C_LIGHTGRAY, C_HEADER);
  render.setCursor(6, 4);
  render.printf("設定");
  render.unloadFont();

  // 英語ヘッダー作成
  _sprHeaderEN.setPsram(true);
  _sprHeaderEN.createSprite(LCD_W, 26);
  _sprHeaderEN.fillSprite(C_HEADER);
  _sprHeaderEN.pushImage(146, 3, CFG_ICON_WIDTH, CFG_ICON_HEIGHT, cfgIcon);
  render.setDrawer(_sprHeaderEN);
  render.setAlignment(Align::TopLeft);
  render.loadFont(fontMain, sizeof(fontMain));
  render.setFontSize(16);
  render.setFontColor(C_LIGHTGRAY, C_HEADER);
  render.setCursor(6, 4);
  render.printf("Settings");
  render.unloadFont();

  xSemaphoreGive(spFrameBuffer);
}

// 表示
void CFGWindow::show() {
  const bool isOpening = disp.currentView != ViewMode::Config;
  if (isOpening) {
    _isChanged = false;
  }
  disp.currentView = ViewMode::Config;
  _stopTimerDrawing = true;
  drawPanelView();
}

void CFGWindow::close() {
  const bool modeChanged = (tMode)ndConfig.get(CFG_MODE) != ndConfig.currentMode;
  if (_isChanged) {
    if (modeChanged) {
      // 動作モード変更時はこの直後に再起動するため、非同期保存キューを待たずに同期保存する。
      ndConfig.saveCfgNow();
    } else {
      ndConfig.saveCfg();
    }
    _isChanged = false;
  }
  if (modeChanged) {
    ESP.restart();
    return;
  }
  visualWindow.show();
}

void CFGWindow::eventHandler(event ev) {
  if (disp.currentView != ViewMode::Config) return;
  switch (ev) {
    case event::Up:
      moveSelection(-1);
      break;
    case event::Down:
      moveSelection(1);
      break;
    case event::Left:
      if (ndConfig.items[currentItemIndex].index > 0) {
        int prevRandom = ndConfig.get(CFG_SHUFFLE);
        ndConfig.items[currentItemIndex].index--;
        const tConfig changedItem = ndConfig.configAt(currentItemIndex);
        ndConfig.applyItem(changedItem);
        _isChanged = true;

        // シャッフル設定の切り替え時は、シャッフルステートをリセット
        if (changedItem == CFG_SHUFFLE && prevRandom != ndConfig.get(CFG_SHUFFLE)) {
          ndFile.resetRandomSession();
        }

        // 言語はすぐ再描画
        if (changedItem == CFG_LANG) {
          drawPanelView();
        } else {
          refreshCurrentItem();
        }
      }
      break;
    case event::Right:
      if (ndConfig.items[currentItemIndex].index + 1 <
          ndConfig.items[currentItemIndex].optionValues.size()) {
        int prevRandom = ndConfig.get(CFG_SHUFFLE);
        ndConfig.items[currentItemIndex].index++;
        const tConfig changedItem = ndConfig.configAt(currentItemIndex);
        ndConfig.applyItem(changedItem);
        _isChanged = true;

        // シャッフル設定の切り替え時は、シャッフルステートをリセット
        if (changedItem == CFG_SHUFFLE && prevRandom != ndConfig.get(CFG_SHUFFLE)) {
          ndFile.resetRandomSession();
        }

        // 言語はすぐ再描画
        if (changedItem == CFG_LANG) {
          drawPanelView();
        } else {
          refreshCurrentItem();
        }
      }
      break;
    case event::Close:
      close();
      break;
    default:
      break;
  }
}

void CFGWindow::drawPanelView() {
  if (ndConfig.items.empty()) return;
  if (currentItemIndex < 0) currentItemIndex = 0;
  if (currentItemIndex >= ndConfig.items.size()) currentItemIndex = ndConfig.items.size() - 1;

  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);
  frameBuffer.fillSprite(TFT_WHITE);
  if (ndConfig.get(CFG_LANG) == LANG_JA) {
    _sprHeaderJP.pushSprite(&frameBuffer, 0, 0);
  } else {
    _sprHeaderEN.pushSprite(&frameBuffer, 0, 0);
  }
  pnlConfig.setItemCount(ndConfig.items.size());
  pnlConfig.currentIndex = currentItemIndex;
  pnlConfig.ensureVisible();
  pnlConfig.invalidate();
  pnlConfig.update(frameBuffer);
  currentItemIndex = pnlConfig.currentIndex;
  drawFooter(true);
  frameBuffer.pushSprite(0, 0);
  xSemaphoreGive(spFrameBuffer);
}

void CFGWindow::selectItem(int index) {
  if (ndConfig.items.empty()) return;
  if (index < 0) index = 0;
  if (index >= ndConfig.items.size()) index = ndConfig.items.size() - 1;
  if (index == currentItemIndex) return;

  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);
  const int previousItemIndex = currentItemIndex;
  const int previousScrollTop = pnlConfig.scrollTop;
  currentItemIndex = index;
  pnlConfig.setItemCount(ndConfig.items.size());
  pnlConfig.currentIndex = currentItemIndex;
  pnlConfig.ensureVisible();

  const int previousItemY = pnlConfig.y + previousItemIndex * CFG_ITEM_HEIGHT - pnlConfig.scrollTop;
  const int currentItemY = pnlConfig.y + currentItemIndex * CFG_ITEM_HEIGHT - pnlConfig.scrollTop;
  const bool scrollUnchanged = pnlConfig.scrollTop == previousScrollTop;
  const bool previousFullyVisible =
      previousItemY >= pnlConfig.y && previousItemY + CFG_ITEM_HEIGHT <= pnlConfig.y + pnlConfig.height;
  const bool currentFullyVisible =
      currentItemY >= pnlConfig.y && currentItemY + CFG_ITEM_HEIGHT <= pnlConfig.y + pnlConfig.height;

  if (scrollUnchanged && previousFullyVisible && currentFullyVisible) {
    // スクロール不要な上下移動では全画面 frameBuffer 転送を避ける。
    // XGM PCM送信中は長いLCD転送だけでもタイミング揺れになるため、更新は2行+フッタに限定する。
    drawItem(frameBuffer, previousItemIndex, pnlConfig.x, previousItemY, pnlConfig.itemWidth(), false);
    _sprite.pushSprite(&lcd, pnlConfig.x, previousItemY);
    drawItem(frameBuffer, currentItemIndex, pnlConfig.x, currentItemY, pnlConfig.itemWidth(), true);
    _sprite.pushSprite(&lcd, pnlConfig.x, currentItemY);
    drawFooter(false);
  } else {
    pnlConfig.update(frameBuffer);
    currentItemIndex = pnlConfig.currentIndex;
    drawFooter(true);
    frameBuffer.pushSprite(0, 0);
  }
  xSemaphoreGive(spFrameBuffer);
}

void CFGWindow::refreshCurrentItem() {
  if (ndConfig.items.empty()) return;

  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);
  pnlConfig.setItemCount(ndConfig.items.size());
  pnlConfig.currentIndex = currentItemIndex;
  pnlConfig.ensureVisible();
  currentItemIndex = pnlConfig.currentIndex;

  const int itemY = pnlConfig.y + currentItemIndex * CFG_ITEM_HEIGHT - pnlConfig.scrollTop;
  const bool fullyVisible =
      itemY >= pnlConfig.y && itemY + CFG_ITEM_HEIGHT <= pnlConfig.y + pnlConfig.height;
  if (fullyVisible) {
    drawItem(frameBuffer, currentItemIndex, pnlConfig.x, itemY, pnlConfig.itemWidth(), true);
    _sprite.pushSprite(&lcd, pnlConfig.x, itemY);
    drawFooter(true);
    _sprFooter.pushSprite(&lcd, 0, 293);
  } else {
    pnlConfig.invalidate();
    pnlConfig.update(frameBuffer);
    drawFooter(true);
    frameBuffer.pushSprite(0, 0);
  }
  xSemaphoreGive(spFrameBuffer);
}

void CFGWindow::moveSelection(int delta) { selectItem(currentItemIndex + delta); }

void CFGWindow::drawItem(LGFX_Sprite& target, int index, int x, int y, int width,
                         bool selected) {
  if (index < 0 || index >= ndConfig.items.size() || width <= 0) return;
  if (_sprite.width() != width || _sprite.height() != CFG_ITEM_HEIGHT) {
    _sprite.createSprite(width, CFG_ITEM_HEIGHT);
  }

  const uint16_t titleColor = selected ? TFT_WHITE : C_DARK;
  const uint16_t optionColor = selected ? C_YELLOW : C_MID;
  const uint16_t background = selected ? C_LV_PEAK : TFT_WHITE;

  OpenFontRender ofr;
  ofr.setDrawer(_sprite);
  ofr.loadFont(fontMain, sizeof(fontMain));
  const int fontSize = ndConfig.get(CFG_LANG) == LANG_JA ? 17 : 16;
  const String label = ndConfig.get(CFG_LANG) == LANG_JA ? ndConfig.items[index].labelJp
                                                         : ndConfig.items[index].labelEn;
  const String option = ndConfig.get(CFG_LANG) == LANG_JA
                            ? ndConfig.items[index].optionsJp[ndConfig.items[index].index]
                            : ndConfig.items[index].optionsEn[ndConfig.items[index].index];

  _sprite.fillSprite(background);
  _sprite.drawLine(0, CFG_ITEM_HEIGHT - 1, width - 1, CFG_ITEM_HEIGHT - 1, C_BORDER);
  ofr.setFontSize(fontSize);
  ofr.setAlignment(Align::TopLeft);
  ofr.setFontColor(titleColor, background);
  ofr.setCursor(5, (CFG_ITEM_HEIGHT - fontSize) / 2);
  ofr.printf(label.c_str());
  ofr.setAlignment(Align::TopRight);
  ofr.setFontColor(optionColor, background);
  ofr.setCursor(width - 5, (CFG_ITEM_HEIGHT - fontSize) / 2);
  ofr.printf(option.c_str());
  ofr.unloadFont();
  _sprite.pushSprite(&target, x, y);
}

void CFGWindow::drawFooter(bool toFrameBuffer) {
  const bool up = currentItemIndex > 0;
  const bool down = currentItemIndex + 1 < ndConfig.items.size();
  const bool left = ndConfig.items[currentItemIndex].index > 0;
  const bool right = ndConfig.items[currentItemIndex].index + 1 <
                     ndConfig.items[currentItemIndex].optionValues.size();

  _sprFooter.fillSprite(TFT_WHITE);
  _sprFooter.fillRoundRect(4, 0, 27, 23, 2, up ? C_FOOTER_ACTIVE : C_FOOTER_INACTIVE);
  _sprFooter.fillRoundRect(33, 0, 27, 23, 2, down ? C_FOOTER_ACTIVE : C_FOOTER_INACTIVE);
  _sprFooter.fillRoundRect(64, 0, 27, 23, 2, left ? C_FOOTER_ACTIVE : C_FOOTER_INACTIVE);
  _sprFooter.fillRoundRect(93, 0, 27, 23, 2, right ? C_FOOTER_ACTIVE : C_FOOTER_INACTIVE);
  _sprFooter.fillRoundRect(124, 0, 42, 23, 2, C_FOOTER_ACTIVE);
  if (up) _sprFooter.pushImage(12, 6, CFG_ICON_ARROR_WIDTH, CFG_ICON_ARROR_HEIGHT, cfgUP);
  if (down) _sprFooter.pushImage(41, 6, CFG_ICON_ARROR_WIDTH, CFG_ICON_ARROR_HEIGHT, cfgDOWN);
  if (left) _sprFooter.pushImage(72, 6, CFG_ICON_ARROR_WIDTH, CFG_ICON_ARROR_HEIGHT, cfgLEFT);
  if (right) _sprFooter.pushImage(101, 6, CFG_ICON_ARROR_WIDTH, CFG_ICON_ARROR_HEIGHT, cfgRIGHT);

  OpenFontRender ofr;
  ofr.setUseRenderTask(false);
  ofr.setDrawer(_sprFooter);
  ofr.loadFont(fontMain, sizeof(fontMain));
  ofr.setFontSize(18);
  ofr.setAlignment(Align::TopCenter);
  ofr.setFontColor(C_LIGHTGRAY, C_FOOTER_ACTIVE);
  ofr.setCursor(124 + 42 / 2, 4);
  ofr.printf("OK");
  ofr.unloadFont();
  if (toFrameBuffer) {
    _sprFooter.pushSprite(&frameBuffer, 0, 293);
  } else {
    _sprFooter.pushSprite(&lcd, 0, 293);
  }
}

//---------------------------------------------------------------------------
// Visual view
void VisualWindow::init() {
  keyboardBuffer.createSprite(keyboard2Width, keyboard2Height);
  pushPreparedVisualImage(keyboardBuffer, 0, 0, keyboard2Width, keyboard2Height, keyboard2);
  keyboardBufferSub.createSprite(keyboard2Width, keyboard2Height);

  cutPanMarkerSprite(panMarkerCenter, 0);
  cutPanMarkerSprite(panMarkerLeft, 1);
  cutPanMarkerSprite(panMarkerRight, 2);
  cutPanMarkerSprite(panMarkerMute, 3);
  for (uint8_t i = 0; i < kLevelSpriteCount; i++) {
    cutLevelSprite(i);
  }

  for (uint8_t i = 0; i < kNumberSpriteCount; i++) {
    cutNumberSprite(i);
  }

  for (uint8_t i = 0; i < kLabelSpriteCount; i++) {
    cutLabelSprite(i);
  }

  createVisualShuffleIconSprite(_sprShuffleOn, C_MDX_ON);
  createVisualShuffleIconSprite(_sprShuffleOff, C_MDX_OFF);

  _sprTime.setPsram(false);
  _sprTime.createSprite(14, 48);
}

void VisualWindow::drawTimestamp(int64_t sec) {
  lockDrawing();
  drawTimestamp(sec, false, true);
  unlockDrawing();
}

void VisualWindow::drawTimestamp(int64_t sec, bool visible) {
  lockDrawing();
  drawTimestamp(sec, false, visible);
  unlockDrawing();
}

void VisualWindow::drawTimestamp(int64_t sec, bool toFrameBuffer, bool visible) {
  if (_sprTime.width() == 0) {
    return;
  }
  if (_lastTimestampSec == sec && _lastTimestampVisible == visible) {
    return;
  }

  static constexpr int kTimestampSourceW = 48;
  static constexpr int kTimestampSourceH = 14;
  LGFX_Sprite source(&lcd);
  source.setPsram(false);
  source.createSprite(kTimestampSourceW, kTimestampSourceH);
  if (source.width() == 0) {
    return;
  }

  source.fillSprite(TFT_BLACK);
  render.setDrawer(source);
  render.setAlignment(Align::TopRight);
  render.loadFont(nimbusBold, sizeof(nimbusBold));
  render.setFontSize(14);
  render.setFontColor(C_MDX_ON, TFT_BLACK);
  if (visible) {
    char timestamp[24];
    formatTimestamp(timestamp, sizeof(timestamp), sec);
    render.setCursor(source.width(), 0);
    render.printf("%s", timestamp);
  }
  render.unloadFont();

  _sprTime.fillSprite(TFT_BLACK);
  source.setPivot(source.width() / 2.0f, source.height() / 2.0f);
  const float angle = kVisualRotate180 ? 90.0f : -90.0f;
  source.pushRotateZoom(&_sprTime, _sprTime.width() / 2.0f, _sprTime.height() / 2.0f,
                        angle, 1.0f, 1.0f, TFT_BLACK);
  source.deleteSprite();

  const int x = LCD_W - _sprTime.width() - 2;
  const int y = 0;
  if (toFrameBuffer) {
    pushVisualSpriteToFrameBuffer(_sprTime, x, y);
  } else {
    pushVisualSpriteToLcd(_sprTime, x, y);
  }
  _lastTimestampSec = sec;
  _lastTimestampVisible = visible;
}

void VisualWindow::draw() {
  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);
  _stopTimerDrawing = true;

  frameBuffer.fillSprite(TFT_BLACK);
  pushVisualImage(frameBuffer, 0, 0, keyboardWidth, keyboardHeight, keyboard);

  switch (ND::fileFormat) {
    case FileFormat::VGM:
    case FileFormat::VGZ:
      drawLabelClip(frameBuffer, 2, kLabelVgmX, kLabelVgmY);
      break;
    case FileFormat::XGM1:
      drawLabelClip(frameBuffer, 1, kLabelXgm1X, kLabelXgm1Y);
      break;
    case FileFormat::XGM2:
      drawLabelClip(frameBuffer, 0, kLabelXgm2X, kLabelXgm2Y);
      break;
    default:
      break;
  }

  String songTitle;
  if (ndConfig.get(CFG_LANG) == LANG_JA) {
    songTitle = playerWindow.dispData.trackJp;
    if (playerWindow.dispData.gameJp != "") {
      songTitle += " / " + playerWindow.dispData.gameJp;
    }
  } else {
    songTitle = playerWindow.dispData.trackEn;
    if (playerWindow.dispData.gameEn != "") {
      songTitle += " / " + playerWindow.dispData.gameEn;
    }
  }
  lblSongTitle.setCaption(songTitle);

  // シャッフル再生のアイコン表示
  if (ndConfig.get(CFG_SHUFFLE) != TRANDOM_NO) {
    pushVisualSpriteToFrameBuffer(_sprShuffleOn, 154, 113);
  } else {
    pushVisualSpriteToFrameBuffer(_sprShuffleOff, 154, 113);
  }

  _lastTimestampSec = INT64_MAX;
  _lastTimestampVisible = false;
  drawTimestamp(playerWindow.dispData.time, true, true);
  frameBuffer.pushSprite(0, 0);

  for (int i = 0; i < kLevelTrackCount; i++) {
    lastTrackLevel[i] = -1;
    lastTrackPeak[i] = -1;
    trackLevelDisplayQ8[i] = 0;
    trackLevelFallSpeedQ8[i] = 0;
    trackPeakDisplayQ8[i] = 0;
    trackPeakFallSpeedQ8[i] = 0;
    trackPeakHoldFrames[i] = 0;
  }
  for (int i = 0; i < kPanTrackCount; i++) {
    lastTrackPan[i] = -1;
  }
  for (int i = 0; i < kNoteTrackCount; i++) {
    lastTrackNote[i] = -1;
    heldTrackNote[i] = 0xff;
  }

  _stopTimerDrawing = false;
  xSemaphoreGive(spFrameBuffer);
}

void VisualWindow::update() {
  if (disp.currentView != ViewMode::Visual) {
    return;
  }

  NoteInfo keySnapshot[kFmTrackCount];
  NoteInfo sn0KeySnapshot[4];
  NoteInfo sn1KeySnapshot[4];
  NoteInfo snKeySnapshot[8];
  tPan panSnapshot[kPanTrackCount];
  uint8_t levelSnapshot[kLevelTrackCount];
  if (xSemaphoreTake(KeyBoard.keyinfoMutex, 0) != pdTRUE) {
    return;
  }
  memcpy(keySnapshot, KeyBoard.keyInfo[YM2612_KEY], sizeof(keySnapshot));
  memcpy(sn0KeySnapshot, KeyBoard.keyInfo[SN76489_0_KEY], sizeof(sn0KeySnapshot));
  memcpy(sn1KeySnapshot, KeyBoard.keyInfo[SN76489_1_KEY], sizeof(sn1KeySnapshot));
  memcpy(panSnapshot, KeyBoard.trackPan, sizeof(panSnapshot));
  memcpy(levelSnapshot, KeyBoard.trackLevel, sizeof(levelSnapshot));
  // Peak meters are latched one-shot values. Clear the sampled source and let the waterfall
  // decay.
  memset(KeyBoard.trackLevel, 0, sizeof(KeyBoard.trackLevel));
  xSemaphoreGive(KeyBoard.keyinfoMutex);

  uint8_t noteSnapshot[kNoteTrackCount];
  memset(noteSnapshot, 0xff, sizeof(noteSnapshot));
  for (int i = 0; i < kFmTrackCount; i++) {
    uint8_t ymNote = noteInfoToDisplayNoteNo(YM2612_KEY, keySnapshot[i]);
    if (ymNote != 0xff) {
      heldTrackNote[i] = ymNote;
    }
    noteSnapshot[i] = heldTrackNote[i];
  }
  for (int i = 0; i < 3; i++) {
    uint8_t sn0Note = noteInfoToDisplayNoteNo(SN76489_0_KEY, sn0KeySnapshot[i]);
    const int sn0Track = kSn0ToneTrackFirst + i;  // UI Track 9-11: SN76489 (1) tone
    if (sn0Note != 0xff) {
      heldTrackNote[sn0Track] = sn0Note;
    }
    noteSnapshot[sn0Track] = heldTrackNote[sn0Track];

    uint8_t sn1Note = noteInfoToDisplayNoteNo(SN76489_1_KEY, sn1KeySnapshot[i]);
    const int sn1Track = kSn1ToneTrackFirst + i;  // UI Track 13-15: SN76489 (2) tone
    if (sn1Note != 0xff) {
      heldTrackNote[sn1Track] = sn1Note;
    }
    noteSnapshot[sn1Track] = heldTrackNote[sn1Track];
  }
  for (int i = 0; i < 4; i++) {
    snKeySnapshot[i] = sn0KeySnapshot[i];
    snKeySnapshot[i + 4] = sn1KeySnapshot[i];
  }

  if (!tryLockDrawing()) {
    return;
  }

  keyboardBuffer.pushSprite(&keyboardBufferSub, 0, 0);
  drawKeyboard(keyboardBufferSub, YM2612_KEY, keySnapshot);
  pushVisualSpriteToLcd(keyboardBufferSub, 2, 0);

  keyboardBuffer.pushSprite(&keyboardBufferSub, 0, 0);
  drawKeyboard(keyboardBufferSub, SN76489_MIX_KEY, snKeySnapshot);
  pushVisualSpriteToLcd(keyboardBufferSub, 30, 0);

  for (int i = 0; i < kLevelTrackCount; i++) {
    uint8_t displayLevel = updateTrackLevelWaterfall(i, levelSnapshot[i]);
    uint8_t peakLevel = updateTrackPeakHold(i);
    if (lastTrackLevel[i] != displayLevel || lastTrackPeak[i] != peakLevel) {
      drawLevel(i, displayLevel, peakLevel);
      lastTrackLevel[i] = displayLevel;
      lastTrackPeak[i] = peakLevel;
    }
    if (isNoteDisplayTrack(i) && lastTrackNote[i] != noteSnapshot[i]) {
      drawNote(i, noteSnapshot[i]);
      lastTrackNote[i] = noteSnapshot[i];
    }
    if (i >= kPanTrackCount || i == kUnusedTrack) {
      continue;
    }
    const bool monoPanTrack = i == kPcmTrack || i >= kSn0ToneTrackFirst;
    if (monoPanTrack) {
      if (levelSnapshot[i] > 0 && lastTrackPan[i] != PAN_CENTER) {
        drawPan(i, PAN_CENTER);
        lastTrackPan[i] = PAN_CENTER;
      }
      continue;
    }
    if (lastTrackPan[i] != panSnapshot[i]) {
      drawPan(i, panSnapshot[i]);
      lastTrackPan[i] = panSnapshot[i];
    }
  }

  lblSongTitle.update();
  unlockDrawing();
}

void VisualWindow::updateLabels() {
  if (!tryLockDrawing()) {
    return;
  }

  // 再生ホールド中も Visual 画面の曲名スクロールだけは止めない。
  lblSongTitle.update();
  unlockDrawing();
}

// 個別のキーボードを描画する
// 戻り値: true 描画更新した
boolean VisualWindow::drawKeyboard(LGFX_Sprite& sprite, t_device device, const NoteInfo* notes) {
  // 鍵盤描画位置用定数
  const int notePos[12] = {5, 7, 10, 12, 15, 20, 22, 25, 27, 30, 32, 35};
  const int noteWidth[12] = {4, 3, 4, 3, 4, 4, 3, 4, 3, 4, 3, 4};

  bool touched = false;
  const uint16_t keyOnColor = static_cast<uint16_t>(ndConfig.get(CFG_KEYON));

  for (int i = 0; i < device_channels[device]; i++) {
    int oct = notes[i].octave;
    int note = notes[i].note;
    if (oct > 0 || (oct == 0 && note > 8)) {  // オクターブ0は A以上
      if (note < 0 || note > 11) {
        continue;
      }

      touched = true;
      sprite.setColor(keyOnColor);
      int y = keyboard2Height - ((oct - 1) * 35 + notePos[note]) - 10;
      if (y < 0 || y >= keyboard2Height) {
        continue;
      }

      if (noteWidth[note] == 3) {
        fillVisualRect(sprite, 0, y, 15, 3);  // 黒鍵
      } else {
        // keyboard2Width=25 に対して x=15 の白鍵側は 9px 幅が上限。
        fillVisualRect(sprite, 15, y, 10, 4);
        switch (note) {
          case 0:
          case 5:
            fillVisualRect(sprite, 0, y + 1, 15, 3);
            break;
          case 4:
          case 11:
            fillVisualRect(sprite, 0, y, 15, 3);
            break;
          case 2:
          case 7:
          case 9:
            fillVisualRect(sprite, 0, y + 1, 15, 2);
            break;
        }
      }
    }
  }
  return touched;
}

boolean VisualWindow::drawPan(uint8_t trackNo, tPan pan) {
  LGFX_Sprite* marker = &panMarkerCenter;
  if (pan == PAN_LEFT) {
    marker = &panMarkerLeft;
  } else if (pan == PAN_RIGHT) {
    marker = &panMarkerRight;
  } else if (pan == PAN_MUTE) {
    marker = &panMarkerMute;
  }

  marker->pushSprite(visualX(99, marker->width()), visualY(264 - trackNo * 17, marker->height()));
  return true;
}

boolean VisualWindow::drawLevel(uint8_t trackNo, uint8_t level, uint8_t peakLevel) {
  if (trackNo >= kLevelTrackCount) {
    return false;
  }
  if (level >= kLevelSpriteCount) {
    level = kLevelSpriteCount - 1;
  }
  if (peakLevel >= kLevelSpriteCount) {
    peakLevel = kLevelSpriteCount - 1;
  }

  // levels.png は上から大→小の順なので、表示レベルからスプライト番号を反転する。
  const uint8_t spriteIndex = (kLevelSpriteCount - 1) - level;
  memcpy(levelWorkBuffer, levelSprite[spriteIndex], sizeof(levelWorkBuffer));

  if (peakLevel > 0) {
    int peakX = kPeakLineX0 - (int)((peakLevel - 1) * 2);
    if (peakX < 1) {
      peakX = 1;
    }
    if (peakX >= kLevelSpriteWidth) {
      peakX = kLevelSpriteWidth - 1;
    }

    for (uint8_t peakY = kPeakLineYTop; peakY <= kPeakLineYBottom; peakY++) {
      const int drawX = kVisualRotate180 ? (kLevelSpriteWidth - 1 - peakX) : peakX;
      const int drawY = kVisualRotate180 ? (kLevelSpriteHeight - 1 - peakY) : peakY;
      levelWorkBuffer[drawY * kLevelSpriteWidth + drawX] = C_LV_PEAK;
    }
  }

  const int y = kLevelDrawBottomY - trackNo * kLevelSpriteHeight;
  pushVisualBufferToLcd(kLevelDrawX, y, kLevelSpriteWidth, kLevelSpriteHeight, levelWorkBuffer);
  return true;
}

boolean VisualWindow::drawNote(uint8_t trackNo, uint8_t noteNo) {
  if (trackNo >= kNoteTrackCount) {
    return false;
  }

  char upperChar = '-';
  char lowerChar = '-';
  if (noteNo <= 96) {
    upperChar = (char)('0' + (noteNo / 10));
    lowerChar = (char)('0' + (noteNo % 10));
  }

  const uint8_t upperIndex = getNumberGlyphIndex(upperChar);
  const uint8_t lowerIndex = getNumberGlyphIndex(lowerChar);
  const int y = kNoteDrawBottomY - trackNo * kLevelSpriteHeight;

  // numbers.png は横書きの数字列を左90度回転しているため、一の位を上、十の位を下に積む。
  memcpy(&numberWorkBuffer[0], numberSprite[lowerIndex],
         sizeof(uint16_t) * kNumberSpriteWidth * kNumberSpriteHeight);
  memcpy(&numberWorkBuffer[kNumberSpriteWidth * kNumberSpriteHeight], numberSprite[upperIndex],
         sizeof(uint16_t) * kNumberSpriteWidth * kNumberSpriteHeight);

  pushVisualBufferToLcd(kNoteDrawX, y, kNumberSpriteWidth, kNumberSpriteHeight * 2,
                        numberWorkBuffer);
  return true;
}

void VisualWindow::eventHandler(event ev) {
  switch (ev) {
    case event::Option:
      draw();
      break;
    case event::Close:
      close();
      playerWindow.show();
      break;
    default:
      break;
  }
}

void VisualWindow::show() {
  visible = true;
  _stopTimerDrawing = true;
  disp.currentView = ViewMode::Visual;
  disp.lastView = ViewMode::Visual;
  ndConfig.saveLastView(LAST_VIEW_VISUAL);
  draw();
}

void VisualWindow::close() {
  visible = false;
  _stopTimerDrawing = true;
  lblSongTitle.setEnabled(false);
}

CFGWindow cfgWindow;
