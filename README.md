<p align="center">
  <img src="src/data/images/icon.svg" width="112" height="112" alt="Mozkey icon">
</p>

<h1 align="center">Mozkey（もずきー）</h1>

<p align="center">
  <strong>Mozc をベースに、遅延付きライブ変換・ローカル Zenz 補正・ダークテーマ対応<br>句読点単打確定・文脈を見た変換補正などを統合した、ローカルファーストな日本語入力 fork です。</strong>
</p>

<p align="center">
  <a href="https://github.com/koyasi777/mozkey/releases"><img alt="Releases" src="https://img.shields.io/github/v/release/koyasi777/mozkey?include_prereleases&label=release"></a>
  <img alt="Based on Mozc" src="https://img.shields.io/badge/based%20on-Mozc-88A2DD">
  <img alt="Local first" src="https://img.shields.io/badge/local--first-Zenz-53D4C7">
  <img alt="Release build" src="https://img.shields.io/badge/release-Windows%20MSI-178B8B">
  <img alt="macOS/Linux status" src="https://img.shields.io/badge/macOS%20%2F%20Linux-untested-lightgrey">
</p>

<br>

Mozkey（もずきー）は [google/mozc](https://github.com/google/mozc) をベースにした非公式フォークです。

本 fork は、主に自分の Windows 環境で日常的に使うために、Mozc に入力補助・ライブ変換・文脈補正・ローカル Zenz 補正・オフライン配布向けの調整を加えたものです。

本プロジェクトは Google 日本語入力ではありません。
Google または google/mozc の公式配布物ではありません。
Google によるサポートや品質保証の対象ではありません。

upstream Mozc との追従性および既存インストールとの互換性を保つため、
一部の内部実行ファイル名、パス、実装上の識別子には `mozc` / `Mozc` 名が残ります。

<br>

ダウンロード / インストール
--------------------------

現時点で公開しているビルド済みパッケージと実機確認済み環境は Windows です。

macOS / Linux については、upstream Mozc 自体は対応していますが、この fork で追加した機能、ビルド設定、Zenz 同梱構成、インストーラーまわりについては、まだ実機確認できていません。

Windows 用のビルド済み MSI は [Releases](https://github.com/koyasi777/mozkey/releases) からダウンロードできます。

- 通常の 64-bit Windows では、Releases にある最新の `Mozkey_v*_x64.msi` を使用してください。
- 本 fork のリリースは個人用の experimental build として公開しています。
- Zenz 同梱版は、ローカル推論 runtime と GGUF model を含むため、従来の offline MSI よりファイルサイズが大きくなります。

> [!WARNING]
> このビルドは google/mozc の公式配布物ではありません。
> 個人用 fork の experimental / pre-release build です。
> MSI は署名されていないため、Windows の警告が表示される場合があります。

<br>

主な追加機能
---------------------------

- 曖昧なローマ字規則でも途中表示できるオプションを追加
- ローマ字テーブル編集画面に、そのオプション用のチェックボックス UI を追加
- 句読点・記号を単打で確定できるオプションを追加
- 単打確定の対象を設定画面のチェックボックスで選択可能
- 句読点変換と句読点・記号の単打確定は排他的に動作
- 変換確定直後に Backspace や Cancel キーで取り消した場合のユーザー履歴学習の扱いを改善
- 句読点・記号の単打確定でも、直前の通常変換確定による学習を次の実テキスト入力まで保留し、Backspace / Escape / Revert / Reset / Undo / IME off などで取り消せるようにした
- ライブ変換機能を追加。未確定文字列を自動変換し、確定前の読みをルビ風 overlay で表示
- ライブ変換は設定画面から ON/OFF と変換開始までの遅延時間を変更可能
- ライブ変換は入力直後の不要な変換ちらつきを抑えるため、文字入力後に短いデバウンスを挟んで実行
- 1文字だけの未確定文字列では、助詞などの誤変換を避けるためライブ変換を実行しない
- `え~`、`えー`、`ん？` のような「かな1文字 + 装飾的な末尾記号」でも、短すぎる漢字化を避けるためライブ変換を抑制
- 確定済みの左文脈や直前の文節、限定的な右文脈を参照し、`mainにマージしました`、`githubには`、`2名しかいない`、`追記したい`、`山梨県立美術館`、`滋賀方面` のような文脈で、助詞・複合機能語・機能表現・接尾的な語構成・地名接尾構成が同音漢字候補に負ける挙動を抑制
- キー設定エディタで、1つのキー入力に対して複数のコマンドを順序付きで割り当て可能
- 複数コマンドは `Commit|IMEOff` のような形式で保存され、設定画面では `Commit → IMEOff` のように編集可能
- Windows 版で左 Shift / 右 Shift / 左 Ctrl / 右 Ctrl を個別キーとして設定画面から割り当て可能
- Windows 版で IMEOn / IMEOff に割り当てたキーを押した場合、すでに同じ状態でも IME モードインジケータを表示
- Windows 版の設定画面から、Mozkey を Windows の既定 IME として明示的に設定し、変更前の既定 IME 設定へ戻せるボタンを追加
- Windows 版の候補ウィンドウ/ルビ表示にダークモード切り替えを追加
- Windows 版で未確定文字の文字色・背景色・下線色を設定画面からカスタマイズ可能
- Windows 版の候補ウィンドウや IME 切り替えインジケータの配色・余白・角丸などの見た目を調整
- Windows 版の IME 切り替えインジケータが、Windows のライト / ダークテーマに合わせて表示されるように改善
- system dictionary 強化用の追加辞書生成パイプラインを追加
- merge-ut-dictionaries 由来の地名・SudachiDict 系語彙を system dictionary に取り込めるようにした
- dic-nico-intersection-pixiv 由来のネット・サブカル系固有名詞を、既存辞書との差分として daily 辞書に追加可能
- 文節区切り崩れを抑えるための syntax guard 辞書を daily 辞書生成パイプラインに追加
- 大規模な生成辞書は Git に含めず、ローカルで再生成して Bazel の辞書入力へ切り替える運用に
- `には` や `してたの` のような自然な機能語かな列が、`二は` や `して他の` のような 1 文字漢字候補に過剰変換される挙動を抑制
- `にじ` のような 2 文字ひらがな入力で、`に|じ` のような短すぎる文節分割が全体候補を隠す挙動を抑制
- llama.cpp ベースのローカル Zenz live correction pipeline を追加
- Zenz 補正は `mozc_server` から named pipe 経由で `mozc_zenz_scorer.exe` に依頼し、`llama-server.exe` の localhost endpoint でローカル推論
- Zenz 推論開始までの遅延時間を設定画面から変更可能。デフォルトは 1000 ms
- Zenz 推論を許可する最小読み長を設定画面から変更可能
- Zenz 補正結果のローカル feedback learning を追加。設定画面から ON/OFF 可能
- Zenz feedback は、Zenz 補正結果が表示されただけでは保存されません。Enter や句読点・記号の単打確定などで表示中の Zenz 結果が明示的に確定された場合だけ、accepted feedback の候補として保留されます。
- 保留された accepted feedback は、次の実テキスト入力まで Backspace / Escape / Revert / Undo / IME off などで取り消されなかった場合にローカル TSV へ保存
- Zenz 補正表示後に Space や候補移動など通常変換操作へ移った場合、その Zenz 候補は rejected feedback として扱う
- Zenz 学習データを設定画面から安全に管理できる UI を追加。TSV を直接編集せず、検索、インポート、エクスポート、選択項目削除、全削除が可能
- Zenz accepted feedback を通常変換候補の promotion に利用。1 文節の通常変換では、保存済み accepted feedback が既存候補にあれば先頭へ昇格し、候補にない場合は synthetic candidate として追加
- 文節境界を壊さないため、複数文節に分かれた通常変換では Zenz feedback promotion を行わない
- 複数文節に分かれるライブ変換では、全文補正の学習を保つため、accepted Zenz feedback を session-level live correction fast path として再利用
- sensitive-like context で得られた feedback は、通常文脈の候補 promotion には使わない
- accepted として確定した Zenz 候補は、条件を満たす場合は Mozc の user history にも外部変換結果として学習
- Zenz prompt に使う左文脈は sanitizer を通し、URL、email、file path、token、長い数字列など sensitive-like な文脈は prompt に含めない
- Zenz feedback には raw left context を保存せず、非可逆な context class のみを保存
- Zenz model / llama.cpp runtime の third-party license notice を MSI に同梱
- 自分の Windows 開発環境向けのビルド調整

<br>

プライバシー / ネットワークアクセス
------------------------------------

この fork は、通常の IME 利用時に Mozc の実行時プロセスがネットワーク通信を行わない構成を目指しています。

upstream 由来の使用統計・クラッシュレポート送信オプションは、管理ダイアログおよび設定ダイアログから削除しています。

この fork では `StatsConfigUtil` のデフォルト実装を Null 実装に固定しており、通常の実行経路から使用統計を有効化できません。

Windows 向けリリースバイナリについては、Mozc core runtime executable が `winhttp.dll`, `wininet.dll`, `urlmon.dll` などの代表的なネットワーク関連 DLL を import していないことを検査します。

この fork では、llama.cpp ベースのローカル Zenz 推論 runtime を同梱する場合があります。同梱される `llama-server.exe` はローカル推論用の server として使用され、`127.0.0.1` のみに bind することを前提としています。外部ネットワークサービスを公開したり、入力内容を外部サーバーへ送信したりする目的のものではありません。

Zenz helper process は、同梱された `llama-server.exe` と localhost endpoint のみで通信します。この local HTTP endpoint は推論処理を分離するための内部的なプロセス境界であり、外部ネットワークアクセスを目的としたものではありません。

Zenz の localhost 通信は、固定 endpoint に依存しないようにし、内部 request も誤接続を避けるための保護を加えています。

Zenz feedback learning は、読み、候補、粗い文脈クラス、採用/却下回数、理由 marker などのローカル学習情報だけを保存します。生の左文脈は保存しません。feedback に使う文脈は、`empty`、`japanese_only`、`japanese_with_punctuation`、`mixed_japanese_ascii`、`sensitive_like` などの非可逆な context class に落とします。

さらに、リリース時には Mozc core runtime binaries にテレメトリ、アップデータ、クラッシュアップロード、使用統計関連の危険な marker が含まれないことを確認します。

`http://`, `https://`, `googleapis.com` などの汎用的な URL 風 marker は、manifest、XML namespace、コメント、license file、ライブラリ metadata に由来する場合があるため、監査用に表示しますが hard failure にはしていません。

ソースからビルドする場合、ビルド依存関係の取得にネットワーク接続が必要になる場合があります。これは、インストール済み IME の実行時通信とは別です。

Windows 版では、追加のオフライン防御層として、インストーラーが Mozc の実行ファイルに outbound 通信をブロックする Windows Firewall rule を追加します。これらの rule はアンインストール時に削除されます。

関連ドキュメント:

- [Secure Offline Guarantee](docs/security/offline_guarantee.md)
- [Secure Offline Release Checklist](docs/security/release_checklist.md)

<br>

修正詳細
------

### 曖昧なローマ字規則の途中表示

このオプションを有効にすると、たとえば

- `ms -> ます`
- `mst -> ました`

のような規則で、`ms` を入力した時点で `ます` を未変換表示し、続けて `t` を入力すると `ました` に更新されます。

長い曖昧規則の途中まで進んだ後、その規則が成立しない場合は、最後に表示できていた曖昧結果まで戻り、残りの入力を通常のローマ字として再解釈します。

たとえば

- `ctn -> ことに`
- `ctnnr -> ことになる`
- `ctnnc -> ことなのか`

のような規則がある場合、`ctnnaru` は `ctn + naru` として解釈され、`ことになる` になります。
一方で、`ctnnr` や `ctnnc` のように長い規則として成立する入力は、従来どおりその規則が使われます。

### ライブ変換

ライブ変換を有効にすると、スペースキーを押さなくても、入力中の未確定文字列が自動で変換されます。

入力途中の不要な中間変換表示を抑えるため、この fork では文字入力後に短い設定可能なデバウンス時間を挟んでからライブ変換を実行します。`に`、`を`、`が` のような助詞として使われやすい入力を誤って漢字化しないように、1文字だけの未確定文字列ではライブ変換を行いません。

また、`え~`、`えー`、`ん？` のように、かな1文字の後ろに装飾的な記号だけが続く場合もライブ変換を抑制します。これにより、入力途中の `え~` が `絵～` のように短すぎる漢字候補へ変換される挙動を避けます。

たとえば:

- `kyouha` と入力
- デバウンス時間の経過後、Space を押す前に未確定文字列が `今日は` のように表示される
- 続けて文字を入力しても途中の変換結果は確定されず、同じ未確定文字列として再変換される
- Backspace / Delete では、削除後の状態をすぐにライブ変換結果へ反映する
- Enter で現在のライブ変換結果を確定する

ライブ変換中は、変換後の文字を表示しながら元の読みも分かるように、未確定文字の上付近に Mozc 独自のルビ風 overlay window を表示します。

ライブ変換は設定画面から ON/OFF を切り替えられます。また、変換開始までの遅延時間も設定画面から変更できます。

### Zenz ライブ補正

ライブ変換と Zenz ライブ補正の両方を有効にすると、まず通常の Mozc ライブ変換結果を表示し、その後でローカルの Zenz runtime に非同期で補正を依頼します。

Zenz request は `mozc_server` から Windows named pipe 経由で `mozc_zenz_scorer.exe` に送られます。scorer は同梱された `llama-server.exe` の localhost endpoint を呼び出し、ローカル推論を行います。この localhost 通信は固定 endpoint に依存しないようにし、内部 request も誤接続を避けるための保護を加えています。

Zenz 補正は設定可能なデバウンス時間の後に実行されます。デフォルトは 1000 ms です。Zenz 結果が返る前に入力内容が変わった場合、古い結果は generation / key の検査により破棄されます。

Zenz 出力は表示前に検証されます。空出力、短すぎる入力、Mozc 結果と同一の出力、長すぎる出力、不正な文字列、安全でない可能性のある文字列は拒否されます。拒否された場合は、通常の Mozc ライブ変換結果をそのまま表示します。

Zenz ライブ補正は password field では実行されません。また、入力途中の raw romaji のように日本語文字シグナルを含まない読みは補正対象外です。日本語文字を含む英字混じりの入力は、privacy gate を通る場合に限り補正対象になり得ます。

Zenz feedback learning は任意機能です。有効な場合でも、Zenz 補正結果が表示されただけでは保存されません。Enter や句読点・記号の単打確定などで、表示中の Zenz 結果が明示的に確定された場合だけ、accepted feedback の候補として保留されます。

保留された accepted feedback は、次のユーザー操作で取り消されなかった場合だけローカル TSV に保存されます。Backspace、Escape、Revert、Undo、IME off などの修正操作が入った場合、保留 feedback は破棄されます。表示中の Zenz 補正から Space や候補移動などの通常変換操作へ移った場合、その Zenz 結果は rejected feedback として扱われます。

accepted feedback の再利用方法は、単文節と複数文節で異なります。

単文節の通常変換では、Zenz feedback は rewriter chain 内の candidate promotion として再利用されます。保存済み accepted feedback が既存候補にあれば先頭へ昇格し、候補にない場合は synthetic candidate として追加できます。その後に UserSegmentHistoryRewriter が走るため、明示的なユーザー選択履歴が最終順位を決めます。

複数文節に分かれるライブ変換では、converter の文節境界を feedback promotion で壊さないため、rewriter chain では通常変換候補を書き換えません。その代わり、session-level の live correction fast path として再利用します。これにより、`かれはてんてきです` → `彼は天敵です` のような全文補正の学習も再利用できます。

`sensitive_like` context で得られた feedback は、通常文脈への候補 promotion には使いません。

Zenz 学習データは設定画面から管理できます。管理画面では、学習済みエントリを読み取り専用 table で表示し、検索、インポート、エクスポート、選択項目削除、全削除を行えます。ユーザーが TSV ファイルを直接編集する必要はありません。

### 確定済み左文脈を使った変換補正

この fork では、現在の未確定文字列の直前にある確定済みテキストを、可能な範囲で左文脈として参照します。

たとえば、`main` を確定した直後に `にまーじしました` と入力した場合、

- `mainにマージしました`

のような結果を優先し、

- `main二マージしました`

のように助詞 `に` が同音の漢字・数字候補へ過剰に変換される挙動を抑制します。

空白、改行、句読点、括弧などの強い区切りの直後では、左文脈は接続しません。

内部的には、名詞相当の確定済み左文脈を history segment として復元し、`に`、`を`、`が`、`へ`、`で` などの短い助詞候補が、同音の漢字・数字・記号・濁点付き仮名候補に負けにくくなるように候補順位を補正します。

また、名詞相当の左文脈の後では、`には`、`にも`、`では`、`でも`、`とは` のような保守的な複合機能語も補正対象にします。たとえば `github` を確定した直後に `には` と入力した場合、`github二は` よりも `githubには` を優先しやすくします。

また、名詞や数量表現の後に続く `しか + 否定表現` のような機能表現も保護します。たとえば `2めいしかいない` では、途中の `2名司会...`、`2名視界...`、`2名士会内` のような同音漢字経路よりも、`2名しかいない` を優先しやすくします。

また、サ変接続名詞や行政地名・施設名のような直前文脈、地名に続く場所接尾語のような限定的な右文脈も一部補正対象にします。たとえば `追記したい` では `追記死体` のような同音名詞候補を避け、`山梨県立美術館` では `山梨県率美術館` のような接尾的構成の崩れを抑制します。また、`滋賀方面` や `佐賀空港` のような語では、`子が方面` のような短い `内容語 + が` 分割を避けやすくします。

### 句読点・記号の単打確定

句読点・記号の単打確定オプションを有効にすると、設定した記号を入力した時点で未確定文字列全体をそのまま確定できます。

たとえば

- `tesuto.` → `てすと。`
- `tesuto?` → `てすと？`
- `kakko(` → `かっこ（`

のように動作します。

ローマ字テーブルで句読点・記号を出すように設定している場合も、出力された文字が単打確定の対象に含まれていれば、その時点で確定されます。

たとえば、ローマ字テーブルで `zz -> 。` や `qq -> ？` のように設定している場合でも、句点や疑問符を単打確定の対象にしていれば、`tesutozz` → `てすと。`、`tesutoqq` → `てすと？` のように確定されます。

どの句読点・記号を単打確定の対象にするかは、設定画面のチェックボックスで選択できます。

ライブ変換が有効な場合、句読点・記号の単打確定では、ひらがなの未変換文字列ではなく、現在表示されているライブ変換結果を確定します。

句読点・記号の単打確定でも、直前の通常変換確定による学習は次の実テキスト入力まで保留されます。次の操作が Backspace、Escape、Revert、Reset、Undo、IME off などの場合、その保留学習は保持せず取り消します。

### 確定直後の修正による履歴学習の取り消し

変換確定直後に Backspace、または現在のキー設定で Cancel に割り当てられたキーが入力された場合、このフォークでは直前に確定した学習結果を取り消し対象として扱います。

確定した文字列全体を削除した場合は、その確定によるサジェスト履歴およびユーザーセグメント履歴が、以後のサジェストや変換順位に残らないようにします。

一方で、Backspace により確定文字列の末尾だけを削除した場合は、残っている部分の履歴は保持し、削除された末尾に対応する履歴だけを取り消します。

たとえば:

- `いしとは` を `医師とは` として確定
- Backspace を2回押して `とは` だけを削除
- 残っている `医師` の履歴は保持
- `医師とは` としての末尾込みの学習は取り消し

これにより、変換確定直後の修正操作がユーザーの意図に近い形で履歴学習へ反映されます。

### 複数コマンドのキー割り当て

キー設定エディタで、1つのキー入力に対して複数のコマンドを順序付きで割り当てられるようにしました。

たとえば、次のような割り当てができます。

- `Composition + Ctrl Enter -> Commit → IMEOff`
- `Composition + Ctrl Space -> Convert → ConvertNext`
- `Conversion + Ctrl Enter -> Commit → IMEOff`

コマンド列は左から右へ順に実行されます。途中で入力状態が変わった場合、後続コマンドはその時点の状態に合わせて解決されます。たとえば `Convert → ConvertNext` では、まず未変換状態から変換状態へ入り、その後に次候補へ移動します。

設定ファイル上では、複数コマンドは `Commit|IMEOff` や `Convert|ConvertNext` のように `|` 区切りで保存されます。設定画面では `Commit → IMEOff` のように表示され、コマンドの追加、削除、並べ替えができます。

### 左右 Shift / Ctrl の個別キー割り当て（Windows）

Windows 版では、キー設定エディタ上で左 Shift / 右 Shift / 左 Ctrl / 右 Ctrl を別々のキーとして扱えます。

`Ctrl j` のような従来の汎用 Ctrl キーバインドは、左 Ctrl / 右 Ctrl のどちらでも従来どおり動作します。

たとえば

- `DirectInput + RightShift -> IMEOn`
- `Precomposition + LeftShift -> IMEOff`
- `Composition + LeftShift -> IMEOff`
- `Conversion + LeftShift -> IMEOff`
- `DirectInput + RightCtrl -> IMEOn`
- `Precomposition + LeftCtrl -> IMEOff`

のように設定でき、左右の Shift / Ctrl に別々の IME 操作を割り当てられます。

`IMEOn` または `IMEOff` に明示的に割り当てたキーを押した場合は、すでにその状態であっても IME モードインジケータを表示します。たとえば、IME がすでに無効の状態で `IMEOff` キーを押しても、直接入力状態のインジケータを表示します。

これにより、IME 有効化・無効化キーを「状態を切り替えるキー」としてだけでなく、現在状態を視覚的に確認するためのキーとしても使えます。

### Windows 既定 IME 設定

Windows 版では、設定画面の「その他の設定」→「既定の IME」から、Mozkey を Windows の既定 IME として明示的に設定できます。

この操作はログオン時に自動実行されるものではなく、ユーザーが設定ボタンを押した場合だけ実行されます。

設定時には、変更前の Windows 既定 IME の上書き設定と、日本語入力方式リストの順序を保存します。「以前の Windows 既定 IME 設定に戻す...」ボタンを押すと、保存していた設定へ戻します。

すでに Mozkey が既定 IME として設定されている場合や、未復元のバックアップが残っている場合は、変更前の復元点を上書きしないようにしています。

### Windows 候補ウィンドウと IME インジケータの表示テーマ

Windows 版では、設定画面から候補ウィンドウの通常テーマとダークテーマを切り替えられます。

ダークテーマでは配色だけでなく、余白、角丸、フッター表示なども調整し、候補ウィンドウ全体の見た目をよりモダンにしています。

IME 切り替えインジケータは Windows のライト / ダークテーマに追従し、現在の入力モードを確認しやすいように配色を切り替えます。

### Windows 未確定文字の表示色

Windows 版では、設定画面から未確定文字の表示色をカスタマイズできます。

入力中の文字と変換中の文節について、それぞれ以下を個別に設定できます。

- 文字色
- 背景色
- 下線色

日本語入力中・変換中の未確定文字を見やすくするための機能です。特に、視認性を高めたいユーザー向けのアクセシビリティ改善として追加しています。

この設定は Windows TSF の表示属性として反映されます。対応アプリでのみ有効です。Chrome や Edge など、一部のアプリでは反映されない場合があります。

system dictionary の強化
-----------------------

この fork では、外部辞書を元に Mozc の system dictionary を強化するための生成スクリプトを追加しています。

daily local 辞書は主に以下を元に生成できます。

- merge-ut-dictionaries
  - 地名
  - SudachiDict 由来語彙
- dic-nico-intersection-pixiv
  - 固有名詞、ネットスラング、作品名、キャラクター名、サブカル系語彙
  - 生成済み daily 辞書または Mozc 標準辞書に既に存在する key/value は除外
- Koyasi syntax guard 辞書
  - 文節区切り崩れの影響が大きいケースだけを小さな生成辞書として補強
  - 例: `と打ちたいのに` や `に分ける` のような文法的に自然な経路を保護

巨大な生成辞書ファイルは、このリポジトリには commit しません。`src/data/dictionary_koyasi/generated/` 以下にローカル生成します。

強化辞書を有効にした状態で package build する前に、daily 辞書をローカルで再生成してください。

```powershell
.\tools\dictionary\prepare_daily_dictionary.ps1
```

詳細:

- [Koyasi Dictionary Data](src/data/dictionary_koyasi/README.md)

Note
----

This fork is primarily maintained for personal use.
Upstream-oriented changes are organized in `pr/*` branches.

この fork は主に個人利用向けに保守しています。
upstream 提案向けの変更は `pr/*` branches に整理しています。

<br>

# English

This repository is my personal fork of [google/mozc](https://github.com/google/mozc).

This fork is mainly maintained for my own Windows environment and adds input assistance, live conversion, context-aware conversion, local Zenz correction, and offline-distribution-oriented adjustments to Mozc.

This build is not an official google/mozc distribution.

Privacy / Network Access
------------------------

This fork is designed to run without network communication from Mozc runtime
processes during normal IME operation.

The usage-statistics and crash-report option inherited from upstream has been
removed from the administration and configuration dialogs.

The default `StatsConfigUtil` implementation is fixed to the null implementation
in this fork, and usage statistics cannot be enabled through the normal runtime
path.

Windows release binaries are checked so that Mozc core runtime executables do
not import common networking libraries such as `winhttp.dll`, `wininet.dll`, or
`urlmon.dll`.

This fork may also bundle a local Zenz inference runtime based on llama.cpp.
The bundled `llama-server.exe` is used as a local-only inference server and is
expected to bind to `127.0.0.1`. It is not intended to expose an external
network service or send user input to external servers.

The Zenz helper process communicates with the bundled `llama-server.exe` only
through a localhost endpoint. This local HTTP endpoint is used as an internal
process boundary for inference and is not intended for external network access.

The Zenz localhost transport is hardened so that it does not rely on a fixed
endpoint, and internal requests include protection against accidental or stale
local endpoint mismatches.

Zenz feedback learning stores only local learning records such as reading,
candidate, coarse context class, accepted/rejected counts, and reason markers.
Raw left context is not stored. Context used for feedback is reduced to a
non-reversible class such as `empty`, `japanese_only`,
`japanese_with_punctuation`, `mixed_japanese_ascii`, or `sensitive_like`.

Additional release checks verify that Mozc core runtime binaries do not contain
hard-deny telemetry, updater, crash-upload, or usage-statistics markers.

Generic URL-like markers such as `http://`, `https://`, and `googleapis.com`
are reported for audit, but they are not treated as hard failures because they
can come from manifests, XML namespaces, comments, license files, or library
metadata.

Building from source may require network access to download build dependencies.
This is separate from runtime behavior of the installed IME.

On Windows, the installer also adds outbound Windows Firewall block rules for
Mozc runtime executables as an additional offline hardening layer. These rules
are removed during uninstall.

See also:

- [Secure Offline Guarantee](docs/security/offline_guarantee.md)
- [Secure Offline Release Checklist](docs/security/release_checklist.md)


Download / Install
------------------

At the moment, prebuilt packages and real-machine testing are available only for Windows.

macOS / Linux are supported by upstream Mozc itself, but this fork's added features, build settings, Zenz-bundled configuration, and installer-related behavior have not yet been tested on real macOS / Linux environments.

Windows MSI packages are available from [Releases](https://github.com/koyasi777/mozkey/releases).

- On ordinary 64-bit Windows, use the latest `Mozkey_v*_x64.msi` from Releases.
- For the Zenz-bundled build, choose an MSI whose file name contains `zenz` or `zenz_offline`.
- Releases from this fork are published as personal experimental builds.
- Zenz-bundled builds are larger than the traditional offline MSI because they include a local inference runtime and a GGUF model.

> [!WARNING]
> This build is not an official google/mozc distribution.
> It is an experimental / pre-release build from a personal fork.
> The MSI is not code-signed, so Windows may show a warning.

Main branches
-------------

- `main`: main branch for daily use
- `master`: upstream tracking branch
- `pr/*`: upstream-oriented proposal branches

Main features added in this fork
--------------------------------

- Adds an option to display ambiguous romaji rules before the input is fully disambiguated
- Adds a checkbox UI for that option to the romaji table editor
- Adds an option to directly commit punctuations and symbols with a single key press
- Allows choosing direct-commit punctuations and symbols from the config dialog
- Makes punctuation conversion and punctuation/symbol direct commit mutually exclusive
- Improves user-history learning behavior when a committed conversion is immediately corrected with Backspace or Cancel
- Keeps learning caused by direct-commit punctuations/symbols pending until the next real text input, and allows it to be reverted by Backspace, Escape, Revert, Reset, Undo, or IME off
- Adds live conversion that automatically converts the current composition and shows a ruby-like overlay for the original reading
- Allows enabling/disabling live conversion and configuring its debounce delay from the config dialog
- Applies live conversion after a short debounce delay to avoid noisy intermediate conversions
- Suppresses live conversion for one-character compositions to avoid over-converting particles
- Suppresses live conversion for very short kana compositions with decorative trailing symbols such as `え~`, `えー`, or `ん？`
- Uses committed left context, previous segments, and limited right context to reduce unnatural homophone results in cases such as `mainにマージしました`, `githubには`, `2名しかいない`, `追記したい`, `山梨県立美術館`, and `滋賀方面`
- Allows assigning multiple commands to a single key binding as an ordered command sequence
- Stores command sequences as `Commit|IMEOff` and shows them in the keymap editor as `Commit → IMEOff`
- Allows assigning left/right Shift and left/right Ctrl separately on Windows
- Shows the IME mode indicator even when a key assigned to IMEOn or IMEOff is pressed while Mozc is already in that state
- Adds explicit Windows default IME controls to the config dialog, with restore support for the previous default IME setting
- Adds a dark-mode switch for the Windows candidate window
- Allows customizing Windows preedit text color, background color, and underline color from the config dialog
- Adjusts the appearance of the Windows candidate window and IME mode indicator, including colors, spacing, rounded corners, and layout
- Makes the Windows IME mode indicator follow the Windows light/dark theme
- Adds an enhanced system dictionary generation pipeline
- Allows incorporating place names and SudachiDict-derived vocabulary from merge-ut-dictionaries into the system dictionary
- Allows adding internet/subculture proper nouns from dic-nico-intersection-pixiv as daily-dictionary differences
- Adds a syntax guard dictionary generation pipeline to reduce high-impact segmentation failures
- Keeps large generated dictionary files out of Git and switches Bazel dictionary inputs to locally generated files
- Reduces over-conversion of natural functional kana sequences such as `には` and `してたの`
- Reduces cases where short two-character hiragana inputs such as `にじ` are split too aggressively
- Adds a local Zenz live correction pipeline based on llama.cpp
- Sends Zenz correction requests from `mozc_server` to `mozc_zenz_scorer.exe` through a named pipe, and performs local inference through the localhost endpoint of `llama-server.exe`
- Allows configuring the Zenz inference debounce delay from the config dialog. The default is 1000 ms
- Allows configuring the minimum reading length for Zenz inference
- Adds optional local feedback learning for Zenz correction results
- Does not store Zenz feedback just because a Zenz correction was displayed. A visible Zenz result becomes pending accepted feedback only when the user explicitly commits it, such as with Enter or a direct-commit punctuation/symbol
- Writes pending accepted feedback to the local TSV only if it is not canceled by Backspace, Escape, Revert, Undo, IME off, or similar correction actions before the next real text input
- Treats a visible Zenz correction as rejected feedback when the user moves to normal conversion operations such as Space or candidate movement
- Adds a safe Zenz feedback management UI to the config dialog. Users can search, import, export, delete selected entries, and clear all entries without directly editing the TSV file
- Reuses accepted Zenz feedback for normal conversion candidate promotion. In single-segment conversions, an accepted feedback candidate is promoted to the top if it already exists, or inserted as a synthetic candidate if it does not
- Does not apply Zenz feedback promotion to multi-segment conversions, to avoid collapsing phrase boundaries
- Reuses accepted Zenz feedback via the session-level live-correction fast path for multi-segment live conversion to preserve learned full-phrase corrections
- Does not promote feedback obtained from `sensitive_like` context into ordinary context
- Learns accepted Zenz candidates into Mozc user history as external conversion results when the runtime conditions allow it
- Sanitizes left context before using it in Zenz prompts, and excludes sensitive-like context such as URLs, email addresses, file paths, tokens, and long digit sequences
- Stores only non-reversible context classes in Zenz feedback and never stores raw left context
- Bundles third-party license notices for the Zenz model and llama.cpp runtime in the MSI
- Includes build adjustments for my own Windows development environment

Examples
--------

### Ambiguous romaji display

With the custom option enabled:

- `ms -> ます`
- `mst -> ました`

Typing `ms` shows `ます` in preedit, and then typing `t` updates the preedit to `ました`.

If the input temporarily follows a longer ambiguous rule but the longer rule
later fails, Mozc falls back to the last displayed ambiguous result and replays
the remaining input.

For example, with rules such as:

- `ctn -> ことに`
- `ctnnr -> ことになる`
- `ctnnc -> ことなのか`

typing `ctnnaru` is interpreted as `ctn + naru`, resulting in `ことになる`,
while valid longer rules such as `ctnnr` and `ctnnc` still work.

### Live conversion

With live conversion enabled, Mozc automatically converts the current composition without committing it immediately.

To reduce distracting intermediate conversions, this fork applies live conversion after a short configurable debounce delay instead of converting every character immediately. Single-character compositions are not live-converted, because they are often particles such as `に`, `を`, or `が`.

Live conversion is also suppressed for very short kana compositions followed only by decorative trailing symbols, such as `え~`, `えー`, or `ん？`. This avoids noisy intermediate conversions such as `え~` becoming `絵～` while the user is still typing.

For example:

- Type `kyouha`
- After the debounce delay, the preedit can be shown as `今日は` before pressing Space
- Typing more characters keeps the same uncommitted composition and schedules another live conversion
- Pressing Backspace or Delete updates the live conversion result immediately
- Pressing Enter commits the current live conversion result

During live conversion, this fork shows a small ruby-like overlay window above the preedit text so that the original reading remains visible while the converted text is shown.

The live conversion feature can be enabled or disabled from the config dialog. The debounce delay can also be configured there.

### Zenz live correction

When both live conversion and Zenz live correction are enabled, this fork first
shows the normal Mozc live conversion result and then asynchronously asks a local
Zenz runtime to refine the visible preedit.

The Zenz request is sent from `mozc_server` to `mozc_zenz_scorer.exe` through a
Windows named pipe. The scorer then calls the bundled `llama-server.exe` on a
localhost endpoint for local inference. The localhost transport is hardened so
that it does not rely on a fixed endpoint, and internal requests include
protection against accidental or stale local endpoint mismatches.

Zenz correction is delayed by a configurable debounce interval. The default
delay is 1000 ms. If the current composition changes before the Zenz result
arrives, the old result is discarded by generation/key checks.

Zenz output is validated before display. Outputs that are empty, too short,
identical to the Mozc result, too long, malformed, or likely to contain unsafe
text are rejected. If validation fails, the normal Mozc live conversion result
remains visible.

Zenz live correction is disabled for password fields and for composition text
that has no Japanese-script signal, such as intermediate raw romaji input.
Japanese text mixed with ASCII can still be eligible when it passes the privacy
gate.

Zenz feedback learning is optional. When enabled, a displayed Zenz result is not
stored just because it was shown. It becomes a pending accepted feedback only
when the user explicitly commits the visible Zenz result, such as by pressing
Enter or by using a direct-commit punctuation/symbol.

Pending accepted feedback is written to the local TSV only if it is not canceled
by the next user action. Backspace, Escape, Revert, Undo, IME off, and similar
correction actions discard the pending feedback. Moving from a visible Zenz
correction to normal conversion operations, such as Space or candidate movement,
records the Zenz result as rejected feedback instead.

Accepted feedback is reused differently for single-segment and multi-segment
conversions.

For single-segment normal conversion, Zenz feedback is reused as candidate
promotion inside the rewriter chain. A learned feedback candidate can be
promoted to the top if it already exists, or inserted as a synthetic candidate
if it does not. ZenzFeedbackCandidateRewriter runs before
UserSegmentHistoryRewriter, so explicit user selection history can still make
the final ranking decision.

For multi-segment live conversion, the rewriter-chain feedback promotion does
not rewrite normal conversion candidates, because phrase boundaries are owned by
the converter and should not be collapsed. Instead, accepted feedback can still be replayed via
the session-level live-correction fast path. This preserves learned full-phrase
corrections such as `かれはてんてきです` -> `彼は天敵です`.

Feedback learned in a `sensitive_like` context is not promoted into ordinary-context conversion candidates.

Zenz feedback data can be managed from the config dialog. The management dialog
shows learned entries in a read-only table and supports search, import, export,
single-entry deletion, and full deletion. This avoids requiring users to edit the
TSV file directly.

### Context-aware conversion after committed text

This fork uses committed text immediately before the current composition as
left context when possible.

For example, after committing `main`, typing `にまーじしました`
is more likely to produce:

- `mainにマージしました`

instead of unnatural homophone results such as:

- `main二マージしました`

The left context is ignored after hard boundaries such as spaces, newlines,
punctuation, or brackets.

Internally, this fork reconstructs noun-like preceding text as a history segment
and reranks short particle candidates such as `に`, `を`, `が`, `へ`, and `で`
against homophone kanji, numeric, symbol, or dakuten-kana candidates.

It also reranks conservative compound functional-particle expressions such as
`には`, `にも`, `では`, `でも`, and `とは` after noun-like left context. For
example, after committing `github`, typing `には` is biased toward `githubには`
instead of `github二は`.

It also protects common functional expressions such as `しか` followed by a
negative expression after a noun or quantity-like context. For example,
`2めいしかいない` is biased toward `2名しかいない` instead of intermediate
homophone paths such as `2名司会...`, `2名視界...`, or `2名士会内`.

It also protects some context-sensitive noun and suffix constructions. For
example, `追記したい` is biased away from homophone noun candidates such as
`追記死体`, `山梨県立美術館` is biased away from broken suffix paths such as
`山梨県率美術館`, and place-suffix phrases such as `滋賀方面` or `佐賀空港`
are biased away from short `content + が` splits such as `子が方面`.

### Direct commit for punctuations/symbols

With the direct-commit option enabled, configured punctuations/symbols are committed immediately.

Examples:

- `tesuto.` → `てすと。`
- `tesuto?` → `てすと？`
- `kakko(` → `かっこ（`

You can choose which punctuations/symbols are committed directly in the config dialog.

When live conversion is enabled, direct-commit punctuations/symbols commit the currently displayed live conversion result instead of committing the raw kana composition.

For direct-commit punctuations/symbols, learning caused by the immediately
committed conversion is also kept pending until the next real text input. If the
next action is Backspace, Escape, Revert, Reset, Undo, or IME off, the pending
learning is reverted instead of being kept.

### Partial revert of history learning after immediate correction

When a committed conversion is immediately followed by Backspace, or by a key assigned to Cancel in the current keymap, this fork treats it as a revert signal for the just-committed learning result.

If the entire committed text is erased, the just-committed suggestion history and user segment history do not continue to affect later suggestions or conversions.

If only the tail of the committed text is erased with Backspace, the history for the remaining part is kept while the erased tail is reverted.

For example:

- Commit `いしとは` as `医師とは`
- Press Backspace twice and erase only `とは`
- The remaining `医師` history is kept
- The tail-specific learning for `医師とは` is reverted

This makes immediate correction after conversion confirmation behave closer to user intent.

### Multiple commands per key binding

The keymap editor can assign multiple commands to one key binding as an ordered command sequence.

Examples:

- `Composition + Ctrl Enter -> Commit → IMEOff`
- `Composition + Ctrl Space -> Convert → ConvertNext`
- `Conversion + Ctrl Enter -> Commit → IMEOff`

Commands are executed from left to right. If the input state changes during the sequence, the following command is resolved against the current state at that point. For example, `Convert → ConvertNext` first enters conversion from composition and then moves to the next candidate.

In exported keymap files, command sequences are stored with `|`, such as `Commit|IMEOff` or `Convert|ConvertNext`. In the keymap editor UI, they are displayed with arrows, such as `Commit → IMEOff`.

### Independent left/right Shift and Ctrl key bindings (Windows)

On Windows, left/right Shift and left/right Ctrl can be configured as separate keys in the keybinding editor.

For example:

- `DirectInput + RightShift -> IMEOn`
- `Precomposition + LeftShift -> IMEOff`
- `Composition + LeftShift -> IMEOff`
- `Conversion + LeftShift -> IMEOff`
- `DirectInput + RightCtrl -> IMEOn`
- `Precomposition + LeftCtrl -> IMEOff`

This allows assigning different IME actions to the left and right Shift/Ctrl keys.

Generic Ctrl key bindings such as `Ctrl j` continue to work with either left or right Ctrl.

When a key is explicitly assigned to `IMEOn` or `IMEOff`, the mode indicator is
also shown even if Mozc is already in the requested state. For example, pressing
an `IMEOff` key while IME is already off still shows the direct-input indicator.
This makes mode-confirmation keys useful as explicit visual feedback, not only
as state-changing toggles.

### Windows default IME setting

On Windows, the config dialog can explicitly set Mozkey as the Windows default IME.

This operation is not performed automatically at login. It is executed only when the user presses the setting button.

Before changing the setting, Mozkey saves the previous Windows default input method override and the Japanese input method order. The restore button restores those saved values.

If Mozkey is already the default IME, or if an active restore point already exists, Mozkey does not overwrite the previous restore point.

### Windows candidate window and IME indicator themes

On Windows, the candidate window can be switched between the default light theme and a dark theme from the config dialog.

The dark theme also adjusts the candidate window appearance, including colors, spacing, rounded corners, and footer visibility, to make it look more modern.

The IME mode indicator follows the Windows light/dark theme and changes its colors to keep the current input mode easy to recognize.

### Windows preedit display colors

On Windows, preedit display colors can be customized from the config dialog.

The following display attributes can be configured separately for input text and the converting segment:

- Text color
- Background color
- Underline color

This is intended to improve the visibility of uncommitted text, especially for users who need stronger visual contrast while composing or converting Japanese text.

The setting is applied through Windows TSF display attributes. It works only in applications that honor those attributes. Some applications, including Chrome and Edge, may ignore them.

Enhanced system dictionary
--------------------------

This fork includes scripts to build an enhanced Mozc system dictionary from
external dictionary sources.

The daily local dictionary can be generated from:

- merge-ut-dictionaries
  - place names
  - SudachiDict-derived vocabulary
- dic-nico-intersection-pixiv
  - additional proper nouns, internet slang, works, characters, and subculture terms
  - entries already covered by the generated daily dictionary or the base Mozc dictionaries are skipped
- Koyasi syntax guard dictionary
  - small generated guard entries for high-impact segmentation failures
  - for example, protecting grammar-like paths such as `と打ちたいのに` and `に分ける`

Large generated dictionary files are not committed to this repository.
They are generated locally under `src/data/dictionary_koyasi/generated/`.

Before building a package with the enhanced dictionary enabled, regenerate the daily dictionary locally:

```powershell
.\tools\dictionary\prepare_daily_dictionary.ps1
```

See:

- [Koyasi Dictionary Data](src/data/dictionary_koyasi/README.md)

Note
----

This fork is primarily maintained for personal use.
Upstream-oriented changes are organized in `pr/*` branches.

以下は upstream の README です。

[Mozc - a Japanese Input Method Editor designed for multi-platform](https://github.com/google/mozc)
===================================

Copyright 2010-2026 Google LLC

Mozc is a Japanese Input Method Editor (IME) designed for multi-platform such as
Android OS, Apple macOS, Chromium OS, GNU/Linux and Microsoft Windows.  This
OpenSource project originates from
[Google Japanese Input](http://www.google.com/intl/ja/ime/).

Mozc is not an officially supported Google product.


What's Mozc?
------------
For historical reasons, the project name *Mozc* has two different meanings:

1. Internal code name of Google Japanese Input that is still commonly used
   inside Google.
2. Project name to release a subset of Google Japanese Input in the form of
   source code under OSS license without any warranty nor user support.

In this repository, *Mozc* means the second definition unless otherwise noted.

Detailed differences between Google Japanese Input and Mozc are described in [About Branding](docs/about_branding.md).

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
