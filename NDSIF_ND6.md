# NanoDrive 6 / 6.1 NDSIF 実装

共通フレーム・制御とYM2612時間付きDACのwire仕様は `NDSIF_API.md`、全体方針は `NDSIF.md`、ホストの手順は `NDSIF_HOST_GUIDE.md` を参照する。本書はND6の対応範囲とハードウェア固有の制約を記載する。API 1.0 / revision 40の共通通信コアと、任意のYM2612時間付きDAC profile 1を使用する。

firmware `3.1dev17` のソースには、NDSIF v1策定中のライブDAC profile 2を追加する。profile 2の能力照会とwire形式は `NDSIF_API.md` 第10節の草案に従う。profile 1とは別機能であり、この追記は実機での音質・遅延・長時間再生の検証完了を意味しない。

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

## ライブDAC（profile 2 / 草案）

firmware `3.1dev20` は、FM と DAC を同じ FIFO と時間軸で扱う NDSIF v1 追加草案 profile 3 に対応する。能力は control_features bit3 および `21`、順序付きフレームは応答付き `22`。512 件の FIFO、1 フレーム最大 35 件。空き不足の応答はフレーム全体が未受理であることを示し、同じ sequence で再試行する。従来の profile 2 と同時には使わず RESET で切り替える。開発中の試作であり、音質・実時間性能は未検証。

firmware `3.1dev22` の profile 3 は revision 2 の試作へ変更し、`22` を無応答バーストとする。`23` の状態照会は次の受理 sequence・累積受理数・キュー数・空き数を返す。ホストは既知の空き枠を超えない範囲で複数バーストを連続送信し、必要なときだけ `23` を照会する。revision 1 のフレームごとの応答方式は使用しない。音質・実時間性能は未検証。

firmware `3.1dev23` の profile 3 revision 3 は、再生中に状態照会を必要としない。FIFO 満杯時は受信中フレームを保持し、DAC/FM 出力タスクで空きができたら全体を受理する。ホスト側へのバックプレッシャーは起こり得る。USB reset・切断時は待機を中断する。`23` と `20` / `N6D3` は診断用として残すが通常再生中には要求しない。実機性能・音質は未検証。

firmware `3.1dev24` の profile 3 revision 4 は、短い DAC wait と値を 2 byte とする可変長レコードを採用する。FM と長い wait も同じ無応答バースト・同じ FIFO 内に保持する。各フレームは最大 100 レコードかつ payload 256 byte 以下。欠落した sequence は `23` の `missing_frames` へ記録し、より新しいフレームから再開する。古いフレームの再送は行わない。形式・検証方法は `NDSIF_API.md` 第10.4節。実機性能・音質は未検証。

firmware `3.1dev21` は原因調査用に ND6 固有の暫定診断 `20` / `N6D3` を追加する。status 後の形式は `version:u8=3, queued:u16, peak:u16, accepted:u32, dequeued:u32, full_responses:u32, late_over_100us:u32, late_after_empty:u32, worst_late_us:u32, first_us:u64, last_us:u64, source_ticks:u64`。時刻は物理 I/O の読戻しではなくイベント取り出し時のスケジューラ時刻である。キュー空の後の 500 µs 超の遅い再開には意図的な無音期間も含まれ得る。計測処理による負荷があり、NDSIF v1 の標準機能ではない。

firmware `3.1dev19` では切り分け用に、ND6 固有の一時診断 `20` を応答付き要求、payload `N6D1` で提供する。NDSIF v1 の標準機能ではない。応答 status の後に `version:u8=1, queued:u16, peak:u16, accepted:u32, dequeued:u32, dropped_frames:u32, dropped_records:u32, late_after_empty:u32, worst_late_us:u32` が続く。満杯破棄と空の後に予定時刻を 500 µs 以上過ぎて再開した回数を数える。意図的な無音期間・一時停止などでも後者は増え得るため、音声障害の確定件数ではない。RESET で初期化し、音源の動作・fault の扱いは変えない。diagnostics のカウント処理は測定対象の負荷を多少増やす。

GET_CAPABILITIESのcontrol_features bit2と `1E` で対応を通知する。対象は `52` / instance 0、時間単位は44,100 tick/秒、キューは512レコード、`1F` 1フレームの最大50レコード。各レコードは前のDAC値からのwait:u32と値:u8で、最初に受理した値は前の値がないため直ちに適用する。固定の先行供給やSTARTは必要としない。

キューが空なら最後の値を保持して入力を待つ。満杯なら新規フレームを全体として破棄し、空・満杯のみではfaultや音源リセットにしない。送信側にACKや自動再送はない。時刻の遅れ、処理能力・通信量を超える供給、キュー満杯による欠落や音質への影響はホストと運用条件に依存する。

FMと `$2B` は即時 `52` / `53` を使用できるが、ライブDACとの厳密な前後関係は保証しない。ライブ中の即時 `$2A` はフレーム全体を実行しない。profile 1とは同一物理YM2612で同時に使えず、切り替えにはRESETが必要。RESETとUSBバスリセットはライブキューと過去時刻を破棄する。

## 実行構成と表示

NDSIFモードではDACリング32 KiBとイベントリングを確保し、USB RX 8192 byte・TX 512 byteを使用する。core 0でUSB受信・フレーム処理、core 1の出力タスクでDAC・FM/PSG・RESET・クロック変更を実行する。DACの期限と描画を分離する。即時FM/PSGのみの再生ではDAC STARTは不要。

NDSIF画面のフッタはYM2612と1個目のSN76489のクロックを表示する。Visual画面は音源の鍵盤・レベルを表示し、RESET後に鍵盤情報を更新する。

## ホストとの運用

現行Delphiホストの先行量は80ms。送信開始時のPING同期窓は最大2048 byte、再生中の未確認データ枠は4096 byteで、STATUS応答により制御する。これらはホストの運用値であり、NDSIFの固定要件ではない。時刻0の初期化整理やVGMのbank・seek・stream展開はホストで行い、受信機は受理したDATAをそのまま実行する。現行Delphiホストは最初の正wait前に限り、DACは最後の値だけ、YM2612 `$28` は各チャンネルの最終キー状態を最終書き込み順で送る。正の時刻に進んだ後のデータは間引かない。

再生中のWindows `GetCommState` によるUSB CDC制御要求と受信破損が同時に観測されたため、現行ホストは再生中の呼び出しを避ける。通信状態が不明になった場合は古いDATAを再送せずRESETから再開する。

共通コアは `lib/ndsif/UPSTREAM.md` の方法でND7・ND8と同期する。USB CDCの送信停止を避けるため、64 byteの整数倍となる応答には空のCOBS区切りを1 byte追加する。フレーム本体・CRC・payloadは変わらず、受信側は空の区切りを無視する。
