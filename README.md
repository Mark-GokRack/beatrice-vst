# Beatrice VST

[Beatrice 2](https://prj-beatrice.com) の ~~公式~~ 私家版 VST です。



# How to build

## System requirements

- Windows
  - MSVC 2022
  - CMake >= 3.19

## build steps

- [Make for Windows](https://gnuwin32.sourceforge.net/packages/make.htm) をインストールして Makefile を使うのが公式手順とは思うが、下記の手順でもビルドできる
  - 以下、コマンドプロンプトでの作業
  - コマンドライン下記コマンドで、beatrice.lib をダウンロード
    ```cmd
    curl -fLo lib/beatricelib/beatrice.lib https://huggingface.co/fierce-cats/beatrice-2.0.0-alpha/resolve/beta.1/beta.1/beatrice.lib
    ```
  - ビルド用ディレクトリの作成
    ```cmd
    mkdir build
    ```
  - cmake の実行
    ```cmd
    cd build
    cmake ..
    cmake --build . --config=Release
    ```
  - cmakeによるビルドを実行すると、ユーザーのホームディレクトリ(%USERPROFILE%)以下の "\AppData\Local\Programs\Common\VST3" フォルダにビルドしたファイルへのシンボリックリンクが作成される。

## Trouble shooting

### Debug ビルドが出来ない
- 公開されている beatrice.lib が Release ビルドされているものなので、Debug ビルドは出来ません。

### CMake でのビルド後に "EXEC : CMake error : failed to create symbolic link" というエラーが出る
- 作業しているユーザーにシンボリックリンクを作成する権限が与えられていないのが原因。
  - 「グループポリシーの編集」から 「コンピューターの構成」→ 「Windowsの設定」→「セキュリティの設定」→「ローカル ポリシー」→「ユーザー権利の割り当て」→「シンボリックリンクの作成」 に適切なユーザー名を設定し、PCを再起動。