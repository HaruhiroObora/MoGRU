[MoGRU - Neural Language Model Integrated Mozc](https://github.com/lambda-haru/MoGRU)
========================================================================

本リポジトリは、オープンソースかな漢字変換エンジン **Mozc** に、**文字単位 GRU ニューラル言語モデル（GRU Scorer）** を統合した拡張版 **MoGRU** です。

### 主な特徴
- **macOSライクなライブ変換**: 入力と同時にリアルタイムでインライン自動変換。スペースキーを押す手間なく、流れるようなタイピング体験を実現。
- **大域的ビームサーチ ($B=8$)**: 複数文節を跨いだ文脈をGRUニューラル言語モデルで評価し、文脈に応じた適切な同音異義語を自動リランキング。
- **インプロセス・オンライン継続学習**: 変換確定履歴からリアルタイム（1ms未満）にGRU言語モデルを自動微調整。ユーザー固有の言い回しや選好に即座に適応し、`~/.config/mogru/grumodel_user.bin` へ自動永続化。
- **独立動作（共存対応）**: システム標準の `mozc` と競合せず、Fcitx5 上で `MoGRU` として独立して登録・利用可能。
- **GUI設定対応**: Fcitx5 設定画面からライブ変換のON/OFF、モデルパス、文脈重み係数 $\alpha$（デフォルト 800）をリアルタイムに調整可能。
- **マルチプラットフォーム**: Linux (Fcitx5), macOS, Windows をサポート。

---

## ✨ リアルタイム・ライブ変換機能

macOS 標準日本語入力でおなじみの「**ライブ変換**」を MoGRU にネイティブ統合しました。

- **思考を止めない流れるような入力**: 
  文字を打つそばから、リアルタイムでインラインの漢字かな交じり文へと自動変換。従来の「スペースキーを押して変換」というステップから解放され、思考のスピードで文章を執筆できます。
- **GRU ニューラル言語モデルとの相乗効果**:
  ライブ変換中もバックグラウンドで文字単位 GRU 言語モデル（大域的ビームサーチ $B=8$）が前後の文脈を瞬時に評価・リランキング。同音異義語が多い日本語でも、文脈に即した自然な変換候補がリアルタイムに提示されます。
- **シームレスな手動修正**:
  もし意図と異なる候補になった場合でも、通常通りスペースキーを押すだけで即座に候補ウィンドウが開き、矢印キーや数字キーで別の候補を選択できます。
- **設定で柔軟に切り替え**:
  Fcitx5 等の設定画面から `EnableLiveConversion` のトグルで簡単に ON/OFF を切り替えられます（デフォルト: 有効）。

---

## 🧠 入力履歴を用いたオンライン継続学習（追加学習）機能

確定した入力履歴（前文脈＋確定文）を用いて、C++ 推論エンジン内で GRU 言語モデルをリアルタイムに微調整する **インプロセス・オンライン継続学習機能** を搭載しています。

- **超高速なリアルタイム適応（レイテンシ < 1ms）**:
  文字単位 GRU モデル（$V=4096, H=128$、約4.6MB）の軽量構造と AVX2 最適化により、文字入力や確定の体感速度を一切損なわずにその場で重みを微調整します。
- **忘却防止（L2 アンカー正則化）**:
  更新時にベースモデル（`grumodel.bin`）からの乖離を抑える正則化項 $\lambda (W - W_{\text{base}})$ を適用。使い続けても一般的な日本語の基礎的な文脈理解を崩すことなく、個人の表現や選好のみに自然に適応します。
- **ベースモデルを汚さない安全なユーザー重み管理**:
  オリジナルのベースモデル（`grumodel.bin`）はそのまま保持し、学習済みモデルは `~/.config/mogru/grumodel_user.bin` へアトミックに自動保存・再読み込みされます。
- **ベースモデル切り替え時の自動保護（フィンガープリント照合）**:
  Fcitx5 設定画面などから別のベースモデルに切り替えた場合、モデルヘッダ内の 64bit ハッシュ値を照合。異なるベースモデルに由来する過去のユーザー重みが新しいベースモデルを誤って上書きすることを自動防止し、常に安全に意図通りの挙動を維持します。
- **柔軟な設定カスタマイズ（ホットリロード対応）**:
  `~/.config/mogru/gru_config.ini` を編集・保存するだけで、再起動なしで学習の挙動を即座に調整できます：
  ```ini
  # オンライン学習の有効化
  enable_online_learning = true

  # 更新対象: output_only (出力層のみ更新・高安定) または full (全層BPTT更新・高適応)
  online_learning_mode = output_only

  # 学習率
  online_learning_rate = 0.005

  # ベースモデルへのL2アンカー正則化係数
  online_learning_weight_decay = 0.001

  # 自動保存間隔（確定回数）
  online_learning_save_interval = 5
  ```
- **プライバシー & ロールバック対応**:
  - シークレットモード（`incognito_mode`）や学習無効属性付き候補は自動で学習対象から除外。
  - 直前の変換確定を取り消す操作（`Revert`）時には、直前の学習ステップも正確にロールバック。
  - 設定ツール等からの「履歴の消去（`Clear`）」実行時には、ユーザーモデル（`grumodel_user.bin`）が自動削除され、ベースモデルの初期状態にリセットされます。

---

## クイックスタート（各OSでのビルド & インストール）

リポジトリをクローンした後、お使いのOSに応じたスクリプトを実行するだけでビルド・インストールが完了します。
モデル重みファイル（`grumodel.bin`, `chardata.bin`）も自動配置されます。

### 🐧 Linux (Fcitx5)
```bash
git clone https://github.com/lambda-haru/MoGRU.git
cd MoGRU
./install.sh
# または: ./scripts/install_linux.sh
```
> [!NOTE]
> ソースからビルドする場合、Fcitx5 開発ヘッダーおよび GUI 設定ツール用の Qt6（`qt6-base-dev` 等）が必要ですが、未インストールの場合はスクリプト初回実行時に自動でインストール案内が表示されます。

> [!TIP]
> **CIビルド生成物（Artifacts）を利用する場合（Bazel不要・数秒で完了）**:
> GitHub Actions の `CI for Linux` の実行画面（Artifacts）から `MoGRU-linux.zip` をダウンロードし、引数に指定して実行できます（Fcitx5プラグイン、MoGRU専用GUI設定ツール `mozc_tool`、全アイコンアセット、モデル重み、デスクトップ設定が自動配置され、システムに標準Mozcパッケージがなくてもスタンドアロンで完全動作します）：
> ```bash
> ./install.sh /path/to/MoGRU-linux.zip
> # または: ./scripts/install_linux.sh /path/to/MoGRU-linux.zip
> ```

### 🍎 macOS
```bash
git clone https://github.com/lambda-haru/MoGRU.git
cd MoGRU
./install.sh
# または: ./scripts/install_macos.sh
```
> [!TIP]
> **CIビルド生成物（Artifacts）を利用する場合（Xcode・CMake・Qt・Bazelビルド不要）**:
> GitHub Actions の `CI for macOS` の実行画面（Artifacts）からパッケージ（`MoGRU_arm64.pkg` 等、展開後の `.pkg` または `.zip` のままでも可）をダウンロードし、引数に指定して実行できます（GRUモデル重みと設定ファイルも自動配置されます）：
> ```bash
> ./install.sh /path/to/MoGRU_arm64.pkg
> # または: ./scripts/install_macos.sh /path/to/MoGRU_arm64.pkg.zip
> ```


### 🪟 Windows (PowerShell)
```powershell
git clone https://github.com/lambda-haru/MoGRU.git
cd MoGRU
Set-ExecutionPolicy Bypass -Scope Process -Force
.\scripts\install_windows.ps1
```
> [!TIP]
> **CIビルド生成物（Artifacts）を利用する場合（Visual Studio・Bazelビルド不要）**:
> GitHub Actions の `CI for Windows` の実行画面（Artifacts）からインストーラ（`MoGRU64_x64.msi` 等、展開後の `.msi` または `.zip` のままでも可）をダウンロードし、引数に指定して実行できます（GRUモデル重みと設定ファイルも自動配置されます）：
> ```powershell
> .\scripts\install_windows.ps1 C:\path\to\MoGRU64_x64.msi
> # Git Bash からは ./install.sh /c/path/to/MoGRU64_x64.msi でも実行可能
> ```

---

Build Status
------------

| Linux | Windows | macOS |
|:-----:|:-------:|:-----:|
| [![Linux](https://github.com/lambda-haru/MoGRU/actions/workflows/linux.yaml/badge.svg)](https://github.com/lambda-haru/MoGRU/actions/workflows/linux.yaml) | [![Windows](https://github.com/lambda-haru/MoGRU/actions/workflows/windows.yaml/badge.svg)](https://github.com/lambda-haru/MoGRU/actions/workflows/windows.yaml) | [![macOS](https://github.com/lambda-haru/MoGRU/actions/workflows/macos.yaml/badge.svg)](https://github.com/lambda-haru/MoGRU/actions/workflows/macos.yaml) |

---

## ベースプロジェクト（Mozc）について

MoGRU は、Google が開発・公開しているオープンソースの日本語入力システム [Mozc](https://github.com/google/mozc) をベースに、文字単位 GRU ニューラル言語モデル（GRU Scorer）およびリアルタイムライブ変換機能等を統合・拡張したプロジェクトです。

Mozc 本来のコア変換エンジン設計、基本辞書構造、語彙選定方針、オリジナル版の仕様や詳細な技術ドキュメントについては、公式の Mozc リポジトリを参照してください。

- **Mozc 公式リポジトリ**: [https://github.com/google/mozc](https://github.com/google/mozc)
- **語彙選定ポリシー**: [VOCABULARY_POLICY.md](VOCABULARY_POLICY.md)
- **商標・ブランディングについて**: [About Branding](docs/about_branding.md)

---

License
-------

本ソフトウェアは以下のライセンスのもとで公開されています。

- **MoGRU 拡張・追加コード（GRU Scorer、ライブ変換、各種インストーラスクリプト等）**:
  Copyright (c) 2026 Haruhiro Obora.  
  [The BSD 3-Clause License](http://opensource.org/licenses/BSD-3-Clause) のもとで配布されます。
- **Mozc オリジナルコード**:
  All Mozc code written by Google is released under
  [The BSD 3-Clause License](http://opensource.org/licenses/BSD-3-Clause).
- **Fcitx5 モジュール (`src/unix/fcitx5`)**:
  Copyright (C) CSSlayer and The Fcitx Team.  
  [LGPL-2.1+ / GPL-2.0+](src/unix/fcitx5/org.fcitx.Fcitx5.Addon.Mozc.metainfo.xml.in) のもとで配布されます。

For third party code under [src/third_party](src/third_party) directory,
see each sub directory to find the copyright notice.  Note also that
outside [src/third_party](src/third_party) following directories contain
third party code.

### [src/data/dictionary_oss/](src/data/dictionary_oss)
Mixed.
See [src/data/dictionary_oss/README.txt](src/data/dictionary_oss/README.txt)

### [src/data/test/dictionary/](src/data/test/dictionary)
The same as [src/data/dictionary_oss/](src/data/dictionary_oss).
See [src/data/dictionary_oss/README.txt](src/data/dictionary_oss/README.txt)

### [src/data/test/stress_test/](src/data/test/stress_test)
Public Domain.  See the comment in
[src/data/test/stress_test/sentences.txt](src/data/test/stress_test/sentences.txt)
