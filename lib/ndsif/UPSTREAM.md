# NDSIF 共通コア

共通通信コアの取り込み元は `NanoDrive6/lib/ndsif`。ND6とND8の `ndsif.h` と `ndsif.cpp` は一致する。ND7は同じrevision 40を使用し、ND6で追加した任意のcontrol_features bit2・bit3を設定する拡張より前の版を使用する。ND6プロジェクトの `python tools/sync_ndsif_core.py` はソースの一致を調べるため、現状ではND7の差分を報告する。`--write` は機種側のコアをバックアップ付きで更新する。コアを更新した場合は各機種のビルドと該当機能の試験を行う。

フレーム、COBS、CRC、制御応答、能力通知と音源書き込みの振り分けを共通化する。機種名、搭載音源、クロック、リセット、音量、USB処理、再生タスクは各機種が設定する。`REVISION=40` は共通コアの規格改訂番号で、音源や任意プロファイルの対応を意味しない。能力通知と個別プロファイルの照会を使う。

ND6のYM2612時間付きDACとライブ方式はボード側の拡張callbackで提供する。ND7は即時BURST、ND8は即時BURSTとaudio callbackによるMSM6258 ADPCMを提供する。

ND7の回帰試験は `NanoDrive7/tests/run_native.py`、共通コアのND6・ND8互換試験は `NanoDrive6/tools/ndsif_core_test/test_core.py` で実行する。実機のPCM・ADPCM・複数音源再生も確認する。

共通文書 `NDSIF.md`、`NDSIF_API.md`、`NDSIF_HOST_GUIDE.md` は3機種で同じ内容を保持する。各機種の対応範囲は `NDSIF_ND6.md`、`NDSIF_ND7.md`、`NDSIF_ND8.md` を参照する。
