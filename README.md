# Voyager BLE Hub

ZSA VoyagerをStack-chan（M5Stack CoreS3）にUSB接続し、BLE HIDとしてMacへ中継する
（計画は[spec v0.4](docs/spec-v0.4.md)）。ESP-IDF 5.5.1、M5Unified 0.2.23（ESP-IDFモード）、
M5公式StackChan BSP 1.1.0、NimBLE（esp_hid）、usb_host_hid 1.2.1を使い、Arduino Coreは
使わない（Arduino経由では内部I²Cが応答しなかったため。下記「段階的切り分け」参照）。
Avatarは未移植でビルド対象外。

## ビルド

```sh
export PATH=$HOME/.local/share/mise/installs/python/3.11/bin:$PATH
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf5.5_py3.11_env
source $HOME/esp/esp-idf/export.sh
idf.py build
```

純粋ロジックのコンポーネントはPC上でテストする:
`components/{input_router,hid_descriptor,touchpad}/test_host/run.sh`。

## 書き込みとログ（Wi-Fi）

通常はWi-Fi経由で書き込む。`tools/ota.sh [host]`が`build/stackchan_usb_hub.bin`を
`http://<host>/ota`へ送る（既定`stackchan-hub.local`、mDNSで引けないときはIPを指定）。
認証はリポジトリ直下の`.ota-token`（git管理外）のトークン。完了後は自動で再起動し、
前回選んだ起動モードでUSB Hostを再開する（タップ不要）。
`tools/log.sh`はUDP 5140番に届くログを表示し、`GET /status`は稼働区画、空きメモリ、
USB/Router/BLEのカウンタを返す。OTAとログは信頼できるLAN内の開発用で、平文HTTP。

Wi-Fi設定は`tools/wifi_setup.html`（Chrome/EdgeのWeb Serial）で書き込む。CoreS3本体の
USB-CをMacにつなぎ、起動画面でタップしない状態でSSID、パスワード、自動生成の
OTAトークンを送る。2.4 GHzのみ。

USBで書き込むのは初回やWi-Fiが使えないときだけ。起動直後はVBUSをOFFに保ち、
USB Serial-JTAGで書き込める。画面タップ後はUSB PHYがHostへ切り替わるため、電源ボタン
長押しで再起動し、タップせずにつなぐ（認識されないときはCoreS3側のケーブルを挿し直す）。
CoreS3 USB-CにMacをつないだままタップしない。OTA用パーティションへ切り替える最初の
書き込みでは`build/ota_data_initial.bin`を`0xd000`にも書き、公式ファームの起動先情報を消す。
`idf.py flash`は使わず、bootloader(`0x0`)、partition table(`0x8000`)、
必要なら otadata(`0xd000`)、app(`0x10000`)を`esptool`で書く。bootloaderはESP-IDF 5.5.1版に
置き換わる。公式ファームへ戻す場合はフルイメージを書き戻す。

起動画面の選択: 左半分=OTGなし（台座給電用）、右上=OTGでVoyagerへ給電しNavigatorを
ジェスチャーモードに、右下=OTGのみ（Navigatorはマウス互換モード）。前回のリセット理由も表示する。

接続中のESP32-S3は16MB Flashと確認済み。公式ファームのNVSは
`0x9000`/16KB、OTAメタデータは`0xd000`/8KB。
[partitions.csv](partitions.csv)はこの2領域を維持し、3 MBのアプリ区画`ota_0`/`ota_1`を
`0x10000`/`0x310000`に置く。書き込み前のフルFlashバックアップは別途保存済み。

M5の[台座回路図](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/SCH_Adapter.pdf)では
台座とCoreS3のUSB D+/D-がつながっている。Host試験時は台座USB-Cを
データ接続しない5V電源へ接続し、VoyagerをCoreS3 USB-Cへ接続する。

初回プローブはBSPを含まず、起動ログで`board_M5StackChan`を認識した。
初回の`setUsbOutput(true)`後も`getUsbOutput()`はOFFで、そこで処理が
止まった。黒画面については文字色を明示していなかったため、
診断版では白字・バックライト輝度を指定した。
BSP入りの診断版はビルド・書き込み済みだったが、内部I²Cに接続された
AW9523、AXP2101、台座IO Expanderのいずれも応答せず、LCDは黒画面、
VBUSはOFFだった。診断を中止し、現在はM5公式M5Burner APIに掲載された
`StackChan-UserDemo V1.5.1`のフルイメージを書き戻して照合済み。
公式ファームではユーザーがLCDに「Welcom! Let's get started.」の表示を確認した。
したがってLCD故障ではなく、診断版の初期化処理に問題がある。
公式ファームは現在そのまま保持している。復元前Flashのバックアップは
`/private/tmp/stackchan-before-factory-restore.bin`（16MB、権限600）。
公開されている公式実装はGPIO12/11でESP-IDFの新I²Cドライバを使い、
AXP2101を初期化してバックライト電源を入れる。一方、この診断版の
M5GFXはArduino `Wire1`経由で内部I²Cを開いており、初回起動時の
ボード自動判定後も、通常のI²C操作では0x34/0x58/0x6Fが応答しなかった。
次の確認点はこのI²C経路の差であり、LCDや台座の故障とは判断しない。
BSPの取得元は
[StackChan-BSP](https://github.com/m5stack/StackChan-BSP) の
`8d4d6fc3b7a6be379c6317c45a02a30bff8c492e` で、MITライセンスを
保持して `components/stackchan_bsp` に配置した。BSPは起動時に
M5Unified、台座IO Expander、サーボUART、INA226を初期化する。
USB入力を優先するため、BSPのmotion taskだけ優先度を10から3に変更した。

USB Host/HIDを含む旧プローブは現在の`main.cpp`から外し、
[退避版](docs/archive/phase0-original.cpp)として保持した。
段階Eで描画を単一タスクに直してから戻す。

## Phase 0の記録

### 段階的切り分け（2026-09-29）

| 段階 | 実機確認結果 | 判定 |
|---|---|---|
| A: `M5.begin()` 1回、LCDと内部I²Cだけ | ビルド・書き込み・起動ログ取得済み。LCDはユーザー目視で黒画面。320×240を認識するが、GPIO12/11の内部I²Cで0x34/0x58はともにNACK。`Wire1.begin()`強制、`In_I2C.begin()`再実行、BSP/NimBLE/USB処理は除去済み。 | 不合格。段階A′でI²Cドライバを比較する。 |
| A′: 純ESP-IDF `i2c_master`のみ | ビルド・書き込み・起動ログ取得済み。I²C1 GPIO12/11でAXP2101(0x34)、AW9523(0x58)、台座IO Expander(0x6F)がすべて`ESP_OK`。LCD初期化は含まない。 | I²C応答は合格。Arduino/M5GFX側の経路に原因を絞れるが、どの層かは未確定。 |
| B: `M5StackChan.begin()`のみ | ビルド・書き込み・起動ログ取得済み。BSP内で`M5.begin()`を1回呼ぶ。台座IO Expanderは初期化タイムアウト、INA226も失敗。0x34/0x58/0x6FはすべてNACK。ユーザー目視でLCD黒画面・RGB消灯。ESP32-S3本体はシリアルログを出しており起動している。 | I²C・IO Expander条件を満たさず不合格。CのVBUS試験は行わず、初期化経路の方針を再検討する。 |
| A (IDF): Arduino Coreを依存から外し、M5UnifiedをESP-IDFモードで`M5.begin()` 1回 | ビルド・書き込み・起動ログ取得済み。`board_M5StackChan`(27)、ILI9342C、I²C1 GPIO12/11で0x34/0x58/0x6FすべてOK。Avatar/BSPは`EXCLUDE_COMPONENTS`でビルド対象外。 | I²C合格。Arduino TwoWire経由の経路が原因と確定。LCD表示もユーザー目視で確認。 |
| B (IDF): BSPを`millis`/`delay`互換ヘッダでArduino非依存にし、`M5StackChan.begin()`のみ | ビルド・書き込み・起動ログ取得済み。IO Expander初期化タイムアウトなし、サーボID1/2の零点(460/620)を公式NVSから読込、Motion task起動、0x34/0x58/0x6FすべてOK。 | 合格。LCD表示とRGB(青)をユーザー目視で確認。 |
| C (IDF): 起動時VBUS OFF、画面タップで`setUsbOutput()`をトグル | ビルド・書き込み済み。LCDに状態表示、RGBはON=緑/OFF=赤。`setUsbOutput(false)`直後からUSB Serial-JTAGのログが途切れるが、処理は継続しLCD表示は正常（ユーザー目視）。以降の確認はLCDで行う。台座充電器接続時はOFFでもVoyagerのLEDが点灯（台座給電がCoreS3 USB-Cへ回る可能性）。電池のみではOFF: P0=07/P1=83、ON: P0=27/P1=83、ON時に台座電池電流が147→274mA。LEDはON/OFFとも消灯（QMKはホスト列挙までLED消灯の可能性）。 | 合格（段階EでVoyager列挙とLED点灯を確認）。台座充電器接続中にOTGをONにしない。 |
| E (IDF): タップでVBUS ON→USB Host/HID起動。描画は`ui_task`に集約 | ビルド・書き込み済み。起動直後はUSB Serial-JTAGのまま（書き込み可能）、タップ後はHost PHYへ切替。電池のみで、`VID 3297 PID 1977`とBoot ready表示、VoyagerのLED点灯、キー操作に応じたmod/key変化をユーザー目視で確認。 | 合格。 |
| D+E (IDF): NimBLE Hostを起動し同期確認後、段階EのUSB Host/HIDを併用 | ビルド・書き込み・起動ログ取得済み。`BLE ready`（Host同期）をログとLCDで確認。初回起動時にPHY RF校正データをNVSへ保存。BLE起動中もVoyager列挙、Boot ready、LED点灯、キー入力表示をユーザー目視で確認。 | 合格。F（サーボ＋USBストレス）は保留。 |
| 台座給電試験: 台座USB-Cへ給電し、OTG OFFのままUSB Host起動 | Mac mini給電でVBUS in 4880mV、ディスプレイUSB給電で4553mV。OTG OFFでもVoyagerは点灯。CoreS3では`USB disconnected`と`Boot ready`が交互に表示され、同時にVoyagerの入力がMac miniへ届いた。台座USB-CのD+/D-がCoreS3 USB-Cへ直結しており、Mac miniとCoreS3の2 Hostが同一バスを取り合っている。 | 不合格。台座へはデータ線のない給電（充電専用ケーブル等）が必要。D+/D-を短絡するデータブロッカーは不可。VBUSは台座からのパススルーで足りるためOTGは不要。 |
| 台座給電試験2: Anker Nano II 65W（USB-C）から台座へ給電 | VBUS in 約4772mV。VoyagerのLEDは消灯、CoreS3は`Waiting for Voyager`のまま列挙されない。充電器がD+/D-を短絡（BC1.2 DCP）し、台座経由でVoyagerのデータ線が短絡されたと推定。 | 不合格。D+/D-を持たない充電専用ケーブルで再試験する。 |

| タスク | 現在の根拠と状態 |
|---|---|
| P0-01 Hardware Inventory | ESP32-S3、16MB Flash、BLEをシリアル経由で確認。M5公式のStackChan K151と推定。台座・サーボ・電源定格の実機確認は未了。 |
| P0-02 Voyager Descriptor | Mac接続でVID:PID `3297:1977`、Full Speed。4 InterfaceのEndpoint、MaxPower、Report Descriptorを[記録](docs/voyager-descriptors.md)。 |
| P0-03 USB VBUS | ESP-IDFモードで`setUsbOutput(true)`によりAW9523 P0=27/P1=83となり、電池のみでVoyagerへ給電できることを確認。電圧の数値測定は未了。 |
| P0-04 Framework Integration | Arduino Coreを外し、M5Unified(ESP-IDFモード)＋BSP(互換ヘッダでArduino非依存化)＋usb_host_hidで動作確認。Avatarは未移植でビルド対象外。NimBLE Hostも同時起動で動作確認。 |
| P0-05 USB Enumeration | VoyagerをCoreS3 USB-Cに接続し、VID:PID `3297:1977`のBoot Keyboard Interfaceを列挙。 |
| P0-06 Keyboard Report | Boot Protocolへ切替え、キー操作に応じた修飾キー・Usage表示を実機確認。 |
| P0-07 Servo + USB Stress | 保留（2026-09-29判断）。 |

## 未決: 台座給電の経路（2026-09-30時点で後回し）

台座USB-CのD+/D-がCoreS3 USB-Cへ直結しているため、台座へはデータ線のない
給電（充電専用ケーブル、またはD+/D-を切断したケーブル）が必要。用意できるまでは
台座USB-Cを外し、電池駆動＋OTGでVoyagerへ給電して開発する（段階Eで動作確認済み）。
確定後にP0-07、負荷時VBUS測定、spec 5章・14章の改訂を行う。

## Phase 1 Input Router

[spec 9章](docs/spec-v0.4.md)のRouterを`components/input_router`に実装した。
ESP-IDFに依存しない純粋ロジックで、`components/input_router/test_host/run.sh`で
PC上のユニットテスト（17件、ASan/UBSan付き）を実行する。契約は
`include/input_router.h`。ErrorRollOver(Phantom Report)は無視する。
ファームには未組込み（ルート`CMakeLists.txt`で除外、Phase 2で組み込む）。

## Phase 2 BLE Keyboard 1 Host

`main/ble_keyboard.cpp`でESP-IDF `esp_hid`（NimBLE）によるHID over GATTの
キーボード（Report ID 1、Boot互換8 byte、LED Output付き）を実装した。
PoCのとおりJust Works＋Bonding（NVS保存）、Privacyなし。デバイス名は
`StackChan Hub`。USB Report → `router_task`（Input Router）→ BLE送信の経路で、
暗号化成立をRouterの接続完了として扱う。Slotは1つ（Mac mini）のみで、
切替キー押下時の`kStartConnect`は同一Slotかつ接続済みなら即接続扱いにする。

2026-09-30、Mac miniとペアリング（connect status=0、encryption status=0）し、
電池駆動＋OTGでVoyagerをCoreS3へ接続して、Mac miniへ「Hello World」を入力できた。

## Phase 3 Consumer / Mouse

`components/hid_descriptor`でReport Descriptorを解析し（PC上テスト7件、Voyager実記述子を使用）、
VoyagerのHID Interfaceをすべて開いて用途別に振り分ける。Vendor（Oryx用）Interfaceは閉じる。
RouterはConsumer/System/Mouseを扱い、切替保留はキー・Consumer・System・マウスボタンの
全解放まで続く（テスト22件）。BLE側はConsumer(ID 2)、System(ID 3)、Mouse(ID 4、5ボタン、
X/Y/Wheel/AC Pan)を追加。Navigatorの1 ms Reportに対し、マウス移動は8 msごとに合算して送る。

2026-09-30の実機確認: キー入力、画面輝度キー（Consumer）、Navigatorタッチパッドの
カーソル移動とクリックは動作。スワイプ（スクロール）とドラッグは不可。
Navigatorのマウス用Report（Interface 3、ID 6）はボタン3個とX/Yのみでホイールを持たない。
[ZSAのnavigator_trackpad](https://github.com/zsa/qmk_modules/tree/13890cd7856175de20798689d15ba6a46bf0c5c7/navigator_trackpad)
によると、TouchpadはHostがInput Mode(Feature ID 4)を3にするまでマウス互換モードで、
その間はタップのクリック変換のみ行いスクロール・ドラッグを持たない。
そこでHubがInput Mode=3とSurface/Button Switch(Feature ID 5)を設定し、PTP接点Report(ID 1)を
`components/touchpad`のジェスチャー変換（PC上テスト13件）でマウスReportにする:
1本指=移動、2本指=スクロール（natural）、タップ=左クリック（離しは200 ms遅延）、
2本指タップ=右クリック、タップ直後に触れて動かす=ドラッグ、ダブルタップ=ダブルクリック。

2026-09-30の実機確認: Touchpadの2本指スクロールと、ドラッグによるウィンドウ移動が動作。
初版は`hid_descriptor::Interface`（約6 KB）を一時オブジェクト経由で初期化しており、
`hid_events`タスク（4 KB）のスタックがVoyager列挙時に溢れて再起動していた。
その場初期化とスタック8 KBで解消。USB Host中はシリアルが使えないため、
起動画面に前回のリセット理由（`last reset:`）を表示する。
ZSAの常駐ソフトはVoyagerのVendor Interface（Raw HID）へUSBで直接つなぐため、
Hub経由では`disconnected`のままになる（spec 15章どおり、設定変更時はPCへ直結）。

2本指で素早く横に払う操作（12 mm以上、350 ms以内、縦の2倍以上）はブラウザ履歴の
ショートカットに変換する: 左→右=Cmd+[（戻る）、右→左=Cmd+]（進む）。US配列前提。
1回のタッチで1回だけ。ゆっくり横に動かすと判定後に横スクロールになる。
RouterはショートカットをBLEへ送る際、押下中のキー状態に重ねて押し、元に戻す。

BLE送信は順序を保つ送信待ち列（outbox）を通す。NimBLEが受け付けないときは5 msごとに
先頭から再送し、キー解放の欠落によるStuck Keyを防ぐ。マウス移動は未送信のマウスReportへ
合算する。

2026-09-30に見つけた問題:
- NimBLEが通知のたびにINFOログを2行出し、同期ログ出力でRouterタスクが1回約10 ms止まって
  入力を取りこぼしていた（`dropped`）。NimBLEはWARN、esp_hidの送信失敗ログは非表示にし、
  コンソールUARTを2 Mbpsにした。
- ジェスチャー出力の配列を使い回す際に`swipe`欄を初期化しておらず、一度スワイプすると
  以後のポインタ移動がすべてスワイプ扱いになっていた。
- Wi-Fi追加後、内部RAMが不足してUSBのEndpoint確保（DMA）に失敗した。Interface解析表を
  PSRAMへ移し、Wi-Fiの受信バッファを減らした。
- macOSの`nc -u`は最初の送信元だけを受けるため、再起動後のログを取りこぼす。
  `tools/log.sh`はPythonで受信する。

実機確認（2026-09-30）: キー入力、1本指のカーソル移動（滑らかさは改善余地あり）、
2本指スクロール、スワイプでの戻る・進む、スワイプ後のカーソル移動、`dropped 0`。

M5公式K151にはフィードバック付きの水平・垂直サーボと専用の
StackChan BSPがある。型番と電源定格は実機で確認し、
VBUSとBoot Keyboard入力が安定してからストレス試験を行う。

参考： [StackChan公式資料](https://docs.m5stack.com/en/StackChan)、
[StackChan Servo API](https://docs.m5stack.com/en/arduino/stackchan/servo)、
[ESP-IDF USB HID Host例](https://github.com/espressif/esp-idf/tree/v5.5.1/examples/peripherals/usb/host/hid)。


## Phase 5A/B: 単一Identityへの移行（2026-10-08）

案A（Slotごとに広告アドレスを変更）では、Linuxの明示ペアリング後に入力できたが、
Macへ戻れなくなった。Macを再ペアリングすると今度はLinuxが認証失敗
（Hubログ `disconnect reason=0x205`）で再接続を繰り返した。Linux側の
Paired/Bonded登録は残っており、再ペアリングだけでは往復切替を安定させられなかった。
鍵の上書き自体はまだ確認していない。

ユーザー合意によりspec 11.3の案BをPoC実装した。BLEアドレスはチップの固定Public
Identityに統一し、名称は全Slotで `StackChan Hub`。NimBLEのBonding保存を使い、
`ble_slots_b1` NVS namespaceへSlotごとのPC Identityアドレスを保存する。登録済み
SlotではFilter Accept Listに選択中のPCだけを設定する。設定失敗時は広告を開放しない。
未登録Slotは初回ペアリングを受け付けるが、別Slotの登録済みPCを拒否する。
Hub自身のRPAは使わず、PCのRPAをSlotへ保存しない。

Slot切替要求はNimBLE Hostキューで処理し、旧接続の切断後にAccept Listを更新する。
接続相手・Slot・暗号化・Bondingを確認してからRouterへ接続完了を通知する。
再ペアリング時の鍵削除は認めた接続相手に限る。満杯時に既存PCの鍵を自動追放しない。
初回起動時のみ旧方式のBLE Bondingを削除する。Wi-Fi・台座設定・Active Slotの
NVS領域は維持する。各PCは新しい `StackChan Hub` へ再ペアリングが必要。

検証済み: `sh main/test_host/run.sh`（ASan/UBSan付き）の接続許可・Slot間隔離・
再ペアリング時の対応保持・保存復元・不正データ拒否。既存のInput Router、
HID Descriptor、Touchpadのhost testも全件PASS。ESP-IDF 5.5.1ビルド成功。

実機書き込み済み（2026-10-08）。Wi-Fiが応答しなかったため、USB Serial/JTAGの
`usb_reset`で接続し、otadataのCRC付きsequence 11から起動先 `ota_0` を確認した。
現アプリ3 MBを `/private/tmp/stackchan-app-before-plan-b.bin`、otadataを
`/private/tmp/stackchan-ota-before-plan-b.bin` に退避後、アプリだけを `0x10000` へ
書き込んだ。1,889,904 bytes、esptoolのハッシュ照合成功。ブートローダー・partition
table・otadata・Wi-Fi/台座NVSは書き換えていない（BLE旧Bondingのみ初回移行）。
通常起動ログで新ELF SHA256 prefix `14fa80048`、`plan B migration: removed 1 legacy
bonds`、`advertising slot 2 as StackChan Hub (pairing)`を確認した。
アプリdescriptorのCompile timeはincremental buildで旧日時のままなので、今回の
実機判定はハッシュ・移行ログ・広告Identityを根拠にする。

Linuxで固定Public Identity `<Hubのアドレス>` をペアリングし、Hubの
`encryption status=0`、`paired slot 2 peer type=0 <Linux PCのアドレス>`、
HTTP `/status` の `secured 1` を確認した。再起動後の台座BSP値は3.74V/-5mA。
LCDのPMIC残量は別の電源情報。台座をMac miniへデータ接続したままのUSB入力検証は、
既知の2 Host競合を避けるため行わない。2026-10-08、ユーザーが案BでLinuxへのVoyager文字入力成功を確認した。
Linuxの新IdentityはPaired/Bonded/Trusted/Connectedを確認済み。
**Macペアリング・Mac→Linux→Mac往復・非選択PCの接続排除・再起動後の
Bonding/Slot復元は未確認。**


案Bの追加切り分け: 未登録Slot 1では、登録済みLinuxが繰り返し再接続して初回
Macペアリングを妨げたため、Linux上で対象Hubのみを一時blockして試験した
（Bondingは維持、Mac登録完了後にunblockする）。MacのBluetooth設定の「接続」は
通信路だけを開き、暗号化が始まらなかった。移行時に外した
`ble_gap_security_initiate`を、接続相手のSlot許可確認後・未暗号化時だけ呼ぶよう
修正した。Slot制限のhost testとビルド成功。修正版をWi-Fi OTAで `ota_1` へ
書き込み、`ok, restarting` と新区画での再起動を確認。LinuxのSlot対応を再消去
する移行は行わない。修正版のMacペアリングと往復切替は確認中。

修正版のMac初回ペアリングで、ユーザーが接続成功を報告。Macの新しい
`StackChan Hub` (`<Hubのアドレス>`) 接続と `/status` の `secured 1`、
USB入力・BLE送信カウンタ増加を確認した。Linuxの一時blockは解除済みで、
Paired/Bonded登録は保持。往復切替の入力試験を継続中。

2026-10-09: ユーザーがLinuxへ戻って文字入力できることを確認したが、続けてMacへ
戻ると未接続になった。再現時ログでは旧接続の切断 (`0x216`) とSlot 1の
`selected peer only` 広告開始を確認した。その後Hubの `secured` は0、Macの
登録済み `StackChan Hub` も未接続。Mac接続・認証イベントは取得できておらず、
原因は未確定。LinuxへのSSHは1Password Agentの署名拒否で調査できなかった。

切り分け用に選択PeerのIdentityアドレス、Bond/LTK/IRKの有無、接続時のIdentityと
無線上のアドレスをログへ追加した。鍵そのものはログへ出さず、接続方式と保存情報は
変更しない。PeerSlots host test PASS、ESP-IDF 5.5.1 build成功、OTAの
`ok, restarting` と `/status` の `ota_0` / `reset software` を確認。
診断版での往復ログ取得とMac入力の確認は未完了。

追加の往復ログでは、LinuxはSlot 2で接続・暗号化に成功し、Macへ戻ると広告のみで
未接続になることを再確認した。Slot 1の保存Identity `<Mac miniのアドレス>` は
Macのコントローラアドレスと一致。MacのLTK/IRK、LinuxのLTKが残っていることも
確認した。Voyagerはバッテリー＋OTG onで列挙できる。台座給電時のOTG offで
認識しないというユーザー報告があり、BLE試験は既知のバッテリー構成へ戻した。

ユーザー合意により、登録済みSlot 1の広告だけControllerのFilter Accept Listを
空にし、広告のfilter policyをNONEにする比較版を作成。他Slotのフィルタ、
`admit_link`でのIdentity/Slot確認、暗号化/Bondingを確認した後の入力転送は維持する。
未登録Slotの登録方法・NVS保存情報は変更しない。これは一時的な切り分け版で、
正式方式として採用したものではない。

PeerSlots host test PASS、ESP-IDF 5.5.1 build成功、OTAの `ok, restarting` と
`ota_1` / `reset software` を確認。起動後はLinuxが約0.5秒ごとにSlot 1へ再接続し、
Hubが毎回拒否する実機ログを取得した。Mac接続・暗号化はまだ成立していない。
Linuxの自動再接続が比較に干渉するため、Linux Bluetoothを一時OFFにした状態での
Mac再接続確認をユーザーへ依頼中。許可リストが原因かどうかは未確定。

比較結果: ユーザーがLinuxのBluetoothをOFFにして30秒以上待っても、Macは
未接続のまま。Linuxの接続試行が止まった後もMacの接続成立イベントはなく、
許可リストを外すだけでは改善しなかった。比較用の変更は撤回し、登録済みSlotの
Controllerフィルタをすべて復元した（host test PASS、build成功、OTA応答
`ok, restarting`）。Linux BluetoothをOFFにする操作は比較試験のみに用い、
通常運用の条件にはしない。両PCのBluetoothをONにしてF21/F22だけで往復する
要件は引き続き未達。Macの明示接続と自動再接続の切り分けも未完了。

### Macの通知購読失敗とCCCD容量修正（2026-10-09）

Claudeの `claude-findings.md` とMacログを評価した。初回Mac登録時にReport ID 2/3/4の
通知購読が `CBATTErrorDomain Code=8` で失敗し、HIDサービスの起動タイムアウトと
Hubの `reconnecting: N` が記録されている。一方、広告の受信は続いている。
ESP-IDF 5.5.1のNimBLEではCCCD保存枠が全Peer合計で、既定8件。枠不足時の
`BLE_HS_ESTORE_CAP` に対し `store_status_cb == nullptr` は `BLE_HS_ENOTSUP` (8)を
返し、CCCD書き込み経路がその値をATT応答に直接返す。この経路はMacログと整合する。
ただし当時のHubの枠不足ログ・正確なPeer別使用数は未取得なので、枠不足によるHID
初期化失敗が自動再接続不可を引き起こしたという因果は修正後の実機確認で検証する。

Claudeの変更 `CONFIG_BT_NIMBLE_MAX_CCCDS=32` を採用し、実効sdkconfigとビルド設定も
32件であることを確認した。満杯時はログを出して拒否し、他Peerの鍵・購読を追放しない。
追加修正としてCCCDあふれには `BLE_ATT_ERR_INSUFFICIENT_RES` (0x11)を返す。
Host側エラー27はこのIDFのCCCD経路で未定義ATTエラーになるため、CCCDのみ変換する。
鍵保存など他のあふれには従来の `BLE_HS_ESTORE_CAP` を返す。

PeerSlots host test PASS、ESP-IDF 5.5.1 build成功、差分の空白チェック成功。
OTA応答 `ok, restarting` と `/status` の `ota_1` / `reset software` / 新ビルド日時
`Oct 9 2026 20:30:48` を確認した。Mac再登録・購読成功・両PCのBluetooth ONでの
往復切替と入力は未確認。電池電圧が3.32Vへ下がったため、Mac再登録前に本体直結給電を
依頼した。接続方式・Accept List・Linux登録・Slot対応のNVSは変更していない。

給電後（本体USB-C直結、USB Host未起動）、Macの固定Public Identityの
`StackChan Hub` だけを登録解除して再ペアリングした。旧 `StackChan Hub 1` と
他のBluetoothデバイスは変更していない。20:44:39 JSTにMac側で暗号化成立、
Hub側でもMac Identityとの `connect status=0` / `encryption status=0` と
`/status` の `secured 1` を確認。MacのBTLEServerは20:44:45までに各サービスを
起動し、再登録時のReport ID 2/3/4の通知購読エラーと30秒のサービス起動
タイムアウトは出ていない。ログは `/private/tmp/stackchan-cccd32-mac.log` と
`/private/tmp/stackchan-cccd32-hub.log`。初期化時の問題は改善したが、Voyager入力と
自動再接続の確認はこれだけでは成立しないため、両PC Bluetooth ONでMac入力→
F22/Linux入力→F21/Mac入力の実機試験を依頼した。

ユーザーが電池残量不足のため充電後に試験を再開することを希望し、実機試験を停止した。
修正版でのMac再登録・暗号化と購読エラー解消までは確認済み。VoyagerのMac入力、
Linux入力、F21でMacへ戻った後の入力、および両PC Bluetooth ONでの安定した往復は
未確認であり、解決済みとは扱わない。

充電後、依頼したMac→Linux→Macの往復試験について、ユーザーが各接続先での
文字入力とトラックパッド操作の成功を報告した。CCCD増枠・Mac再登録後に、
従来失敗していたMacへの戻りを含む往復が実機で成功した。これまでのMac再接続不可は
この試験で改善を確認でき、CCCD容量不足を主因とする説明を強く支持する。
今回の確認は1往復のユーザー報告であり、100回切替、長時間入力、Sleep/Wake、
Hub再起動後のBonding/Slot復元、3台目の接続試験は引き続き未実施。

### 当面の範囲と将来計画

当面はMac miniとLinux/Omarchyの2台を、F21/F22だけで往復して使用できればよい。
MacBook Airの追加は後回しにし、既存の3 Slot構造は維持する。

将来、LCDのメニューからBLE接続管理を行えるようにする。候補は、時間を限った
新規・再ペアリング受付、対象Slotの登録解除、PC交換時の再登録。登録解除は対象PCの
Bond・通知購読・Slot対応をまとめて扱う。通常の登録済みPCへの再接続には本体操作を
要求しない。現時点では計画のみで、操作方法・受付時間・認証方式は未確定。
PoCのJust Worksと未登録Slotのペアリング動作は今回は変更しない。

コードレビュー後の当面の対応は、切断処理中の再接続済み通知を防ぐこと、送信時にも
接続Slotと選択Slotの一致を確認すること、広告停止・開始と切断要求の戻り値を確認する
ことに絞る。自動再試行・新しい復旧UI・孤立Bondの後始末は後回し。診断ログは復元と
切替の検証まで保持し、CCCD使用数とPC側RPAの確認は追加Hostの検証時に行う。

### 切替中の状態保護とエラー処理（2026-10-09）

切断要求から完了イベントまでを明示的に管理し、その間に元のSlotへ戻しても
暗号化済み通知や入力送信を再開しないよう修正した。送信時にも接続Slotと選択Slotの
一致を確認する。切断要求・広告停止・広告開始の戻り値を確認し、広告開始失敗を
成功として通知しない。新しいペアリングUIや自動再試行は追加していない。

`sh main/test_host/run.sh` はPeerSlotsテストと、実際の `ble_keyboard.cpp` をSDKの
スタブで実行する回帰テスト11件がPASS。高速なSlot往復、切断中の遅延イベント、
Slot不一致での送信拒否、切断・広告APIの失敗、CCCDあふれを検証した。
このテストは実際の無線タイミングやタスク間の同時実行を再現するものではない。
InputRouter 24件、HID Descriptor 8件、Touchpad 19件もPASS。
ESP-IDF 5.5.1ビルドと `git diff --check` が成功した。

OTA応答 `ok, restarting` と、起動先 `ota_0` / `reset software` を確認した。
再起動後に `encryption status=0` と `/status` の `secured 1` を確認し、
登録情報を保った自動再接続が成立した。USB入力は起動直後0件だったため、
OTGをONにしてMac→Linux→Macの文字入力・トラックパッド操作を確認するよう依頼した。
修正後のMac→Linux→Macの文字入力・トラックパッド操作について、ユーザーから
実機試験に問題なしとの報告を得た。当面の2台間往復という要件は確認済み。
高速連打による切替、100回切替、長時間入力、Sleep/Wakeの実機試験は未実施。

### 常時給電の切り分け（2026-10-09）

ユーザーの充電タップ＋USB-Cケーブル＋データ遮断アダプタ構成では、USB Host使用後に
VBUS 0、電池3.77V、約139mA放電を表示した。アダプタを外し、本体直結へ変更しても
同様だった。Voyagerと給電を外し、RST短押し後に画面をタップせず給電すると、
本体直結でVBUS 5024mV / 電池3.97V / -693mA、台座経由で4855mV / 3.95V /
-693mA、台座＋アダプタ経由で4826mV / 3.96V / -693mAとなった（LCDのユーザー報告）。
充電経路は確認できたが、以前のVBUS 0の原因は未確定。

その後、台座＋アダプタ給電のままVoyagerを接続し、左側のOTG OFFでUSB Hostを
開始したところ、VoyagerのLEDは消灯し `Waiting for Voyager` のまま。
充電とUSB入力の両立は未達。アダプタのデータ線仕様と、Voyager側の給電・接続条件を
引き続き切り分ける。外部給電検出時にOTGを有効化しない既存ガードは維持する。

同じ構成でHost開始後にVoyagerを抜き差ししても、LED消灯・Waitingのままだった。
起動順序だけによる失敗という仮説は支持されない。Claudeの台座給電提案を回路図・
ソースと照合し、先にアダプタ有無とVoyager側の実電圧を確認する方針とした。
公式CoreS3 v1.0回路図では、台座用4P端子J6 PIN1とUSB-C J1 VCCは同じ `VUSB`。
PMIC側の `VBUS` はU18（USB_OTG_ENがLowで有効）を経た別の線であり、台座が
PMIC側だけに入るという仮説はこの図とは一致しない。実機の配線・基板差は未測定。
`setUsbOutput(true)` はUSB出力スイッチだけでなくBOOST_ENも有効化するため、
外部給電ガードを単純に外す変更は行わない。

`getVBUSVoltage()` はAXP2101の入力検出が無効なら0を返す。LCDのVBUS 0はUSB-C
端子の実電圧0Vの証拠ではなく、以前の説明を訂正する。
充電電流約693mAと電池容量550mAhだけでは許容充電電流を判断できないため、
充電電流・終止電圧の変更は電池仕様と公式設定の確認まで保留。
起動時の自動電源判定・給電断時の昇圧切替・再判定UIも、外部給電で入力できる
構成を確定してから設計する。root-port power APIは外部5Vを物理的に切断する
ものではないため、Voyagerの電源再投入を保証する対策としては採用しない。

充電タップへ直接つなぐためデータ遮断アダプタを外し、Voyagerを抜き差しした
比較でも認識しないとのユーザー報告を得た。アダプタだけが原因という説明も
支持されない。次はVoyager側の物理VBUSを確認し、給電と信号の問題を分ける。

USB-C間に挟む電圧計で、充電タップ→台座は5.09V / 0.92A、
本体USB-C→電圧計→Voyagerは4.902V / 0.071Aとのユーザー報告を得た。
少なくとも電圧計を挟んだ構成ではVoyager側にも5Vと負荷電流があり、
OTG OFFで端子側が無給電になるという仮説は支持されない。
電圧計のデータ透過・CC配線は未確認なので、挿入前と同じUSB接続条件とは断定しない。
充電タップ・データ遮断アダプタのD+/D-配線を次の確認対象とする。

使用アダプタはユーザー申告「MS Forceデータブロッカー」（MSL FORCE製品候補）。
給電元をMac miniへ替え、データブロッカー→台座、Voyagerは本体へ直接という比較では、
Voyager左側LEDの一部のみ点灯し、本体はWaitingのままだった。MacのUSB機器一覧にも
Voyagerは現れず、HubのUSB入力数は0だった。ブランド名と公開商品説明だけでは
アダプタのD+/D-開放・短絡仕様は確認できていない。

配線変更の負担を減らすため、PMIC側VBUS・入力状態、AW9523出力（OTG/BOOST）と
充電設定レジスタを `/status` へ追加した。UIタスクでchecked I2C読み取りを行い、
HTTPはatomicスナップショットのみ読む。読取失敗は-1で区別する。
比較用に充電電流のみ一時200mA設定とし、充電電圧・入力電流制限・VINDPMは変更しない。
ビルド・差分チェック成功、OTA応答 `ok, restarting`、起動先 `ota_1` を確認した。
再起動後は `charge_reg62 8`（200mA）、電池4.11V / -187mA、PMIC VBUS4934mV、
OTG OFF / BOOST ON / AW出力0x8307、BLE secured 1、USB入力0件だった。
充電負荷低減後の入力可否をユーザーへ確認中。充電負荷が原因と確定した扱いにはしない。

ユーザーが200mA診断版でもWaiting・入力不可を確認した。単純な充電負荷低減では
解決しなかった。追加の読み取り診断をビルド・OTAし、USB Hostの登録Clientが1、
列挙済みDeviceが0、DWC HPRTが0x00001c00（root power ON、接続検出なし、
line status=3）であることを継続して確認した。PMIC VBUS4939mV、電池約-187mA、
OTG OFF / BOOST ON / AW0x8307、充電設定200mAのまま。
line status=3はD+/D-両方HighのSE1状態に相当し、正常なFull-Speedの接続待機状態ではない。
給電側のデータ線短絡・バイアス等を疑うが、場所と電気的原因はまだ確定していない。
診断はHost Library情報とコントローラレジスタの読み取りのみで、USBポート制御は変更しない。

Voyagerだけを外した状態（2026-10-10）ではHPRTが0x00001000へ変化し、
root power ONのままline status=0（D+/D-両方Low）となった。PMIC VBUS5097mV、
OTG OFF / BOOST ON、列挙Device0。両方HighはVoyager接続時に発生しており、
給電側だけから両線を常時Highへ駆動しているわけではない。Voyagerのプルアップが
両線へ回る短絡等と整合するが、短絡箇所は未測定のため確定していない。

### 給電側による通信阻害の比較と対策方針（2026-10-10）

Claude経由でユーザー実施の追加試験結果を受領した。電池のみで起動し、OTG ONで
Voyager入力が成立した状態へ、データブロッカー経由の台座給電を追加すると入力が
止まった。同じUSB入力経路が給電側の追加で失敗するため、OTG OFF固有の問題ではない。
これは、先のSE1観測と合わせて、給電側（ブロッカー・給電元・そのケーブル）による
D+/D-短絡またはバイアスの説明を強く支持する。原因部品はまだ特定していない。
以前の「給電中OTG ONで入力可」は配線条件が今回と同一だったと確認できず、
今回の構成が動作する根拠には使わない。外部給電中のOTG ONを通常運用には採用しない。

充電負荷200mAでも改善せず、Voyager側で4.902Vと負荷電流を確認できたことから、
単純な充電電流過大・定常電圧不足を主因とする説は採用しない。瞬間的な電圧低下を
測定して完全に除外したという意味ではない。

次の推奨構成は、外部5VのUSB-A充電器→D+/D-が未結線のA-C給電専用ケーブル→
台座、本体USB-C→Voyager。採用条件は、台座側D+/D-が互いにも電源にも接続されず
開放されること、必要な5V電源・GNDとUSB-C給電用CC処理が維持されること。
「充電専用」「データ遮断」という商品名だけでは条件を満たすと判断しない。
BC1.2 DCPはD+/D-間200Ω以下の結線を持つので、普通のデータケーブルでは
充電器の結線が台座へ届く。端末側でD+/D-を短絡する給電専用ケーブルも今回には不適合。
分解・基板改造ではなく、配線仕様を確認できるケーブルによる給電側の分離を優先する。

ケーブル仕様を確認した後の実機試験は一つの構成に絞り、外部給電・OTG OFFで
Voyager列挙と入力が成立し、USB線状態がSE1でなく、電池が放電し続けないことを確認する。
診断用200mA設定は根本対策として確定せず、最終的な充電設定は別に判断する。
起動自動判定・給電断切替・外部給電ガード解除は今回の対策に追加しない。

### 充電専用ケーブルで給電・入力の両立を確認（2026-10-10）

ユーザーがMac mini→充電専用ケーブル→台座で給電し、本体USB-CへVoyagerを接続、
起動画面の左半分（OTG OFF）でUSB Hostを開始して、文字入力の成功を確認した。
入力成功時の `/status` はPMIC VBUS4956mV、電池4.19V / -138mA（充電方向）、
OTG OFF / BOOST ON / AW0x8307、USB Device1 / Client1、HPRT0x00021405
（接続・ポート有効、Full-Speed、line status=1）、USB入力374件、dropped0、
BLE secured1。先のSE1状態は解消しており、このケーブル構成で外部給電と入力が両立した。

当面の実用構成はこのMac mini給電＋充電専用ケーブル＋OTG OFFを使用する。
既存のデータブロッカー構成は採用しない。ケーブルの型番・内部配線は未確認だが、
今回の実機で通信に干渉しないことを確認できた。充電タップへの変更はまだ未検証。
短時間の確認であり、長時間使用中に電池残量が減り続けないかは普段使いで確認する。
充電電流は診断用200mA設定のままで、最終的な充電設定は未確定。

### LCDメニュー・BLE接続管理の初回統合（2026-10-11）

ClaudeがUI統合・ビルド・OTAを実施。以下の検証結果はClaudeからの報告であり、
この記録更新時にCodexによる再ビルド・書き込み・実機操作は行っていない。

構成：

- `main/ui_model.h`：画面遷移・タッチ判定の純粋ロジック。
- `main/ui_view.{h,cpp}`：PSRAMキャンバスを使ったLCD描画。
- `main/main.cpp`：ホーム・設定・接続先管理・診断・起動時給電選択の統合。
  ホームはMac mini/Linuxの大きなボタンを基本とし、3台目の登録に対応する。
  起動時は3秒の手動選択猶予と給電自動判定、通常画面は30秒で減光し消灯しない。
- `main/ble_keyboard.*` / `main/peer_slots.h`：時間制限付きペアリング受付、
  中止、対象PCのみの登録解除、接続先スナップショット、完了・失敗通知。
  登録済みPCの通常再接続は受付を開かずに行う。
- `components/input_router/`：LCDからの切替用 `OnSelectSlot`。
  ペアリング開始はRouterタスクで `OnSelectSlot` → `SelectSlot` →
  `StartPairing` の順。選択中PCの解除は `Router.Reset` による全解放と
  outbox送出を経て `Unbind` を要求する。
- `main/dev_link.*`：`GET /screen` とトークン付き `POST /tap`。
  `tools/screenshot.sh` は画面をPNGへ保存、`tools/tap.sh` は遠隔タップを送る。
- `main/test_host_ui/`：UIモデルのhost test。

確認状況：

| 項目 | 結果 |
|---|---|
| ファームウェアビルド・OTA | Claude実施、成功 |
| BLE host test (`main/test_host`) | 33件PASS |
| UI host test (`main/test_host_ui`) | PASS |
| Input Router host test | PASS |
| 各画面の描画 | 実機キャンバスのスクリーンショットで確認。起動時の実際のカウントダウン動作は未確認 |
| LCDのMac miniボタンからの切替 | 実機でMacへ再接続、`secured 1` を確認。実際の指でのタッチ操作は未確認 |

残る実機確認：

- ペアリング開始・中止・時間切れと、登録解除の完了／失敗表示。
- 通常起動時の3秒カウントダウン・手動選択・無操作時の自動開始。
- 30秒減光と復帰タップ（最初のタップは復帰のみ、消灯しない）。
- キーボード給電設定の切替。
- 実際の指によるタッチ操作と読みやすさ。
- 統合後のMac/Linux往復、文字入力・トラックパッド操作の回帰確認。

スクリーンショット・host test・OTA成功は、上記の未確認操作の実機合格を意味しない。
給電断時の自動昇圧は保留。PC交換は確認付き登録解除の成功後にペアリングを開始する。
