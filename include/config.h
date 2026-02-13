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

typedef enum { REPEAT_ALL, REPEAT_ONE, REPEAT_FOLDER } tRepeat;
typedef enum { SCROLL_0, SCROLL_1, SCROLL_2, SCROLL_INFINITE } tSCroll;
typedef enum { HISTORY_NONE, HISTORY_FOLDER, HISTORY_FILE } tHistory;
typedef enum { FO_0 = 0, FO_2 = 2000, FO_5 = 5000, FO_8 = 8000, FO_10 = 10000, FO_12 = 12000, FO_15 = 15000 } tFadeout;
typedef enum { UPDATE_YES, UPDATE_NO } tUpdate;
typedef enum { MODE_PLAYER, MODE_SERIAL } tMode;
typedef enum { FMPCM_BOTH, FMPCM_FM, FMPCM_PCM } tFMPCM;

typedef enum {
  CFG_LANG,      // 言語
  CFG_NUM_LOOP,  // ループ回数
  CFG_REPEAT,    // リピート単位
  CFG_SCROLL,    // テキストスクロール回数
  CFG_HISTORY,   // 起動時復旧
  CFG_FADEOUT,   // フェードアウト時間
  CFG_UPDATE,    // 画面更新有無
  CFG_MODE,      // 動作モード
  CFG_FMPCM,     // FM PCM 再生モード
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
  void saveCfg();
  void saveHistory();
  void loadCfg();
  u32_t loadHistory();
  void remove();
  int get(tConfig item);
  String lastFolderName = "";

 private:
};

extern NDConfig ndConfig;

#endif
