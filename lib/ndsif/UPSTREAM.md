# 取り込み元と ND6 差分

ND7 の `lib/ndsif/ndsif.h` / `ndsif.cpp`（API 1.0 revision 39）を取り込んだ。
ND7 の実装は ND8 の COBS、CRC、message_type、制御応答を基にし、ボード所有の identity / Capability / register callback を追加している。

ND6 のローカル差分:

- revision 40、実験制御 callback と Capability control_features bit1 を追加。
- SN76489 の 50/30 は instance + page + byte 列として register callback へ渡す。
- DAC は新規 `dac_stream.h` / `dac_stream.cpp` へ分離。既存の B7/10～13 は変更せず ND6 では無効。
- ボード callback、USB、単独所有の音源 I/O タスクは `src/ndsif_serial.cpp`。
- 正式な共通規格との違い、wire、制約、試験手順はルートの `NDSIF_ND6.md` に記録。

ND7/ND8 の元ファイルへの書き戻しは行わない。
