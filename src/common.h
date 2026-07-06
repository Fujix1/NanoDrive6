#ifndef COMMON_H
#define COMMON_H

// Current hardware setup
#define CHIP0 CHIP_YM2612
#define CHIP1 CHIP_SN76489_0
#define CHIP2 CHIP_SN76489_1

#define USE_YM2612
#define USE_SN76489
#define USE_YM2203_0

#define CHIP0_CLOCK CLK_0
#define CHIP1_CLOCK CLK_1
#define CHIP2_CLOCK CLK_0

// LCD
#define LCD_W 170
#define LCD_H 320
#define LCD_CLK 2
#define LCD_MOSI 44
#define LCD_RST 42
#define LCD_DC 41

// DISPLAY
#define TITLE_DEVIDER " *** "
#define SCROLL_SPEED_TITLE .8F   // 文字スクロール速度 px
#define SCROLL_SPEED_GAME .6F    // 文字スクロール速度 px
#define SCROLL_SPEED_AUTHOR .4F  // 文字スクロール速度 px
#define SCROLL_DELAY 2000        // スクロール開始までのディレイ ms
#define DISP_TIMER_INTERVAL 20   // ms 表示更新タイマー間隔
#define DISP_UPDATE_TASK_STACK 6144
#define DISP_UPDATE_TASK_PRIORITY 1
#define DISP_UPDATE_TASK_CORE 0

// PLAYBACK
// VGM/XGM の再生処理は Arduino loop から分離し、入力/描画処理の長い処理に巻き込まれにくくする。
// loop と同じ優先度で Core1 に固定し、FreeRTOS の time slice で入力処理と共存させる。
#define PLAYBACK_TASK_STACK 8192
#define PLAYBACK_TASK_PRIORITY 1
#define PLAYBACK_TASK_CORE APP_CPU_NUM

// SD Card
#define SD_CS 7
#define SD_MOSI 6
#define SD_CLK 5
#define SD_MISO 4

#define MAX_PNG_WIDTH 640
#define MAX_FILE_SIZE 7000000  // 6613100

// I2C
#define I2C_SDA 16
#define I2C_SCL 15
#define I2C_CLOCK 400000

// NJU72341
#define NJU72341_MUTE_PIN 17

#endif
