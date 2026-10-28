# ND6 / ND6.1 NDSIF DAC 試験実装

音源単位への共通化方針は `NDSIF_YM2612.md` に整理する。本書は現行ND6の実装と制約を記録する。

ND8 文書版 0.40、ND7/ND8 API 1.0 revision 39 のフレームを基にした ND6 向け実験拡張。
表示する API 版は 1.0、ローカル revision は 40。正式な共通仕様の成立や ND7/ND8 の対応を意味しない。
既存 MSM6258 用 `10`～`13` と `B7` は流用しない。ND7/ND8 のソース・文書は変更しない。

## 目的と実行経路

DAC の USB 到着時刻への依存をなくし、一定容量の先行キャッシュから出力する。
内部 RAM に 8192 件 × 4 byte = 32 KiB の DAC リングを確保する。
別途 FM/PSG 用に 512 件のイベントリングを持つ。これらは NDSIF モードでのみ確保する。
USB RX 8192 byte、TX 512 byte は USB 開始前に設定する。USB の初期化と ISR 割り当ても core 0 で行う。

- core 0: USB ISR、受信、応答送信、送信復帰、COBS/CRC、DATA 検証、バッファへの登録。受信の仕事量は一周 512 byte。
- core 1: 優先度 3 の出力タスク。DAC、通常 FM/PSG、RESET、クロック変更の音源 I/O を単独所有する。
- 通常 FM/PSG だけの再生では DAC を START せず、ND7 同様の無応答 BURST とホスト時刻で動作する。3.1dev5 では停止中の出力タスクをジョブキュー待ちにし、新しい BURST で直ちに起床する。毎フレーム後の固定1tick待ちは入れない。
- DAC の値と `$2B` は同じ FIFO で順序を維持する。受信や画面描画から直接音源バスへ書き込まない。
- FM/PSG は指定時刻より前には出さず、DAC 期限まで余裕があるときに処理する。DAC と同じ時刻精度・同時刻の全音源間順序は保証しない。
- 共有状態の短い更新だけを critical section に入れる。DAC フレームの検証、CRC/COBS、音源 I/O はその外で処理する。

1 tick = 1/44100 秒。ウェイトは「この値を出力した後、次のレコードまで」の tick 数。
DAC の固定 sample rate を意味せず、2・3 tick 等の可変間隔、0 wait、PCM のない期間も保持する。
期限は `origin + floor(cumulative_tick * 1000000 / 44100)` で求める。
USB パケット境界・チャンク境界・各書き込みの処理時間を次のウェイトへ加算しない。

## フレームと基本制御

整数は little-endian。外枠は `00 + COBS(raw) + 00`。
`raw = 'ND' + version:u8(1) + message_type:u8 + command_id:u8 + transaction_id:u16 + length:u16 + payload + crc16:u16`。
最大 payload 256 byte、CRC-16/CCITT-FALSE。message_type は 00=無応答、01=制御要求、02=応答。
制御応答は同じ command_id / transaction_id、payload 先頭の status が 0=成功、1=拒否。
不正フレームは破棄し、未完フレームは 500ms で期限切れ。WRITE への ACK・自動再送はない。

対応する基本制御は RESET `00`、PING `01`、GET_INFO `02`、SET_CHIP_CLOCK `04`、GET_CAPABILITIES `05`。
3.1dev9 から SET_OUTPUT_VOLUME `03` に対応。音量 IC 搭載時に control_features bit0 を通知する。
attenuation は0～96（0=減衰なし、1～95=dB、96=ミュート）。既存の NJU72341/NJU72342 ドライバで最終出力へ適用する。
I2C は受信タスク（core 0）で実行し、DAC 出力タスク（core 1）や streamMux の区間へ入れない。
音源レジスタ・DAC のサンプル値は変更しない。Delphi は再生開始時に0へ戻し、再生時刻に合わせて03を送信する。
RESET はバッファ・診断と音源を初期化し、現在のクロックを維持する。
GET_INFO の機種名は NanoDrive 6 / NanoDrive 6.1。GET_CAPABILITIES は従来の 9 byte entry。

| WRITE ID | instance | page | payload の書き込み部分 |
| --- | --- | --- | --- |
| 52 | 0 | 0 | `(address:u8, value:u8)` 1～127 組 |
| 53 | 0 | 1 | 同上 |
| 50 | 0 | 0 | SN76489 の byte 列 1～254 byte |
| 30 | 1 | 0 | 2 個目の SN76489 の byte 列 1～254 byte |

各 payload の先頭は instance:u8 + page:u8。すべて message_type=00。
DAC データを一度でも登録した後は、即時 52 の `$2A/$2B` を含むフレーム全体を拒否する。
これらは DATA の DAC / SELECT レコードで送る。通常 FM/PSG の即時書き込みは受信順を維持する。
演奏中のクロック変更は拒否する。初期化時に設定する。

CLK0 は YM2612 と 2 個目の SN76489 が共有し、CLK1 は 1 個目の SN76489。
3.1dev8 は PAL PSG の 3,546,895 Hz を追加。25MHz基準の PLL を 28 + 9379/25000、整数分周を200とし、指定周波数を生成する。
Time Trax の YM2612=7,600,489 Hz / PSG=3,546,895 Hz は別々の PLL/CLK を使用する。近傍値の許容幅0.1%は維持し、3,579,545 Hz へ置き換えない。
3.1dev6 の NDSIF Player 画面は、フッタ上段に YM2612 (CLK0)、下段に1個目の SN76489 (CLK1) を黄色・MHz小数3桁で表示する。
SET_CHIP_CLOCK の適用後に更新要求だけを通知し、表示タスクがフッタ40pxのみを再描画する。設定画面を閉じて戻った場合も現在値を表示する。
3.1dev7 ではフッタ専用の RGB565 sprite (13,600 bytes) を廃止し、既存の画面バッファへ描いて LCD のクリップ範囲で部分転送する。
内部 RAM を消費する追加ピクセルバッファをなくし、3.1dev6 で発生した受信タスク生成失敗の起動ループを修正する。
DAC キャッシュ32KiBとタスクのスタックサイズは維持する。
`52/53/30` のクロック設定は CLK0、`50` は CLK1 に作用する。共有先へ別々の周波数を設定できない。
周波数は `include/ClockMatch.h` の対応値へ最大 0.1% の差を正規化し、未対応値を拒否する。
base Capability の data_capacity / event_capacity は 0。control_features bit1 を実験 DAC 問い合わせ対応に使用する。
この bit は共通仕様の確定値ではなく、次の GET_DAC_CAPABILITIES で必ず revision と形式を確認する。

## 実験 DAC 制御

profile=1、対象は YM2612 instance=0。命令 ID はすべて 16 進。

| ID | 名称 | message_type | 要求 payload |
| --- | --- | --- | --- |
| 18 | GET_DAC_CAPABILITIES | 01 | 空 |
| 19 | DAC_DATA | 00 | profile:u8 + write_id:u8(52) + instance:u8(0) + sequence:u32 + position:u64 + records |
| 1A | DAC_START | 01 | profile:u8 + minimum_lead_ticks:u32 |
| 1B | DAC_END | 00 | profile:u8 + next_sequence:u32 + end_tick:u64 |
| 1C | DAC_STATUS | 01 | profile:u8 |
| 1D | REGISTER_EVENT | 00 | profile:u8 + tick:u64 + write_id:u8 + instance:u8 + page:u8 + writes |

CAPABILITIES 成功応答の status に続く部分（21 byte）:
`profile:u8 + revision:u32(40) + ticks_per_second:u32(44100) + record_capacity:u32(8192) + event_capacity:u16(512) + max_records:u8(60) + record_bytes:u8(4) + max_dac_late_us:u32(2000)`。

### DAC_DATA

1～60 レコードをまとめる。1 レコードは `kind:u8 + value:u8 + wait_ticks:u16` の 4 byte。

| kind | 動作 | value |
| --- | --- | --- |
| 0 | `$2A` へ DAC 値を書き込む | unsigned 8bit、無音中心 80 |
| 1 | `$2B` の bit7 を切り替える | 00=CH6 FM、80=DAC |
| 2 | 時間だけ進める | 0 固定、wait_ticks は 1 以上 |

すべての kind に後続 wait が付く。kind 0/1 の wait=0 は有効。
3.1dev11 は受信した全 DAC レコードへ元の2,000us遅延判定を適用する。dev10 の同一 tick 特例は撤回した。
受信側で VGM の初期化判定・値の間引き・補間・暗黙の wait 追加を行わず、DATA の値と wait を順番に実行する。
VGM の正規化は Delphi ホストの責務。ND6 の通常 VGM 再生と同じく、最初の正 wait 前の時刻0に限り、
DAC は最後の値だけ、YM2612 $28 はチャンネルごとの最終キー状態を最終書き込み順に送る。
元の全コマンドを読み、bank の位置・seek と総 wait は維持する。正の時刻へ進んだ後のデータには適用しない。
時刻0の通常 FM/PSG 設定は START 前に完了し、最終キーと DAC は時刻0の再生列へ置く。
Sonic 2 / Emerald Hill Zone の開始 DAC は1,612件から最終値122の1件へまとまる。
late_dac / max_late_us は、受信した DAC 書き込みごとの到着遅延を記録する。
sequence は RESET 後のレコード通番で、最初は 0。position は先行 DATA の末尾 tick と完全一致させる。
通番は 0 wait のチャンク重複・欠落も検出する。全レコードを検証してから一括格納し、部分受理しない。
sequence の u32 周回と position の u64 周回を認めない。実装は position <= UINT64_MAX/1000000 を要求する。

### 先行供給と終了

START は明示要求。バッファが空でなく、指定した正の先行 tick 数が蓄積している場合に開始する。
短い曲は、すべて DATA を送った後に END で確定すれば先行量未満でも START できる。
現行Delphiホストの先行量は80ms。チャンク長は別であり、80ms分を一つのフレームへ収める必要はない。
START 成功応答は開始したことを示し、全 DAC 出力完了を示さない。
END は最後の DATA の次通番・末尾 tick を指定し、以後の DATA/EVENT を禁止する。
最終レコードの wait まで経過し、末尾に残る FM/PSG を処理した後に Ended となり音止めする。
途中の無音・通常の離鍵には END を使わず、WAIT 等で時間を進める。

REGISTER_EVENT は対応 DATA より先に送る。tick は登録済み DATA の末尾以上、イベント間で非減少。
writes は YM の address/value 組、SN は byte 列。1 組/byte がリングの 1 件。
`$2A/$2B` を含む FM イベントはフレーム全体を拒否し fault にする。
EVENT と DATA を別送するため、ホストが対応区間の全イベントを先に送った後で DATA によって区間を確定する。
再生の精密な順序対象は DAC と SELECT。通常 FM/PSG は混雑時に数 ms 以上遅れる可能性があり、実機評価が必要。

### DAC_STATUS

status に続く 72 byte は次のとおり。

- profile:u8、state:u8（0=Ready、1=Running、2=Ended、3=Fault）、fault:u8、sealed:u8。
- u32 × 11: accepted_sequence、consumed_sequence、queued_records、free_records、queued_events、consumed_events、underflows、overflows、rejected、late_dac_count、max_dac_late_us。
- u64 × 3: accepted_tick、played_tick、executed_record_tick。

played_tick は再生時計の現在位置を受理済み末尾で制限した値。executed_record_tick は最後に取り出したレコードの時刻。
max_dac_late_us はスケジューラ取り出し時点の最大遅延であり、実チップの WR エッジを測定した値ではない。
fault は 0=なし、1=供給不足、2=容量超過、3=位置/通番不正、4=引数不正、5=USB reset、6=DAC の 2ms 超過遅延。
供給不足や故障時は音止めし、DATA を補って再開しない。RESET と初期化をやり直す。
各 DAC レコードの短いスケジューラ遅延は最大値・件数へ記録する。2ms 超の遅延は追いつき出力を続けず停止する。
USB SOF の一時消失を切断とみなさず、明示 bus reset で fault とフレーム再同期を行う。
USB ドライバー内部の欠落は直接集計できないが、後続 DATA/END の通番照合、CRC、供給不足で検出する。
接続断 watchdog は実装していないため、長い WAIT 中のホスト異常終了を直ちに音止めする保証はない。

## ホストの送信制御

Delphi ホストは、ND6 と ND8 のバッファ再生に共通の80ms先行量を使用する。
開始前はエンコード済み片方向パケットを最大2048byteごとに区切り、PINGで同期する。
再生中のND6 DAC / FM/PSGデータは未確認量を4096byte以内に制限し、
非同期STATUSの応答を使って送信枠を返す。ENDと音量設定には予約枠を使う。
値・waitの変更、DATAの再送、受信側での間引きは行わない。

再生中のGetCommStateはUSB CDCの制御要求を発生させることがあり、
ESP32-S3の受信データ破損と同時に観測されたため、ホストは再生中に実行しない。
実機では修正後、問題があった曲が完走した。アイドル時の接続確認は維持する。

## 64 byte 応答

64 byte の整数倍となる応答には空の区切り 00 を一つ追加する。
COBS フレーム本体・CRC・payloadは変更せず、空の区切りを無視する規約を使う。
