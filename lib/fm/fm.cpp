#include "fm.h"

#include <driver/dedic_gpio.h>

dedic_gpio_bundle_handle_t dataBus = NULL;  // GPIOバンドル用ハンドラ

// ------------------------------------------------------------------------------
// FM音源クラス
//    表記の違い
//    YM**** -> SN76489AN
//    WR -> WE
//    CS -> CE(OE)
//    READY -> No connect

void FMChip::begin() {
  // データバス用 GPIO バンドル
  const int bundleA_gpios[] = {D0, D1, D2, D3, D4, D5, D6, D7};
  gpio_config_t io_conf = {
      .mode = GPIO_MODE_OUTPUT,
  };
  for (int i = 0; i < sizeof(bundleA_gpios) / sizeof(bundleA_gpios[0]); i++) {
    io_conf.pin_bit_mask = 1ULL << bundleA_gpios[i];
    gpio_config(&io_conf);
  }
  // decic config
  dedic_gpio_bundle_config_t bundle_config = {
      .gpio_array = bundleA_gpios,
      .array_size = sizeof(bundleA_gpios) / sizeof(bundleA_gpios[0]),
      .flags =
          {
              .out_en = 1,
          },
  };
  ESP_ERROR_CHECK(dedic_gpio_new_bundle(&bundle_config, &dataBus));

  // その他の GPIO
  pinMode(WR, OUTPUT);
  pinMode(CS0, OUTPUT);
  pinMode(CS1, OUTPUT);
  pinMode(CS2, OUTPUT);

  pinMode(A0, OUTPUT);
  pinMode(A1, OUTPUT);
  pinMode(IC, OUTPUT);

  WR_HIGH;
  A0_LOW;
  A1_LOW;
  IC_LOW;

  CS0_HIGH;
  CS1_HIGH;
  CS2_HIGH;
}

void FMChip::reset(void) {
  CS0_LOW;
  CS1_LOW;
  CS2_LOW;

  WR_HIGH;
  A0_LOW;
  IC_LOW;

  ets_delay_us(32);
  // 72 cycles for YM2151 at 4MHz: 0.25us * 72 = 18us
  // 192 cycles for YM3438 -> 8MHz 0.125us * 192 = 24us
  IC_HIGH;
  CS0_HIGH;
  CS1_HIGH;
  CS2_HIGH;

  // stop sound output from SN76489
  FM.write(0x9f, 1, SI5351_1500);
  FM.write(0xbf, 1, SI5351_1500);
  FM.write(0xdf, 1, SI5351_1500);
  FM.write(0xff, 1, SI5351_1500);

  FM.write(0x9f, 2, SI5351_1500);
  FM.write(0xbf, 2, SI5351_1500);
  FM.write(0xdf, 2, SI5351_1500);
  FM.write(0xff, 2, SI5351_1500);

  _psgFrqLowByte = 0;

  delay(16);
}

// SN76489
void FMChip::write(byte data, byte chipno, si5351Freq_t freq) {
  //

  if ((data & 0x90) == 0x80 && (data & 0x60) >> 5 != 3) {
    // Low byte 周波数 0x8n, 0xan, 0xcn
    _psgFrqLowByte = data;

  } else if ((data & 0x80) == 0) {  // High byte
    if ((_psgFrqLowByte & 0x0F) == 0) {
      if ((data & 0x3F) == 0) _psgFrqLowByte |= 1;
    }
    writeRaw(_psgFrqLowByte, chipno, freq);
    writeRaw(data, chipno, freq);

  } else {
    writeRaw(data, chipno, freq);
  }
}

void FMChip::writeRaw(byte data, byte chipno, si5351Freq_t freq) {
  switch (chipno) {
    case 0:
      CS0_LOW;
      break;
    case 1:
      CS1_LOW;
      break;
    case 2:
      CS2_LOW;
      break;
  }
  WR_HIGH;
  dedic_gpio_bundle_write(dataBus, 0xff, data);

  // コントロールレジスタに登録するには WR_LOW → WR_HIGH 最低32クロック
  // 4MHz     :　0.25us   * 32 = 8 us
  // 3.579MHz :  0.2794us * 32 = 8.94 us
  // 1.5MHz   :  0.66us   * 32 = 21.3 us
  WR_LOW;

  ets_delay_us((32000000 / freq) + 1);

  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }
}

byte lastAddr = 0;
void FMChip::setYM2612(byte bank, byte addr, byte data, uint8_t chipno) {
  switch (chipno) {
    case 0:
      CS0_LOW;
      break;
    case 1:
      CS1_LOW;
    case 2:
      CS2_LOW;
      break;
  }

  if (bank == 1) {
    A1_HIGH;
  } else {
    A1_LOW;
  }

  lastAddr = addr;

  // Address
  A0_LOW;
  dedic_gpio_bundle_write(dataBus, 0xff, addr);
  WR_LOW;
  WR_HIGH;
  A0_HIGH;

  // アドレスライト後の待ちサイクル
  // アドレス＄21-＄B6 待ちサイクル 17 = 2.21us
  ets_delay_us(5);  // 3 は一部足りない

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);

  WR_LOW;
  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }

  if (bank == 1) {
    A1_LOW;
  }
  // unsigned long deltaTime = micros() - startTime;
  // Serial.printf("%x%d\n", addr, deltaTime);
  if (addr == 0x2a) {
  } else if (addr >= 0x21 && addr <= 0x9e) {
    ets_delay_us(12);  // 83 cycles = 10.79us,
  } else if (addr >= 0xa0 && addr <= 0xb6) {
    ets_delay_us(8);  // 47 cycles = 6.11us
  }

  // YM3438 Twww マニュアルより
  // WR_LOW -> WR_HIGH: Tww 200 ns
  // Dn -> WR_HIGH: Twds 100 ns

  // データ-アドレスライト間, データデータ間 ($21 - $9E) 83サイクル = 10.79 us
  // データ-アドレスライト間  データデータ間 ($A0 - $B6) 47サイクル = 6.11 us
}

// YM2612 の DAC データ送信専用
void FMChip::setYM2612DAC(byte data, uint8_t chipno) {
  switch (chipno) {
    case 0:
      CS0_LOW;
      break;
    case 1:
      CS1_LOW;
      break;
  }

  if (lastAddr != 0x2a) {
    lastAddr = 0x2a;
    // Address
    A0_LOW;
    dedic_gpio_bundle_write(dataBus, 0xff, 0x2a);
    WR_LOW;
    WR_HIGH;
    A0_HIGH;
    // アドレスライト後の待ちサイクル
    // アドレス＄21-＄B6 待ちサイクル 17 = 2.21us
    ets_delay_us(4);
  }

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);
  WR_LOW;
  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
  }
}

// YM2203, AY-8910用レジスタ設定
void FMChip::setRegister(byte addr, byte data, int chipno = 0) {
  // Address
  dedic_gpio_bundle_write(dataBus, 0xff, addr);
  A0_LOW;  // 375ns
  switch (chipno) {
    case 0:
      CS0_LOW;
      CS1_HIGH;
      CS2_HIGH;
      break;
    case 1:
      CS0_HIGH;
      CS1_LOW;
      CS2_HIGH;
      break;
    case 2:
      CS0_HIGH;
      CS1_HIGH;
      CS2_LOW;
      break;
  }
  ets_delay_us(2);
  WR_LOW;
  ets_delay_us(2);
  WR_HIGH;
  A0_HIGH;

  ets_delay_us(3);

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);
  ets_delay_us(2);
  WR_LOW;
  ets_delay_us(2);
  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }
  ets_delay_us(16);  // 最低16
}

// 　YM2151用レジスタ設定(最適化済)
void FMChip::setRegisterOPM(byte addr, byte data, uint8_t chipno = 0) {
  dedic_gpio_bundle_write(dataBus, 0xff, addr);
  A0_LOW;
  switch (chipno) {
    case 0:
      CS0_LOW;
      CS1_HIGH;
      CS2_HIGH;
      break;
    case 1:
      CS0_HIGH;
      CS1_LOW;
      CS2_HIGH;
      break;
    case 2:
      CS0_HIGH;
      CS1_HIGH;
      CS2_LOW;
      break;
  }
  ets_delay_us(2);
  WR_LOW;
  ets_delay_us(2);
  WR_HIGH;
  A0_HIGH;

  ets_delay_us(3);

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);
  ets_delay_us(2);
  WR_LOW;
  ets_delay_us(2);
  WR_HIGH;

  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }
  ets_delay_us(12);
}

void FMChip::setRegisterOPL3(byte port, byte addr, byte data, int chipno) {
  switch (chipno) {
    case 0:
      CS0_LOW;
      CS1_HIGH;
      CS2_HIGH;
      break;
    case 1:
      CS0_HIGH;
      CS1_LOW;
      CS2_HIGH;
    case 2:
      CS0_HIGH;
      CS1_HIGH;
      CS2_LOW;
      break;
  }
  if (port == 1) {
    A1_HIGH;
  } else {
    A1_LOW;
  }

  // Address
  A0_LOW;
  dedic_gpio_bundle_write(dataBus, 0xff, addr);
  WR_LOW;
  ets_delay_us(16);
  WR_HIGH;
  A0_HIGH;

  ets_delay_us(16);

  // 32 clocks to write address
  // 14.318180 MHz: 69.84 ns / cycle
  //  x 32 = 2,234.88 ns = 2.235 us

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);
  WR_LOW;
  ets_delay_us(16);
  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }
  if (port == 1) {
    A1_LOW;
  }

  ets_delay_us(16);
}

FMChip FM;
