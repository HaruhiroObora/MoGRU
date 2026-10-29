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

## 🚀 高精度モデル（$H=256$ 学術・リッチ版）の紹介と切り替え方法

MoGRU には、標準の軽量モデル（$H=128$）に加えて、隠れ層の表現力を大幅に強化した **高精度・学術モデル（$H=256$）** が同梱されています。

| モデル名 | 隠れ層次元 ($H$) | ファイルサイズ | 特徴 | 推奨環境 |
|---|:---:|:---:|---|---|
| **標準モデル**<br>(`grumodel.bin`, `chardata.bin`) | $H=128$ | 約4.4MB | 超軽量・省メモリ。低スペックなマシンやバッテリー優先の環境でも極めて軽快に動作。 | 全環境（初期設定） |
| **高精度モデル**<br>(`grumodel_new.bin`, `chardata_new.bin`) | $H=256$ | 約9.6MB | 表現力・文脈理解能力が向上。学術文書や専門的な長文文脈において、より自然で適切な同音異義語を提示。 | メモリやCPUに余裕のあるデスクトップ・ノートPC |

> [!TIP]
> 高精度モデルは配布パッケージ（zip）およびリポジトリの `models/` に同梱されており、インストール実行時に標準モデルと一緒に所定のディレクトリへ自動配置されます。お使いの環境で十分なスペックがある場合は、以下の手順で高精度モデルへ切り替えてご活用ください。

### 高精度モデルへの切り替え方法（GUIで簡単切り替え・再起動不要）

> [!IMPORTANT]
> **【必須】モデル（grumodel）と文字辞書（chardata）は必ずセットで変更してください**:
> オリジナル標準モデルと新モデル（new）では**扱う文字種・語彙インデックス（charset）が一部異なっています**。片方だけを変更すると文字インデックスの対応が不整合を起こし、推論精度の低下や誤変換の原因となります。
> そのため、モデルを切り替える際は **必ず `grumodel_new.bin` と `chardata_new.bin` をセットで同時に指定** してください（標準モデルに戻す際も同様に両方をセットで指定、または両方のパスを空欄にして初期化してください）。

MoGRU の **設定ツール（Configuration Tool）** の「**MoGRU**」タブから、GUI 上で簡単に高精度モデルへ切り替えることができます。

1. **設定ツール（Configuration Tool）を開く**:
   - **🐧 Linux**: Fcitx5 トレイメニューの「Configuration Tool」（または「MoGRU Settings」、アプリ一覧の「MoGRU 設定」）を開く
   - **🍎 macOS**: 入力メニューから「環境設定...」（または `/Applications/MoGRU/ConfigDialog.app`）を開く
   - **🪟 Windows**: 言語バーのツールアイコンから「プロパティ」を開く
2. 「**MoGRU**」（または「**MoGRU設定**」）タブを選択します。
3. 「**モデルファイル設定**」グループ内の参照ボタン（`Browse...`）をクリックし、高精度モデルを選択します：
   - **ベースモデル (grumodel.bin)**: `grumodel_new.bin` を選択
   - **文字辞書データ (chardata.bin)**: `chardata_new.bin` を選択
   > 各 OS のモデル配置先（インストール時に自動配置済み）：
   > - 🐧 Linux: `~/.config/mogru/`
   > - 🍎 macOS: `~/Library/Application Support/MoGRU/`
   > - 🪟 Windows: `%USERPROFILE%\AppData\LocalLow\MoGRU\`
4. 「**適用**」または「**OK**」をクリックします。
   ホットリロードに対応しているため、**再起動なしで次の入力から即座に高精度モデルが有効化**されます。

> [!NOTE]
> ベースモデルを切り替えた場合でも、内部のフィンガープリント照合機能により、異なるモデルの過去の学習重みが誤って適用されることは自動防止されます。
> なお、CUI 環境などで設定ファイルを直接編集したい場合は、`gru_config.ini` 内の `gru_model_path` / `gru_chardata_path` を指定することでも切り替え可能です。

---

## クイックインストール（推奨・Gitクローン/ビルド不要）

GitHub Releases または GitHub Actions の実行画面（Artifacts）からお使いの OS に応じたパッケージ（zip）をダウンロードして展開し、同梱のインストールスクリプトを実行するだけで数秒でインストールが完了します。
リポジトリのクローン（`git clone`）やビルド環境（Bazel / C++コンパイラ等）は一切不要です。
モデル重みファイル（`grumodel.bin`, `chardata.bin`）やアイコン、設定ファイルも自動配置されます。

### 🐧 Linux (Fcitx5)
1. `MoGRU-linux.zip` をダウンロードして展開します。
2. 展開したディレクトリでスクリプトを実行します：
```bash
cd MoGRU-linux
./install.sh
```
> [!NOTE]
> Fcitx5 プラグイン、MoGRU 専用 GUI 設定ツール（`mozc_tool`）、全アイコン、モデル重み、デスクトップ設定が自動配置されます。システム標準の Mozc パッケージ（`mozc-data` 等）が一切インストールされていない環境でもスタンドアロンで完全動作します。

### 🍎 macOS
1. `MoGRU-macOS-arm64.zip`（Intel Mac の場合は `MoGRU-macOS-intel64.zip`）をダウンロードして展開します。
2. 展開したディレクトリでスクリプトを実行します：
```bash
cd MoGRU-macOS-arm64
./install.sh
```

### 🪟 Windows (PowerShell)
1. `MoGRU-windows-x64.zip` をダウンロードして展開します。
2. PowerShell を管理者権限で開き、展開したディレクトリでスクリプトを実行します：
```powershell
cd MoGRU-windows-x64
Set-ExecutionPolicy Bypass -Scope Process -Force
.\install.ps1
```

---

## 🛠️ ソースコードから自前ビルドする場合（開発者向け）

ソースコードから自前でビルド・開発したい場合は、リポジトリをクローンして各 OS のビルドスクリプトを実行してください：

### 🐧 Linux (Fcitx5)
```bash
git clone https://github.com/lambda-haru/MoGRU.git
cd MoGRU
./install.sh
# または: ./scripts/install_linux.sh
```
> [!NOTE]
> Fcitx5 開発ヘッダーおよび GUI 設定ツール用の Qt6（`qt6-base-dev` 等）が必要ですが、未インストールの場合はスクリプト初回実行時に自動でパッケージマネージャのインストール案内が表示されます。

### 🍎 macOS
```bash
git clone https://github.com/lambda-haru/MoGRU.git
cd MoGRU
./install.sh
# または: ./scripts/install_macos.sh
```

### 🪟 Windows (PowerShell)
```powershell
git clone https://github.com/lambda-haru/MoGRU.git
cd MoGRU
Set-ExecutionPolicy Bypass -Scope Process -Force
.\scripts\install_windows.ps1
```

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
