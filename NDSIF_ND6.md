# NanoDrive 6 / 6.1 NDSIF 実装

更新日 2026-11-20。FW `3.1dev26`。API `1.0` / 共通コア revision `40`。

共通仕様は `NDSIF.md`、wire仕様は `NDSIF_API.md`、ホスト手順は `NDSIF_HOST_GUIDE.md` を参照する。ND6 / ND6.1は即時BURSTと、時間付きDAC profile 1、ライブDAC profile 2、順序付きライブ profile 3を提供する。

## 対応音源と制御

| WRITE ID | instance | page | 対象 |
| --- | ---: | ---: | --- |
| `52` | 0 | 0 | YM2612 page 0、address/value組 |
| `53` | 0 | 1 | YM2612 page 1、address/value組 |
| `50` | 0 | 0 | SN76489 1個目、byte列 |
| `30` | 1 | 0 | SN76489 2個目、byte列 |

各対象は即時書き込みと可変クロックに対応する。SET_OUTPUT_VOLUMEは音量IC搭載時に対応し、最終ミキシング後の出力に適用する。GET_INFOは検出した `NanoDrive 6` または `NanoDrive 6.1` を返す。RESETは音源とキュー、診断、Visualの鍵盤状態を初期化し、クロック設定を維持する。

CLK0はYM2612と2個目のSN76489で共有し、CLK1は1個目のSN76489に使う。`52` / `53` / `30` のクロック設定はCLK0、`50` はCLK1に作用するため、共有先へ別々の周波数は設定できない。対応するSI5351の周波数に対して最大0.1%の差を正規化し、それ以外は拒否する。PALのYM2612 7,600,489 HzとPSG 3,546,895 Hzも対応する。ストリーム再生中のクロック変更は拒否する。

## 時間付きDAC

GET_CAPABILITIESのcontrol_features bit1が `18` への問い合わせ対応を示す。ND6の `18` 応答はprofile 1、revision 40、44,100 ticks/s、DACリング8192レコード、イベントリング512件、1 DATA最大60レコード、1レコード4 byte、DAC遅延上限2,000 µsを通知する。基本能力のdata_capacity / event_capacityは0であり、このプロファイルの容量は `18` で取得する。

DAC対象は `52/instance=0`。時刻付きFM/PSGイベントの対象は上記4経路。DATAを一度登録した後、即時 `52` に `$2A` / `$2B` を含むフレームは全体が処理されない。以後これらはDATAのDAC / SELECTレコードで扱う。YM page 0の `$2A` / `$2B` をREGISTER_EVENTに入れた場合はfaultになる。

DACと `$2B` は同じFIFOで順序を保ち、指定された値・waitを間引きや補間なしに実行する。FM/PSGは別イベントFIFOで処理し、DACと同じ精度や全音源間の同時刻順序を保証しない。末尾tickは `UINT64_MAX / 1000000` 以下。供給不足、位置不正、容量超過、USB reset、2msを超えるDAC遅延などのfaultでは音止めし、RESETからやり直す。接続断watchdogはない。

## ライブDAC（profile 2 / revision 1）

GET_CAPABILITIESのcontrol_features bit2と `1E` で対応を通知する。対象は `52` / instance 0、時間単位は44,100 tick/秒、キューは512レコード、`1F` 1フレームの最大50レコード。各レコードは前のDAC値からのwait:u32と値:u8で、最初に受理した値は前の値がないため直ちに適用する。固定の先行供給やSTARTは必要としない。

キューが空なら最後の値を保持して入力を待つ。満杯なら新規フレームを全体として破棄し、空・満杯のみではfaultや音源リセットにしない。送信側にACKや自動再送はない。時刻の遅れ、処理能力・通信量を超える供給、キュー満杯による欠落や音質への影響はホストと運用条件に依存する。

FMと `$2B` は即時 `52` / `53` を使用できるが、ライブDACとの厳密な前後関係は保証しない。ライブ中の即時 `$2A` はフレーム全体を実行しない。profile 1とは同一物理YM2612で同時に使えず、切り替えにはRESETが必要。RESETとUSBバスリセットはライブキューと過去時刻を破棄する。

## 順序付きライブ（profile 3 / revision 5）

GET_CAPABILITIESのcontrol_features bit3と `21` で対応を通知する。44,100 tick/秒、FIFO 512件、1フレーム最大100件かつpayload 256 byte以下。YM2612のFM・DAC・DAC有効切替と1個目のSN76489を、単一FIFOで受理順に処理する。PSGは `FF` レコードのpage 2・register `00` で指定する。現行の順序付き形式が扱うPSGは1個目のみで、profile 1と即時BURSTでは2個目も利用できる。

受信FIFOが満杯の場合は当該フレームを保持して空きを待つ。空の場合は次の受理データで再開する。`22` は無応答で、`23` は受理連番・件数・空き・欠落フレーム数を照会する。モード切替やクロックを再設定する場合はRESETする。現行のMAMEブリッジはこの方式を既定で使用する。DelphiのYM2612 DAC付きVGM再生はprofile 1を使用する。

profile 3はFW `3.1dev20` で導入し、`3.1dev22` で無応答バースト、`3.1dev23` で満杯時の待機、`3.1dev24` で可変長レコード、`3.1dev25` で順序付きPSGに対応した。

## ND6固有の診断

profile 3は原因調査用の応答付き要求 `20` / payload `N6D3` で調べられる。status 後の形式は `version:u8=3, queued:u16, peak:u16, accepted:u32, dequeued:u32, full_retries:u32, late_over_100us:u32, late_after_empty:u32, worst_late_us:u32, first_us:u64, last_us:u64, source_ticks:u64`。時刻は物理 I/O の読戻しではなくイベント取り出し時のスケジューラ時刻である。キュー空の後の 500 µs 超の遅い再開には意図的な無音期間も含まれ得る。計測処理による負荷があり、NDSIF v1 の標準機能ではない。

profile 2は応答付き要求 `20` / payload `N6D1` で調べられる。NDSIF v1 の標準機能ではない。応答 status の後に `version:u8=1, queued:u16, peak:u16, accepted:u32, dequeued:u32, dropped_frames:u32, dropped_records:u32, late_after_empty:u32, worst_late_us:u32` が続く。満杯破棄と空の後に予定時刻を 500 µs 以上過ぎて再開した回数を数える。意図的な無音期間・一時停止などでも後者は増え得るため、音声障害の確定件数ではない。RESET で初期化し、音源の動作・fault の扱いは変えない。diagnostics のカウント処理は測定対象の負荷を多少増やす。

## 実行構成と表示

NDSIFモードの起動時に、時間付きDAC用の32 KiBのDACリングとイベントリング、および各ライブ方式の512件のFIFOを確保する。USB RX 8192 byte・TX 512 byteを使用する。core 0でUSB受信・フレーム処理、core 1の出力タスクでDAC・FM/PSG・RESET・クロック変更を実行する。DACの期限と描画を分離する。即時FM/PSGのみの再生ではDAC STARTは不要。

NDSIF画面のフッタはYM2612と1個目のSN76489のクロックを表示する。Visual画面は音源の鍵盤・レベルを表示し、RESET後に鍵盤情報を更新する。

## ホストとの運用

現行Delphiホストの先行量は80ms。送信開始時のPING同期窓は最大2048 byte、再生中の未確認データ枠は4096 byteで、STATUS応答により制御する。これらはホストの運用値であり、NDSIFの固定要件ではない。時刻0の初期化整理やVGMのbank・seek・stream展開はホストで行い、受信機は受理したDATAをそのまま実行する。現行Delphiホストは最初の正wait前に限り、DACは最後の値だけ、YM2612 `$28` は各チャンネルの最終キー状態を最終書き込み順で送る。正の時刻に進んだ後のデータは間引かない。

再生中のWindows `GetCommState` によるUSB CDC制御要求と受信破損が同時に観測されたため、現行ホストは再生中の呼び出しを避ける。通信状態が不明になった場合は古いDATAを再送せずRESETから再開する。

共通コアの由来と差分の確認方法は `lib/ndsif/UPSTREAM.md` を参照する。USB CDCの送信停止を避けるため、64 byteの整数倍となる応答には空のCOBS区切りを1 byte追加する。フレーム本体・CRC・payloadは変わらず、受信側は空の区切りを無視する。
