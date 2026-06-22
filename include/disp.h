#ifndef DISP_H
#define DISP_H

#include <LovyanGFX.h>
#include <SPI.h>

#include "OpenFontRender.h"
#include "common.h"
#include "config.h"
#include "file.h"
#include "fonts.h"
#include "input.h"

#define C_BASEBG TFT_BLACK
#define C_BASEFG TFT_WHITE
#define C_ORANGE 0xfdc7
#define C_YELLOW 0xfee0

#define C_ACCENT_LIGHT 0x26df
#define C_ACCENT_DARK 0x1396

#define C_LIGHTGRAY 0xef7d
#define C_HIGHGRAY 0xd6da
#define C_GRAY 0xad55
#define C_MID 0x73ae
#define C_DARK 0x10c4
#define C_LV_PEAK 0x52b5

#define C_HEADER 0x4228           // 0x444444
#define C_HEADERSUB 0x5aec
#define C_BORDER 0xad55           // 0xadaaad
#define C_FOOTER_ACTIVE 0x530c    // 0x506065
#define C_FOOTER_INACTIVE 0xbe1a  // 0xbbc0d0

#define CFG_ITEM_HEIGHT 32

class LGFX : public lgfx::LGFX_Device {
 private:
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

 public:
  LGFX(void);
};

extern LGFX lcd;

enum class ViewMode { Player, Config };

class Disp {
 public:
  ViewMode currentView = ViewMode::Player;
};

extern Disp disp;

typedef struct {
  String trackEn, trackJp, gameEn, gameJp, systemEn, systemJp, authorEn, authorJp, date;
  String chip0, chip1, type;
  uint64_t time;
  uint32_t no, maxFiles;
} tDispData;

bool initDisp();
void redrawOnCore0();
void serialModeDraw();
void startTimer();
void stopTimer();

bool openPNG(String dirName, String fileName, bool AA, bool sprite);

class PlayerWindow {
 public:
  tDispData dispData;
  void drawBG();
  void redraw();
  void updateDisp(tDispData data);
  void updateHeader(uint64_t sec);
  void eventHandler(event ev);
  void show();
};

extern PlayerWindow playerWindow;

// Label クラス
class Label {
 public:
  Label(const int16_t x,  // 座標
        const int16_t y,
        const int16_t w,  // 幅
        const uint16_t fontColor, const uint16_t bgColor, const uint16_t fontSize, const float scrollSpeed,
        const Align textAlign);
  void setCaption(String newCaption);
  void update();
  void setEnabled(bool state);

 private:
  float _n = 0;  // ラベルスクロール量
  uint32_t _x = 0, _y = 0, _labelWidth = 0, _textWidth = 0, _devWidth = 0, _startTick = 0;
  uint16_t _fontColor;
  uint16_t _fontSize;
  uint16_t _bgColor;
  String _caption;
  LGFX_Sprite _sprite;
  Align _textAlign;
  int _scrollCount;  // スクロール済み回数
  bool _isScrolling = false;
  float _scrollSpeed;
  bool _enabled = false;
};

//------------------------------------------------------------------
// Panel クラス

// パネル描画インタフェース
class IPanelRenderer {
 public:
  virtual ~IPanelRenderer() = default;
  virtual void onDrawItem(LGFX_Sprite& target, int itemIndex, int x, int y, int width,
                          bool selected) = 0;
};

// 設定画面の描画クラス
class ConfigPanelRenderer : public IPanelRenderer {
 public:
  void onDrawItem(LGFX_Sprite& target, int itemIndex, int x, int y, int width,
                  bool selected) override;
};

class Panel {
 public:
  Panel(uint16_t x,  // 座標
        uint16_t y,
        uint16_t width,  // 幅高
        uint16_t height, uint8_t itemHeight,
        IPanelRenderer* renderer);

  void update(LGFX_Sprite& targetBuffer);  // 表示更新
  void setItemCount(int newCount);
  void ensureVisible();
  void invalidate();
  uint16_t itemWidth() const;

  int scrollTop = 0;
  int currentIndex = 0;
  uint16_t x, y, width, height;

 private:
  static constexpr uint8_t SCROLLBAR_WIDTH = 12;   // スクロールバー幅
  static constexpr uint8_t PADDING = 3;            // スクロールバー PADDING
  static constexpr uint8_t INDICATOR_HEIGHT = 14;  // ▲ の高さ

  uint8_t _itemHeight;  // 項目の高さ
  int _itemCount = 0;
  uint16_t _innerHeight = 0;
  int _prevCurrentIndex = -1;
  int _prevScrollTop = 0;
  int _prevItemCount = -1;
  bool _needsFullRedraw = true;
  IPanelRenderer* _renderer;  // 描画更新処理
  LGFX_Sprite _scrollbarBackground;
  LGFX_Sprite _scrollbar;  // スクロールバーのスプライト

  void drawScrollbar();
  void redrawItem(LGFX_Sprite& targetBuffer, int index);
  void redrawScrollEdgeItems(LGFX_Sprite& targetBuffer, int scrollDelta);
  void drawVisibleItems(LGFX_Sprite& targetBuffer);
};

// 設定画面クラス
class CFGWindow {
 public:
  int currentItemIndex = 0;

  void init();
  void show();
  void close();
  void eventHandler(event ev);
  void drawItem(LGFX_Sprite& target, int index, int x, int y, int width, bool selected);
  void drawFooter(bool toFrameBuffer);

 private:
  LGFX_Sprite _sprite;
  LGFX_Sprite _sprFooter;
  LGFX_Sprite _sprHeaderJP;
  LGFX_Sprite _sprHeaderEN;

  void initHeaders();
  void drawPanelView();
  void refreshCurrentItem();
  void selectItem(int index);
  void moveSelection(int delta);
};

extern CFGWindow cfgWindow;

#endif
