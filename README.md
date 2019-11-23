# linux-kernel-mainline -- 各種タブレット及び小型端末向け Linux カーネルのソースコードのリポジトリ

## 概要

本リポジトリは、以下の URL に示す Linux カーネルのリポジトリのミラーです。ここで、ミラー元となるリポジトリは、 Ubuntu 向けの mainline の Linux カーネルをビルドする為のリポジトリです。

- [git://git.launchpad.net/~ubuntu-kernel-test/ubuntu/+source/linux/+git/mainline-crack][KERN]

また、本リポジトリにおいて以下の名前で示されるブランチは、主に各種 Windows 搭載タブレット及び小型端末等向けに修正された Linux カーネルのソースコードを示すブランチであり、以下のデバイスに対応した Linux カーネルのソースコードが置かれています。

- ```mainline-v5.3.11-xmc10```
    - [King Jim 社製 XMC10][PORT] 向けに修正された Linux 5.3.11 カーネルのソースコードが置かれるブランチ

なお、 Ubuntu 向けの mainline の Linux カーネルに修正を加えたソースコード及び Linux カーネルのソースコードをビルドした生成物である .deb パッケージについては、以下の本リポジトリの release ページにて配布致します。

- [https://github.com/z80oolong/linux-kernel-mainline/releases][REL_]

また、 Ubuntu 向けの mainline の Linux カーネルのソースコードへの修正の詳細に関しては、上記の release ページの記述を参照して下さい。

## 使用条件

本リポジトリは、[Linux カーネル][KERN]のリポジトリのミラーであり、本リポジトリの使用条件は [Linux カーネルの使用条件と同様の使用条件に従うものとします。][GPL2]

本リポジトリの使用条件の詳細については、本リポジトリのディレクトリ [```LICENSE``` の各文書][GPL2]を参照して下さい。

なお、本リポジトリに適用した各種差分ファイルに関しては各種差分ファイルの作者が著作権を有し、使用条件は各差分ファイルの使用条件に従うものとします。

[KERN]:https://git.launchpad.net/~ubuntu-kernel-test/ubuntu/+source/linux/+git/mainline-crack
[PORT]:https://www.kingjim.co.jp/sp/portabook/xmc10/
[REL_]:https://github.com/z80oolong/linux-kernel-mainline/releases
[GPL2]:https://github.com/z80oolong/linux-kernel-mainline/tree/master/LICENSES

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
