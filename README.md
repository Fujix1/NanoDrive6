# NanoDrive 6 Development Repository

This is a repository for NanoDrive 6 VGM player with YM2612/YM3438 + SN76489 x 2, which plays back VGM, XGM 1.1 and XGM 2 format files. It supports MegaDrive/Genesis, Sega System 1 and some SN76489 compatible systems.

Also in version 2, I added a support for sound data transfer via USB serial connection and playback with "MAmidiMEmo" and "Real chip VGM/XGM/MGS player" created by [Itoken](https://github.com/110-kenichi/mame).

Nano Drive 6 は、YM2612/YM3438 + SN76489 x 2 を搭載した VGM プレーヤーです。サポートするフォーマットは VGM、XGM 1.1、XGM 2 で、SD カードから再生します。また、シリアルモードに切り替えることで、[Itoken](https://github.com/110-kenichi/mame) さん制作の MAmidiMEmo と Real chip VGM/XGM/MGS player を使用して、Windows からデータ送信を行うこともできます。
<br><br>

<figure><img src="https://github.com/user-attachments/assets/a15e7b2c-7026-4bf4-94d2-e90b153d7c28" width="800"></figure>
<br>
<br>

## Manual PDF / マニュアル PDF

You can get the manual at [nd6_manual_r3.pdf](https://github.com/user-attachments/files/18299302/nd6_manual_r3.pdf)
<br>
<br><br>


## PCB

![pcb](https://github.com/user-attachments/assets/ec0ef72e-edaa-413a-92b3-2d8dc88f904d)

![Schematics](https://github.com/user-attachments/assets/1caab077-61fb-4a6f-99a3-fba038a5c54c)

<br>
<br>

## File structure / ファイル構成
microSD カードのルート直下にあるフォルダが再生対象となります。それ以外の場所に配置したファイルは無視されます。「__MACOSX」フォルダは消してください。
拡張子が「.vgm」のものをvgmファイルとして、「.xgm」のものをxgmファイルとして認識します。大文字小文字は区別しません。スクリーンショットは最大サイズ640x320 までのPNG ファイルです。同じフォルダ内にある、任意のPNG ファイルが使用されます。

ZIP圧縮された「.vgz」ファイルはサポートしません。あらかじめ解凍して拡張子「.vgm」を追加してください。
<br>
<br>
Folders located directly under the root of the microSD card are the target for playback. Files in other locations will be ignored. Files with the extension ".vgm" (case insensitive) are recognized as vgm files, and those with the extension ".xgm" (case insensitive) as xgm files. Make sure to remove "__MAXOSX" folders.
Screenshots are PNG files with a maximum size of 640x320. Any PNG file found in the same folder will be used by default.
<br>
<br>
<br>
## How to view screenshots by song / 曲別のスクリーンショット表示方法
曲のあるフォルダ内に「snap」サブフォルダを作成します。「曲名.png」が最優先で表示されます。次に「曲番号.png」が検索されます。1 曲目はファイル名「1.png」、2 曲目はファイル名「2.png」です。何もない場合は、親フォルダ内の任意のpngファイルを探します。

Create a "snap" sub-folder in the folder where the song is located. The highest priority file displayed is “``songname.png``”. The next highest priority is “``songnumber.png``”, such like "5.png" for the fifth song in the folder.
<br>
<br>
<br>
## Serial Mode / シリアルモード (v2.0 and later / v2.0 以降)
メニューからシリアルモードを選択し再起動すると、USB 経由のシリアル受信モードになります。MAmidiMEmo と Real chip VGM/XGM/MGS player がサポートしています。MIDI 制御や Windows からのデータ送信に対応します。ただし、性能的に PCM はノイズが乗ります。
Dir+ と Dir- キーで YM2612 と SN76489 の周波数を4種類切り替えできます。

Select the "Serial mode" via the option menu and reboot to enter the USB serial mode. "MAmidiMEmo" and "Real chip VGM/XGM/MGS player" support this function. You can control ND6 by the MIDI interface or send music data from Windows. Note that the PCM sound is noisy because of technical restriction. To change the frequency of YM2612 or SN76489 by pressing Dir+ or Dir- key respectively.
<br>
<br>
<br>
## Thanks to

- Hiromasha for XGM parsing technichs at
https://github.com/h1romas4/libymfm.wasm
https://chipstream.netlify.app/

- Kumatan for the strongest and most consolidated MD music development assets at 
https://github.com/kuma4649/mml2vgm

- Itoken for supporting Nano Drive 6 by his "MAmidiMEmo" and "Real chip VGM/XGM/MGS player" applications.
https://github.com/110-kenichi/mame
<br>
<br>

## Firmware Update 手順

ファームウェアのアップデート手順を[ほくとさん](https://x.com/NightBird_hoku)にまとめていただきました。ありがとうございます。

[NanoDrive6のFirmwareVersionUP手順.pdf](https://github.com/user-attachments/files/17610335/NanoDrive6.FirmwareVersionUP.pdf)
<br>
<br>
<br>
## Credits and licenses

- Open Font Render by takkaO: FTL license
https://github.com/takkaO/OpenFontRender

- LovyganGFX by lovyan: FreeBSD license
https://github.com/lovyan03/LovyanGFX

- PNGdec by Larry Bank: Apache 2.0 license
https://github.com/bitbank2/PNGdec

- Portions of this software are copyright © The FreeTypeProject (www.freetype.org). All rights reserved.
