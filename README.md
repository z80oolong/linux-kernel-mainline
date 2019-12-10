# linux-kernel-mainline -- 各種タブレット及び小型端末向け Linux カーネルのリポジトリ

## 概要

Windows OS を搭載している各種タブレット及び小型携帯端末に Debian 系ディストリビューションである Ubuntu を導入する場合、多くのタブレット及び端末においては、 Ubuntu 標準の Linux カーネルを使用しても問題なく動作しますが、一部のタブレット及び端末においては、画面の描画や内蔵無線 LAN 及び内蔵 Bluetooth デバイス等の挙動が、ファームウェアに関する問題等により、 Ubuntu 標準の Linux カーネルが正常に動作しない場合があります。

この場合、標準の Linux カーネルのソースコードに一部タブレット及び小型携帯端末に搭載の各種デバイスの挙動等の不具合に関する修正を行い、 Linux カーネルの再ビルドを行った上で、当該端末に導入した Ubuntu に不具合の修正を行った Linux カーネルの再導入を行います。

本リポジトリは、以下に示すオリジナルの Linux カーネルのリポジトリを基にして、一部タブレット及び小型携帯端末に搭載の各種デバイスに関する問題を修正した Linux カーネルのリポジトリです。

- [https://git.launchpad.net/~ubuntu-kernel-test/ubuntu/+source/linux/+git/mainline-crack][REP1]
- [https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git][REP2]
- [https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git][REP3]

ここで、一部タブレット及び小型携帯端末の各種デバイスに関する問題を修正した Linux カーネルについては、ブランチ ```mainline-v{version_number}-dev (ここに、 {version_number} は修正元の Linux カーネルのバージョン番号)``` を参照して下さい。

なお、本リポジトリのブランチ ```mainline-v{version_number}-dev``` 上のソースコードをビルドし、 .deb 形式のパッケージ化したものについては、以下のページにて配布しています。

- [https://github.com/z80oolong/linux-kernel-mainline/releases][RELS]

.deb パッケージの対応端末及び詳細な使用法等については、 [上記の配布ページ][RELS]を御覧下さい。

## 使用条件

本リポジトリに置かれているソースコードのリポジトリは、各種タブレット及び小型端末向けに Linux カーネルのソースコードを修正したものであり、使用条件はオリジナルの Linux カーネルの使用条件に従うものとします。使用条件の詳細については、 [.deb パッケージの配布ページ][RELS]を参照して下さい。

<!-- リンク一覧 -->

[PORT]:https://www.kingjim.co.jp/sp/portabook/xmc10/
[REP1]:https://git.launchpad.net/~ubuntu-kernel-test/ubuntu/+source/linux/+git/mainline-crack
[REP2]:https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
[REP3]:https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
[RELS]:https://github.com/z80oolong/linux-kernel-mainline/releases

## 追記

以下に、オリジナルの ```README``` の内容を示します。

----

Linux kernel
============

There are several guides for kernel developers and users. These guides can
be rendered in a number of formats, like HTML and PDF. Please read
Documentation/admin-guide/README.rst first.

In order to build the documentation, use ``make htmldocs`` or
``make pdfdocs``.  The formatted documentation can also be read online at:

    https://www.kernel.org/doc/html/latest/

There are various text files in the Documentation/ subdirectory,
several of them using the Restructured Text markup notation.

Please read the Documentation/process/changes.rst file, as it contains the
requirements for building and running the kernel, and information about
the problems which may result by upgrading your kernel.
