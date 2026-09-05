#ifndef FILE_H
#define FILE_H

#include <FS.h>
#include <SD.h>

#include <vector>

#include "NJU72341.h"
#include "common.h"
#include "disp.h"
#include "fm.h"
#include "nd.h"
#include "vgm.h"

int mod(int i, int j);

#define CACHE_SIZE (128 * 1024)
#define NUM_CACHE 2

struct Node;

// extern uint8_t cache[NUM_CACHE][CACHE_SIZE] __attribute__((aligned(4)));
extern uint8_t* cache[NUM_CACHE];  // PSRAM用キャッシュ

// 読み込みキャッシュ
extern volatile int activeCache;  // アクティブなキャッシュ
extern volatile int cachePos;     // キャッシュ内の位置

void fillCacheTask(void* pvParameters);
bool initCache(String path);

enum AccessMode { ACCESS_PSRAM, ACCESS_CACHE };  // アクセスモード PSRAM に全部入れる | 逐次

//------------------------------------------------------
// FileTree 保持
enum NodeType : u8_t { NODE_TYPE_DIR, NODE_TYPE_FILE };

// ファイルノード
struct Node {
  NodeType type;
  char* name;            // ファイル名 PSRAM配置
  char* pngName;         // ディレクトリ既定 or ファイル固有のpngファイル名 PSRAM配置
  Node* parent;          // 親ディレクトリ
  Node* firstChild;      // 最初の子ノード
  Node* lastChild;       // 最後の子ノード
  Node* prev;            // 前の兄弟
  Node* next;            // 次の兄弟
  int fileCount;         // ディレクトリ直下の有効ファイル数
  int dirCount;          // ディレクトリ直下の有効ディレクトリ数
  int subtreeFileCount;  // 自ノード配下の全有効ファイル数

  Node()
      : type(NODE_TYPE_FILE),
        name(nullptr),
        pngName(nullptr),
        parent(nullptr),
        firstChild(nullptr),
        lastChild(nullptr),
        prev(nullptr),
        next(nullptr),
        fileCount(0),
        dirCount(0),
        subtreeFileCount(0) {
  }
};

class FileTree {
 public:
  FileTree();
  ~FileTree();

  bool begin(const char* rootPath);
  String getFullPath(Node* node);
  Node* findNodeByPath(const String& path);
  Node* getNextDirNode(Node* node);
  Node* getPrevDirNode(Node* node);
  Node* getNextFileNode(Node* node, bool wrap);
  Node* getPrevFileNode(Node* node, bool wrap);
  int getFileIndexInParent(Node* node) const;
  Node* getFileNodeByIndexInDir(Node* dir, int index) const;
  int getGlobalFileIndex(Node* node) const;
  Node* getFileNodeByGlobalIndex(int index) const;
  int getDirIndex(Node* node) const;
  Node* getDirNodeByIndex(int index) const;

  Node* getRoot() const { return _rootNode; }
  int getTotalFiles() const { return _totalFiles; }

 private:
  Node* _rootNode;
  int _totalFiles;

  bool _isPlayableDir(Node* node) const;
  Node* _findNextDirSibling(Node* node) const;
  Node* _findPrevDirSibling(Node* node) const;
  Node* _findFirstRootDir() const;
  Node* _findLastRootDir() const;
  Node* _findNodeByPath(Node* node, const String& path);
  Node* _findFirstPlayableDirFrom(Node* node) const;
  Node* _findLastPlayableDirFrom(Node* node) const;
  Node* _findFirstPlayableDirInSubtree(Node* node) const;
  Node* _findLastPlayableDirInSubtree(Node* node) const;
  bool _findGlobalFileIndex(Node* node, Node* target, int& index) const;
  Node* _findFileNodeByGlobalIndex(Node* node, int& index) const;
  bool _findDirIndex(Node* node, Node* target, int& index) const;
  Node* _findDirNodeByIndex(Node* node, int& index) const;
  Node* _buildTree(const char* path, Node* parent);
  bool _isTargetFile(const char* filename);
  char* _ps_strdup(const char* s);
  void _deleteTree(Node* node);
};

class NDFile {
 public:
  bool init();
  void listDir(const char* dirname);
  FileFormat readFile(String path);

  // 入力/表示イベント側からの再生要求API。
  // 要求はバッファリングせず最新1件だけ保持し、processPlaybackQueue() が再生ループ側で処理する。
  bool requestFilePlay(int count);
  bool requestDirPlay(int count);
  // シリアル操作用。別の再生要求を処理中ならキューへ入れず破棄する。
  bool requestFilePlayIfIdle(int count);
  bool requestDirPlayIfIdle(int count);
  bool requestAutoNextPlay();
  bool requestPlay(uint16_t d, uint16_t f, int8_t att = -1);
  bool requestPlay(Node* node, int8_t att = -1);
  bool processPlaybackQueue();
  bool isPlaybackPending();
  void clearPlaybackQueue();
  uint8_t getFolderAttenuation(String path);  // フォルダの音量減衰取得
  Node* findFileNodeByHistory(const String& dir, const String& file);

  uint16_t currentDir;      // 現在のディレクトリ
  uint16_t currentFile;     // 現在のファイル
  uint16_t totalSongs = 0;  // 合計曲数
  uint16_t getNumFilesinCurrentDir();
  uint16_t getCurrentFileIndex();
  uint16_t getCurrentDirFileCount();
  String getCurrentFileName();
  String getCurrentDirPath();
  String getCurrentFilePath();
  String getCurrentDirPngName();
  String getCurrentFilePngName();
  Node* currentNode;

  uint8_t* data;                           // データ本体
  uint32_t pos;                            // データ位置

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
  void resetRandomSession();

 private:
  enum class PlaybackCommandType : u8_t {
    FileRelative,
    DirRelative,
    AutoNext,
    PlayIndex,
    PlayNode,
  };

  struct PlaybackCommand {
    PlaybackCommandType type;
    int32_t a;
    int32_t b;
    int8_t att;
    Node* node;
  };

  enum RandomStateKind : u8_t {
    RANDOM_STATE_NONE,
    RANDOM_STATE_FOLDER_FILE,
    RANDOM_STATE_ALL_FILE,
  };

  struct RandomSequenceState {
    RandomStateKind kind;
    Node* scopeNode;
    Node* currentFile;
    int total;
    int offset;
    int anchorIndex;
    int anchorPermutation;
    u32_t salt;

    RandomSequenceState()
        : kind(RANDOM_STATE_NONE),
          scopeNode(nullptr),
          currentFile(nullptr),
          total(0),
          offset(0),
          anchorIndex(0),
          anchorPermutation(0),
          salt(0) {
    }
  };

  RandomSequenceState _folderFileRandomState;
  RandomSequenceState _allFileRandomState;
  QueueHandle_t _playbackQueue = nullptr;
  bool _playbackBusy = false;

  // 再生所有者側の実行API。
  // 外部からは直接呼ばず、request*() -> processPlaybackQueue() 経由で実行する。
  bool _filePlay(int count);
  bool _dirPlay(int count);
  bool _autoNextPlay();
  bool _playIndex(uint16_t d, uint16_t f, int8_t att = -1);
  bool _fileOpen(uint16_t d, uint16_t f, int8_t att = -1);
  bool _openFile(String path, int8_t att = -1);

  bool _sendPlaybackCommand(const PlaybackCommand& command, bool discardIfBusy = false);
  bool _playNode(Node* node, int8_t att = -1);
  bool _playRandomFile(int count);
  bool _playRandomAll(int count);
  Node* _getCurrentDirNode() const;
  void _updateCurrentIndexes();
  bool _prepareRandomState(RandomSequenceState& state, RandomStateKind kind, Node* scopeNode,
                           int total, int currentIndex);
  Node* _advanceRandomState(RandomSequenceState& state, int count);
  Node* _getNodeFromRandomState(const RandomSequenceState& state, int logicalIndex) const;
  int _normalizeModulo(int value, int mod) const;
  int _getPermutationValue(int index, int total, u32_t salt) const;
  u32_t _permuteDomainValue(u32_t value, int bits, u32_t salt) const;
  u32_t _nextRandomValue() const;
  void _resetRandomState(RandomSequenceState& state);
};

extern NDFile ndFile;
extern FileTree fileTree;

#endif
