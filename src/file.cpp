#include "file.h"

static SPIClass SPI_SD;
std::vector<String> dirs;                // ルートのディレクトリ一覧
std::vector<String> pngs;                // ディレクトリごとのpng
std::vector<std::vector<String>> files;  // 各ディレクトリ内のファイル一覧
static File _vgmFile;
static SemaphoreHandle_t spFileOpen;  // ファイル開く処理用セマフォ

struct CacheTaskParam {
  u32_t pos;
  int cacheIndex;
};

// キャッシュ
QueueHandle_t cacheQueue;  // メッセージキュー

// uint8_t cache[NUM_CACHE][CACHE_SIZE] __attribute__((aligned(4)));  // SRAM キャッシュ
uint8_t* cache[NUM_CACHE];  // PSRAM用キャッシュ

volatile int activeCache = 0;
static File _cacheFile;
volatile int cachePos = 0;

void fillCache(u32_t pos, int chaceIndex) {
  int readSize = vgm.size - vgm.gd3Size - pos - CACHE_SIZE;  // 読み込みサイズ

  //
  // |  キャッシュ  |  キャッシュ  |
  // |              vgm               | gd3 |

  // キャッシュサイズ全部読めるとき
  if (readSize >= CACHE_SIZE) {
    int bytesRead = _cacheFile.read(cache[chaceIndex], CACHE_SIZE);
    Serial.printf("Bytes read: readSize: 0x%x, 0x%x\n", CACHE_SIZE, bytesRead);
    Serial.printf("cache[%d]: %0x, %x, %x\n", chaceIndex, pos + CACHE_SIZE, cache[chaceIndex][0],
                  cache[chaceIndex][CACHE_SIZE - 1]);
  } else if (0 <= readSize && readSize < CACHE_SIZE) {
    // |  キャッシュ  |  キャッシュ  |
    // |        vgm        | gd3 |

    // キャッシュ終了またぐとき
    int bytesRead = _cacheFile.read(cache[chaceIndex], readSize);
    /*Serial.printf("[またぐ] readSize: 0x%x, Bytes read: 0x%x\n", readSize, bytesRead);
    Serial.printf("cache[%d]: %0x, %x, %x\n", chaceIndex, pos + CACHE_SIZE, cache[chaceIndex][0],
                  cache[chaceIndex][readSize - 1]);
*/

    // ループ開始部分補充
    if (vgm.loopOffset != 0) {
      int padSize = CACHE_SIZE - readSize;
      _cacheFile.seek(vgm.loopOffset + 0x1C);  // ループ開始地点

      int bytesRead = _cacheFile.read(cache[chaceIndex] + readSize, padSize);
      /*
            Serial.printf("[ループ開始] loopOffset: 0x%x, readSize: 0x%x, Bytes read: 0x%x\n", vgm.loopOffset + 0x1C,
                          readSize, bytesRead);
            Serial.printf("cache[%d]: @0x%0x %x, @0x%x %x\n", chaceIndex, readSize, cache[chaceIndex][readSize],
         CACHE_SIZE, cache[chaceIndex][CACHE_SIZE - 1]);
                          */
    }
  } else {
    // |  キャッシュ  |  キャッシュ  |
    // |  vgm   | gd3 |

    // キャッシュが超えるとき

    // ループ続き補充
    if (vgm.loopOffset != 0) {
      readSize = CACHE_SIZE;
      // int start = vgm.loopOffset + 0x1C + CACHE_SIZE - readSize;
      //_cacheFile.seek(vgm.loopOffset + 0x1C + CACHE_SIZE - readSize);

      int bytesRead = _cacheFile.read(cache[chaceIndex], readSize);
      // Serial.printf("[ループ続き]: %x, %x\n", cache[chaceIndex][0], cache[chaceIndex][readSize - 1]);
    }
  }

  //  Serial.printf("file size: 0x%x, gd3Size: 0x%x\n", vgm.size, vgm.gd3Size);
}

void fillCacheTask(void* pvParameters) {
  CacheTaskParam* param = (CacheTaskParam*)pvParameters;
  u32_t pos = param->pos;
  int cacheIndex = param->cacheIndex;

  fillCache(pos, cacheIndex);
  delete param;
  Serial.printf("Task End.\n");
  vTaskDelete(NULL);
}

void cacheTask(void* pvParameters) {
  CacheTaskParam param;
  while (1) {
    if (xQueueReceive(cacheQueue, &param, portMAX_DELAY) == pdTRUE) {
      fillCache(param.pos, param.cacheIndex);
    }
  }
}

// キャッシュ初期化
bool initCache(String path) {
  if (_cacheFile) {
    _cacheFile.close();
  }
  _cacheFile = SD.open(path.c_str());
  if (!_cacheFile) {
    Serial.printf("ERROR: Failed to open cache file: %s\n", path.c_str());
    return false;
  }

  // 初期充填
  activeCache = 0;
  _cacheFile.seek(0);
  _cacheFile.read(cache[0], CACHE_SIZE);
  _cacheFile.read(cache[1], CACHE_SIZE);
  cachePos = 0;

  return true;
}
//-------------------------------------------------------------------------

bool NDFile::init() {
  currentDir = 0;
  currentFile = 0;

  // セマフォ作成
  spFileOpen = xSemaphoreCreateBinary();
  xSemaphoreGive(spFileOpen);

  // SD用 SPI開始
  SPI_SD.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

  int n = 0;
  lcd.print("Checking SD card");
  while (!SD.begin(SD_CS, SPI_SD, 80000000)) {  // SD マウント試行 @ 80MHz
    vTaskDelay(200);
    n++;
    lcd.print(".");
    if (n == 10) {
      lcd.print("\n\nERROR: SD card open failed.\n");
      return false;
    }
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    lcd.println("\n\nERROR: No SD card attached.\n");
    return false;
  }

  lcd.print("\nSD card detected.");
  lcd.print("\n- Type: ");
  if (cardType == CARD_MMC) {
    lcd.println("MMC");
  } else if (cardType == CARD_SD) {
    lcd.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    lcd.println("SDHC");
  } else {
    lcd.println("UNKNOWN");
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  lcd.printf("- Size: %llu MB\n", cardSize);
  vTaskDelay(100);

  // メモリ確保
  psramInit();  // ALWAYS CALL THIS BEFORE USING THE PSRAM
  data = (u8_t*)ps_calloc(MAX_FILE_SIZE, sizeof(u8_t));

  // PSRAMキャッシュ確保
  for (int i = 0; i < NUM_CACHE; i++) {
    if (!cache[i]) {
      cache[i] = (uint8_t*)ps_calloc(CACHE_SIZE, sizeof(uint8_t));
    }
  }

  // キューを初期化
  cacheQueue = xQueueCreate(2, sizeof(CacheTaskParam));
  if (!cacheQueue) {
    Serial.println("ERROR: cacheQueue create failed!");
    return false;
  }

  // キャッシュタスク
  xTaskCreatePinnedToCore(cacheTask, "cacheTask", 4096, NULL, 1, NULL, PRO_CPU_NUM);

  return true;
}

uint16_t NDFile::getNumFilesinCurrentDir() { return files[currentDir].size(); }

void NDFile::listDir(const char* dirname) {
  File root = SD.open(dirname);
  if (!root) {
    lcd.println("Error: SD card open failed.");
    return;
  }

  String dirName;
  String filename;
  bool isDir;

  lcd.println("\nReading files...");

  // ディレクトリ取得
  dirName = root.getNextFileName(&isDir);
  while (dirName != "") {
    if (isDir) {
      if (dirName != "/System Volume Information") {
        // ディレクトリ内の有効ファイルチェック
        File dir = SD.open(dirName);
        int validFileCount = 0;
        while (1) {
          filename = dir.getNextFileName(&isDir);
          if (filename == "") break;
          if (isDir) break;
          String ext = filename.substring(filename.length() - 4);
          if (ext.equalsIgnoreCase(".vgm")) {
            validFileCount++;
          } else if (ext.equalsIgnoreCase(".xgm")) {
            validFileCount++;
          }
        }
        dir.close();

        if (validFileCount > 0) {
          dirs.push_back(dirName);
          pngs.push_back("");
        }
      }
    }
    dirName = root.getNextFileName(&isDir);
  }
  root.close();

  // 各ディレクトリ内のファイル名取得
  files.resize(dirs.size());

  u16_t x = lcd.getCursorX(), y = lcd.getCursorY();
  for (int i = 0; i < dirs.size(); i++) {
    File dir = SD.open(dirs[i]);
    lcd.setCursor(x, y);
    lcd.printf("%s                                                                                         ",
               dirs[i].c_str());

    while (1) {
      filename = dir.getNextFileName(&isDir);
      if (filename == "") break;
      if (!isDir) {
        String ext = filename.substring(filename.length() - 4);
        if (ext.equalsIgnoreCase(".vgm")) {
          totalSongs++;
          files[i].push_back(filename.substring(dirs[i].length() + 1));
        } else if (ext.equalsIgnoreCase(".xgm")) {
          totalSongs++;
          files[i].push_back(filename.substring(dirs[i].length() + 1));
        } else if (ext == ".png") {
          pngs[i] = filename.substring(dirs[i].length() + 1);
        }
      }
    }
    dir.close();
  }

  lcd.setCursor(0, y);
  return;
}

//----------------------------------------------------------------------
// ファイル開いてPSRAMに配置
bool NDFile::readFile(String path) {
  int n = 0;
  Serial.printf("File name: %s\n", path.c_str());
  _vgmFile = SD.open(path.c_str());
  if (!_vgmFile) {
    lcd.printf("ERROR: Failed to open file.\n%s", path.c_str());
    _vgmFile.close();
    return false;
  }

  vgm.size = _vgmFile.size();
  Serial.printf("vgm.size: %u Bytes.\n", vgm.size);

  if (vgm.size > MAX_FILE_SIZE) {
    // lcd.printf("ERROR: The file is too large.\nMax file size is %d.\n%s", MAX_FILE_SIZE, path.c_str());
    //_vgmFile.close();
    // return false;
    //  シーケンシャルモード始動
    accessMode = ACCESS_CACHE;
    Serial.printf("Cache mode.\n");
  } else {
    accessMode = ACCESS_PSRAM;
    Serial.printf("PSRAM mode.\n");
  }

  if (accessMode == ACCESS_PSRAM) {
    _vgmFile.read(data, vgm.size);
  }
  _vgmFile.close();

  if (accessMode == ACCESS_CACHE) {
    initCache(path.c_str());
  }

  return true;
}

//----------------------------------------------------------------------
// ディレクトリ内の count 個あとの曲再生。マイナスは前の曲
// 戻り値: 成功/不成功
bool NDFile::filePlay(int count) {
  currentFile = mod(currentFile + count, files[currentDir].size());
  ndConfig.saveHistory();
  return fileOpen(currentDir, currentFile);
}

//----------------------------------------------------------------------
// count 個あとのディレクトリを開いて最初のファイルを再生。
// マイナスは前のディレクトリ
// 戻り値: 成功/不成功
bool NDFile::dirPlay(int count) {
  currentFile = 0;
  currentDir = mod(currentDir + count, dirs.size());
  ndConfig.saveHistory();
  return fileOpen(currentDir, currentFile, ndFile.getFolderAttenuation(dirs[currentDir]));
}

//----------------------------------------------------------------------
// 直接ファイル再生
// 戻り値: 成功/不成功
bool NDFile::play(uint16_t d, uint16_t f, int8_t att) {
  currentFile = f;
  currentDir = d;
  ndConfig.saveHistory();
  return fileOpen(currentDir, currentFile, ndFile.getFolderAttenuation(dirs[currentDir]));
}

//----------------------------------------------------------------------
// ディレクトリ番号＋ファイル番号でファイルを開く
// 戻り値: 成功/不成功
// att: 音量減衰率 0 - 96 dB, -1 = 変更しない

bool NDFile::fileOpen(uint16_t d, uint16_t f, int8_t att) {
  if (xSemaphoreTake(spFileOpen, 0) != pdTRUE) {
    Serial.printf("Semapho is already taken.\n");
    return false;
  }

  nju72341.mute();
  nju72341.resetFadeout();
  FM.reset();

  String st = dirs[d] + "/" + files[d][f];

  bool result = false;

  // check file type
  String ext = st.substring(st.length() - 4);
  if (ext.equalsIgnoreCase(".vgm")) {
    if (readFile(st)) {
      result = vgm.ready();
    }

  } else if (ext.equalsIgnoreCase(".xgm")) {
    if (readFile(st)) {
      result = vgm.XGMReady();
    }
  }

  nju72341.reset(att);
  xSemaphoreGive(spFileOpen);
  nju72341.unmute();

  return result;
}

//----------------------------------------------------------------------
// フォルダの減衰量取得
// 戻り値: 0 - 96 dB
// 設定無ければ 0
uint8_t NDFile::getFolderAttenuation(String path) {
  bool isDir;

  File dir = SD.open(path);
  if (!dir) {
    return 0;
  }

  while (1) {
    String filePath = dir.getNextFileName(&isDir);
    if (filePath == "") return 0;

    if (!isDir) {
      String fileName = filePath.substring(filePath.lastIndexOf("/") + 1);
      if (fileName.substring(0, 3) == "att") {
        int att = fileName.substring(3).toInt();
        if (att > 0 && att <= 24) {
          return att;
        } else {
          return 0;
        }
      }
    }
  }
  dir.close();
  return 0;
}

//----------------------------------------------------------------------
// ヘッダのキャッシュ取得
// true: 成功
bool NDFile::getHeaderCache(String filePath) {
  Serial.println("getHeaderCache: open file");
  File file = SD.open(filePath);
  if (!file) {
    Serial.println("getHeaderCache: failed to open file");
    return false;
  }

  // Serial.print("getHeaderCache: file size = ");
  // Serial.println(file.size());
  vgm.size = file.size();
  if (file.size() < 256) {
    Serial.println("getHeaderCache: file too small");
    file.close();
    return false;
  }

  // Serial.println("getHeaderCache: read header");
  int readBytes = file.read(header, 256);
  // Serial.print("getHeaderCache: read bytes = ");
  // Serial.println(readBytes);
  file.close();
  // Serial.println("getHeaderCache: success");
  return true;
}

//----------------------------------------------------------------------
// GD3部分のキャッシュ取得
// 0 = 取得できなかった
u16_t NDFile::getGD3Cache(String filePath, u32_t gd3Offset) {
  if (gd3Offset == 0x14) return 0;  // 0x14 = data offset

  File file = SD.open(filePath);
  if (!file) {
    return 0;
  }

  if (gd3Offset >= file.size()) {
    file.close();
    return 0;
  }
  file.seek(gd3Offset);
  gd3Cache.clear();
  while (file.available()) {
    gd3Cache.push_back(file.read());
  }
  file.close();
  return gd3Cache.size();
}

// data access
// 8 bit 返す
u8_t NDFile::get_ui8() {
  u8_t result;

  if (accessMode == ACCESS_PSRAM) {
    result = data[pos++];
  } else {
    result = cache[activeCache][cachePos++];
    pos++;
    if (cachePos == CACHE_SIZE) {
      CacheTaskParam param;
      param.pos = pos;
      param.cacheIndex = activeCache;
      xQueueSend(cacheQueue, &param, 0);

      cachePos = 0;
      activeCache = 1 - activeCache;
    }
  }

  return result;
}
// 16 bit 返す
u16_t NDFile::get_ui16() { return get_ui8() + (get_ui8() << 8); }
// 24 bit 返す
u32_t NDFile::get_ui24() { return get_ui8() + (get_ui8() << 8) + (get_ui8() << 16); }
// 32 bit 返す
u32_t NDFile::get_ui32() { return get_ui8() + (get_ui8() << 8) + (get_ui8() << 16) + (get_ui8() << 24); }

// 指定場所の 8 bit 返す
u8_t NDFile::get_ui8_at(uint32_t p) { return data[p]; }

// 指定場所の 16 bit 返す
u16_t NDFile::get_ui16_at(uint32_t p) { return (u32_t(data[p])) + (u32_t(data[p + 1]) << 8); }
// 指定場所の 24 bit 返す
u32_t NDFile::get_ui24_at(uint32_t p) {
  return (u32_t(data[p])) + (u32_t(data[p + 1]) << 8) + (u32_t(data[p + 2]) << 16);
}
// 指定場所の 32 bit 返す
u32_t NDFile::get_ui32_at(uint32_t p) {
  return (u32_t(data[p])) + (u32_t(data[p + 1]) << 8) + (u32_t(data[p + 2]) << 16) + (u32_t(data[p + 3]) << 24);
}

// キャッシュ版
u8_t NDFile::get_ui8_at_header(uint32_t p) {
  if (p >= sizeof(header)) {
    Serial.printf("[WARN] get_ui8_at_header: out of range! p=%u (size=%u)\n", p, sizeof(header));
    return 0;
  }
  return header[p];
}

u16_t NDFile::get_ui16_at_header(uint32_t p) {
  if (p + 1 >= sizeof(header)) {
    Serial.printf("[WARN] get_ui16_at_header: out of range! p=%u (size=%u)\n", p, sizeof(header));
    return 0;
  }
  return (u32_t(header[p])) + (u32_t(header[p + 1]) << 8);
}

u32_t NDFile::get_ui24_at_header(uint32_t p) {
  if (p + 2 >= sizeof(header)) {
    Serial.printf("[WARN] get_ui24_at_header: out of range! p=%u (size=%u)\n", p, sizeof(header));
    return 0;
  }
  return (u32_t(header[p])) + (u32_t(header[p + 1]) << 8) + (u32_t(header[p + 2]) << 16);
}

u32_t NDFile::get_ui32_at_header(uint32_t p) {
  if (p + 3 >= sizeof(header)) {
    Serial.printf("[WARN] get_ui32_at_header: out of range! p=%u (size=%u)\n", p, sizeof(header));
    return 0;
  }
  return (u32_t(header[p])) + (u32_t(header[p + 1]) << 8) + (u32_t(header[p + 2]) << 16) + (u32_t(header[p + 3]) << 24);
}

NDFile ndFile = NDFile();

int mod(int i, int j) { return (i % j) < 0 ? (i % j) + 0 + (j < 0 ? -j : j) : (i % j + 0); }
