#include "config.h"

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>

#include <vector>

#include "file.h"
#include "fm.h"
#include "input.h"
#include "nd.h"

Preferences preferences;
static constexpr const char* kLastViewKey = "last_view";

static tNJU72341_GAIN amplifyToInputGain(int amplify) {
  switch (amplify) {
    case AMP_0:
      return GAIN0;
    case AMP_6:
      return GAIN6;
    case AMP_9:
      return GAIN9;
    case AMP_3:
    default:
      return GAIN3;
  }
}

static const char* configSlug(tConfig item) {
  switch (item) {
    case CFG_LANG:
      return "lang";
    case CFG_SHUFFLE:
      return "shuffle";
    case CFG_NUM_LOOP:
      return "loop";
    case CFG_REPEAT:
      return "repeat";
    case CFG_SCROLL:
      return "scroll";
    case CFG_HISTORY:
      return "resume";
    case CFG_FADEOUT:
      return "fadeout";
    case CFG_FMPCM:
      return "fmpcm";
    case CFG_YM2612_PAN:
      return "ym2612pan";
    case CFG_AMPLIFY:
      return "amplify";
    case CFG_KEYON:
      return "keyon";
    case CFG_PAUSE:
      return "pause";
    case CFG_SNATT:
      return "snatt";
    case CFG_MODE:
      return "mode";
    default:
      return "";
  }
}

static tConfig configFromSlug(const String& slug) {
  for (int i = CFG_LANG; i < CFG_UNKNOWN; i++) {
    const tConfig item = static_cast<tConfig>(i);
    if (slug == configSlug(item)) {
      return item;
    }
  }
  return CFG_UNKNOWN;
}

QueueHandle_t cfgSaveQueue;  // 設定保存用メッセージキュー

static void saveConfigItems() {
  for (int i = 0; i < ndConfig.items.size(); i++) {
    preferences.putUChar(ndConfig.items[i].slug.c_str(), ndConfig.items[i].index);
  }
}

// 設定保存メッセージ待ち受け
void cfgSaveTask(void* pvParameters) {
  while (1) {
    u32_t dummy;
    if (xQueueReceive(cfgSaveQueue, &dummy, portMAX_DELAY) == pdTRUE) {
      // 保存
      saveConfigItems();
    }
    vTaskDelay(1);
  }
}

void NDConfig::init() {
  items.push_back(
      {"lang", LANG_JA, "言語", "Language", {"日本語", "英語"}, {"Japanese", "English"}, {LANG_JA, LANG_EN}});

  items.push_back({"shuffle",
                   0,  // 初期値idx
                   "シャッフル",
                   "Shuffle",
                   {"なし", "フォルダ", "全曲"},
                   {"No", "Folder", "All"},
                   {TRANDOM_NO, TRANDOM_FOLDER, TRANDOM_ALL}});
  items.push_back({"pause",
                   0,  // 初期値idx
                   "再生ホールド",
                   "Play Hold",
                   {"オフ", "オン", "3秒前"},
                   {"Off", "On", "3 sec."},
                   {HOLD_NONE, HOLD_YES, HOLD_3SEC}});
  items.push_back({
      "loop",
      LOOP_1,  // 初期値
      "曲ループ",
      "Song Loop",
      {"1回", "2回", "3回", "4回", "5回", "無限"},
      {"1", "2", "3", "4", "5", "Infinite"},
      {LOOP_1, LOOP_2, LOOP_3, LOOP_4, LOOP_5, LOOP_INIFITE},
  });

  items.push_back({"repeat",
                   0,  // 初期値
                   "リピート",
                   "Repeat",
                   {"全曲", "フォルダ", "1曲"},
                   {"All", "Folder", "One Song"},
                   {REPEAT_ALL, REPEAT_FOLDER, REPEAT_ONE}});
  items.push_back({"resume",
                   2,  // 初期値
                   "起動時",
                   "Resume",
                   {"初めから再生", "最後のフォルダ", "最後の曲"},
                   {"No", "Last Folder", "Last Song"},
                   {HISTORY_NONE, HISTORY_FOLDER, HISTORY_FILE}});
  items.push_back({"fadeout",
                   4,  // 初期値
                   "フェードアウト",
                   "Fadeout",
                   {"なし", "2秒", "5秒", "8秒", "10秒", "12秒", "15秒"},
                   {"None", "2 sec.", "5 sec.", "8 sec.", "10 sec.", "12 sec.", "15 sec."},
                   {FO_0, FO_2, FO_5, FO_8, FO_10, FO_12, FO_15}});
  items.push_back({"fmpcm",
                   0,  // 初期値
                   "FM/PCM",
                   "FM/PCM",
                   {"両方", "FMのみ", "PCMのみ"},
                   {"Both", "FM Only", "PCM Only"},
                   {FMPCM_BOTH, FMPCM_FM, FMPCM_PCM}});
  const u8_t ym2612PanDefault = (ND::version == nd_v60) ? TPAN_INVERT : TPAN_NORMAL;
  items.push_back({"ym2612pan",
                   ym2612PanDefault,  // 初期値idx
                   "YM2612パン",
                   "YM2612 Pan",
                   {"ふつう", "反転"},
                   {"Normal", "Invert"},
                   {TPAN_NORMAL, TPAN_INVERT}});
  items.push_back({"snatt",
                   0,  // 初期値idx
                   "SN音量調整",
                   "SN Volume",
                   {"-0dB", "-2dB", "-4dB"},
                   {"-0dB", "-2dB", "-4dB"},
                   {SN_ATT_0, SN_ATT_2, SN_ATT_4}});
  items.push_back({"amplify",
                   2,  // 初期値idx
                   "出力増幅",
                   "Output Gain",
                   {"0dB", "3dB", "6dB", "9dB"},
                   {"0dB", "3dB", "6dB", "9dB"},
                   {AMP_0, AMP_3, AMP_6, AMP_9}});
  items.push_back({"scroll",
                   3,  // 初期値
                   "文字スクロール",
                   "Text scroll",
                   {"なし", "1回", "2回", "無限"},
                   {"None", "1", "2", "Infinite"},
                   {SCROLL_0, SCROLL_1, SCROLL_2, SCROLL_INFINITE}});
  items.push_back({"keyon",
                   0,  // 初期値idx
                   "キーオン色",
                   "Keyon Color",
                   {"赤", "緑", "青"},
                   {"Red", "Green", "Blue"},
                   {KEYON_RED, KEYON_GREEN, KEYON_BLUE}});
  items.push_back(
      {"mode", 0, "動作モード", "Mode", {"プレーヤー", "シリアル"}, {"Player", "Serial"}, {MODE_PLAYER, MODE_SERIAL}});
  preferences.begin("NanoDrive");

  // キュー初期化
  cfgSaveQueue = xQueueCreate(2, sizeof(uint32_t));
  xTaskCreateUniversal(cfgSaveTask, "cfgSaveTask", 4096, NULL, 1, NULL, PRO_CPU_NUM);
}

void NDConfig::applyCfg() {
  nju72341.setFadeoutDuration(get(CFG_FADEOUT));
  const tNJU72341_GAIN inputGain = amplifyToInputGain(get(CFG_AMPLIFY));
  nju72341.setInputGain(1, inputGain);
  nju72341.setInputGain(2, inputGain);
  // Play Hold設定だけは、現在ホールド中の再生にも即時反映する。
  syncPlayHoldConfig();
}

void NDConfig::applyItem(tConfig item) {
  switch (item) {
    case CFG_FADEOUT:
      nju72341.setFadeoutDuration(get(CFG_FADEOUT));
      break;
    case CFG_AMPLIFY: {
      const tNJU72341_GAIN inputGain = amplifyToInputGain(get(CFG_AMPLIFY));
      nju72341.setInputGain(1, inputGain);
      nju72341.setInputGain(2, inputGain);
      break;
    }
    case CFG_PAUSE:
      // Play Hold設定だけは、現在ホールド中の再生にも即時反映する。
      syncPlayHoldConfig();
      break;
    case CFG_FMPCM:
    case CFG_YM2612_PAN:
      FM.requestApplyYM2612OutputMode();
      break;
    default:
      // LOOP/REPEAT/SCROLL/HISTORY/SHUFFLE/LANG/MODE は、変更時点でハードウェア反映不要。
      break;
  }
}

void NDConfig::saveCfg() {
  uint32_t dummy = 0;
  xQueueSend(cfgSaveQueue, &dummy, 0);
  applyCfg();
  FM.requestApplyYM2612OutputMode();
  return;
}

void NDConfig::saveCfgNow() {
  saveConfigItems();
  applyCfg();
  FM.requestApplyYM2612OutputMode();
}

// ヒストリ保存
void NDConfig::saveHistory() {
  // メインスレッドで保存
  if (ndFile.currentNode) {
    preferences.putString("dir", ndFile.getCurrentDirPath());
    preferences.putString("file", ndFile.getCurrentFileName());
  }
}

void NDConfig::saveLastView(tLastView view) {
  if (_lastView == view) return;
  preferences.putUChar(kLastViewKey, static_cast<u8_t>(view));
  _lastView = view;
}

void NDConfig::loadCfg() {
  // READ FILE
  for (int i = 0; i < ndConfig.items.size(); i++) {
    u8_t idx = preferences.getUChar(ndConfig.items[i].slug.c_str(), ndConfig.items[i].index);
    // 範囲チェック
    if (ndConfig.items[i].optionValues.size() > idx) {
      ndConfig.items[i].index = idx;
    }
  }

  // 現在の動作モード
  currentMode = (tMode)get(CFG_MODE);
}

// 最後に開いたフォルダ番号の照合
// 返り値: 0xAAAA BBBB
// 0xAAAA -> ファイル番号
// 0xBBBB -> フォルダ番号

u32_t NDConfig::loadHistory() {
  String dir = preferences.getString("dir");
  String file = preferences.getString("file");

  Node* node = ndFile.findFileNodeByHistory(dir, file);
  if (node) {
    u16_t fld = fileTree.getDirIndex(node->parent);
    u16_t trk = fileTree.getFileIndexInParent(node);
    return (trk << 16) + fld;
  }

  return 0;
}

tLastView NDConfig::loadLastView() {
  const u8_t rawView = preferences.getUChar(kLastViewKey, static_cast<u8_t>(LAST_VIEW_PLAYER));
  _lastView =
      (rawView == static_cast<u8_t>(LAST_VIEW_VISUAL)) ? LAST_VIEW_VISUAL : LAST_VIEW_PLAYER;
  return _lastView;
}

void NDConfig::remove() { preferences.clear(); }

int NDConfig::get(tConfig item) {
  const int itemIndex = indexOf(item);
  if (itemIndex < 0) {
    return 0;
  }

  const sConfig& config = items[itemIndex];
  if (config.index >= config.optionValues.size()) {
    return 0;
  }
  return config.optionValues[config.index];
}

int NDConfig::indexOf(tConfig item) {
  const char* slug = configSlug(item);
  if (slug[0] == '\0') {
    return -1;
  }
  for (int i = 0; i < items.size(); i++) {
    if (items[i].slug == slug) {
      return i;
    }
  }
  return -1;
}

tConfig NDConfig::configAt(int index) {
  if (index < 0 || index >= items.size()) {
    return CFG_UNKNOWN;
  }
  return configFromSlug(items[index].slug);
}

NDConfig ndConfig = NDConfig();
