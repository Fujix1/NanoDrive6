# NanoDrive 6 / 6.1 NDSIF 実装

共通フレーム・制御とYM2612時間付きDACのwire仕様は `NDSIF_API.md`、全体方針は `NDSIF.md`、ホストの手順は `NDSIF_HOST_GUIDE.md` を参照する。本書はND6の対応範囲とハードウェア固有の制約を記載する。API 1.0 / revision 40の共通通信コアと、任意のYM2612時間付きDAC profile 1を使用する。

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

## 実行構成と表示

NDSIFモードではDACリング32 KiBとイベントリングを確保し、USB RX 8192 byte・TX 512 byteを使用する。core 0でUSB受信・フレーム処理、core 1の出力タスクでDAC・FM/PSG・RESET・クロック変更を実行する。DACの期限と描画を分離する。即時FM/PSGのみの再生ではDAC STARTは不要。

NDSIF画面のフッタはYM2612と1個目のSN76489のクロックを表示する。Visual画面は音源の鍵盤・レベルを表示し、RESET後に鍵盤情報を更新する。

## ホストとの運用

現行Delphiホストの先行量は80ms。送信開始時のPING同期窓は最大2048 byte、再生中の未確認データ枠は4096 byteで、STATUS応答により制御する。これらはホストの運用値であり、NDSIFの固定要件ではない。時刻0の初期化整理やVGMのbank・seek・stream展開はホストで行い、受信機は受理したDATAをそのまま実行する。現行Delphiホストは最初の正wait前に限り、DACは最後の値だけ、YM2612 `$28` は各チャンネルの最終キー状態を最終書き込み順で送る。正の時刻に進んだ後のデータは間引かない。

再生中のWindows `GetCommState` によるUSB CDC制御要求と受信破損が同時に観測されたため、現行ホストは再生中の呼び出しを避ける。通信状態が不明になった場合は古いDATAを再送せずRESETから再開する。

共通コアは `lib/ndsif/UPSTREAM.md` の方法でND7・ND8と同期する。USB CDCの送信停止を避けるため、64 byteの整数倍となる応答には空のCOBS区切りを1 byte追加する。フレーム本体・CRC・payloadは変わらず、受信側は空の区切りを無視する。
