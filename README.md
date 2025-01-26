# NanoDrive 6 Development Repository

This is a repository for NanoDrive 6 VGM player with YM2612/YM3438 + SN76489 x 2, which plays back VGM, XGM 1.1 and XGM 2 format files. It supports MegaDrive/Genesis, Sega System 1 and some SN76489 compatible systems. The XGM 2 support is experimental.

![nanodora6](https://github.com/user-attachments/assets/a15e7b2c-7026-4bf4-94d2-e90b153d7c28)

# Manual PDF

You can get the manual at [nd6_manual_r3.pdf](https://github.com/user-attachments/files/18299302/nd6_manual_r3.pdf)

# PCB

![pcb](https://github.com/user-attachments/assets/ec0ef72e-edaa-413a-92b3-2d8dc88f904d)

![Schematics](https://github.com/user-attachments/assets/1caab077-61fb-4a6f-99a3-fba038a5c54c)


# File structure / ファイル構成
microSD カードのルート直下にあるフォルダが再生対象となります。それ以外の場所に配置したファイルは無視されます。「__MACOSX」フォルダは消してください。
拡張子が「.vgm」のものをvgmファイルとして、「.xgm」のものをxgmファイルとして認識します。大文字小文字は区別しません。スクリーンショットは最大サイズ640x320 までのPNG ファイルです。同じフォルダ内にある、任意のPNG ファイルが使用されます。

Folders located directly under the root of the microSD card are the target for playback. Files in other locations will be ignored. Files with the extension ".vgm" (case insensitive) are recognized as vgm files, and those with the extension ".xgm" (case insensitive) as xgm files. Make sure to remove "__MAXOSX" folders.
Screenshots are PNG files with a maximum size of 640x320. Any PNG file found in the same folder will be used by default.

## How to view screenshots by song / 曲別のスクリーンショット表示方法
曲のあるフォルダ内に「snap」サブフォルダを作成します。「曲名.png」が最優先で表示されます。次に「曲番号.png」が検索されます。1 曲目はファイル名「1.png」、2 曲目はファイル名「2.png」です。何もない場合は、親フォルダ内の任意のpngファイルを探します。

Create a "snap" sub-folder in the folder where the song is located. The highest priority file displayed is “``songname.png``”. The next highest priority is “``songnumber.png``”, such like "5.png" for the fifth song in the folder.


# Thanks to

- Hiromasha for XGM parsing technichs at
https://github.com/h1romas4/libymfm.wasm
https://chipstream.netlify.app/

- Kumatan for the strongest and most consolidated MD music development assets at 
https://github.com/kuma4649/mml2vgm

# Firmware Update 手順

ファームウェアのアップデート手順を[ほくとさん](https://x.com/NightBird_hoku)にまとめていただきました。ありがとうございます。

[NanoDrive6のFirmwareVersionUP手順.pdf](https://github.com/user-attachments/files/17610335/NanoDrive6.FirmwareVersionUP.pdf)

# Credits and licenses

- Open Font Render by takkaO: FTL license
https://github.com/takkaO/OpenFontRender

- LovyganGFX by lovyan: FreeBSD license
https://github.com/lovyan03/LovyanGFX

- PNGdec by Larry Bank: Apache 2.0 license
https://github.com/bitbank2/PNGdec


Portions of this software are copyright © The FreeTypeProject (www.freetype.org). All rights reserved.
