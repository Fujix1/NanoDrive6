#include "disp.h"

#include "pics.h"
#include "png_renderer.h"

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
Disp disp;
PlayerWindow playerWindow;
static LGFX_Sprite frameBuffer(&lcd);

static OpenFontRender render;
static TimerHandle_t hDispTimer;
static int currentPage = 0;

static Label lblTitle = Label(0, 28, LCD_W, C_ACCENT_LIGHT, C_BASEBG, 20, SCROLL_SPEED_TITLE, Align::TopCenter);
static Label lblGame = Label(0, 53, LCD_W, C_LIGHTGRAY, C_BASEBG, 15, SCROLL_SPEED_GAME, Align::TopCenter);
static Label lblAuthor = Label(28, 233, LCD_W - 28, C_GRAY, C_BASEBG, 16, SCROLL_SPEED_AUTHOR, Align::TopLeft);
static Label lblSystem = Label(28, 211, LCD_W - 28, C_GRAY, C_BASEBG, 16, SCROLL_SPEED_AUTHOR, Align::TopLeft);

static SemaphoreHandle_t spFrameBuffer;  // 描画用セマフォ

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
// Draw header info
static LGFX_Sprite sprHeader(&lcd);
void PlayerWindow::updateHeader(uint64_t sec) {
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
  if (disp.currentView != ViewMode::Player) {
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
    if (playerWindow.dispData.time != sec) {
      playerWindow.updateHeader(sec);
      playerWindow.dispData.time = sec;
    }
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

  if (dispData.no != 0 && dispData.maxFiles != 0) {
    render.setFontSize(14);
    render.setFontColor(C_GRAY, C_HEADER);
    render.setCursor(167, 4);
    render.setAlignment(Align::TopRight);
    render.printf("%02d/%02d", dispData.no, dispData.maxFiles);
  }

  render.setAlignment(Align::TopCenter);
  render.setFontSize(14);
  render.setFontColor(C_LIGHTGRAY, C_HEADER);
  render.setCursor(LCD_W / 2, 4);
  render.printf("%d:%02d", (uint8_t)(dispData.time / 60), (uint8_t)(dispData.time % 60));

  render.setFontSize(13);
  if (ndFile.accessMode == ACCESS_CACHE) {
    render.setFontColor(C_ACCENT_LIGHT, C_HEADER);
  } else {
    render.setFontColor(C_ORANGE, C_HEADER);
  }

  render.setCursor(4, 4);
  render.setAlignment(Align::TopLeft);
  render.printf(dispData.type.c_str());
  render.unloadFont();

  if (ndConfig.get(CFG_LANG) == LANG_JA) {
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

  String fileName = ndFile.files[ndFile.currentDir][ndFile.currentFile];
  fileName = fileName.substring(0, fileName.length() - 4);

  if (openPNG(ndFile.dirs[ndFile.currentDir] + "/snap", fileName + ".png", true, true) == false) {
    if (openPNG(ndFile.dirs[ndFile.currentDir] + "/snap", String(dispData.no) + ".png", true, true) == false) {
      openPNG(ndFile.dirs[ndFile.currentDir], ndFile.pngs[ndFile.currentDir], true, true);
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

  frameBuffer.pushImage(170 - 2 - USB_ICON_WIDTH, 2, USB_ICON_WIDTH, USB_ICON_HEIGHT, usb_icon);  // usb icon

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

  render.setFontColor(C_ORANGE, C_HEADER);
  render.setCursor(4, 4);
  render.setAlignment(Align::TopLeft);
  render.printf("USB");
  render.unloadFont();

  render.loadFont(fontMain, sizeof(fontMain));
  render.setFontSize(16);
  render.setFontColor(C_GRAY, C_BASEBG);
  render.setCursor(28, 256);
  render.printf("--");
  render.unloadFont();

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

  frameBuffer.pushSprite(0, 0);
  xSemaphoreGive(spFrameBuffer);
}

void PlayerWindow::updateDisp(tDispData data) {
  //
  dispData = data;
  dispData.time = 0;

  if (disp.currentView == ViewMode::Player) {
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

  if (!initPNGRenderer()) {
    return false;
  }

  _stopTimerDrawing = true;

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
}

// 表示
void CFGWindow::show() {
  disp.currentView = ViewMode::Config;
  _stopTimerDrawing = true;
  drawPanelView();
}

void CFGWindow::close() {
  if ((tMode)ndConfig.items[CFG_MODE].index != ndConfig.currentMode) {
    ESP.restart();
    return;
  }
  playerWindow.show();
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
        ndConfig.items[currentItemIndex].index--;
        ndConfig.saveCfg();
        // 言語はすぐ再描画
        if (currentItemIndex == CFG_LANG) {
          drawPanelView();
        } else {
          refreshCurrentItem();
        }
      }
      break;
    case event::Right:
      if (ndConfig.items[currentItemIndex].index + 1 <
          ndConfig.items[currentItemIndex].optionValues.size()) {
        ndConfig.items[currentItemIndex].index++;
        ndConfig.saveCfg();
        // 言語はすぐ再描画
        if (currentItemIndex == CFG_LANG) {
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
  currentItemIndex = index;
  pnlConfig.setItemCount(ndConfig.items.size());
  pnlConfig.currentIndex = currentItemIndex;
  pnlConfig.ensureVisible();
  pnlConfig.update(frameBuffer);
  currentItemIndex = pnlConfig.currentIndex;
  drawFooter(true);
  frameBuffer.pushSprite(0, 0);
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

CFGWindow cfgWindow;
