# Phase 0 レビューと切り分け計画（Claude Code, 2026-09-29）

## 指摘

1. **診断コード自体が原因になりうる**（`main/main.cpp:193-216`）
   - `M5.begin()` を app_main で呼び、`M5StackChan.begin()` 内でも再度呼んでいる（二重初期化）。
   - `Wire1.begin(12, 11)` の強制と `M5.In_I2C.begin()` の再初期化。Arduino環境のM5GFXは
     Port1を `Wire1` 経由で扱う（`m5gfx common.inl:1840`）ため、同一ポートを別経路で開き直している。
2. **動作する公式実装と構成が違う**
   - 公式（`/private/tmp/m5stack-stackchan-official/firmware/main/hal/board/stackchan.cc`）は
     Arduinoなしの純ESP-IDF。`i2c_new_master_bus`（Port1, SDA12/SCL11）→ AXP2101 → AW9523 の順。
   - 本プロジェクトは Arduino Core + M5Unified(TwoWire) + BSP を重ね、`initArduino()` も呼ばない。
     要素が多すぎて切り分けできていない。
3. **`initArduino()` を避ける根拠が弱い**
   - NVS消去は `NO_FREE_PAGES`/`NEW_VERSION_FOUND` 時のみで、その場合は現コードも停止する。
   - BLEメモリ解放は `btInUse()` を上書き（`bool btInUse(){ return true; }`）すれば回避できる。
4. **USB Host部（未実機検証）**
   - `M5.Display` へ hid_events(core1)・display(core0)・app_main・Avatarタスクが無排他で描画。
     描画は1タスクに集約し、他はQueueでメッセージを送るだけにする。
   - 最後の `avatar.init()` が直前の「Waiting for Voyager」を即上書きする。

## 切り分け計画（1段階ずつ追加し、各段階で実機確認）

| 段階 | 内容 | 合格条件 |
|---|---|---|
| A | 診断コードを除去し、`M5.begin()` 1回＋文字表示のみ（BSP/NimBLE/USBなし） | LCD表示、0x34/0x58応答 |
| A' | Aが駄目なら純IDF `i2c_master` で 0x34/0x58 をprobeする最小コード | 応答すればArduino/M5GFXのI²C経路が原因と確定 |
| B | `M5StackChan.begin()` のみ（`M5.begin()` 二重呼び出し廃止） | IO Expander(0x6F)とLED動作 |
| C | `M5.Power.setUsbOutput(true)` とテスターでVBUS測定 | 5V出力 |
| D | NimBLE追加（`initArduino()`＋`btInUse()`上書き方式を再検討） | sync確認 |
| E | USB Host/HID追加。描画は単一タスクに集約 | Voyager VID/PIDとキー入力表示 |
| F | サーボ動作中のUSB入力ストレス（P0-07） | 欠落・リセットなし |

A'で「純IDFは動き、Arduino経由は動かない」と確定した場合は、Arduinoを外し
M5Unified(IDFモード)＋自前サーボUARTへ移行することを推奨。
