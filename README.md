# NanoDrive 6 / 6.1 Development Repository

This is a repository for NanoDrive 6 and 6.1 VGM player with YM2612/YM3438 + SN76489 x 2, which plays back VGM, VGZ, XGM 1.1 and XGM 2 format files. It supports MegaDrive/Genesis, Sega System 1 and some SN76489 compatible systems.

Also in version 2, I added support for sound data transfer via USB serial connection and playback with "MAmidiMEmo" and "Real chip VGM/XGM/MGS player" created by [Itoken](https://github.com/110-kenichi/mame).

VGZ files are supported in version 2.2. The max file size of the original VGM (ungzipped) is 7MB. You have to expand files larger than 7MB in advance.

Version 3 is hybrid firmware compatible with both NanoDrive 6 and 6.1. Most of the software features introduced for 6.1 are also available on NanoDrive 6.
<br><br><br>

Nano Drive 6 / 6.1 は、YM2612/YM3438 + SN76489 x 2 を搭載した VGM プレーヤーです。サポートするフォーマットは VGM、XGM 1.1、XGM 2 で、SD カードから再生します。また、シリアルモードに切り替えることで、[Itoken](https://github.com/110-kenichi/mame) さん制作の MAmidiMEmo と Real chip VGM/XGM/MGS player を使用して、Windows からデータ送信を行うこともできます。

バージョン2.2 から gzip 圧縮された vgz ファイルをサポートします。圧縮元の vgm ファイルサイズ 7MB まで動作します。7MB を超えるサイズの VGM についてはあらかじめ解凍して配置してください。

バージョン3 は 6/6.1 両対応のハイブリッドファームウェアです。6.1のソフトウェア側の新機能が6でもほとんど利用できます。
<br><br>
<img width="6016" height="4016" alt="ND61" src="https://github.com/user-attachments/assets/222cf56d-1cc6-4abc-88bf-68b984ca4bbe" style="max-width: 100%;">
<br><br>

## NanoDrive 6.1 について / What's new in NanoDrive 6.1

NanoDrive 6.1 は 6 の問題点の解消を図ったものです。6 との違いは以下の通り:

- **電源回路の改善とデジタルノイズの低減**<br>後継の ND7, ND8 世代の電源回路を使い、SDカードアクセス、液晶更新などのデジタル側の電源ゆらぎによるノイズの影響を低減。不安定だったり低電圧な USB 電源での動作安定度向上
- **音源、ミキシング回路の改良**<br>FM音源チップ、PSGチップ、ミキシング部分などのVCC、GNDの配線をがんばりました
- **コンデンサの変更**<br> 音響用DIPコンデンサの生産終了をうけ、主要コンデンサをニチコンUCQチップ電解コンデンサに変更し容量アップ。PSGラインにあったセラコンもUCQに置き換え。
- **ファイルシステムの改善**<br> SDカードの入れ子ファイル構造に対応。ファイルブラウザ実装。
- **ケースサイズの拡大**<br> 100mm x 100mm から 120mm x 120mm に拡大。自作ケース作成の自由度向上。

NanoDrive 6.1 was designed to address issues found in NanoDrive 6. The main differences are:

- **Improved power circuitry and reduced digital noise**<br>It uses power circuitry derived from the later ND7 and ND8 generations, reducing the effect of power fluctuations caused by SD card access, display updates, and other digital activity. It also improves stability when using an unstable or low-voltage USB power supply.
- **Improved sound and mixing circuitry**<br>The VCC and GND routing for the FM sound chip, PSG chip, and mixing section has been improved.
- **Updated capacitors**<br>Following the discontinuation of the audio-grade DIP capacitors, the main capacitors were replaced with higher-capacity Nichicon UCQ chip electrolytic capacitors. The ceramic capacitor in the PSG signal path was also replaced with a UCQ capacitor.
- **Improved file system support**<br>Nested folder structures on the SD card are now supported, along with a file browser.
- **Larger enclosure**<br>The enclosure size has increased from 100 mm x 100 mm to 120 mm x 120 mm, providing greater flexibility for custom enclosure designs.


## ファームウェアのアップデート方法 / How to update the firmware.

[https://nanodrive.netlify.app/](https://nanodrive.netlify.app/) よりブラウザ経由で更新できます。USB接続してボタンをクリックし、デバイスを選択すると自動で書き換えが行われます。h1romas4 さん制作です。

自分でコンパイルする場合は以下の手順です：

1. [Visual Studio Code](https://code.visualstudio.com/) をインストールし、拡張機能 [Platform I/O](https://platformio.org/)を導入します。これでコンパイル環境が完成します。
2. このGitをクローンするかダウンロードして、VSCode で開きます。初回、必要なファイル類は自動でダウンロードされるので数分間待ちます。
3. NanoDrive6 本体を USB で接続します。VSCode の左側の一番下の欄に「→」ボタンがあるのでクリックするとコンパイルと書き換え始まります。または CTRL + ALT + U でもOKです。

You can update the firmware from a web browser at [https://nanodrive.netlify.app/](https://nanodrive.netlify.app/). Connect NanoDrive via USB, click the button, and select the device to begin the update automatically. This updater was created by h1romas4.

To compile and upload the firmware yourself:

1. Install [Visual Studio Code](https://code.visualstudio.com/) and add the [PlatformIO](https://platformio.org/) extension to set up the build environment.
2. Clone or download this repository and open it in Visual Studio Code. On the first launch, wait a few minutes while the required files are downloaded automatically.
3. Connect NanoDrive 6 via USB. Click the “→” button at the bottom of the left sidebar in Visual Studio Code to compile and upload the firmware. You can also press `Ctrl+Alt+U`.
   <br>
   <br>
   <br>

## Manual PDF / マニュアル PDF

You can get the NanoDrive 6 manual at [nd6_manual_r3.pdf](https://github.com/user-attachments/files/18299302/nd6_manual_r3.pdf)
<br>
<br><br>

## PCB (ND6)

![pcb](https://github.com/user-attachments/assets/ec0ef72e-edaa-413a-92b3-2d8dc88f904d)

![ND6 Schematics](https://github.com/user-attachments/assets/1caab077-61fb-4a6f-99a3-fba038a5c54c)

## Schematics (ND6.1)
<img width="4760" height="3314" alt="schematic61" src="https://github.com/user-attachments/assets/bdda9ad4-52c8-48bc-8df0-fc791ed0fa86" />

<br>
<br>

## File structure / ファイル構成

microSD カードに保存した、拡張子が「.vgm」「.vgz」のものをvgmファイルとして、「.xgm」のものをxgmファイルとして認識します。大文字小文字は区別しません。スクリーンショットは最大サイズ640x320 までのPNG ファイルです。同じフォルダ内にある、任意のPNG ファイルが使用されます。
GZip圧縮された VGZ ファイルの展開時サイズが 7MBを超えるとエラーになるので、あらかじめ解凍して .vgm 拡張子に変更してください。XGM サイズの最大サイズは 7MB までです。
<br>
<br>
Files on the microSD card with the extensions `.vgm` or `.vgz` are recognized as VGM files, while files with the `.xgm` extension are recognized as XGM files. Extension matching is case-insensitive. Screenshots must be PNG files with a maximum size of 640 x 320 pixels. Any PNG file in the same folder may be used.

An error will occur if a gzip-compressed VGZ file expands to more than 7 MB. In that case, decompress it beforehand and save it with the `.vgm` extension. The maximum XGM file size is also 7 MB.
<br>
<br>
<br>

## How to view screenshots by song / 曲別のスクリーンショット表示方法

曲のあるフォルダ内に「snap」サブフォルダを作成します。「曲名.png」が最優先で表示されます。次に「曲番号.png」が検索されます。1 曲目はファイル名「1.png」、2 曲目はファイル名「2.png」です。何もない場合は、親フォルダ内の任意のpngファイルを探します。

Create a "snap" sub-folder in the folder where the song is located. The highest priority file displayed is “`songname.png`”. The next highest priority is “`songnumber.png`”, such like "5.png" for the fifth song in the folder.
If neither file is found, any PNG file in the parent folder will be used.
<br>
<br>
<br>

## Serial Mode / シリアルモード (v2.0 and later / v2.0 以降)

メニューからシリアルモードを選択し再起動すると、USB 経由のシリアル受信モードになります。MAmidiMEmo と Real chip VGM/XGM/MGS player がサポートしています。MIDI 制御や Windows からのデータ送信に対応します。ただし、性能的に PCM はノイズが乗ります。
Dir+ と Dir- キーで YM2612 と SN76489 の周波数を4種類切り替えできます。

Select the "Serial mode" via the option menu and reboot to enter the USB serial mode. "MAmidiMEmo" and "Real chip VGM/XGM/MGS player" support this function. You can control ND6 by the MIDI interface or send music data from Windows. Note that the PCM sound is noisy because of technical limitation. To change the frequency of YM2612 or SN76489, press the Dir+ or Dir- key.
<br><br>
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


## Credits and licenses

- Open Font Render by takkaO: FTL license
  https://github.com/takkaO/OpenFontRender

- LovyganGFX by lovyan: FreeBSD license
  https://github.com/lovyan03/LovyanGFX

- PNGdec by Larry Bank: Apache 2.0 license
  https://github.com/bitbank2/PNGdec

- BIZ UDPGothic: SIL Open Font License 1.1
  https://fonts.google.com/specimen/BIZ+UDPGothic/license

- Portions of this software are copyright © The FreeTypeProject (www.freetype.org). All rights reserved.
<br><br>
<figure><img src="https://github.com/user-attachments/assets/a15e7b2c-7026-4bf4-94d2-e90b153d7c28" width="800"></figure>
