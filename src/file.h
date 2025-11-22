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

#define CACHE_SIZE (128 * 1024)
#define NUM_CACHE 2

// extern uint8_t cache[NUM_CACHE][CACHE_SIZE] __attribute__((aligned(4)));
extern uint8_t* cache[NUM_CACHE];  // PSRAM用キャッシュ

// 読み込みキャッシュ
extern volatile int activeCache;  // アクティブなキャッシュ
extern volatile int cachePos;     // キャッシュ内の位置

void fillCacheTask(void* pvParameters);
bool initCache(String path);

enum AccessMode { ACCESS_PSRAM, ACCESS_CACHE };  // アクセスモード PSRAM に全部入れる | 逐次

class NDFile {
 public:
  bool init();
  void listDir(const char* dirname);
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

  uint8_t* data;                           // データ本体
  uint32_t pos;                            // データ位置
  std::vector<String> dirs;                // ルートのディレクトリ一覧
  std::vector<String> pngs;                // ディレクトリごとのpng
  std::vector<std::vector<String>> files;  // 各ディレクトリ内のファイル一覧

  u8_t get_ui8();
  u16_t get_ui16();
  u32_t get_ui24();
  u32_t get_ui32();

  u8_t get_ui8_at(uint32_t p);
  u16_t get_ui16_at(uint32_t p);
  u32_t get_ui24_at(uint32_t p);
  u32_t get_ui32_at(uint32_t p);

  // キャッシュ対応版
  u8_t get_ui8_at_header(uint32_t p);
  u16_t get_ui16_at_header(uint32_t p);
  u32_t get_ui24_at_header(uint32_t p);
  u32_t get_ui32_at_header(uint32_t p);

  AccessMode accessMode;

  uint8_t header[256] __attribute__((aligned(4)));  // ヘッダのキャッシュ
  std::vector<u8_t> gd3Cache;                       // GD3部分のキャッシュ

  boolean getHeaderCache(String filePath);  // ヘッダキャッシュ取得

  u16_t getGD3Cache(String filePath, u32_t gd3Offset);  // GD3部分のキャッシュを取得する
 private:
};

extern NDFile ndFile;

#endif
