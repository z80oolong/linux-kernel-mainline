# linux-kernel-mainline -- 各種タブレット及び小型端末向け Linux カーネルのリポジトリ

## 概要

Windows OS を搭載している各種タブレット及び小型携帯端末に Ubuntu を導入する場合、多くのタブレット及び端末においては、 Ubuntu 標準の Linux カーネルを使用しても問題なく動作しますが、一部のタブレット及び端末においては、画面の描画や内蔵無線 LAN 及び内蔵 Bluetooth デバイス等が、ファームウェアに関する問題等により、正常に動作しない場合があります。

この場合、標準の Linux カーネルのソースコードに修正を適用して、 Linux カーネルの再ビルドと Ubuntu への再導入を行うことにより、当該端末の Ubuntu の動作に関する問題の解決を行います。

このリポジトリでは、以下に示す Linux カーネルのリポジトリを基にして、各種タブレット及び端末についての問題を解消したカスタム Linux カーネルのリポジトリが置かれています。

- [https://git.launchpad.net/~ubuntu-kernel-test/ubuntu/+source/linux/+git/mainline-crack][REP1]
- [https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git][REP2]
- [https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git][REP3]

なお、カスタム Linux カーネルのパッケージ及びソースコードの tarball については、 [Linux カスタムカーネルの配布ページ][RELS]にて配布していますので、詳細な使用法等については、 [Linux カスタムカーネルの配布ページ][RELS]を御覧下さい。

## 使用条件

本リポジトリに置かれている Linux カスタムカーネルのソースコードのリポジトリは、各種タブレット及び小型端末向けに Linux カーネルのソースコードを修正したものであり、使用条件はオリジナルの Linux カーネルの使用条件に従うものとします。使用条件の詳細については、 [Linux カスタムカーネルの配布ページ][RELS]を参照して下さい。

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
