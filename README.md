# Voyager BLE Hub

ZSA VoyagerとNavigatorをStack-chan（CoreS3）にUSB接続し、BLE HID経由でPCを操作するファームウェアです。
Mac miniとLinux/Omarchyで入力・接続先の往復切替を確認しています。

```text
Voyager + Navigator → USB → Stack-chan → BLE → 選択したPC
```

## 必要なもの

- M5Stack公式のStackChan（CoreS3 / ESP32-S3、16MB Flash）と台座。
- ZSA Voyager。タッチパッドも使う場合はNavigator。
- Voyagerと本体をつなぐUSBケーブル、初回書き込み用のUSBデータケーブル。
- 台座へ給電するUSB電源と、データ通信をしない給電専用ケーブル。
- ビルド・書き込み用PCとESP-IDF **5.5.1**。OTAには2.4GHz Wi-FiとChrome/Edgeを使用します。

本体USB-Cは使用時にVoyager専用です。台座をPCへデータ接続するとUSBホストが競合するため、
台座側は給電専用にしてください。初回書き込みは既存ファームウェアを置き換えます。

## 試す手順

### 1. ビルドする

[ESP-IDF 5.5.1を準備](docs/setup.md#1-開発環境を準備する)したターミナルで実行します。

```sh
git clone https://github.com/pipboy3000/stack-chan-hub.git
cd stack-chan-hub
idf.py build
```

成果物は `build/stackchan_usb_hub.bin` です。依存コンポーネントはビルド時に取得します。

### 2. 初回はUSBで書き込む

Voyagerを外し、本体USB-Cを開発PCへ接続します。
[バックアップと初回書き込み](docs/setup.md#2-初回はusbで書き込む)に従い、
bootloader・partition table・OTA起動情報・アプリを書き込みます。

### 3. Voyagerを接続して起動する

1. 本体USB-Cから開発PCを外し、Voyagerへつなぎ替える。
2. 台座へ給電専用ケーブルで電源を接続する。
3. 起動時に3秒待つと給電を自動判定してUSBホストを開始する。
   手動で選ぶ場合は、カウントダウン中にタップして「外部給電（台座のUSB）」を選ぶ。
4. キーボードの認識状態を画面で確認する。

本体USB-Cに開発PCをつないだままUSBホストを開始しないでください。

### 4. PCとペアリングする

1. LCDの「設定」→「接続先の管理」で登録する接続先を選ぶ。
2. ペアリング受付を開始する（既定60秒）。
3. PCのBluetooth設定で **StackChan Hub** を登録する。
4. 接続後、文字入力を確認する。Navigatorがあればポインタ移動・スクロールも試す。
5. 別のPCも別の接続先へ登録する。

通常の再接続にはペアリング受付を開く必要はありません。PC交換は対象の登録を解除してから再登録します。

### 5. 接続先を切り替える

| 操作 | 接続先 |
|---|---|
| Voyagerに割り当てたF21 | Mac mini（Slot 1） |
| Voyagerに割り当てたF22 | Linux（Slot 2） |
| Voyagerに割り当てたF23 | MacBook Air（Slot 3） |
| LCDの接続先ボタン | 選んだPC |

F21〜F23はホストへ送らず切替操作として使います。切替後は押しているキーをすべて離してから入力します。
3台目は設定から登録できますが、現時点の実機確認はMac mini／Linuxの2台です。

## 更新と基本操作

- 次回からの更新：[Wi-Fi設定とOTA](docs/setup.md#3-wi-fiを設定する)。ビルド後に `sh tools/ota.sh` で更新します。
- ドラッグ・クリック・LED表示・消灯・休止：[操作ガイド](docs/usage.md)。
- 接続できないとき：[セットアップの切り分け](docs/setup.md#接続できないとき)。

Oryx/Keymappで設定を変更するときはVoyagerをPCへ直接USB接続してください。
通常のキーボード入力は6KROです。PCの起動前認証やBIOS等での利用は保証していません。

## 開発・検証の資料

- [開発構成・テスト・診断](docs/development.md)
- [開発・実機検証の時系列記録](docs/development-log.md)
- [VoyagerのUSB記述子](docs/voyager-descriptors.md)

## ライセンス

[MIT License](LICENSE)。同梱している次のコンポーネントは、それぞれのディレクトリにある
ライセンス（いずれもMIT）に従います。

- `components/stackchan_bsp/`：M5Stackの[StackChan-BSP](https://github.com/m5stack/StackChan-BSP)。
  motion taskの優先度を変更しています。内部の `src/utils/uitk/` と `src/drivers/FTServo_Arduino/` は
  それぞれのLICENSEを含みます。
- `components/m5stack_avatar/`：Shinya Ishikawa氏の[m5stack-avatar](https://github.com/stack-chan/m5stack-avatar)（現在はビルド対象外）。
