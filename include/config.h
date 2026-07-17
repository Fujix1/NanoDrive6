#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#include <vector>

typedef enum {
  LANG_JA,
  LANG_EN,
} tLang;

typedef enum {
  LOOP_1 = 1,
  LOOP_2 = 2,
  LOOP_3 = 3,
  LOOP_4 = 4,
  LOOP_5 = 5,
  LOOP_INIFITE = 0,
} tLoop;

typedef enum { REPEAT_ALL,
               REPEAT_ONE,
               REPEAT_FOLDER } tRepeat;
typedef enum { SCROLL_0,
               SCROLL_1,
               SCROLL_2,
               SCROLL_INFINITE } tSCroll;
typedef enum { HISTORY_NONE,
               HISTORY_FOLDER,
               HISTORY_FILE } tHistory;
typedef enum { FO_0 = 0,
               FO_2 = 2000,
               FO_5 = 5000,
               FO_8 = 8000,
               FO_10 = 10000,
               FO_12 = 12000,
               FO_15 = 15000 } tFadeout;
typedef enum { AMP_0 = 0,
               AMP_3 = 3,
               AMP_6 = 6,
               AMP_9 = 9 } tAmp;
typedef enum { TRANDOM_NO,
               TRANDOM_FOLDER,
               TRANDOM_ALL } tRandom;
typedef enum { MODE_PLAYER,
               MODE_SERIAL } tMode;
typedef enum { FMPCM_BOTH,
               FMPCM_FM,
               FMPCM_PCM } tFMPCM;
typedef enum { TPAN_NORMAL,
               TPAN_INVERT } tCfgPan;
typedef enum { HOLD_NONE,
               HOLD_YES,
               HOLD_3SEC } tPause;  // 再生時一時停止
typedef enum { SN_ATT_0,
               SN_ATT_2,
               SN_ATT_4 } tSNAtt;  // SN76489 アッテネータ
typedef enum { LAST_VIEW_PLAYER = 0,
               LAST_VIEW_VISUAL = 1 } tLastView;  // 最後開いていたウィンドウ
typedef enum {
  KEYON_RED = 0xF800,
  KEYON_GREEN = 0x67e1,
  KEYON_BLUE = 0x843f,
} tKeyon;  // キーオン色
typedef enum {
  CTRL_1,
  CTRL_2,
} tControl;  // 操作セット

typedef enum {
  CFG_LANG,        // 言語
  CFG_SHUFFLE,     // シャッフル再生
  CFG_NUM_LOOP,    // ループ回数
  CFG_REPEAT,      // リピート単位
  CFG_SCROLL,      // テキストスクロール回数
  CFG_HISTORY,     // 起動時復旧
  CFG_FADEOUT,     // フェードアウト時間
  CFG_FMPCM,       // FM PCM 再生モード
  CFG_YM2612_PAN,  // YM2612パン
  CFG_AMPLIFY,     // 出力増幅
  CFG_KEYON,       // キーオン色
  CFG_PAUSE,       // 再生時一時停止
  CFG_SNATT,       // SN76489 アッテネータ
  CFG_MODE,        // 動作モード
  CFG_CONTROL,     // 操作セット
  CFG_UNKNOWN,
} tConfig;

// 設定用構造体
typedef struct {
  String slug;                    // スラッグ
  u8_t index;                     // 選択中のインデックス
  String labelJp, labelEn;        // 設定表示名
  std::vector<String> optionsJp;  // 選択肢ラベル日本語
  std::vector<String> optionsEn;  // 選択肢ラベル英語
  std::vector<int> optionValues;  // 選択肢の値
} sConfig;

class NDConfig {
 public:
  tMode currentMode;
  std::vector<sConfig> items;
  void init();
  void applyCfg();
  void applyItem(tConfig item);
  void saveCfg();
  void saveCfgNow();
  void saveHistory();
  void saveLastView(tLastView view);
  void loadCfg();
  u32_t loadHistory();
  tLastView loadLastView();
  void remove();
  int get(tConfig item);
  int indexOf(tConfig item);
  tConfig configAt(int index);
  String lastFolderName = "";

 private:
  tLastView _lastView = LAST_VIEW_PLAYER;
};

extern NDConfig ndConfig;

#endif
