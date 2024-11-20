#ifndef FILE_H
#define FILE_H

#include <FS.h>
#include <SD.h>

#include "NJU72341.h"
#include "common.h"
#include "disp.h"
#include "fm.h"
#include "vgm.h"

int mod(int i, int j);

class NDFile {
 public:
  bool init();
  void listDir(const char *dirname);
  bool readFile(String path);
  bool filePlay(int count);
  bool dirPlay(int count);
  bool play(uint16_t d, uint16_t f, int8_t att = -1);
  bool fileOpen(uint16_t d, uint16_t f, int8_t att = -1);
  uint8_t getFolderAttenuation(String path);  // フォルダの音量減衰取得

  uint16_t currentDir;      // 現在のディレクトリ
  uint16_t currentFile;     // 現在のファイル
  uint16_t totalSongs = 0;  // 合計曲数
  uint16_t getNumFilesinCurrentDir();

  std::vector<String> dirs;                // ルートのディレクトリ一覧
  std::vector<String> pngs;                // ディレクトリごとのpng
  std::vector<std::vector<String>> files;  // 各ディレクトリ内のファイル一覧

 private:
  uint16_t _numDirs;  //
};

extern NDFile ndFile;

#endif
