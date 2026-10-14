# 開発・検証

利用開始は[README](../README.md)、環境構築と書き込みは[セットアップ](setup.md)を参照してください。

## 構成

ESP-IDF 5.5.1、M5Unified 0.2.23（ESP-IDFモード）、NimBLE / esp_hid、
usb_host_hid 1.2.1、M5公式StackChan BSP 1.1.0を使用します。
依存は [main/idf_component.yml](../main/idf_component.yml) と
[dependencies.lock](../dependencies.lock) を参照してください。Avatarはビルド対象外です。

| 場所 | 責務 |
|---|---|
| `main/main.cpp` | USB Host、Router task、UI taskの統合と給電管理 |
| `main/ble_keyboard.*` / `peer_slots.h` | BLE HID、接続先・Bond・ペアリング管理 |
| `main/ui_model.h` / `ui_view.cpp` | 画面・タッチの純粋ロジックと描画 |
| `main/led_model.h` | 左右LEDの表示判定 |
| `main/dev_link.*` | Wi-Fi設定、OTA、状態・画面取得、遠隔タップ |
| `components/input_router/` | 切替と入力の整合性・全解放 |
| `components/hid_descriptor/` | HID記述子・入力Report解析 |
| `components/touchpad/` | 絶対座標からマウス・ジェスチャーへの変換 |
| `components/stackchan_bsp/` | 台座・サーボ・LED・INA226等のBSP |

USB入力処理はUIから分離し、M5の描画・I²C操作はUI taskに集約します。
単一BLE Identityを使い、選択中Slotの登録済みPCに接続を制限します。
UIはBLEの非同期要求・完了通知とSlotSnapshotを使います。
BSPのmotion taskはUSB入力を優先するため低優先度にしています。

## ホストテスト

リポジトリのルートで実行します。C++17のコンパイラとASan/UBSanが使える環境が必要です。

```sh
sh components/input_router/test_host/run.sh
sh components/hid_descriptor/test_host/run.sh
sh components/touchpad/test_host/run.sh
sh main/test_host/run.sh
sh main/test_host_ui/run.sh
git diff --check
```

各テストは `-Wall -Wextra -Werror` とASan/UBSanを使います。
BLEテストはSDK呼び出しを置き換えて本実装を実行します。
詳細は [BLE host testのREADME](../main/test_host/README.md) を参照してください。
実際のBLEタイミング・物理タッチ・配線はホストテストでは検証しません。

## ログと画面取得

USB Host使用中は本体USB-CのSerial/JTAGを同時利用できません。
通常の診断はLCDの診断画面とWi-Fi経由で行います。

```sh
sh tools/log.sh
curl --fail --max-time 10 http://stackchan-hub.local/status
sh tools/screenshot.sh stackchan-hub.local /tmp/stackchan-screen.png
sh tools/tap.sh 160 100
```

- `log.sh`：UDP 5140のログを受信。Ctrl+Cで終了する。
- `/status`：稼働区画・ビルド日時・空きメモリ・USB/Router/BLEのカウンタ・電源状態。
- `screenshot.sh`：LCDキャンバスをPNGへ保存。Python3とmacOSの `sips` を使用する。
- `tap.sh`：実機への遠隔タップ。`.ota-token` が必要で、PCの切替等も実行される。

IP指定やプロキシ設定は[セットアップ](setup.md#4-wi-fiで更新する)を参照してください。
キャンバス取得成功は指タッチや物理LCDの見え方の確認にはなりません。

## ジェスチャー設定

[touchpad_gestures.h](../components/touchpad/include/touchpad_gestures.h) の `Config` に定義しています。
現在のUIにこれらを変更する設定画面はありません。

| 項目 | 既定値 | 用途 |
|---|---|---|
| `drag_window_ms` | 200ms | タップからドラッグ開始までの窓 |
| `drag_slop_mm` | 4mm | 開始時の再着地位置の許容距離 |
| `redrag_ms` | `kRedragNoLimit` | 0=なし、有限値=その時間、`UINT32_MAX`=期限なし |
| `redrag_edge_mm` | 0 | 0以下=離す位置によらず保持、正値=端付近だけ保持 |
| `lift_timeout_ms` | 150ms | フレーム欠落を指リフトとみなす時間 |

座標上限はHID記述子からFrameへ渡します。端判定を使う場合、どちらかの上限が不明なら常に保持します。
上端に届く座標範囲はまだ実機確認が必要です。

## 調査・検証の記録

- [開発・実機検証の記録](development-log.md)：旧READMEを移した時系列資料。途中の失敗・保留・修正後の結果を保存。
- [USB記述子](voyager-descriptors.md)：Voyager直結時に採取した記述子。

現行の利用手順はREADME・セットアップ・操作ガイドを優先してください。
初期仕様・当時の作業計画・旧診断コードはGit履歴から参照できます。
