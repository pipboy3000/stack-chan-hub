# セットアップ

[README](../README.md)から初めて試す人向けの詳細手順です。
確認済みの機体はStackChan K151 / CoreS3、ESP32-S3、16MB Flashです。

## 1. 開発環境を準備する

ESP-IDFは **5.5.1** を使います。OS依存パッケージは
[Espressif公式のmacOS/Linux手順](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/get-started/linux-macos-setup.html)に従って準備してください。
以下は新規インストールする例です。同じ版があれば環境の有効化だけ行います。

```sh
mkdir -p ~/esp
cd ~/esp
git clone --branch v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

別のターミナルでは再度 `. ~/esp/esp-idf/export.sh` を実行します。
ESP-IDFとプロジェクトの配置先には空白を含めないでください。

```sh
cd ~/esp
git clone https://github.com/pipboy3000/stack-chan-hub.git
cd stack-chan-hub
idf.py build
```

`sdkconfig.defaults` がESP32-S3と16MB Flashを指定し、Component Managerが依存を取得します。
Arduino Coreのインストールは不要です。

## 2. 初回はUSBで書き込む

既存のbootloaderとアプリを置き換えます。最初に16MBのFlashをバックアップします。
以下はリポジトリのルートで、ESP-IDFを有効化したターミナルから実行してください。

1. Voyagerを外し、本体USB-CをUSBデータケーブルでPCにつなぐ。
2. 本体のRESETを約3秒長押しし、緑のインジケータが点灯したら離してDownload Modeへ入る。
   ボタンの位置・操作は[CoreS3公式手順](https://docs.m5stack.com/en/core/CoreS3#download-mode)を参照。
3. ポートを確認する。macOSなら `/dev/cu.usbmodem…`、Linuxなら `/dev/ttyACM…` など。
   次の例のポートは自分の機体のものへ変更する。

```sh
HUB_PORT=/dev/cu.usbmodemXXXXXXXX
HUB_BACKUP="$HOME/stackchan-backup.bin"
python -m esptool --chip esp32s3 --port "$HUB_PORT" --baud 460800 \
  --after no_reset read_flash 0x0 0x1000000 "$HUB_BACKUP"
chmod 600 "$HUB_BACKUP"
```

この例ではバックアップをホームディレクトリへ保存します。同名のファイルがある場合は保存先を変えてください。
バックアップにはWi-Fi設定やBLE鍵などが含まれる場合があります。公開せずリポジトリ外へ保管してください。
別の機体のバックアップを復元すると、サーボ校正や個体固有の設定も上書きします。

初回は次の4領域を書き込みます。`erase_flash` や `idf.py flash` は使わず、この手順で書き込みます。

```sh
python -m esptool --chip esp32s3 --port "$HUB_PORT" --baud 460800 \
  --before default_reset --after no_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x10000 build/stackchan_usb_hub.bin
```

[partitions.csv](../partitions.csv) はNVS（0x9000）とPHY（0xf000）を保持します。
`ota_data_initial.bin` は起動先を `ota_0` に初期化するため初回に必要です。通常の更新はOTAにしてください。
`--after no_reset` で書き込み後の自動起動を避けています。Wi-Fiを設定する場合は下記手順へ、
そのまま使う場合はPCを外してからVoyagerと台座電源を接続し、本体を再起動します。

## 3. Wi-Fiを設定する

OTAは2.4GHz Wi-Fi経由です。設定にはChrome/EdgeのWeb Serialを使います。

1. 本体USB-CをPCにつないだ状態で再起動する。
2. **3秒のカウントダウン中にLCDをタップし、「開始しない（USB書き込み用）」を選ぶ。**
   USBホストを開始するとUSB Serial-JTAGが使えなくなります。
3. Chrome/Edgeで [tools/wifi_setup.html](../tools/wifi_setup.html) をローカルファイルとして開く。
   または次のコマンドで配信し、`http://localhost:8000/wifi_setup.html` を開く。

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory tools
```

4. 「シリアルポートを選ぶ」で本体を選び、SSIDとパスワードを入力する。
5. 表示されたOTAトークンをコピーし、リポジトリ直下の `.ota-token` に1行で保存する。
   `.ota-token` はGit管理外。秘密情報なので公開しない。
6. 「送信して再起動」を押す。成功応答を確認したらPCから外し、Voyagerへつなぎ替える。
   PCにつないだまま再起動した場合は、再びカウントダウン中に「開始しない」を選ぶ。

配信サーバーは設定後にCtrl+Cで終了できます。

## 4. Wi-Fiで更新する

本体が通常起動し、同じLANに接続されている状態で実行します。

```sh
idf.py build
sh tools/ota.sh
curl --fail --max-time 10 http://stackchan-hub.local/status
```

`ok, restarting` が返ると自動で再起動します。再起動後に `/status` のビルド日時、
区画 `ota_0` / `ota_1`、`secured 1`、`usb_devices 1` を確認してください。
ビルド日時が変わらない場合もあるため、更新応答と区画切替も確認します。

mDNSで名前が引けないときは、ルーター等で確認した本体IPを `sh tools/ota.sh 本体IP` に渡します。
プロキシ環境では `NO_PROXY=stackchan-hub.local no_proxy=stackchan-hub.local sh tools/ota.sh`、
状態取得は `curl --noproxy '*' http://stackchan-hub.local/status` を使えます。
OTAはトークン付きの平文HTTPです。信頼できるLAN内で使ってください。

## 接続できないとき

| 症状 | まず確認すること |
|---|---|
| USBシリアルが見えない | データケーブルか、Download Modeへ入っているか、USBホストを開始していないか |
| キーボードが認識されない | 本体側へVoyager、台座側へ給電専用ケーブルという配線か。台座のデータ接続を外す |
| Bluetooth一覧に現れない | 選択先のペアリング受付を開いているか。未登録Slotは受付中だけ広告する |
| PC交換後に接続できない | 対象Slotの登録解除を行い、PC側の古い登録も削除して再ペアリングする |
| OTAが403になる | `.ota-token` が本体に保存したものと一致するか。プロキシが応答していないか |
| OTA後に見つからない | 数秒待って `/status` を再取得し、Wi-Fi設定・本体IPを確認する |

USBホスト使用中は本体USB-Cでシリアルログを取れません。
ネットワーク診断は[開発ガイド](development.md#ログと画面取得)を参照してください。

## 元のファームウェアに戻す

Download Modeで、同じ機体から保存した16MBバックアップを0x0へ書き戻します。
これは現在のWi-Fi設定・登録・校正を含むFlash全体の復元になります。

```sh
python -m esptool --chip esp32s3 --port "$HUB_PORT" --baud 460800 \
  --after no_reset write_flash 0x0 "$HUB_BACKUP"
```

バックアップがなければ、StackChanの公式復元方法・M5Burnerを使用してください。
