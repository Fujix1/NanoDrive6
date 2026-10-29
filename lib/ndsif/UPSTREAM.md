# NDSIF 共通コア

ND6・ND7・ND8の `lib/ndsif/ndsif.h` と `ndsif.cpp` は同じ通信コアを使用する。正本は `NanoDrive6/lib/ndsif` に置く。ND6プロジェクトで `python tools/sync_ndsif_core.py` を実行すると3機種の一致を確認し、`--write` を付けるとND7・ND8へバックアップ付きで反映する。同期後は各機種のビルドと該当機能の試験を行う。

フレーム、COBS、CRC、制御応答、能力通知と音源書き込みの振り分けを共通化する。機種名、搭載音源、クロック、リセット、音量、USB処理、再生タスクは各機種が設定する。`REVISION=40` は共通コアの規格改訂番号で、音源や任意プロファイルの対応を意味しない。能力通知と個別プロファイルの照会を使う。

ND6のYM2612時間付きDACは `dac_stream` とボード側の拡張callbackで提供し、ND7・ND8では有効化しない。ND8のMSM6258 ADPCMは従来どおり `audio` callbackで提供する。共通コア内に搭載機種を決め打ちしない。

ND7の回帰試験は `NanoDrive7/tests/run_native.py`、共通コアのND6・ND8互換試験は `NanoDrive6/tools/ndsif_core_test/test_core.py` で実行する。実機のPCM・ADPCM・複数音源再生も確認する。
