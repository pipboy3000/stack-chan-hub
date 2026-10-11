# Voyager BLE Hub 計画・実装仕様書 v0.4

> 2026-09-29 時点の実機結果との差分（v0.5で反映予定）:
> - 6.1/6.2/P0-04: Arduino as componentはこの機体で内部I²Cが動作しないため不採用。ESP-IDF + M5Unified(ESP-IDFモード)で構成する。Arduino USB Device初期化の確認項目は不要。
> - 5.1/5.2: 台座給電中はOTG OFFでもVoyagerへ5Vが届く現象を確認。台座給電＋OTG ONの同時使用と、台座USB-C経由のD+/D-共有による列挙への影響は未検証。
> - 詳細はREADMEの「段階的切り分け」を参照。

## 0. 変更履歴

### v0.3 → v0.4

- ArduinoコアがUSB Device側を自動初期化しない設定を追加
- P0-04にUSB初期化競合の確認項目を追加
- BLE Identity比較の案BへFilter Accept List / Directed Advertisingを追加
- BLE Privacy(RPA)を比較時はOFFに固定
- 切替先が見つからない場合のRouter/接続状態仕様を追加
- Host切替タイムアウトと未接続状態を追加
- P0-06とPhase 1の重複を解消
- Phase 1をRouter単体実装・ユニットテストへ変更

---

# 1. 目的

ZSA VoyagerをStack-chan（CoreS3）へUSB接続し、CoreS3をUSB Hostとして動作させる。

Voyagerから受信したUSB HID入力をBLE HIDへ変換し、以下の3台を切り替えて操作する。

- Mac mini
- Linux / Omarchy
- MacBook Air

Stack-chanは机上に常設し、入力デバイスハブを最初の実用機能として実装する。

その後、

- PC状態表示
- 通知
- LLM / OpenClaw連携
- タイマー
- デスクトップマスコット

などへ発展させる。

---

# 2. ハードウェア

## 2.1 Stack-chan

CoreS3版を使用する。

必須条件：

- ESP32-S3
- USB OTG
- BLE
- LCD
- サーボ
- USB VBUS出力制御
- 台座経由の外部給電

USB-CはVoyager専用USB Hostポートとして使用する。

CoreS3本体への常時給電はStack-chan台座からM5-Bus経由で行い、USB-Cからは給電しない。

Core2等、ESP32-S3を搭載していないStack-chanは対象外。

---

## 2.2 ZSA Voyager

QMKベースのUSB HIDキーボード。

Voyager内部で以下を処理する。

- Layer
- Mod-Tap
- Tap Dance
- Macro
- Key Remap

CoreS3には処理済みのHID Reportが届くものとする。

起動時RGB LEDはOryxでOFFまたは低輝度に設定し、USB給電負荷を抑える。

---

# 3. システム構成

```text
                         ┌─ Slot 1: Mac mini
ZSA Voyager              ├─ Slot 2: Linux / Omarchy
    │ USB-C              └─ Slot 3: MacBook Air
    ▼                          ▲
┌──────────────────────┐       │ BLE HID
│ Stack-chan / CoreS3 │────────┘
│                      │
│ USB Host             │
│ HID Parser           │
│ Input Router         │
│ BLE HID              │
│ UI / Servo           │
└──────────────────────┘
           ▲
           │ M5-Bus 5V
           │
     Stack-chan台座
           ▲
           │
        外部電源
```

---

# 4. 設計方針

- 据え置き・常時給電を前提とする
- バッテリー駆動は初期要件から外す
- 省電力最適化は初期要件から外す
- 最優先は `Voyager → CoreS3 → BLE → Mac` の安定動作
- USB/BLE/RouterとStack-chan UIを分離する
- 将来的に別ESP32-S3ハードへ移植できる構造にする
- 入力処理はUI・サーボ・ネットワーク処理より常に優先する
- 機能追加より入力デバイスとしての信頼性を優先する

---

# 5. 電源設計

## 5.1 給電経路

```text
外部電源
   ↓
Stack-chan台座
   ↓
M5-Bus 5V
   ↓
CoreS3
   ↓
USB VBUS
   ↓
Voyager
```

CoreS3 USB-CはVoyagerとのデータ通信とVoyagerへのVBUS供給だけに使用する。

---

## 5.2 USB VBUS

起動時に以下を使用する。

```cpp
M5.Power.setUsbOutput(true);
```

実際のM5UnifiedバージョンでAPI仕様と対応ハードを確認する。

Phase 0で以下を確認する。

- Stack-chan台座から給電中でもVBUSを有効化できる
- 無負荷で約5V出力される
- Voyager接続後もUSB規格下限を下回らない
- Voyagerが安定して起動する

M5Unified側の条件判定によってVBUSが有効にならない場合は、

- M5Unifiedの実装確認
- 必要ならIO Expanderの `USB_OTG_EN` 制御

を検討する。

---

## 5.3 サーボ電源との干渉

Stack-chanのサーボは起動・加速・保持時に瞬間的な電流を消費する。

VoyagerのUSB VBUSと同じ5V系統を使用する場合、

- VBUS電圧低下
- Voyagerリセット
- USB再Enumeration
- キー抜け
- Stuck Key

が発生する可能性がある。

Phase 0でサーボ負荷試験を必須とする。

問題発生時の対策候補：

- Voyager VBUS側へ100µF以上のバルクコンデンサ追加
- サーボ電源分離
- 外部5V電源容量増加
- サーボ加減速を緩やかにする
- 入力中はサーボを動かさない
- 接続先切替後のアイドル時のみサーボを動かす

---

# 6. 開発環境

## 6.1 基本構成

第一候補：

```text
ESP-IDF v5.x
+
Arduino as ESP-IDF component
```

PlatformIOを使用する場合は、

```ini
framework = arduino, espidf
```

相当の構成を候補とする。

Phase 0で互換性のあるバージョンを固定する。

---

## 6.2 USB Host

USB Host実装は以下に統一する。

```text
ESP-IDF usb_host
+
espressif/usb_host_hid
```

使用機能：

- USB Host Library
- Device Enumeration
- HID Interface
- HID Report Descriptor
- SET_PROTOCOL
- SET_REPORT
- Interrupt IN
- Device disconnect handling

使用しない：

- Arduino USBHost
- Arduino TinyUSB Host API
- Arduino `USBHIDKeyboard`

### Arduino USB Device側の初期化禁止

Arduino Coreが起動時にUSB Device側スタックを初期化し、USB OTG PHYを確保しないようにする。

使用するarduino-esp32のバージョンに応じて設定を確認し、少なくとも以下相当を満たす。

```text
ARDUINO_USB_CDC_ON_BOOT = 0
```

必要に応じてUSB Mode関連設定も明示的に無効化する。

P0-04では、

- Arduino初期化後にUSB Device側スタックが起動していない
- `usb_host` が正常にHost PHYを確保できる

ことを確認する。

---

## 6.3 BLE

NimBLEを使用する。

---

## 6.4 Stack-chan UI

- LCD / 電源：M5Unified
- 顔：m5stack-avatar
- サーボ：Stack-chan構成に合わせて実装

公式Stack-chan Moddable SDK版ファームウェアは使用しない。

---

# 7. デバッグ・ログ

USB OTGとUSB Serial/JTAGは同じUSB PHYを共有する。

CoreS3 USB-CをUSB Hostとして使用中はUSB CDC/JTAGへ依存しない。

優先順位：

1. LCD
2. UART
3. BLE Debug Service
4. Wi-Fi Log

UART使用時はサーボとGPIO/Portが競合しないことを確認する。

ファームウェア更新時はVoyagerを外し、USB-Cを開発PCへ接続する。

---

# 8. ソフトウェア構成

## 8.1 データフロー

```text
Voyager
   ↓
USB Host
   ↓
HID Parser
   ↓
Input Router
   ↓
BLE HID
   ↓
Active Host
```

UIは入力経路から分離する。

---

## 8.2 FreeRTOSタスク

| タスク | 役割 | 優先度 | Core目安 |
|---|---|---:|---:|
| usb_host_task | USB列挙、HID、SET_REPORT | 高 | 1 |
| input_router_task | 入力状態管理、切替 | 高 | 1 |
| NimBLE Host | BLE HID | 高 | 0 |
| ui_task | LCD / Avatar | 低 | 0 |
| servo_task | サーボ制御 | 低 | 0 |
| network_task | 将来のWi-Fi機能 | 最低 | 0 |

入力系処理をUIがブロックしてはならない。

---

## 8.3 タスク間通信

Queue / Event Groupを基本とする。

```text
usb_host_task
    ↓ Queue
input_router_task
    ↓ Queue
ble_task

input_router_task
    ↓ Event
ui_task
```

---

## 8.4 ディレクトリ

```text
src/
├── usb/
│   ├── usb_host.cpp
│   ├── hid_keyboard.cpp
│   ├── hid_consumer.cpp
│   └── hid_mouse.cpp
├── ble/
│   ├── ble_hid.cpp
│   ├── bonding.cpp
│   ├── identity.cpp
│   └── host_slots.cpp
├── router/
│   ├── input_router.cpp
│   ├── special_keys.cpp
│   └── release_all.cpp
├── ui/
│   ├── face.cpp
│   ├── display.cpp
│   └── servo.cpp
├── system/
│   ├── power.cpp
│   ├── config.cpp
│   └── logger.cpp
└── main.cpp
```

---

# 9. Router仕様

Routerは入力整合性を管理する中心コンポーネントとする。

## 9.1 基本動作

通常のUSB HID ReportをBLE側へ転送する。

BLE Host未接続中の入力Reportは破棄する。

再接続後に過去の入力を送信しない。

---

## 9.2 切替キー

| Key | HID Usage | Destination |
|---|---:|---|
| F21 | 0x70 | Slot 1 |
| F22 | 0x71 | Slot 2 |
| F23 | 0x72 | Slot 3 |

F21〜F23はホストへ送信しない。

---

## 9.3 切替キー除去

Report全体を破棄しない。

対象キーだけを除去し、残りを前詰めする。

例：

```text
Before:
Ctrl + F22 + C

After:
Ctrl + C
```

---

## 9.4 Edge Detection

切替はF21〜F23の立ち上がり時に一度だけ発火する。

保持中に繰り返し発火しない。

---

## 9.5 All Keys Released

以下でRelease Reportを送信する。

| Event | Destination |
|---|---|
| Host Slot切替 | 旧Host |
| Voyager USB切断 | 現在Host |
| USB Host Reset | 現在Host |
| Router Reset | 現在Host |
| BLE再接続直後 | 再接続Host |

対象：

- Keyboard
- Consumer Control
- Mouse Button

---

## 9.6 Host切替手順

1. 旧HostへAll Released送信
2. 切替キーをReportから除去
3. Active Slot変更
4. BLE接続先切替開始
5. Voyagerの全物理キーが解放されるまで新Hostへの転送を保留
6. BLE接続成立
7. 新HostへAll Released送信
8. 通常入力再開
9. UIへ切替完了通知
10. 必要ならサーボ移動

---

## 9.7 切替先が接続不能な場合

切替先PCが、

- 電源OFF
- Sleep
- BLE無効
- 圏外

などで接続できない場合を明示的に扱う。

### 旧Host

Host切替開始時に旧Hostとは切断する。

特にBLE Identityを変更する方式では切断を前提とする。

### タイムアウト

接続試行には有限のタイムアウトを設定する。

初期値の目安：

```text
5秒
```

実測後に調整する。

### タイムアウト後

状態を、

```text
ACTIVE_SLOT = Slot N
CONNECTION_STATE = Disconnected
```

とする。

LCD・表情に未接続を表示する。

例：

```text
Linux
Not connected
```

### 未接続中の入力

通常入力は9.1に従い破棄する。

ただしF21/F22/F23は引き続きRouterが検出する。

したがって、切替先が見つからなくてもVoyagerから別Slotへ切り替え可能でなければならない。

未接続状態によって操作不能なデッドロックを作らない。

---

# 10. USB HID対応

## Phase 0

Boot Keyboard Reportを取得できることまで確認する。

## Phase 3以降

Report Descriptorを解析し、

- Consumer Control
- Mouse
- System Control

へ対応する。

将来的に必要性が確認された場合のみNKROへ対応する。

Raw HIDは対象外。

---

# 11. BLE HID

## 11.1 HID Report

最終：

- Keyboard 6KRO
- Consumer Control
- Mouse

PoCではKeyboardのみ。

---

## 11.2 Host Slot

3スロットを持つ。

```text
Slot 1 = Mac mini
Slot 2 = Linux / Omarchy
Slot 3 = MacBook Air
```

各Slot：

- Bonding情報
- Host名
- LED状態
- Identity情報
- 最終接続情報

を管理する。

---

## 11.3 BLE Identity比較

### 共通条件

案A/Bを比較する際はBLE Privacy / RPAをOFFにし、条件を揃える。

Phase 5AでIdentity方式を確定する。

### 案A：SlotごとにIdentity

```text
Slot 1 → Static Random Address A
Slot 2 → Static Random Address B
Slot 3 → Static Random Address C
```

各Slotで独立Bondingを持つことを目標とする。

### 案B：単一Identity + 複数Bonding

単一BLE Identityを使用する。

ただし全Bonding済みHostから同時再接続されないよう、アクティブSlotに応じて接続対象を制限する。

第一候補：

- Filter Accept ListへアクティブHostだけを登録

代替：

- アクティブHostへのDirected Advertising

アクティブでないHostからの自動接続を許可しない。

### 比較項目

- macOS再接続安定性
- Linux再接続安定性
- 切替速度
- Bonding管理
- 再起動後復元
- Host間競合
- NimBLE実装の複雑さ

結果でV1方式を決定する。

---

## 11.4 Bonding

NVSへ保存する。

再起動後も保持する。

Active Slotも保存する。

---

## 11.5 Pairing

PoC：

```text
Just Works
```

V1以降：

```text
DisplayOnly
```

を検討する。

---

# 12. 双方向HID

BLE HostからOutput Reportを受信したらUSB側へSET_REPORTする。

対象：

- Caps Lock
- Num Lock
- Scroll Lock

Slot切替後は切替先HostのLED状態をVoyagerへ反映する。

---

# 13. UI

## LCD

- USB状態
- BLE状態
- Active Slot
- Host名
- Pairing
- Error
- Debug Log

## 表情

| 状態 | 表情 |
|---|---|
| Connected | 通常 |
| Connecting | 探している |
| Disconnected | 困り顔 |
| USB disconnected | 困り顔 |
| Error | 驚き |

## サーボ

```text
左   = Mac mini
中央 = Linux
右   = MacBook Air
```

電源試験結果に応じて制限する。

---

# 14. 電源・省電力ポリシー

据え置き版ではVoyager VBUSを常時ON。

BLE切断によるVBUS OFFは禁止。

USB SuspendはRemote Wakeup検証まで実装しない。

---

# 15. BLE化による制限

- Oryx / KeymappはPCへ直接USB接続
- QMK OS Detectionは期待しない
- 初期版は6KRO
- FileVault / GRUB / LUKS / BIOS等ではBLE利用不可の可能性あり

Voyager用USBケーブルは残しておく。

---

# 16. 性能・安定性要件

V1：

- 1時間連続入力：キー抜け0
- Stuck Key：0
- Host切替100回：Stuck Key 0
- Voyager抜き差し後：自動復帰
- Stack-chan再起動後：Bonding維持
- Active Slot復元
- Mac Sleep/Wake後：自動再接続
- Servo動作中：USB再Enumeration 0
- Servo動作中：キー抜け0
- 接続不能なSlotへ切替後も別Slotへ切替可能

---

# 17. 実装フェーズ

| Phase | 内容 | 成功条件 |
|---|---|---|
| 0 | Hardware / Framework / USB検証 | 18章Go条件 |
| 1 | Router単体実装・テスト | 9章のロジックテスト合格 |
| 2 | BLE Keyboard 1 Host | Hello World入力 |
| 3 | Consumer / Mouse | Volume / Mouse Keys |
| 4 | 双方向HID | Caps Lock反映 |
| 5A | BLE Identity PoC | Identity方式確定 |
| 5B | 3 Host Switching | F21〜F23切替 |
| 6 | Stack-chan UI | Face / LCD / Servo |

---

# 18. Phase 0

## 0-1 Voyager USB調査

```bash
lsusb
lsusb -v -d <VID>:<PID>
sudo usbhid-dump -d <VID>:<PID>
```

記録：

- VID/PID
- Interface
- Class
- SubClass
- Protocol
- Endpoint
- Boot Keyboard
- MaxPower
- Report Descriptor

---

## 0-2 Voyager消費電力

- LED OFF
- 通常
- 最大輝度

---

## 0-3 Stack-chan Hardware Inventory

確認：

- CoreS3
- 台座型番
- Servo型式
- Servo接続
- M5-Bus給電経路
- UART
- 外部電源

---

## 0-4 Framework Integration

以下を含む最小Firmwareを作る。

- ESP-IDF
- Arduino component
- M5Unified
- m5stack-avatar
- Servo
- NimBLE
- usb_host
- usb_host_hid

確認：

- Build成功
- Avatar表示
- Servo動作
- NimBLE初期化
- Arduino USB Device側スタックが起動していない
- `ARDUINO_USB_CDC_ON_BOOT=0` 相当が有効
- `usb_host` がOTG PHYを正常に確保できる

---

## 0-5 USB VBUS

1. 台座給電
2. `setUsbOutput(true)`
3. VBUS測定
4. Voyager接続
5. Voyager起動
6. 負荷時VBUS測定

目安：

```text
4.75V以上
```

---

## 0-6 USB Enumeration / Keyboard Input

ESP-IDF USB HostからVoyagerを列挙する。

LCDへ、

- VID/PID
- Interface
- Endpoint
- HID情報

を表示する。

Boot Keyboard InterfaceへSET_PROTOCOL(Boot)を送り、キー入力を取得してLCDへ表示する。

成功条件：

```text
Voyagerで押したキーをCoreS3が取得できる
```

---

## 0-7 Servo Power Stress

- Servo全可動域を5分往復
- Voyager連続入力
- Disconnect回数
- Enumeration回数
- Key Event欠落
- Brownout

Go：

```text
USB再Enumeration = 0
キー抜け = 0
Brownout = 0
```

---

# 19. Phase 1：Router単体実装

RouterはESP32実機に依存しない純粋ロジックとして可能な限り分離する。

PC上でユニットテスト可能な構造にする。

## テスト項目

### 特殊キー除去

```text
[A, F22, C]
→
[A, C]
```

### Edge Detection

```text
F22 Down → Switch 1回
F22 Hold → Switchなし
F22 Up   → Switchなし
```

### Modifier併用

```text
Ctrl + F22 + C
→
Ctrl + C
```

### All Released

Host切替時に、

```text
Keyboard = neutral
Consumer = neutral
Mouse = neutral
```

となる。

### 切替保留

F22押下後、

```text
Ctrlが物理的に押されたまま
→
新Hostへの転送開始禁止
```

全キー解放後に転送開始する。

### 未接続状態

接続不能Slotへ切替後：

- 通常キーは破棄
- F21/F22/F23は認識
- 別Slotへ移動可能

Phase 1ではBLE実装を行わない。

---

# 20. Phase 0 実装タスク

- P0-01 Hardware Inventory
- P0-02 Voyager Descriptor Capture
- P0-03 CoreS3 USB VBUS Test
- P0-04 Framework Integration Test
- P0-05 USB HID Enumeration
- P0-06 Keyboard Report Test
- P0-07 Servo + USB Stress Test

P0-01〜07完了後、Phase 1へ進む。

---

# 21. 将来機能

- PC死活監視
- OpenClaw通知
- LLM Task状態
- GitHub / CI
- Home Server
- Backup
- Pomodoro
- TRPG Session Timer
- Idle Animation

---

# 22. Wi-Fi利用時の制約

BLEとWi-Fiは2.4GHz無線を共有する。

方針：

- 低頻度Polling
- Push/Event中心
- 常時Streaming禁止
- Network Taskは最低優先度
- Wi-Fi導入前後で入力品質比較

入力品質を最優先する。

---

# 23. 非目標

Phase 0〜2では以下を実装しない。

- 3 Host Switching
- Consumer Control
- Mouse
- NKRO
- Raw HID
- Servo UI演出
- PC Monitoring
- OpenClaw Integration
- Wi-Fi
- Battery
- OTA
- Power Optimization
- USB Suspend

---

# 24. 未決事項

| 項目 | 確定方法 |
|---|---|
| Stack-chan台座5V経路 | P0-01 |
| Servo型式 / Port | P0-01 |
| UART Log Port | P0-01 |
| VBUS出力条件 | P0-03 |
| ESP-IDF / Arduino version | P0-04 |
| Arduino USB無効化設定 | P0-04 |
| Servo電源干渉 | P0-07 |
| BLE Identity方式 | Phase 5A |
| Pairing方式 | PoC=Just Works |
| Servo動作制限 | P0-07 |
| BLE Output Report互換性 | Phase 4 |
| Host切替timeout | Phase 5Bで実測調整 |

---

# 25. Go / No-Go

Stack-chan/CoreS3正式採用条件：

- USB VBUS安定
- Voyager列挙成功
- Keyboard HID取得成功
- Servo動作中もUSB切断なし
- Avatar / Servo / USB Host / NimBLE同居可能
- Arduino USB Device初期化との競合なし
- USB Host中にもデバッグ経路あり

満たした場合、Stack-chan/CoreS3で継続する。

---

# 26. 最終コンセプト

Stack-chanを、

> Voyagerと複数PCの間に存在する、机上常設の入力ハブ兼デスクトップマスコット

として設計する。

V1では入力ハブとしての信頼性を完成させる。

その上で、

- 接続先
- PC稼働状態
- AI処理状態
- システム異常

をLCD・表情・首の動きで伝える機能を追加する。

入力デバイスとしての信頼性を最優先し、その上にマスコット機能を積み重ねる。
