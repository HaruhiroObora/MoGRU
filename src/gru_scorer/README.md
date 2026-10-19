# Mozc GRU 言語モデル統合モジュール (`gru_scorer`)

このディレクトリは、Mozc のかな漢字変換エンジンに対して文字単位 GRU 言語モデルを用いたリランキングを行うための推論エンジンおよび関連コンポーネントを含んでいます。

詳細なアーキテクチャやビルド・デプロイ手順、既知の注意点については、Antigravity アーティファクトの [handoff_mozc_gru.md](file:///home/haruhiro/.gemini/antigravity/brain/96e03b9b-1ff8-4e36-98f6-e54c8da6c7a3/handoff_mozc_gru.md) を参照してください。

## ファイル構成
- `charindex.hh` / `charindex.cc`: 4バイト UTF-8 対応文字インデックス・絵文字判定
- `gru_model.hh` / `gru_model.cc`: GRU 言語モデル順伝播推論・スコアリング・`<unk>` ペナルティ
- `math_ops.hh`: 行列演算・活性化関数
- `dummy_model.bin`: テスト用モデル重み
- `gru_scorer_test.cc`: 単体テスト
- `BUILD.bazel`: Bazel ビルド定義

## 主要コマンド（`mozc/src` 直下で実行）
```bash
# テスト
bazel test //gru_scorer:gru_scorer_test
bazel test //rewriter:gru_rewriter_test

# 最適化ビルド
bazel build -c opt //unix/fcitx5:fcitx5-mozc-gru.so --//unix/fcitx5:use_server=False

# インストールと再起動
mkdir -p ~/.local/lib/fcitx5
cp bazel-bin/unix/fcitx5/fcitx5-mozc-gru.so ~/.local/lib/fcitx5/
chmod 755 ~/.local/lib/fcitx5/fcitx5-mozc-gru.so
FCITX_ADDON_DIRS="/home/haruhiro/.local/lib/fcitx5:/usr/lib/x86_64-linux-gnu/fcitx5" fcitx5 -r -d
```
