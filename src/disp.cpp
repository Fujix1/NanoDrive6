#include "./disp.h"

#include "pics.h"

enum class cfgEvent { Open, Close, Up, Down, Left, Right };

static PNG png;
static bool _stopTimerDrawing = true;  // タイマーによる描画更新を止める

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
static LGFX_Sprite frameBuffer(&lcd);
static LGFX_Sprite sprPng(&lcd);
static LGFX_Sprite sprPngResized(&lcd);
static String lastPNGPath = "";

static OpenFontRender render;
static TimerHandle_t hDispTimer;
static int currentPage = 0;

static Label lblTitle = Label(0, 28, LCD_W, C_ACCENT_LIGHT, C_BASEBG, 20, SCROLL_SPEED_TITLE, Align::TopCenter);
static Label lblGame = Label(0, 53, LCD_W, C_LIGHTGRAY, C_BASEBG, 15, SCROLL_SPEED_GAME, Align::TopCenter);
static Label lblAuthor = Label(28, 233, LCD_W - 28, C_GRAY, C_BASEBG, 16, SCROLL_SPEED_AUTHOR, Align::TopLeft);
static Label lblSystem = Label(28, 211, LCD_W - 28, C_GRAY, C_BASEBG, 16, SCROLL_SPEED_AUTHOR, Align::TopLeft);

static tDispData _dispData;
static SemaphoreHandle_t spFrameBuffer;  // 描画用セマフォ
static QueueHandle_t xQueueCFGWindow;
static TaskHandle_t tskCFGEventLoop;

void redrawOnCore0Task(void* pvParameters) {
  redraw();
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
// PNG draw for PNGDec lib
void pngDraw(PNGDRAW* pDraw) {
  uint16_t lineBuffer[MAX_PNG_WIDTH];  // Line buffer for rendering
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  sprPng.pushImage(0, pDraw->y, pDraw->iWidth, 1, lineBuffer);
}

//---------------------------------------------------------------------------
// Draw header info
static LGFX_Sprite sprHeader(&lcd);
void updateHeader(uint64_t sec) {
  if (xSemaphoreTake(spFrameBuffer, portMAX_DELAY) == pdTRUE) {
    sprHeader.createSprite(70, 14);
    sprHeader.fillSprite(C_HEADER);
    render.setDrawer(sprHeader);
    render.setAlignment(Align::TopCenter);
    render.loadFont(nimbusBold, sizeof(nimbusBold));
    render.setFontSize(14);
    render.setFontColor(TFT_WHITE);
    render.setCursor(35, 0);
    render.printf("%d:%02d", (uint8_t)(sec / 60), (uint8_t)(sec % 60));
    render.unloadFont();
    sprHeader.pushSprite(50, 4);
    sprHeader.deleteSprite();
    xSemaphoreGive(spFrameBuffer);
  }
}

//---------------------------------------------------------------------------
// Timer Handler
void dispTimerHandler(void* param) {
  if (cfgWindow.isVisible) {
    return;
  }

  // 画面更新停止か
  if (ndConfig.get(CFG_UPDATE) == UPDATE_NO) {
    return;
  }

  if (!_stopTimerDrawing) {
    if (xSemaphoreTake(spFrameBuffer, 0) == pdTRUE) {
      lblTitle.update();
      lblGame.update();
      lblAuthor.update();
      lblSystem.update();
      xSemaphoreGive(spFrameBuffer);
    }
    uint64_t sec = vgm.getCurrentTime();
    if (_dispData.time != sec) {
      updateHeader(sec);
      _dispData.time = sec;
    }
  }
}

// 背景描画
void drawBG() {
  frameBuffer.fillSprite(C_BASEBG);
  frameBuffer.fillRect(0, 0, LCD_W, 19, C_HEADER);
  // frameBuffer.fillRect(0, 75, LCD_W, 125, C_DARK);
  frameBuffer.fillRoundRect(1, 279, LCD_W - 2, 40, 2, C_DARK);
  frameBuffer.pushImage(8, 211, ICONS_WIDTH, ICONS_HEIGHT, icons);
  frameBuffer.fillRoundRect(6, 283, 17, 14, 2, C_FOOTER_ACTIVE);
  frameBuffer.fillRoundRect(6, 301, 17, 14, 2, C_FOOTER_ACTIVE);
}

// 再描画
void redraw() {
  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);
  _stopTimerDrawing = true;
  drawBG();
  render.setUseRenderTask(false);
  render.setDrawer(frameBuffer);
  render.setAlignment(Align::TopLeft);

  render.loadFont(fontMain, sizeof(fontMain));
  render.setFontSize(16);
  render.setFontColor(C_GRAY, C_BASEBG);
  render.setCursor(27, 256);
  render.printf(_dispData.date.c_str());
  render.unloadFont();

  render.loadFont(nimbusBold, sizeof(nimbusBold));
  render.setFontSize(13);
  render.setFontColor(C_YELLOW, C_DARK);
  render.setCursor(27, 284);
  render.printf(_dispData.chip0.c_str());
  render.setCursor(27, 303);
  render.printf(_dispData.chip1.c_str());

  render.setFontColor(C_LIGHTGRAY, C_FOOTER_INACTIVE);
  render.setCursor(11, 284);
  render.printf("1");
  render.setCursor(11, 303);
  render.printf("2");

  if (_dispData.no != 0 && _dispData.maxFiles != 0) {
    render.setFontSize(14);
    render.setFontColor(C_GRAY, C_HEADER);
    render.setCursor(167, 4);
    render.setAlignment(Align::TopRight);
    render.printf("%02d/%02d", _dispData.no, _dispData.maxFiles);
  }

  if (ndConfig.get(CFG_UPDATE) == UPDATE_YES) {
    render.setAlignment(Align::TopCenter);
    render.setFontSize(14);
    render.setFontColor(C_LIGHTGRAY, C_HEADER);
    render.setCursor(LCD_W / 2, 4);
    render.printf("%d:%02d", (uint8_t)(_dispData.time / 60), (uint8_t)(_dispData.time % 60));
  }

  render.setFontSize(13);
  render.setFontColor(C_ORANGE, C_HEADER);
  render.setCursor(4, 4);
  render.setAlignment(Align::TopLeft);
  render.printf(_dispData.type.c_str());
  render.unloadFont();

  if (ndConfig.get(CFG_LANG) == LANG_JA) {
    lblTitle.setCaption(_dispData.trackJp);
    lblGame.setCaption(_dispData.gameJp);
    lblAuthor.setCaption(_dispData.authorJp);
    lblSystem.setCaption(_dispData.systemJp);
  } else {
    lblTitle.setCaption(_dispData.trackEn);
    lblGame.setCaption(_dispData.gameEn);
    lblAuthor.setCaption(_dispData.authorEn);
    lblSystem.setCaption(_dispData.systemEn);
  }

  // /snap/songno.png を優先
  if (openPNG(ndFile.dirs[ndFile.currentDir] + "/snap", String(_dispData.no) + ".png", true, true) == false) {
    openPNG(ndFile.dirs[ndFile.currentDir], ndFile.pngs[ndFile.currentDir], true, true);
  }

  frameBuffer.pushSprite(0, 0);
  _stopTimerDrawing = false;
  xSemaphoreGive(spFrameBuffer);
}

void updateDisp(tDispData data) {
  //
  _dispData.authorEn = data.authorEn;
  _dispData.authorJp = data.authorJp;
  _dispData.chip0 = data.chip0;
  _dispData.chip1 = data.chip1;
  _dispData.date = data.date;
  _dispData.gameEn = data.gameEn;
  _dispData.gameJp = data.gameJp;
  _dispData.no = data.no;
  _dispData.maxFiles = data.maxFiles;
  _dispData.systemEn = data.systemEn;
  _dispData.systemJp = data.systemJp;
  _dispData.trackEn = data.trackEn;
  _dispData.trackJp = data.trackJp;
  _dispData.type = data.type;
  _dispData.time = 0;

  if (!cfgWindow.isVisible) {
    redraw();
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

  // 縮小済み PNG スプライト
  sprPngResized.setPsram(true);
  sprPngResized.createSprite(LCD_W + 1, 125);

  _stopTimerDrawing = true;

  // タイマー生成
  hDispTimer = xTimerCreate("DISP_TIMER", DISP_TIMER_INTERVAL, pdTRUE, NULL, dispTimerHandler);
  xTimerStart(hDispTimer, 0);

  return true;
}

void startTimer() { xTimerStart(hDispTimer, 0); }
void stopTimer() { xTimerStop(hDispTimer, 0); }

// -----------------------------------------------------------------------
// Opening a png file on the SD card
File myfile;

void* myOpen(const char* filename, int32_t* size) {
  myfile = SD.open(filename);
  *size = myfile.size();
  return &myfile;
}
void myClose(void* handle) {
  if (myfile) myfile.close();
}
int32_t myRead(PNGFILE* handle, uint8_t* buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}
int32_t mySeek(PNGFILE* handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}

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
  //
  String path = dirName + "/" + fileName;

  if (lastPNGPath != path) {
    //  PNG 開いて sprPNG に転送
    if (!SD.exists(path)) {
      return false;
    }

    myfile = SD.open(path.c_str());
    if (!myfile) {
      sprPng.deleteSprite();
      lastPNGPath = "";
      frameBuffer.setFont(&fonts::Font2);
      frameBuffer.setCursor(0, 77);
      frameBuffer.printf("PNG open error:\n%s", path);
      return false;
    }

    int16_t rc = png.open(path.c_str(), myOpen, myClose, myRead, mySeek, pngDraw);
    if (rc == PNG_SUCCESS) {
      sprPng.setPsram(true);
      sprPng.createSprite(png.getWidth(), png.getHeight());
      rc = png.decode(NULL, 0);

      // リサイズ
      float w, h;
      // 640x400 の場合比率保持
      if (sprPng.width() == 640 && sprPng.height() == 400) {
        w = (float)(LCD_W + 1) / png.getWidth();
        h = 0.2646;
        sprPngResized.fillSprite(TFT_BLACK);
      } else if (sprPng.width() > sprPng.height()) {
        // 横長
        w = (float)(LCD_W + 1) / png.getWidth();
        h = 127.0 / png.getHeight();
        sprPngResized.fillSprite(C_HEADER);
      } else {
        // 縦長画像
        w = 94.5 / png.getWidth();
        h = 127.0 / png.getHeight();
        sprPngResized.fillSprite(C_HEADER);
      }

      if (AA) {
        sprPng.pushRotateZoomWithAA(&sprPngResized, 84.5, 63, 0, w, h);
      } else {
        sprPng.pushRotateZoom(&sprPngResized, 84.5, 63, 0, w, h);
      }
      sprPng.deleteSprite();
      lastPNGPath = path;
    } else {
      frameBuffer.setFont(&fonts::Font2);
      frameBuffer.setCursor(0, 77);
      frameBuffer.printf("PNG file error:\n%s", path);
      sprPng.deleteSprite();
      lastPNGPath = "";
      return false;
    }
    png.close();
  }

  if (toSprite) {
    sprPngResized.pushSprite(&frameBuffer, 0, 75);
  } else {
    sprPngResized.pushSprite(&lcd, 0, 75);
  }

  return true;
}

//---------------------------------------------------------------------------
// 設定画面クラスなど

// CFGウィンドウのイベント処理
void CFGWindowEventLoop(void* pvPrams) {
  while (1) {
    cfgEvent event;
    if (xQueueReceive(xQueueCFGWindow, &event, 0) == pdTRUE) {
      switch (event) {
        case cfgEvent::Open: {
          cfgWindow.draw();
          break;
        }
        case cfgEvent::Close: {
          cfgWindow.isVisible = false;
          redraw();
          break;
        }
        case cfgEvent::Up: {
          if (cfgWindow.currentItemIndex != 0) {
            cfgWindow.currentItemIndex--;
            cfgWindow.drawItem(cfgWindow.currentItemIndex + 1, false);
            cfgWindow.drawItem(cfgWindow.currentItemIndex, false);
            cfgWindow.drawFooter(false);
          }
          break;
        }
        case cfgEvent::Down: {
          if (cfgWindow.currentItemIndex != ndConfig.items.size() - 1) {
            cfgWindow.currentItemIndex++;
            cfgWindow.drawItem(cfgWindow.currentItemIndex - 1, false);
            cfgWindow.drawItem(cfgWindow.currentItemIndex, false);
            cfgWindow.drawFooter(false);
          }
          break;
        }
        case cfgEvent::Left: {
          if (ndConfig.items[cfgWindow.currentItemIndex].index != 0) {
            ndConfig.items[cfgWindow.currentItemIndex].index--;
            // 言語はすぐ再描画
            if (cfgWindow.currentItemIndex == CFG_LANG) {
              cfgWindow.draw();
            } else {
              cfgWindow.drawItem(cfgWindow.currentItemIndex, false);
              cfgWindow.drawFooter(false);
            }
            ndConfig.saveCfg();
          }
          break;
        }
        case cfgEvent::Right: {
          if (ndConfig.items[cfgWindow.currentItemIndex].index !=
              ndConfig.items[cfgWindow.currentItemIndex].optionValues.size() - 1) {
            ndConfig.items[cfgWindow.currentItemIndex].index++;
            // 言語はすぐ再描画
            if (cfgWindow.currentItemIndex == CFG_LANG) {
              cfgWindow.draw();
            } else {
              cfgWindow.drawItem(cfgWindow.currentItemIndex, false);
              cfgWindow.drawFooter(false);
            }
            ndConfig.saveCfg();
          }
          break;
        }
      }
    }
    vTaskDelay(66);
  }
}

void CFGWindow::init() {
  // キュ～作成
  xQueueCFGWindow = xQueueCreate(2, sizeof(cfgEvent));
  xTaskCreateUniversal(CFGWindowEventLoop, "CFG", 12192, NULL, 1, &tskCFGEventLoop, PRO_CPU_NUM);
  _sprite.createSprite(LCD_W, ITEM_HEIGHT);
  _sprFooter.createSprite(120, 23);
}

void CFGWindow::show() {
  if (isVisible) return;

  if (uxQueueSpacesAvailable(xQueueCFGWindow)) {
    isVisible = true;
    cfgEvent event = cfgEvent::Open;
    xQueueSend(xQueueCFGWindow, &event, 0);
  }
}

void CFGWindow::close() {
  if (uxQueueSpacesAvailable(xQueueCFGWindow)) {
    cfgEvent event = cfgEvent::Close;
    xQueueSend(xQueueCFGWindow, &event, 1);
  }
}

void CFGWindow::up() {
  if (!isVisible) return;
  if (uxQueueSpacesAvailable(xQueueCFGWindow)) {
    cfgEvent event = cfgEvent::Up;
    xQueueSend(xQueueCFGWindow, &event, 0);
  }
}
void CFGWindow::down() {
  if (!isVisible) return;
  if (uxQueueSpacesAvailable(xQueueCFGWindow)) {
    cfgEvent event = cfgEvent::Down;
    xQueueSend(xQueueCFGWindow, &event, 0);
  }
}
void CFGWindow::left() {
  if (!isVisible) return;
  if (uxQueueSpacesAvailable(xQueueCFGWindow)) {
    cfgEvent event = cfgEvent::Left;
    xQueueSend(xQueueCFGWindow, &event, 0);
  }
}
void CFGWindow::right() {
  if (!isVisible) return;
  if (uxQueueSpacesAvailable(xQueueCFGWindow)) {
    cfgEvent event = cfgEvent::Right;
    xQueueSend(xQueueCFGWindow, &event, 0);
  }
}

void CFGWindow::draw() {
  xSemaphoreTake(spFrameBuffer, portMAX_DELAY);

  frameBuffer.fillSprite(TFT_WHITE);
  frameBuffer.fillRect(0, 0, LCD_W, 26, C_HEADER);
  frameBuffer.pushImage(146, 3, CFG_ICON_WIDTH, CFG_ICON_HEIGHT, cfgIcon);

  frameBuffer.fillRoundRect(124, 293, 42, 23, 2, C_FOOTER_ACTIVE);

  render.setDrawer(frameBuffer);
  render.setAlignment(Align::TopLeft);
  render.setFontColor(C_LIGHTGRAY, C_FOOTER_ACTIVE);
  if (ndConfig.get(CFG_LANG) == LANG_JA) {
    render.loadFont(fontMain, sizeof(fontMain));
    render.setFontSize(17);
    render.setCursor(6, 4);
    render.printf("設定");
    render.setCursor(130, 297);
    render.printf("戻る");
  } else {
    render.loadFont(nimbusBold, sizeof(nimbusBold));
    render.setFontSize(16);
    render.setCursor(6, 5);
    render.printf("Preferences");
    render.setCursor(133, 298);
    render.printf("OK");
  }

  drawFooter(true);

  for (int i = 0; i < ndConfig.items.size(); i++) {
    cfgWindow.drawItem(i, true);
  }

  render.unloadFont();
  frameBuffer.pushSprite(0, 0);
  xSemaphoreGive(spFrameBuffer);  // 描画完了
}

void CFGWindow::drawItem(int index, bool toFrameBuffer) {
  int titleTextColor = C_DARK, optionTextColor = C_MID, backgroundColor = TFT_WHITE, borderColor = C_BORDER;

  if (currentItemIndex == index) {
    titleTextColor = TFT_WHITE;
    optionTextColor = C_YELLOW;
    backgroundColor = C_ACCENT_DARK;
  }

  OpenFontRender ofr;
  ofr.setDrawer(_sprite);

  String label, option;
  int fontSize;
  if (ndConfig.get(CFG_LANG) == LANG_JA) {
    ofr.loadFont(fontMain, sizeof(fontMain));
    fontSize = 17;
    label = ndConfig.items[index].labelJp;
    option = ndConfig.items[index].optionsJp[ndConfig.items[index].index];
  } else {
    fontSize = 17;
    ofr.loadFont(nimbusBold, sizeof(nimbusBold));
    label = ndConfig.items[index].labelEn;
    option = ndConfig.items[index].optionsEn[ndConfig.items[index].index];
  }

  _sprite.fillSprite(backgroundColor);
  _sprite.drawLine(0, ITEM_HEIGHT - 1, LCD_W, ITEM_HEIGHT - 1, borderColor);

  ofr.setFontSize(fontSize);
  ofr.setAlignment(Align::TopLeft);
  ofr.setFontColor(titleTextColor, backgroundColor);
  ofr.setCursor(6, (ITEM_HEIGHT - fontSize) / 2);
  ofr.printf(label.c_str());

  /*
  if (ndConfig.get(CFG_LANG) == LANG_EN) {
    ofr.unloadFont();
    ofr.loadFont(nimbusRegular, sizeof(nimbusRegular));
  }
  */
  ofr.setFontColor(optionTextColor, backgroundColor);
  ofr.setAlignment(Align::TopRight);
  ofr.setCursor(LCD_W - 6, (ITEM_HEIGHT - fontSize) / 2);
  ofr.printf(option.c_str());

  ofr.unloadFont();
  if (toFrameBuffer) {
    _sprite.pushSprite(&frameBuffer, 0, 26 + ITEM_HEIGHT * (index));
  } else {
    _sprite.pushSprite(&lcd, 0, 26 + ITEM_HEIGHT * (index));
  }
}

void CFGWindow::drawFooter(bool toFrameBuffer) {
  bool up, down, left, right;
  int color;

  _sprFooter.fillSprite(TFT_WHITE);

  up = (cfgWindow.currentItemIndex != 0);
  down = (cfgWindow.currentItemIndex != ndConfig.items.size() - 1);
  left = (ndConfig.items[cfgWindow.currentItemIndex].index != 0);
  right = (ndConfig.items[cfgWindow.currentItemIndex].index !=
           ndConfig.items[cfgWindow.currentItemIndex].optionValues.size() - 1);

  _sprFooter.fillRoundRect(4, 0, 27, 23, 2, up ? C_FOOTER_ACTIVE : C_FOOTER_INACTIVE);
  _sprFooter.fillRoundRect(33, 0, 27, 23, 2, color = down ? C_FOOTER_ACTIVE : C_FOOTER_INACTIVE);
  _sprFooter.fillRoundRect(64, 0, 27, 23, 2, color = left ? C_FOOTER_ACTIVE : C_FOOTER_INACTIVE);
  _sprFooter.fillRoundRect(93, 0, 27, 23, 2, color = right ? C_FOOTER_ACTIVE : C_FOOTER_INACTIVE);
  if (up) {
    _sprFooter.pushImage(12, 6, CFG_ICON_ARROR_WIDTH, CFG_ICON_ARROR_HEIGHT, cfgUP);
  }
  if (down) {
    _sprFooter.pushImage(41, 6, CFG_ICON_ARROR_WIDTH, CFG_ICON_ARROR_HEIGHT, cfgDOWN);
  }
  if (left) {
    _sprFooter.pushImage(72, 6, CFG_ICON_ARROR_WIDTH, CFG_ICON_ARROR_HEIGHT, cfgLEFT);
  }
  if (right) {
    _sprFooter.pushImage(101, 6, CFG_ICON_ARROR_WIDTH, CFG_ICON_ARROR_HEIGHT, cfgRIGHT);
  }

  if (toFrameBuffer) {
    _sprFooter.pushSprite(&frameBuffer, 0, 293);
  } else {
    _sprFooter.pushSprite(&lcd, 0, 293);
  }
}

CFGWindow cfgWindow = CFGWindow();
