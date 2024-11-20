#include "file.h"

static SPIClass SPI_SD;
std::vector<String> dirs;                // ルートのディレクトリ一覧
std::vector<String> pngs;                // ディレクトリごとのpng
std::vector<std::vector<String>> files;  // 各ディレクトリ内のファイル一覧
static File _vgmFile;

static SemaphoreHandle_t spFileOpen;  // ファイル開く処理用セマフォ

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
  return true;
}

uint16_t NDFile::getNumFilesinCurrentDir() { return files[currentDir].size(); }

void NDFile::listDir(const char *dirname) {
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
  _numDirs = dirs.size();

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

  _vgmFile = SD.open(path.c_str());
  if (!_vgmFile) {
    lcd.printf("ERROR: Failed to open file.\n%s", path.c_str());
    _vgmFile.close();
    vgm.size = 0;
    return false;
  }

  if (_vgmFile.size() > MAX_FILE_SIZE) {
    lcd.printf("ERROR: The file is too large.\nMax file size is %d.\n%s", MAX_FILE_SIZE, path.c_str());
    _vgmFile.close();
    vgm.size = 0;
    return false;
  }
  vgm.size = _vgmFile.size();
  _vgmFile.read(vgm.vgmData, vgm.size);
  Serial.printf("File name: %s\n", path.c_str());
  _vgmFile.close();

  // check file
  String ext = path.substring(path.length() - 4);
  if (ext.equalsIgnoreCase(".vgm")) {
    vgm.format = FORMAT_VGM;
  } else if (ext.equalsIgnoreCase(".xgm")) {
    vgm.format = FORMAT_XGM;
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
  currentDir = mod(currentDir + count, _numDirs);
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

  // stop sound output off SN76489
  FM.write(0x9f, 1, SI5351_1500);
  FM.write(0xbf, 1, SI5351_1500);
  FM.write(0xdf, 1, SI5351_1500);
  FM.write(0xff, 1, SI5351_1500);

  FM.write(0x9f, 2, SI5351_1500);
  FM.write(0xbf, 2, SI5351_1500);
  FM.write(0xdf, 2, SI5351_1500);
  FM.write(0xff, 2, SI5351_1500);

  String st = dirs[d] + "/" + files[d][f];

  bool result = false;

  if (readFile(st)) {
    if (vgm.format == FORMAT_VGM && vgm.ready()) {
      result = true;
    } else if (vgm.format == FORMAT_XGM && vgm.XGMReady()) {
      result = true;
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

  return 0;
}

NDFile ndFile = NDFile();

int mod(int i, int j) { return (i % j) < 0 ? (i % j) + 0 + (j < 0 ? -j : j) : (i % j + 0); }
