#include "./config.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

#include <vector>

#include "./file.h"

void _saveCFGonCore0(void *param) {
  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_WRITE, true);
  if (!file) {
    Serial.println("There was an error opening the file for writing.");
  } else {
    for (int i = 0; i < ndConfig.items.size(); i++) {
      file.printf("%d:%d\n", i, ndConfig.items[i].index);
    }
    file.close();
  }
  vTaskDelete(NULL);
}

// 開いたフォルダ名を SPIFFS に記録
void _saveHistoryonCore0(void *param) {
  if (ndFile.dirs.size() > 0) {
    File file = SPIFFS.open(CONFIG_LAST_FOLDER, FILE_WRITE, true);
    if (!file) {
      Serial.println("There was an error opening the file for writing.");
    } else {
      file.printf("%s\n", ndFile.dirs[ndFile.currentDir].c_str());
      file.printf("%s\n", ndFile.files[ndFile.currentDir][ndFile.currentFile].c_str());
      file.close();
    }
  }
  vTaskDelete(NULL);
}

bool NDConfig::init() {
  items.push_back({LANG_JA, "言語", "Language", {"日本語", "英語"}, {"Japanese", "English"}, {LANG_JA, LANG_EN}});
  items.push_back({
      LOOP_1,
      "曲ループ",
      "Song Loop",
      {"1回", "2回", "3回", "4回", "5回", "無制限"},
      {"1", "2", "3", "4", "5", "Infinite"},
      {LOOP_1, LOOP_2, LOOP_3, LOOP_4, LOOP_5, LOOP_INIFITE},
  });
  items.push_back({0,  // 初期値
                   "リピート",
                   "Repeat",
                   {"全曲", "フォルダ", "1曲"},
                   {"All", "Folder", "One Song"},
                   {REPEAT_ALL, REPEAT_FOLDER, REPEAT_ONE}});
  items.push_back({3,  // 初期値
                   "文字スクロール",
                   "Text scroll",
                   {"なし", "1回", "2回", "無制限"},
                   {"None", "1", "2", "Infinite"},
                   {SCROLL_0, SCROLL_1, SCROLL_2, SCROLL_INFINITE}});
  items.push_back({2,  // 初期値
                   "起動時",
                   "Resume",
                   {"初めから再生", "最後のフォルダ", "最後の曲"},
                   {"No", "Last Folder", "Last Song"},
                   {HISTORY_NONE, HISTORY_FOLDER, HISTORY_FILE}});
  items.push_back({4,  // 初期値
                   "フェードアウト",
                   "Fadeout",
                   {"なし", "2秒", "5秒", "8秒", "10秒", "12秒", "15秒"},
                   {"None", "2 sec.", "5 sec.", "8 sec.", "10 sec.", "12 sec.", "15 sec."},
                   {FO_0, FO_2, FO_5, FO_8, FO_10, FO_12, FO_15}});

  items.push_back({0, "画面更新", "LCD Update", {"する", "しない"}, {"On", "Off"}, {UPDATE_YES, UPDATE_NO}});

  if (!SPIFFS.begin(true)) {
    return false;
  }

  return true;
}

void NDConfig::saveCfg() {
  xTaskCreateUniversal(_saveCFGonCore0, "saveCFG", 8192, NULL, 1, NULL, PRO_CPU_NUM);
  nju72341.setFadeoutDuration(get(CFG_FADEOUT));
  return;
}

void NDConfig::saveHistory() {
  xTaskCreateUniversal(_saveHistoryonCore0, "saveHistory", 8192, NULL, 1, NULL, PRO_CPU_NUM);
  return;
}

void NDConfig::loadCfg() {
  // READ FILE
  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file) {
    Serial.println("No config file.");
    return;
  }

  String st;
  int cfgMax = items.size();
  while (file.available()) {
    st = file.readStringUntil('\n');
    int n = st.substring(0, st.indexOf(":")).toInt();
    int idx = st.substring(st.indexOf(":") + 1).toInt();

    if (items.size() > n) {  // 項目があれば
      if (items[n].optionValues.size() > idx) {
        items[n].index = idx;
      }
    }
  }
  file.close();
}

// 最後に開いたフォルダ番号の照合
// 返り値: 0xAAAA BBBB
// 0xAAAA -> ファイル番号
// 0xBBBB -> フォルダ番号

u32_t NDConfig::loadHistory() {
  File file = SPIFFS.open(CONFIG_LAST_FOLDER, FILE_READ);
  if (!file) {
    Serial.println("No history file.");
    return 0;
  }

  String folder, track;
  folder = file.readStringUntil('\n');
  track = file.readStringUntil('\n');
  file.close();

  u16_t fld = 0, trk = 0;

  for (int i = 0; i < ndFile.dirs.size(); i++) {
    if (ndFile.dirs[i] == folder) {
      fld = i;
      break;
    }
  }

  for (int i = 0; i < ndFile.files[fld].size(); i++) {
    if (ndFile.files[fld][i] == track) {
      trk = i;
      break;
    }
  }

  return (trk << 16) + fld;
}

void NDConfig::remove() {
  if (SPIFFS.remove(CONFIG_FILE_PATH)) {
    Serial.println("Removed the config.txt.");
  } else {
    Serial.println("Failed to remove the config.txt.");
  }
  if (SPIFFS.remove(CONFIG_LAST_FOLDER)) {
    Serial.println("Removed the lastfolder.txt.");
  } else {
    Serial.println("Failed to remove the lastfolder.txt.");
  }
}

int NDConfig::get(tConfig item) { return items[item].optionValues[items[item].index]; }

NDConfig ndConfig = NDConfig();
