# Beatrice VST

[Beatrice 2](https://prj-beatrice.com) の公式 VST です。


## ビルド時のトラブルシューティング

### CMake でのビルド後に "EXEC : CMake error : failed to create symbolic link" というエラーが出る
- 作業しているユーザーにシンボリックリンクを作成する権限が与えられていないのが原因。
  - 「グループポリシーの編集」から 「コンピューターの構成」→ 「Windowsの設定」→「セキュリティの設定」→「ローカル ポリシー」→「ユーザー権利の割り当て」→「シンボリックリンクの作成」 に適切なユーザー名が設定し、PCを再起動。