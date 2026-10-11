# Phase 1 タスク: Input Router 実装とホスト上ユニットテスト（Codex担当）

依頼者: Claude Code（レビュー担当）。仕様: [spec-v0.4.md](../spec-v0.4.md) 9章・19章。

## 作業範囲（厳守）

- 触ってよいのは `components/input_router/` 配下のみ。
- `components/input_router/include/input_router.h` は**インターフェース契約**。変更しない。
  仕様上どうしても変更が必要なら、変更せずに理由を返答すること。
- `main/`、`README.md`、ルートの `CMakeLists.txt`、`sdkconfig*`、`build/` は触らない。
- `idf.py`、`esptool`、シリアルポートは使わない（実機はClaudeが使用中）。

## 作るもの

1. `components/input_router/src/input_router.cpp` — ヘッダのコメント通りに実装。
   ESP-IDF/FreeRTOS/ヒープ/例外/iostreamに依存しない。C++17。
2. `components/input_router/CMakeLists.txt` —
   `idf_component_register(SRCS "src/input_router.cpp" INCLUDE_DIRS "include")`
3. `components/input_router/test_host/router_test.cpp` — 外部ライブラリなしの
   最小テストハーネス（失敗時にテスト名と行番号を出し、終了コード非0）。
4. `components/input_router/test_host/run.sh` —
   `c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined` でビルドして実行。

## 必須テスト（spec 19章 + ヘッダ契約）

- 特殊キー除去: `[A, F22, C]` → `[A, C, 0, 0, 0, 0]`
- Modifier併用: `Ctrl + F22 + C` → 送信時は `Ctrl + C`
- Edge: F22 Down→Switch 1回、Hold→なし、Up→なし、再Down→再度Switch
- 切替時の出力順: 旧Slot接続中なら `kSendAllReleased(旧)` → `kStartConnect(新)` → `kNotifyUi`
- 旧Slot未接続なら `kSendAllReleased` を出さない
- 切替保留: F22押下後、Ctrl保持のままBLE接続成立 → 転送しない。全解放Report後から転送再開
- `OnBleConnected(active)` で `kSendAllReleased(active)` と `kNotifyUi`、状態 `kConnected`
- 非アクティブSlotの `OnBleConnected` / `OnBleDisconnected` / `OnConnectTimeout` は無視（出力なし）
- 未接続中: 通常キーは出力なし、F21/F23は検出され別Slotへ切替できる
- `OnConnectTimeout(active)` で `kDisconnected` + `kNotifyUi`、その後も切替可能
- 接続済みの同一Slotを再押下 → 出力なし。未接続の同一Slotを押下 → `kStartConnect` 再発行
- `OnUsbDisconnected` / `Reset`: 接続中なら `kSendAllReleased(active)`。押下中切替キーの記憶と保留を解除
- 複数の切替キーが同時に立ち上がったら `keys` 順で最初の1つだけ
- 1イベントの出力が `Actions::kCapacity` を超えない

## 完了時の返答

- 作成ファイル一覧
- `run.sh` の実行結果（全テスト名とPASS/FAIL）
- ヘッダ契約に曖昧さ・矛盾を感じた点（あれば）
