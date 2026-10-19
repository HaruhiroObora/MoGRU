[Mozc GRU - Neural Language Model Integrated Mozc](https://github.com/google/mozc)
========================================================================

本リポジトリは、オープンソースかな漢字変換エンジン **Mozc** に、**文字単位 GRU ニューラル言語モデル（GRU Scorer）** を統合した拡張版です。

### 主な特徴
- **大域的ビームサーチ ($B=8$)**: 複数文節を跨いだ文脈をGRU言語モデルで評価し、文脈に応じた適切な同音異義語を自動リランキング。
- **独立動作（共存対応）**: システム標準の `mozc` と競合せず、Fcitx5 上で `Mozc (GRU)` として独立して登録・利用可能。
- **GUI設定対応**: Fcitx5 設定画面からモデルパスや文脈重み係数 $\alpha$（デフォルト 800）をリアルタイムに調整可能。
- **マルチプラットフォーム**: Linux (Fcitx5), macOS, Windows をサポート。

---

## クイックスタート（各OSでのビルド & インストール）

リポジトリをクローンした後、お使いのOSに応じたスクリプトを実行するだけでビルド・インストールが完了します。
モデル重みファイル（`grumodel.bin`, `chardata.bin`）も自動配置されます。

### 🐧 Linux (Fcitx5)
```bash
git clone https://github.com/HaruhiroObora/mozc-gru.git
cd mozc-gru
./install.sh
# または: ./scripts/install_linux.sh
```

### 🍎 macOS
```bash
git clone https://github.com/HaruhiroObora/mozc-gru.git
cd mozc-gru
./scripts/install_macos.sh
```
> [!TIP]
> GitHub Actions の各実行画面（Artifacts）からビルド済みの `Mozc_arm64.pkg` / `Mozc_intel64.pkg` をダウンロードして直接実行・インストールすることも可能です。


### 🪟 Windows (PowerShell)
```powershell
git clone https://github.com/HaruhiroObora/mozc-gru.git
cd mozc-gru
Set-ExecutionPolicy Bypass -Scope Process -Force
.\scripts\install_windows.ps1
```

---

Build Status
------------

| Linux | Windows | macOS |
|:-----:|:-------:|:-----:|
| [![Linux](https://github.com/HaruhiroObora/mozc-gru/actions/workflows/linux.yaml/badge.svg)](https://github.com/HaruhiroObora/mozc-gru/actions/workflows/linux.yaml) | [![Windows](https://github.com/HaruhiroObora/mozc-gru/actions/workflows/windows.yaml/badge.svg)](https://github.com/HaruhiroObora/mozc-gru/actions/workflows/windows.yaml) | [![macOS](https://github.com/HaruhiroObora/mozc-gru/actions/workflows/macos.yaml/badge.svg)](https://github.com/HaruhiroObora/mozc-gru/actions/workflows/macos.yaml) |


What's Mozc?
------------
For historical reasons, the project name *Mozc* has two different meanings:

1. Internal code name of Google Japanese Input that is still commonly used
   inside Google.
2. Project name to release a subset of Google Japanese Input in the form of
   source code under OSS license without any warranty nor user support.

In this repository, *Mozc* means the second definition unless otherwise noted.

Detailed differences between Google Japanese Input and Mozc are described in [About Branding](docs/about_branding.md).

For policies on vocabulary and conversion results, see
[Vocabulary Policy](VOCABULARY_POLICY.md).

Build Instructions
------------------

* [How to build Mozc for Android](docs/build_mozc_for_android.md): for Android library (`libmozc.so`)
* [How to build Mozc for Linux](docs/build_mozc_for_linux.md): for Linux desktop
* [How to build Mozc for macOS](docs/build_mozc_in_osx.md): for macOS build
* [How to build Mozc for Windows](docs/build_mozc_in_windows.md): for Windows

Release Plan
------------

tl;dr. **There is no stable version.**

As described in [About Branding](docs/about_branding.md) page, Google does
not promise any official QA for OSS Mozc project.  Because of this,
Mozc does not have a concept of *Stable Release*.  Instead we change version
number every time when we introduce non-trivial change.  If you are
interested in packaging Mozc source code, or developing your own products
based on Mozc, feel free to pick up any version.  They should be equally
stable (or equally unstable) in terms of no official QA process.

[Release History](docs/release_history.md) page may have additional
information and useful links about recent changes.

License
-------

All Mozc code written by Google is released under
[The BSD 3-Clause License](http://opensource.org/licenses/BSD-3-Clause).
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
