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
#include "keyinfo.h"
#include "nd.h"

struct Node;

#define C_BASEBG TFT_BLACK
#define C_BASEFG TFT_WHITE
#define C_ORANGE 0xfdc7        // #ffba3a
#define C_YELLOW 0xfee0        // #ffdf00
#define C_ACCENT_LIGHT 0x26df  // #21dbff
#define C_ACCENT_DARK 0x1396   // #1071b5
#define C_LIGHTGRAY 0xef7d     // #efefef
#define C_HIGHGRAY 0xd6da      // #d6dbd6
#define C_GRAY 0xad55          // #adaaad
#define C_MID 0x73ae           // #737573
#define C_DARK 0x10c4          // #101821
#define C_LV_PEAK 0x52b5       // #5255ad
#define C_MASKEDKEY 0x322f     // #324579
#define C_MDX_ON 0x843f        // #8787ff
#define C_MDX_OFF 0x212a       // #23234f

#define C_HEADER 0x4228           // #424542
#define C_HEADERSUB 0x5aec        // #5a5d63
#define C_BORDER 0xad55           // #adaaad
#define C_FOOTER_ACTIVE 0x530c    // #526163
#define C_FOOTER_INACTIVE 0xbe1a  // #bdc2d6
#define C_LISTBG 0xdf3d            // #e2e8ed

#define CFG_ITEM_HEIGHT 32
#define BROWSER_CURRENT_DIR_HEIGHT 28
#define BROWSER_ITEM_HEIGHT 28

// font icon
// 丂 - random

// Nano Drive 6.1 以降の LCD は Fujix1/LovyanGFX の ST7789 を使用
// Nano Drive 6 はガンマ違いのため派生クラスでオーバーライド
class Panel_ST7789_ND : public lgfx::Panel_ST7789 {
 protected:
  const uint8_t* getInitCommands(uint8_t listno) const override;
};

class LGFX : public lgfx::LGFX_Device {
 private:
  Panel_ST7789_ND _panel_instance;
  lgfx::Bus_SPI _bus_instance;

 public:
  LGFX(void);
};

extern LGFX lcd;

enum class ViewMode { Player,
                      Config,
                      Visual,
                      Browser };

class Disp {
 public:
  ViewMode currentView = ViewMode::Player;
  ViewMode lastView = currentView;
  bool stopTimerDrawing = true;
};

extern Disp disp;

typedef struct {
  String trackEn, trackJp, gameEn, gameJp, systemEn, systemJp, authorEn, authorJp, date;
  String chip0, chip1, type;
  int64_t time;
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
  void updateHeader(int64_t sec, bool visible = true, uint32_t ticksToWait = portMAX_DELAY);
  void updateHeaderBlocking(int64_t sec);
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

// 左に90度回転したラベル
class RotatedLabel {
 public:
  RotatedLabel(const int16_t x,  // 座標
               const int16_t y,
               const int16_t h,  // 高さ
               const uint16_t fontColor, const uint16_t bgColor, const uint16_t fontSize,
               const float scrollSpeed, const Align textAlign);
  void setCaption(String newCaption);
  void update();
  void setEnabled(bool state);

 private:
  float _n = 0;  // ラベルスクロール量
  int32_t _lastDrawOffset = -1;
  uint32_t _x = 0, _y = 0, _labelHeight = 0, _textWidth = 0, _devWidth = 0, _startTick = 0;
  uint16_t _fontColor;
  uint16_t _fontSize;
  uint16_t _bgColor;
  String _caption;
  LGFX_Sprite _sprite;
  Align _textAlign;
  int _scrollCount = 0;  // スクロール済み回数
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

// ファイルブラウザ画面の描画クラス
class BrowserPanelRenderer : public IPanelRenderer {
 public:
  void init();
  void deinit();
  void setBrowseDirNode(Node* browseDirNode);
  bool hasParentEntry() const;
  Node* getNodeByDisplayIndex(int itemIndex) const;
  OpenFontRender& getRender() { return _render; }
  void onDrawItem(LGFX_Sprite& target, int itemIndex, int x, int y, int width,
                  bool selected) override;

 private:
  Node* _browseDirNode = nullptr;
  int _dotsWidth = 0;
  OpenFontRender _render;
  bool _fontLoaded = false;
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
  void close(bool restoreLastView = false);
  void eventHandler(event ev);
  void drawItem(LGFX_Sprite& target, int index, int x, int y, int width, bool selected);
  void drawFooter(bool toFrameBuffer);

 private:
  LGFX_Sprite _sprite;
  LGFX_Sprite _sprFooter;
  LGFX_Sprite _sprHeaderJP;
  LGFX_Sprite _sprHeaderEN;
  bool _isChanged = false;

  void initHeaders();
  void drawPanelView();
  void refreshCurrentItem();
  void selectItem(int index);
  void moveSelection(int delta);
};

extern CFGWindow cfgWindow;

// ビジュアル画面クラス
class VisualWindow {
 public:
  VisualWindow() : _sprShuffleOn(&lcd), _sprShuffleOff(&lcd), _sprTime(&lcd) {
  }
  void init();
  void draw();
  void update();
  void updateLabels();
  void drawTimestamp(int64_t sec);
  void drawTimestamp(int64_t sec, bool visible);
  void show();
  void close();
  void eventHandler(event ev);
  bool visible = false;

 private:
  boolean drawKeyboard(LGFX_Sprite& sprite, t_device device, const NoteInfo* notes);
  boolean drawPan(uint8_t trackNo, tPan pan);
  boolean drawLevel(uint8_t trackNo, uint8_t level, uint8_t peakLevel);
  boolean drawNote(uint8_t trackNo, uint8_t noteNo);
  LGFX_Sprite _sprShuffleOn;
  LGFX_Sprite _sprShuffleOff;
  LGFX_Sprite _sprTime;
  int64_t _lastTimestampSec = INT64_MAX;
  bool _lastTimestampVisible = false;
  void drawTimestamp(int64_t sec, bool toFrameBuffer, bool visible);
};

extern VisualWindow visualWindow;

// ファイルブラウザ画面クラス
class BrowserWindow {
 public:
  void init();
  void show();
  void close();
  void eventHandler(event ev);
  void openDirectory(Node* dirNode, Node* selectedNode = nullptr);
  bool openParentDirectory();
  void onCurrentNodeChanged(Node* previousNode, Node* currentNode);
  bool visible = false;

 private:
  LGFX_Sprite _sprFooter;
  LGFX_Sprite _sprHeaderJP;
  LGFX_Sprite _sprHeaderEN;
  LGFX_Sprite _sprCurrentDir;
  Node* _lastCurrentDirNode = nullptr;
  Node* _browseDirNode = nullptr;
  Node* _selectedNode = nullptr;

  void initHeaders();
  void initFooter();
  void draw();
  void drawCurrentDir();
  void drawFooter(bool toFrameBuffer);
  int getItemCount() const;
  void selectItem(int index);
  void moveSelection(int delta);
  void selectCurrentItem();
  void openAdjacentDirectory(int delta);
};

extern BrowserWindow browserWindow;

#endif
