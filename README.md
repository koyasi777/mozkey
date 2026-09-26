<p align="center">
  <img src="src/data/images/icon.svg" width="112" height="112" alt="Mozkey icon">
</p>

<h1 align="center">Mozkey（もずきー）</h1>

<p align="center">
  <strong>Mozc をベースに、遅延付きライブ変換・ローカル Zenz 補正・外観カスタマイズ機能<br>句読点単打確定・文脈を見た変換補正などを統合した、ローカルファーストな日本語入力 fork です。</strong>
</p>

<p align="center">
  <a href="https://github.com/koyasi777/mozkey/releases"><img alt="Releases" src="https://img.shields.io/github/v/release/koyasi777/mozkey?include_prereleases&label=release"></a>
  <img alt="Based on Mozc" src="https://img.shields.io/badge/based%20on-Mozc-88A2DD">
  <img alt="Local first" src="https://img.shields.io/badge/local--first-Zenz-53D4C7">
  <img alt="Release build" src="https://img.shields.io/badge/release-Windows%20MSI-178B8B">
  <img alt="macOS status" src="https://img.shields.io/badge/macOS-tested-2EA44F">
  <img alt="Linux status" src="https://img.shields.io/badge/Linux-untested-lightgrey">
</p>

<br>

Mozkey（もずきー）は [google/mozc](https://github.com/google/mozc) をベースにした非公式フォークです。

本 fork は、主に自分の Windows / macOS 環境で日常的に使うために、Mozc に入力補助・ライブ変換・文脈補正・ローカル Zenz 補正・オフライン配布向けの調整を加えたものです。

本プロジェクトは Google 日本語入力ではありません。
Google または google/mozc の公式配布物ではありません。
Google によるサポートや品質保証の対象ではありません。

upstream Mozc との追従性および既存インストールとの互換性を保つため、
一部の内部実行ファイル名、パス、実装上の識別子には `mozc` / `Mozc` 名が残ります。

<br>

ダウンロード / インストール
--------------------------

現時点で Releases から公開しているビルド済みパッケージは Windows 向けです。

Windows に加え、macOS でもこの fork を実機で検証しています。macOS についても、ビルドだけでなく、実際の PKG / install / runtime / 入力・表示経路まで確認しています。

Linux については、upstream Mozc 自体は対応していますが、この fork 固有の Zenz 構成や追加機能はまだ実機確認できていません。

Windows 用のビルド済み MSI は [Releases](https://github.com/koyasi777/mozkey/releases) からダウンロードできます。

- 通常の x64 Windows では、Releases にある最新の `Mozkey_v*_x64.msi` を使用してください。
- Windows on Arm（ARM64）では、その release に ARM64 と明記された MSI が含まれる場合は ARM64 版を使用してください。ARM64 MSI は Mozkey 本体、`mozc_zenz_scorer.exe`、`llama-server.exe` を native ARM64 payload として構成します。
- `universal` MSI は x64 / ARM64 の全実行ファイルを dual-native 化した whole-product package ではありません。通常の配布選択では architecture-specific な x64 / ARM64 MSI を優先してください。
- 本 fork のリリースは個人用の experimental build として公開しています。
- Zenz 同梱版は、ローカル推論 runtime と GGUF model を含むため、従来の offline MSI よりファイルサイズが大きくなります。
- Windows 向けリリース MSI には、ローカル生成した `daily` system dictionary profile を同梱する場合があります。
- `daily` profile には、Mozc 標準辞書に加えて、merge-ut-dictionaries、dic-nico-intersection-pixiv、mozcdic-ut-personal-names、Mozkey syntax / expressive kana guard dictionary 由来の生成辞書が含まれます。
- 外部辞書・同梱 runtime・model の出典とライセンス note については [Third-party notices](THIRD_PARTY_NOTICES.md) を参照してください。

> [!WARNING]
> このビルドは google/mozc の公式配布物ではありません。
> 個人用 fork の experimental / pre-release build です。
> MSI は署名されていないため、Windows の警告が表示される場合があります。

<br>

主な追加機能
---------------------------

- 曖昧なローマ字規則でも途中表示できるオプションを追加
- ローマ字テーブル編集画面に、そのオプション用のチェックボックス UI を追加
- カスタムローマ字テーブルの 1 規則あたりの出力を UTF-8 で 1024 bytes 未満まで扱えるように拡張。入力・次入力の上限は従来どおり 300 bytes 未満
- 句読点・記号を単打で確定できるオプションを追加
- 単打確定の対象を設定画面のチェックボックスで選択可能
- 句読点変換と句読点・記号の単打確定は排他的に動作
- 変換確定直後に Backspace や Cancel キーで取り消した場合のユーザー履歴学習の扱いを改善
- 変換中に Esc / Ctrl+Z などのキャンセル操作でひらがなへ戻し、そのまま Enter または句読点・記号の単打確定で確定した場合、F6 ひらがな化確定に近い表記選好として学習されるようにした
- 複数文節の変換キャンセル後は、キャンセル直前の文節境界を可能な限り保持し、全体 1 件としてではなく文節単位のひらがな表記選好としてユーザーセグメント履歴へ反映
- 句読点・記号の単打確定でも、直前の通常変換確定による学習を次の実テキスト入力まで保留し、Backspace / Escape / Ctrl+Z などの Cancel 相当キー / Revert / Reset / Undo などでは取り消し、IMEOff / MakeSureIMEOff では確定扱いにするようにした
- ライブ変換機能を追加。未確定文字列を自動変換し、確定前の読みをルビ風 overlay で表示
- ライブ変換は設定画面から ON/OFF、変換開始までの遅延時間、変換開始の最小文字数を変更可能
- ライブ変換を使わない場合も、既定で初回の変換操作で第1候補のまま候補ウィンドウを開ける（設定で無効化可能）
- ライブ変換は入力直後の不要な変換ちらつきを抑えるため、文字入力後に短いデバウンスを挟んで実行
- デフォルトでは 1 文字だけの未確定文字列で、助詞などの誤変換を避けるためライブ変換を実行しない
- `え~`、`えー`、`ん？` のような「かな1文字 + 装飾的な末尾記号」でも、短すぎる漢字化を避けるためライブ変換を抑制
- 感嘆詞・口語評価語・くだけた挨拶の入力途中ではライブ変換のちらつきを抑えつつ、完成した `うっそ`、`くっそ`、`やっば`、`すっげぇ`、`めっちゃ`、`ちっす`、`ちょりっす`、`ほえ～`、`ほぇ～`、`ほっほーん` などは生成辞書候補として自然なかな表記を救出
- 確定済みの左文脈や直前の文節、限定的な右文脈を参照し、`mainにマージしました`、`githubには`、`彼になった`、`彼なのか`、`2名しかいない`、`追記したい`、`山梨県立美術館`、`滋賀方面` のような文脈で、助詞・複合機能語・名詞相当の左文脈に続く叙述・疑問の機能語列・機能表現・接尾的な語構成・地名接尾構成が同音漢字候補に負ける挙動を抑制
- キー設定エディタで、1つのキー入力に対して複数のコマンドを順序付きで割り当て可能
- 複数コマンドは `Commit|IMEOff` のような形式で保存され、設定画面では `Commit → IMEOff` のように編集可能
- MS-IME 風キー設定では、確定済み文字列を選択した状態で Space を押すと再変換し、未選択時は従来どおり空白を入力
- Windows 版で左 Shift / 右 Shift / 左 Ctrl / 右 Ctrl を個別キーとして設定画面から割り当て可能
- Windows 版で IMEOn / IMEOff に割り当てたキーを押した場合、すでに同じ状態でも IME モードインジケータを表示
- Windows 版の設定画面から、Mozkey を Windows の既定 IME として明示的に設定し、変更前の既定 IME 設定へ戻せるボタンを追加
- Windows 版の設定画面から、タスクバーや IME 一覧に表示される Mozkey の IME アイコンを、既定 / モノクロ（黒）/ モノクロ（白）から選択可能
- Windows Search などの immersive TSF ホストでは、候補ウィンドウ・サジェストウィンドウ・ライブ変換中のルビをホストプロセス内の renderer 経路で表示し、前面表示とマウス操作を安定化。未選択のサジェストは mouse-down だけで通常変換へ移行せず、mouse-up で実際に押したサジェストを確定
- Windows / macOS 版の候補ウィンドウ・サジェストウィンドウ・ライブ変換中のルビ表示について、ライト / ダーク / 自動（システムテーマに合わせる）/ カスタム配色、サイズ、角丸、透明度、影を設定画面から個別に調整可能。新規設定では3項目とも「自動」が既定
- Windows 版のルビ表示は、表示先モニターの DPI に合わせて位置・サイズを補正し、左右の余白、上下の余白、入力文字との距離を設定可能
- サジェストウィンドウとルビ表示は、候補ウィンドウの配色に追従するか、個別のテーマ・カスタム配色を使うかを選択可能。「自動」は各ウィンドウがシステムテーマを直接参照し、「候補ウィンドウに追従」とは別の設定
- Windows 版の候補ウィンドウ・用例ウィンドウ・ライブ変換中のルビ表示に使うフォントを設定画面から変更可能
- Windows 版の候補ウィンドウ・サジェストウィンドウ・ライブ変換中のルビ表示について、主要テキストの太さを 100～900 の範囲で個別に設定可能
- Windows / macOS 版の縦書き入力で、候補ウィンドウ・サジェスト・用例表示・ライブ変換中のルビを縦書きレイアウトとして表示し、対応するキー設定では候補・文節移動も視覚方向に合わせて操作可能
- Windows / macOS 版では、縦書き時の連鎖候補ウィンドウを現在候補から本文の外側へ展開するよう配置し、必要に応じて反対側へフォールバック
- ライブ変換中のルビ表示を設定画面から ON/OFF 可能
- Windows 版で未確定文字の文字色・背景色・下線色を設定画面からカスタマイズ可能
- Windows 版のローマ字入力で、変換規則の途中にある末尾の未確定ローマ字だけを薄く表示する opt-in 機能を追加。既定は OFF で、薄さは 0～90%（既定 75%）の範囲で調整可能
- Windows 版の IME 切り替えインジケータは、設定画面から「システムテーマに合わせる / ダーク / ライト / カスタム」を選択可能。カスタムでは入力モード別の配色とサイズ・角丸・枠線・影を調整でき、画面端付近では表示位置を自動調整
- system dictionary 強化用の追加辞書生成パイプラインを追加
- 日常語彙・実務語彙・外来語・英語綴り候補を小さな manual override 辞書として段階的に補強し、通常語彙は自然な第一候補、英語綴りは補助候補として扱う評価運用を追加
- merge-ut-dictionaries 由来の地名・SudachiDict 系語彙を system dictionary に取り込めるようにした
- dic-nico-intersection-pixiv 由来のネット・サブカル系固有名詞を、既存辞書との差分として daily 辞書に追加可能
- mozcdic-ut-personal-names 由来の人名辞書を、既存辞書や nico/pixiv delta との重複を除いた弱い daily 辞書として追加可能
- 文節区切り崩れを抑えるための syntax guard 辞書を daily 辞書生成パイプラインに追加し、`と打ちたいのに`、`に分ける`、`した方が`、`したにもかかわらず`、`にまで`、`までに`、`までも`、`肌身離さず` などの高影響な経路を保護
- 大規模な生成辞書は Git に含めず、ローカルで再生成して Bazel の辞書入力へ切り替える運用に
- `には` や `してたの` のような自然な機能語かな列が、`二は` や `して他の` のような 1 文字漢字候補に過剰変換される挙動を抑制
- `にじ` のような 2 文字ひらがな入力で、`に|じ` のような短すぎる文節分割が全体候補を隠す挙動を抑制
- llama.cpp ベースのローカル Zenz live correction pipeline を追加
- Zenz 文脈処理を共通化し、通常 Mozc の `preceding_text` / `following_text` とは分離した `zenz_preceding_text` / `zenz_following_text` を使用
- Zenz が必要とする preceding / following の長さを Server から Client へ通知し、Windows TSF / macOS IMK では要求された方向・長さだけ surrounding text を追加取得
- Zenz の前方・後方文脈を用途に応じて独立して選択し、Unicode-aware な文字種判定と privacy filtering を共通処理として適用
- Windows 版では、Zenz 補正を `mozc_server` から named pipe 経由で `mozc_zenz_scorer.exe` に依頼し、`llama-server.exe` の localhost endpoint でローカル推論
- Zenz 補正開始までの遅延時間を設定画面から変更可能。デフォルトは 1000 ms
- Zenz 補正開始の最小文字数を設定画面から変更可能
- 「Zenz 補正結果が返るまで通常のライブ変換結果を表示しない」opt-in 設定を追加。待機中は直前まで安定して表示されていた Zenz 補正部分を可能な範囲で維持し、未解決のローマ字 suffix はその表示へ追従。Zenz が採用されない場合は現在の Mozc ライブ変換結果へフォールバック
- deferred 表示専用の最大待機時間を設定画面から変更可能。既定は Zenz 補正要求の送信後 300 ms で、期限を超えた generation は破棄して、すでに計算済みの Mozc ライブ変換結果へフォールバック
- deferred 表示中の Enter / Shift による英字入力では、裏側の Mozc baseline ではなく、その時点でユーザーに見えている presentation を確定。Enter 確定後の Undo でも同じ presentation を復元
- Zenz 出力が確定済み左文脈の長い suffix を現在入力の先頭へ反復する context echo を検出して拒否し、通常の Mozc ライブ変換結果へフォールバック
- Zenz 補正結果のローカル feedback learning を追加。設定画面から ON/OFF 可能
- Zenz feedback の自動ブロック設定を追加。同じ読み全体・同じ文脈クラス・同じ補正結果について、通常却下回数が「最低拒否回数」と「採用回数 + 通常却下回数に占める最低拒否割合」の両方に達した場合だけ自動ブロックする。未保存の設定では既定で有効、最低拒否回数は 1 回、最低拒否割合は 50%。自動ブロックは TSV に hard reject を固定保存せず、現在の設定と既存 feedback から動的に再評価する
- 自動ブロック中も Zenz 推論は shadow observation として継続する。同じ読みで非表示の Zenz 結果と最終確定値が一致すれば accepted feedback を加算して自己回復でき、拒否割合がしきい値を下回れば自動的にブロックを解除する。同じ読みで異なる値が確定した場合は通常却下を加算し、入力継続などで最終的な読みが変わった場合は neutral として扱う。明示的な hard reject はこの自己回復の対象外
- 同じ読み全体・同じ文脈クラス・同じ補正結果で通常却下回数が採用回数を上回る場合は、auto-block 無効時でも Zenz feedback による優先候補・保存済み feedback による即時補正としては使わず、「却下数優勢」の中立状態として扱います。これは hard block ではなく、Zenz が新しく同じ補正を返すことや通常 Mozc 候補を削除することはありません。
- Zenz feedback は、Zenz 補正結果が表示されただけでは保存されません。Enter や句読点・記号の単打確定などで表示中の Zenz 結果が明示的に確定された場合だけ、accepted feedback の候補として保留されます。
- 保留された accepted feedback は、次の実テキスト入力まで Backspace / Escape / Revert / Undo などで取り消されなかった場合にローカル TSV へ保存。IMEOff / MakeSureIMEOff は取り消しではなく確定後のモード変更として扱う
- Zenz 補正表示後に Space や候補移動など通常変換操作へ移った場合、その Zenz 候補は rejected feedback として扱う。ただし Space などの通常操作による rejected feedback は候補を殺す hard reject ではなく、順位調整用の弱い negative signal として扱う
- Zenz 補正表示中に Space で Mozc 通常変換結果へ戻した場合は、次の文字入力で戻した変換結果を確定してから新しい入力を開始
- Zenz 学習データを設定画面から安全に管理できる UI を追加。TSV を直接編集せず、検索、インポート、エクスポート、選択項目削除、全削除が可能
- Zenz 学習データ管理画面から、選択した Zenz 補正を明示的にブロック可能。ブロック済みの補正は再ブロックできず、解除したい場合は該当エントリを削除して必要に応じて再学習する
- Zenz feedback を通常変換候補の ranking に利用。1 文節の通常変換では、保存済み feedback を score 化し、既存候補があれば cost を調整し、候補にない場合は synthetic candidate として候補集合の自然な位置へ追加。accepted feedback は順位を上げ、通常操作由来の rejected feedback は候補を除外せず順位を下げる
- 文節境界を壊さないため、複数文節に分かれた通常変換では Zenz feedback による通常候補 ranking を行わない
- 複数文節に分かれるライブ変換では、全文補正の学習を保つため、accepted Zenz feedback を session-level live correction fast path として再利用
- sensitive-like context で得られた feedback は、通常文脈の候補 ranking / reuse には使わない
- accepted として確定した Zenz 候補は、条件を満たす場合は Mozc の user history にも外部変換結果として学習
- accepted Zenz 補正を直前の通常 Mozc ライブ変換文節へ安全に逆投影できる場合は、文節列全体を外部 multi-segment commit として Mozc history に学習。通常変換候補を再利用できる場合は candidate 構造も引き継ぎ、Zenz が実際に変更した文節だけを強い選択履歴として扱う
- 通常 Mozc ライブ変換で現在の結果として現れているユーザー辞書由来候補や ASCII / mixed-script 表記を、Zenz live correction の採用時に保護
- ASCII / mixed-script 表記は、読みを安全に特定できる場合に Zenz prompt 内で一時 placeholder 化し、応答後に元の表記へ復元。`もずきー -> Mozkey` のような表記が `モズキー` へ上書きされるのを避けつつ、前後の文は補正できるようにした
- 通常 Mozc が選んだ表記を orthographic baseline として保持し、`東京 -> Tokyo`、`コンピュータ -> computer` のように Zenz だけが新しい英字表記を持ち込む場合は、文節へ安全に逆投影できる範囲で該当部分だけ通常 Mozc 表記へ戻す。`GitHub`、`iPhone`、`AI`、`C++`、`GPT-5` など、通常 Mozc がすでに選んでいる Latin / technical token は維持する
- ASCII の数字リテラル（符号付き整数・小数を含む）は Latin / technical token とは分離して保護し、Zenz prompt 内では placeholder 化せずに残す。数字表記そのものと非日本語の隣接境界は維持しつつ、`1日`、`100円`、`スマートフォン1台`、`第1回`、`3.14倍` のような日本語隣接部分は Zenz で補正可能にする
- Zenz が `（ ）` / `( )`、`？` / `?`、`！` / `!`、`：` / `:` などの記号幅・記号スタイルを正規化して返した場合でも、元の未確定文字列または通常 Mozc ライブ変換結果でユーザーが使っていた表記へ復元
- Zenz が左文脈などから `！` / `!`、`？` / `?`、`。`、`…`、`〜` などの文末・表現記号を、現在の未確定文字列または通常 Mozc ライブ変換結果の末尾にある数を超えて追加した場合は、余分な末尾記号だけを抑制し、漢字・語彙などの補正結果は保持
- 日本語のみのユーザー辞書語は、自然な読みを Zenz prompt に残したまま、Zenz 応答後に表記の境界を検証し、余分なかな付着を安全に修復できる場合だけ採用するようにした
- Zenz prompt に使う左文脈は sanitizer を通し、URL、email、file path、token、長い数字列など sensitive-like な文脈は prompt に含めない
- Zenz feedback は full-sequence 単位だけを保存し、raw left context や segment-local feedback は保存せず、非可逆な context class のみを保存
- Zenz model / llama.cpp runtime の third-party license notice を MSI に同梱
- 自分の Windows / macOS 開発環境向けのビルド調整

<br>

プライバシー / ネットワークアクセス
------------------------------------

この fork は、通常の IME 利用時に Mozc の実行時プロセスがネットワーク通信を行わない構成を目指しています。

upstream 由来の使用統計・クラッシュレポート送信オプションは、管理ダイアログおよび設定ダイアログから削除しています。

この fork では `StatsConfigUtil` のデフォルト実装を Null 実装に固定しており、通常の実行経路から使用統計を有効化できません。

Windows 向けリリースバイナリについては、Mozc core runtime executable が `winhttp.dll`, `wininet.dll`, `urlmon.dll` などの代表的なネットワーク関連 DLL を import していないことを検査します。

この fork では、llama.cpp ベースのローカル Zenz 推論 runtime を同梱する場合があります。Zenz runtime はローカル推論用として使用し、外部ネットワークサービスを公開したり、入力内容を外部サーバーへ送信したりする目的のものではありません。

Windows 向け Zenz 同梱構成では、`llama-server.exe` を `127.0.0.1` のみに bind するローカル推論 server として使用します。Windows 版の Zenz helper process は、同梱された `llama-server.exe` と localhost endpoint のみで通信します。この local HTTP endpoint は推論処理を分離するための内部的なプロセス境界であり、外部ネットワークアクセスを目的としたものではありません。

Zenz の localhost 通信は、固定 endpoint に依存しないようにし、内部 request も誤接続を避けるための保護を加えています。

Zenz feedback learning は、完全な読み、完全な候補、粗い文脈クラス、採用/却下回数、理由 marker などの full-sequence 単位のローカル学習情報だけを保存します。生の左文脈は保存しません。feedback に使う文脈は、`empty`、`japanese_only`、`japanese_with_punctuation`、`mixed_japanese_ascii`、`sensitive_like` などの非可逆な context class に落とします。segment-local や lexical-unit の学習は Zenz feedback TSV には保存せず、安全な局所学習は Mozc history 側の責務として扱います。

さらに、リリース時には Mozc core runtime binaries にテレメトリ、アップデータ、クラッシュアップロード、使用統計関連の危険な marker が含まれないことを確認します。

`http://`, `https://`, `googleapis.com` などの汎用的な URL 風 marker は、manifest、XML namespace、コメント、license file、ライブラリ metadata に由来する場合があるため、監査用に表示しますが hard failure にはしていません。

ソースからビルドする場合、ビルド依存関係の取得にネットワーク接続が必要になる場合があります。これは、インストール済み IME の実行時通信とは別です。

Windows 版では、追加のオフライン防御層として、インストーラーが Mozc の実行ファイルに outbound 通信をブロックする Windows Firewall rule を追加します。Zenz 同梱構成では `mozc_zenz_scorer.exe` と `llama-server.exe` も対象です。これらの rule はアンインストール時に削除されます。

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

### 長いカスタムローマ字テーブル出力

カスタムローマ字テーブルでは、1 規則あたりの出力を UTF-8 で 1024 bytes 未満まで扱えます。入力と次入力の上限は従来どおり 300 bytes 未満です。

従来は入力・出力・次入力のすべてが 300 bytes 未満に制限されていたため、一般的な 3-byte UTF-8 の日本語文字では、1 規則の出力は最大 99 文字相当でした。この fork では出力だけを拡張し、最大 1023 bytes、3-byte 文字なら 341 文字相当まで扱えるようにしています。

この上限はローマ字テーブルの 1 規則に設定する出力に対するものです。変換器全体の入力長制限を変更するものではありません。

### ライブ変換

ライブ変換を有効にすると、スペースキーを押さなくても、入力中の未確定文字列が自動で変換されます。

入力途中の不要な中間変換表示を抑えるため、この fork では文字入力後に短い設定可能なデバウンス時間を挟んでからライブ変換を実行します。`に`、`を`、`が` のような助詞として使われやすい入力を誤って漢字化しないように、デフォルトでは 1 文字だけの未確定文字列ではライブ変換を行いません。ライブ変換を開始する最小文字数は設定画面から変更できます。

また、`え~`、`えー`、`ん？` のように、かな1文字の後ろに装飾的な記号だけが続く場合もライブ変換を抑制します。これにより、入力途中の `え~` が `絵～` のように短すぎる漢字候補へ変換される挙動を避けます。

さらに、短い感嘆詞、口語的な評価語、くだけた挨拶などでは、入力途中の prefix や pending roman suffix によるライブ変換のちらつきを抑えます。一方で、完成した表現は session 側でライブ変換を止めず、converter と辞書候補に渡します。

完成した expressive kana については、生成辞書に自然なかな候補を追加します。これにより初期状態では不自然な漢字分割に寄りにくくしつつ、ユーザーが `ウッソ`、`クッソ`、`ヤッバ`、`チッス`、`ホェ～` などのカタカナ表記を明示的に選んだ場合には、ユーザー履歴やユーザー辞書による表記選好が反映される余地を残します。

対象には、たとえば以下のような入力が含まれます。

- `うっそ`、`くっそ`、`やっば`、`やっべぇ`
- `すっご`、`すっげぇ`、`めっちゃ`
- `ちっす`、`ちょっす`、`ちょりっす`
- `うひょ`、`うひゃ`、`ほほう`、`ほっほーん`
- `ほえー`、`ほえ～`、`ほぇ`、`ほぇー`、`ほぇ～`

これらは通常変換そのものを禁止するものではありません。完成した表現は通常の変換候補として扱われるため、Space 変換やユーザー履歴による候補順位の調整も従来どおり有効です。

たとえば:

- `kyouha` と入力
- デバウンス時間の経過後、Space を押す前に未確定文字列が `今日は` のように表示される
- 続けて文字を入力しても途中の変換結果は確定されず、同じ未確定文字列として再変換される
- ローマ字テーブルで `v. -> …` や `v, -> ‥` のような省略記号を入力する場合も、直前まで表示されていたライブ変換結果を保ったまま入力を継続する
- Shift による英字入力へ移る場合は、表示中のライブ変換結果を先に確定してから英字入力を開始する
- Backspace / Delete では、削除後の状態をすぐにライブ変換結果へ反映する
- Enter で現在のライブ変換結果を確定する

ライブ変換中は、変換後の文字を表示しながら元の読みも分かるように、未確定文字の上付近に Mozc 独自のルビ風 overlay window を表示します。

ライブ変換は設定画面から ON/OFF を切り替えられます。また、変換開始までの遅延時間と、ライブ変換を開始する最小文字数も設定画面から変更できます。

### ライブ変換 OFF 時の初回変換操作での候補表示

ライブ変換を使わない場合の補助機能として、初回の変換操作で候補ウィンドウを開く設定を追加しています。この設定は既定で有効です。

この設定が有効な場合、未確定文字列の入力中に通常変換コマンドを実行した時点で、第1文節の候補ウィンドウを表示します。このとき選択候補は第1候補のままで、第2候補へは進みません。さらに次候補キーを押した場合は、従来どおり次候補へ進みます。

この機能はライブ変換がオフの場合だけ適用されます。Space 以外のキーに通常変換を割り当てている場合も、keymap で `Convert` に解決された初回の変換操作で同じように適用されます。ライブ変換がオンの場合は、既存のライブ変換中の Space 操作、つまり通常変換候補への移行と候補移動の挙動を優先します。従来の表示タイミングに戻したい場合は、設定画面から無効化できます。

### Zenz ライブ補正

ライブ変換と Zenz ライブ補正の両方を有効にすると、通常は Mozc のライブ変換結果を表示したうえで、ローカルの Zenz runtime に非同期で補正を依頼します。

設定画面の「Zenz 補正結果が返るまで通常のライブ変換結果を表示しない」を有効にすると、Zenz 補正を実行する入力では、新しい Mozc ライブ変換結果を応答待ちの間は先に表示しません。すでに安定して表示できている Zenz 補正部分がある場合は、その表示を可能な範囲で維持し、入力途中の未解決ローマ字 suffix はその後ろへ追従表示します。Mozc 側の再文節化だけを理由に、一度安定した Zenz 表示を不用意に細分化・巻き戻ししないように扱います。Zenz が失敗、タイムアウト、検証拒否、または Mozc と同一の結果になって採用されなかった場合は、その時点の通常 Mozc ライブ変換結果へフォールバックします。

この deferred 表示には、Zenz 補正要求を実際に送信してからの最大待機時間を設定できます。既定は 300 ms です。この期限までに結果が得られなければ、その generation の Zenz 補正を中止して、すでに計算済みの Mozc ライブ変換結果を表示します。遅れて返った同じ generation の Zenz 結果は採用しません。Zenz 補正開始前のデバウンス時間はこの 300 ms には含まれません。

Windows 版では、Zenz request は `mozc_server` から Windows named pipe 経由で `mozc_zenz_scorer.exe` に送られます。scorer は同梱された `llama-server.exe` の localhost endpoint を呼び出し、ローカル推論を行います。この localhost 通信は固定 endpoint に依存しないようにし、内部 request も誤接続を避けるための保護を加えています。

Windows の Zenz named-pipe 通信は、接続・request/response I/O を有限の deadline で扱い、停止時には待機中の I/O を明示的に解除します。scorer または peer が応答しない場合でも Zenz worker / scorer が無期限に待機せず、stall から復帰した後は次の request を処理できるようにしています。macOS の Unix-domain socket 経路でも、停止時に待機中の poll を明示的に解除します。

Zenz に渡す surrounding context は、通常 Mozc の generic context とは分離した専用 field を使用します。Server は Zenz 補正に必要な preceding / following length を Client へ通知し、Windows TSF と macOS IMK は要求された方向・長さだけを追加取得して、`zenz_preceding_text` / `zenz_following_text` として渡します。通常 Mozc の surrounding text semantics は変更しません。

Zenz 補正は設定可能なデバウンス時間の後に実行されます。デフォルトは 1000 ms です。また、Zenz 補正を開始する最小文字数も設定画面から変更できます。Zenz 結果が返る前に入力内容が変わった場合、古い結果は generation / key の検査により破棄されます。

deferred 表示で Zenz の応答を待っている間に Space を押した場合は、候補を次へ進めず、まずその時点の Mozc 通常変換結果を表示します。Enter はその瞬間に見えている presentation をそのまま確定し、Undo では同じ presentation を未確定状態として復元します。Shift による英字入力へ移る場合も、表示中の presentation を先に確定してから英字入力を開始します。Backspace では削除後の Mozc 結果を即時に反映し、そのキー操作だけで新しい Zenz 補正を直ちに走らせず、続く実テキスト入力から通常の deferred Zenz scheduling を再開します。

Zenz 出力は表示前に検証されます。空出力、短すぎる入力、Mozc 結果と同一の出力、長すぎる出力、不正な文字列、安全でない可能性のある文字列は拒否されます。さらに、確定済み左文脈の長い末尾部分を Zenz 出力の先頭へ繰り返す context echo についても、現在の読み・Mozc 結果から見て不自然に長い反復だけを検出して拒否します。短い一致や、現在の読みそのものが同じ語句の反復を要求している場合は一律には拒否しません。拒否された場合は、その時点の通常 Mozc ライブ変換結果へフォールバックします。

通常 Mozc ライブ変換で現在の結果として現れているユーザー辞書由来候補や ASCII / mixed-script 表記は、Zenz 採用時に保護されます。ASCII / mixed-script 表記は、読みを安全に特定できる場合に Zenz prompt 内で一時 placeholder 化し、Zenz 応答後に元の表記へ復元します。これにより、`もずきー -> Mozkey` のような表記が `モズキー` のように上書きされることを避けつつ、対象語の前後にある文の補正は採用できるようにしています。

通常 Mozc が選んだ表記は、Zenz 採用時の orthographic baseline としても扱います。Zenz だけが `東京 -> Tokyo` や `コンピュータ -> computer` のような Latin 表記を新たに導入したり、通常 Mozc が選んだ Latin / technical token を削除・変形したりした場合、文節境界へ安全に逆投影できるときは問題のある文節だけを通常 Mozc の表記へ戻し、ほかの Zenz 補正は維持します。安全に逆投影できない場合や、保存していた baseline 文節が欠落・不整合な場合は、表記上安全でない遷移を含む Zenz 結果を採用しません。一方、通常 Mozc がすでに `GitHub`、`iPhone`、`AI`、`C++`、`GPT-5`、`UTF-8`、`HTTP/2`、`Windows11` などを選んでいる場合、それらは正当な baseline として維持します。

ASCII の数字リテラルは、この Latin / technical identity とは別に扱います。符号付き整数・小数を含む数字表記そのものは Zenz prompt 内で placeholder 化せず、応答後も同じ表記・同じ出現数を要求します。各数字リテラルの左右について、通常 Mozc の baseline で隣接文字が日本語なら Zenz 側でも日本語文字であることを要求し、日本語部分の変更は許可します。一方、baseline の隣接文字が非日本語なら同じ文字を、文字列端なら同じ端位置を要求します。これにより `1代 -> 1台` のような助数詞・単位側の補正や周囲の文脈補正を許可しつつ、`100円 -> 100¥`、`100% -> 100$`、`2026/09/23 -> 2026-09-23`、`HTTP/2 -> HTTP 2` のように数字自体または非日本語境界を変える出力は採用しません。

また、Zenz が括弧、疑問符、感嘆符、一部の全角 ASCII 記号 などを正規化して返した場合でも、採用前にユーザー可視の記号スタイルを復元します。これは全角化ではなく、現在の未確定文字列または通常 Mozc ライブ変換結果に現れていた表記の保存です。たとえば `（テスト）` は `（ ）` のまま、`(test)` は `( )` のまま維持します。URL、path、ASCII token 風の文脈では、ASCII 記号を不用意に全角化しないよう保守的に扱います。

さらに、Zenz が左文脈の文体などを引き継いで、現在の未確定文字列や通常 Mozc ライブ変換結果にない文末・表現記号を末尾へ追加する場合があります。Mozkey は `！` / `!`、`？` / `?`、`。`、`…`、`〜` などについて、現在の未確定文字列と通常 Mozc ライブ変換結果の末尾に既にある数を上限として、超過した末尾記号だけを採用前に取り除きます。これにより、漢字・語彙などの Zenz 補正は残しつつ、文脈だけから追加された余分な末尾記号を抑制します。文中の記号はこの repair の対象にしません。

日本語のみのユーザー辞書語は、自然な読みを Zenz prompt に残したまま、Zenz 応答後に表記の境界を検証します。たとえば保護対象の直後に余分なかなが付着した場合は、安全に修復できる場合だけ採用し、修復できない場合は通常の Mozc ライブ変換結果に戻します。

Zenz が保護対象の表記を落としたり変更したりし、placeholder 復元や安全な repair でも必要な出現数を満たせない場合、その Zenz 結果は採用せず、通常の Mozc ライブ変換結果を表示します。保護対象はユーザー辞書に登録されている全候補ではなく、現在の通常ライブ変換結果に実際に現れた表記です。

Zenz ライブ補正は password field では実行されません。また、入力途中の raw romaji のように日本語文字シグナルを含まない読みは補正対象外です。日本語文字を含む英字混じりの入力は、privacy gate を通る場合に限り補正対象になり得ます。

Windows TSF の password input scope と macOS の Secure Event Input では、application の surrounding text を Zenz 用に取得せず、Zenz extended context acquisition も実行しません。

Zenz feedback learning は任意機能です。有効な場合でも、Zenz 補正結果が表示されただけでは保存されません。Enter や句読点・記号の単打確定などで、表示中の Zenz 結果が明示的に確定された場合だけ、accepted feedback の候補として保留されます。

保留された accepted feedback は、次のユーザー操作で取り消されなかった場合だけローカル TSV に保存されます。Backspace、Escape、Revert、Undo などの修正操作が入った場合、保留 feedback は破棄されます。一方、IMEOff / MakeSureIMEOff は取り消しではなく確定後のモード変更として扱い、保留 feedback は確定扱いにします。表示中の Zenz 補正から Space や候補移動などの通常変換操作へ移った場合、その Zenz 結果は rejected feedback として扱われます。ただし Space などの通常操作由来の rejected feedback は、候補を永久に抑止する hard reject ではなく、以後の candidate ranking で順位を下げるための negative signal として扱います。

Zenz feedback の自動ブロックは、通常却下回数だけではなく拒否割合も使って判定します。同じ full-sequence feedback entry について、`通常却下回数 >= 最低拒否回数` かつ `通常却下回数 / (採用回数 + 通常却下回数) >= 最低拒否割合` の両方を満たした場合にだけブロックします。既定値は最低拒否回数 1 回、最低拒否割合 50% で、境界値も含むため `accepted=1 / rejected=1` の 50% はブロック対象です。明示的な hard reject はこの割合計算とは別の絶対ブロックとして扱います。

通常の自動ブロック中は、Zenz の内部生成自体を止めず、結果だけをユーザーには表示しない shadow observation を続けます。その状態で同じ読みが最終確定され、非表示の Zenz 結果と確定値が一致した場合は accepted feedback を 1 件加算します。この hidden match は Zenz feedback の回復観測であり、Mozc user history への追加学習は行いません。一致が積み重なって拒否割合がしきい値を下回れば、たとえば `accepted=2 / rejected=1` の 33% のように自動ブロックは解除されます。同じ読みで異なる値が確定した場合は通常却下を加算し、入力継続などで最終的な読みが shadow observation の読みと異なる場合は feedback を更新しません。明示的な hard reject は shadow observation を行わず、自動回復しません。

Zenz feedback TSV は、完全な読み key、完全な補正 value、粗い非可逆 context class からなる full-sequence 単位に限定します。segment-local や lexical-unit の feedback は保存しません。accepted Zenz 補正は条件を満たす場合に Mozc user history へ外部変換結果として学習されますが、それは Zenz feedback store の追加 record ではなく、別の Mozc-history 経路です。さらに、accepted Zenz 補正を直前の通常 Mozc ライブ変換文節へ安全に逆投影できる場合は、逆投影後の文節列全体を外部 multi-segment commit として Mozc history に学習します。このとき、通常 Mozc 変換で同じ key/value の候補を再取得できる場合は、その candidate 構造を再利用し、通常変換確定に近い形で user history に渡します。Zenz が実際に変更した文節だけを強い選択履歴として扱い、変更されていない文節は文脈として保持します。逆投影できない場合や privacy / password gate に該当する場合は、full-sequence 学習だけに戻ります。

特に Space は、Zenz 補正を単にキャンセルしてライブ変換中の入力列へ戻すキーではなく、通常変換候補へ戻る候補変更操作として扱います。deferred 表示でまだ Zenz の応答待ちの場合は、最初の Space でその時点の Mozc 通常変換結果を表示しますが、候補は次へ進めません。すでに Zenz 補正が表示されている場合も、Space を押すと補正前の Mozc 変換結果を通常変換状態として表示し、候補ウィンドウはまだ開きません。そのまま次の文字を入力した場合は、戻した Mozc 変換結果を確定してから新しい入力を開始します。さらに Space を押した場合は、従来どおり通常変換の候補ウィンドウを開いて次候補へ進みます。

Zenz feedback の再利用方法は、単文節と複数文節で異なります。

単文節の通常変換では、Zenz feedback は rewriter chain 内の ranked candidate reuse として再利用されます。保存済み feedback は accepted / rejected reason を score 化し、既存候補があれば score に応じて cost を調整します。候補にない場合は synthetic candidate として候補集合へ追加できますが、無条件に先頭へ挿入するのではなく、feedback-adjusted cost に基づく自然な位置へ挿入します。通常操作由来の rejected feedback は候補を削除する命令ではなく順位を下げる signal として扱い、明示的な hard reject reason だけを強い抑止として扱います。その後に UserSegmentHistoryRewriter が走るため、明示的なユーザー選択履歴が最終順位を決めます。

複数文節に分かれるライブ変換では、converter の文節境界を feedback ranking で壊さないため、rewriter chain では通常変換候補を書き換えません。その代わり、session-level の live correction fast path として再利用します。これにより、`かれはてんてきです` → `彼は天敵です` のような全文補正の学習も再利用できます。

`sensitive_like` context で得られた feedback は、通常文脈への候補 ranking / reuse には使いません。

Zenz 学習データは設定画面から管理できます。管理画面では、学習済みエントリを読み取り専用 table で表示し、検索、インポート、エクスポート、選択項目削除、全削除を行えます。ユーザーが TSV ファイルを直接編集する必要はありません。

また、選択した補正を「この補正をブロック」から明示的にブロックできます。ブロック済みの補正は再ブロックできません。ブロックを解除したい場合は、該当エントリを削除してから必要に応じて再学習します。

#### Zenzai v3/v3.2 条件フィールド

Zenz ライブ補正では、Zenzai v3/v3.2 の特殊トークン形式に沿って、追加の条件フィールドを設定できます。これらは ChatGPT の system prompt ではなく、Zenzai が学習時に見ている条件付き入力形式に対応する短いヒントです。

- `profile`: `U+EE03` profile として、書き手や用途の短い説明を渡します。
- `topic`: `U+EE04` topic として、現在の話題を渡します。experimental なフィールドです。
- `style`: `U+EE05` style として、文体や用途を渡します。experimental なフィールドです。
- `settings`: `U+EE06` settings として、変換方針の短いヒントを渡します。experimental なフィールドです。
- 右文脈: Zenz 専用のカーソル右側テキストが利用可能な場合、`U+EE07` right context として Zenzai v3.2 の prompt に含めます。

`profile`、`topic`、`style`、`settings` は空欄なら prompt に含めません。右文脈はユーザーが固定文を入力する欄ではありません。Windows TSF / macOS IMK では、Server が要求した必要量をカーソル右側から自動取得し、`zenz_following_text` として渡します。


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

さらに、名詞相当の確定済み左文脈の直後では、現在入力の先頭にある機能語 prefix や、`なのか`、`なのだ`、`なんです`、`なら`、`ならば`、`ならでは` のような名詞述語・条件表現に続く機能語列が、`担った`、`七日`、`奈良` のような内容語候補に食われるケースも抑制します。たとえば `彼` を確定してから `になった` や `なのか` と入力した場合、`彼担った` / `彼七日` ではなく、`彼になった` / `彼なのか` を優先しやすくします。また、`練習` を確定してから `なら` と入力した場合、`練習奈良` ではなく `練習なら` を優先しやすくします。

また、名詞や数量表現の後に続く `しか + 否定表現` のような機能表現も保護します。たとえば `2めいしかいない` では、途中の `2名司会...`、`2名視界...`、`2名士会内` のような同音漢字経路よりも、`2名しかいない` を優先しやすくします。

また、サ変接続名詞や行政地名・施設名のような直前文脈、地名に続く場所接尾語のような限定的な右文脈も一部補正対象にします。たとえば `追記したい` では `追記死体` のような同音名詞候補を避け、サ変接続名詞を確定した直後の `した`、`したの`、`したん` では `下`、`舌`、`シタン` のような内容語候補に寄りすぎる挙動を抑制します。`山梨県立美術館` では `山梨県率美術館` のような接尾的構成の崩れを抑制します。また、`滋賀方面` や `佐賀空港` のような語では、`子が方面` のような短い `内容語 + が` 分割を避けやすくします。

### 句読点・記号の単打確定

句読点・記号の単打確定オプションを有効にすると、設定した記号を入力した時点で未確定文字列全体をそのまま確定できます。

たとえば

- `tesuto.` → `てすと。`
- `tesuto?` → `てすと？`
- `kakko(` → `かっこ（`
- `tesuto/` → `てすと・`（記号スタイルで `/` が中黒になる場合）

のように動作します。

対象は、句点、読点、疑問符、感嘆符、丸括弧、鉤括弧、中黒から選択できます。`「`、`」` はそれぞれ独立した設定で扱います。中黒 `・` は、語や句を並列・区切りとしてつなぐ日本語の約物として独立した設定で扱います。

ローマ字テーブルで句読点・記号を出すように設定している場合も、出力された文字が単打確定の対象に含まれていれば、その時点で確定されます。

たとえば、ローマ字テーブルで `zz -> 。`、`qq -> ？`、`md -> ・` のように設定している場合でも、出力先の記号を単打確定の対象にしていれば、`tesutozz` → `てすと。`、`tesutoqq` → `てすと？`、`tesutomd` → `てすと・` のように確定されます。

どの句読点・記号を単打確定の対象にするかは、設定画面のチェックボックスで選択できます。

ただし、数字直後の句点・読点系文字は、数値入力を妨げないよう単打確定の対象外として扱います。これにより、句読点・記号の単打確定を有効にしたままでも、`3.14` や `1,000` のような数値を途中で確定せずに入力できます。通常の日本語入力での句点・読点の単打確定は従来どおり動作します。

ライブ変換が有効な場合、句読点・記号の単打確定では、ひらがなの未変換文字列ではなく、現在表示されているライブ変換結果を確定します。

句読点・記号の単打確定でも、直前の通常変換確定による学習は次の実テキスト入力まで保留されます。次の操作が Backspace、Escape、Revert、Reset、Undo などの場合、その保留学習は保持せず取り消します。一方、IMEOff / MakeSureIMEOff は取り消しではなく確定後のモード変更として扱い、保留学習は確定扱いにします。

句読点・記号の単打確定直後に Ctrl+Z など、現在のキー設定で Cancel に相当するキーが押され、そのキーがアプリケーションへ渡される場合も、Mozkey 側の保留学習は破棄します。実際に確定済み文字列が取り消されるかどうかは、アプリケーション側の Undo 挙動に従います。

### 変換キャンセル後のひらがな確定学習

変換中に Esc / Ctrl+Z など、現在のキー設定で Cancel に割り当てられた操作で変換をキャンセルし、ひらがなの未確定文字列に戻したうえで、そのまま Enter、Commit → IMEOff、IMEOff / MakeSureIMEOff、または句読点・記号の単打確定で確定した場合、このフォークではそのひらがな表記をユーザーが明示的に選んだ候補として扱います。

たとえば、`きょう` を変換して `今日` が表示されたあと、Esc で `きょう` に戻して Enter や Commit → IMEOff で確定した場合、IMEOff で確定して直接入力へ戻した場合、または `きょう。` のように句読点・記号の単打確定で確定した場合、F6 でひらがな化して Enter 確定した場合に近い形で、ひらがな本体の `きょう` をユーザーセグメント履歴へ反映します。これにより、次回以降の同じ読みで、ひらがな候補が上がりやすくなります。

複数文節に分かれていた変換をキャンセルした場合は、可能な限りキャンセル直前の文節境界を保持します。たとえば `おつかれぺん` が `お疲れ | ペン` のように複数文節として変換されていた場合、キャンセル後に `おつかれぺん` をそのまま確定すると、全体 1 件としてではなく、`おつかれ -> おつかれ`、`ぺん -> ぺん` のような文節単位のひらがな表記選好としてユーザーセグメント履歴へ反映します。

この強い学習は、キャンセル直後の未確定文字列が編集されず、そのまま Enter、Commit → IMEOff、IMEOff / MakeSureIMEOff、または句読点・記号の単打確定で確定された場合だけ有効です。句読点・記号の単打確定では、句読点・記号を含む全文ではなく、キャンセル直後のひらがな本体だけを表記選好として扱います。通常の未変換 Enter、キャンセル後に編集した文字列、1文字だけのひらがな、ひらがな以外の文字列、パスワード欄での入力は対象外です。

ライブ変換が有効な場合も、ユーザーが Esc / Ctrl+Z などのキャンセル操作で明示的にひらがなへ戻し、そのまま Enter、Commit → IMEOff、IMEOff / MakeSureIMEOff、または句読点・記号の単打確定で確定した場合は同じ扱いになります。一方で、入力継続などの内部処理としてライブ変換状態が解除されただけの場合は、この強い学習の対象にはなりません。

### 確定直後の修正による履歴学習の取り消し

変換確定直後に Backspace、または現在のキー設定で Cancel に割り当てられたキーが入力された場合、このフォークでは直前に確定した学習結果を取り消し対象として扱います。

句読点・記号の単打確定のように、確定文字列がすでにアプリケーションへ送られており Mozkey 側の通常の Undo context に乗らない場合でも、直後の Ctrl+Z などの Cancel 相当キーは保留学習の取り消しシグナルとして扱います。この場合、キー入力自体はアプリケーションへ渡し、画面上の文字列を実際に戻すかどうかはアプリケーション側の Undo 挙動に任せます。

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

### 選択文字列の Space 再変換（Windows / MS-IME 風キー設定）

Windows 版の MS-IME 風キー設定では、確定済みテキストを範囲選択した状態で Space を押すと、その選択文字列を再変換します。何も選択していない場合は、従来どおり空白を入力します。

この挙動は、キー設定画面では「選択文字列を再変換（未選択時は空白）」というコマンドとして表示されます。Space キーを無条件に横取りするものではなく、MS-IME 風キー設定で「入力文字なし」の Space にこのコマンドが割り当てられている場合だけ有効です。ユーザーが Space の割り当てを変更している場合は、そのキー設定が優先されます。

既存のカスタムキー設定を使っている場合は、MS-IME 風キー設定を選び直すか、キー設定画面で「入力文字なし」の Space に「選択文字列を再変換（未選択時は空白）」を手動で割り当てることで有効にできます。

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

### Windows IME アイコン設定

Windows 版では、設定画面から Mozkey の IME アイコンを切り替えられます。

選択肢は以下です。

- 既定
- モノクロ（黒）
- モノクロ（白）

この設定は、Windows の TSF language profile に登録されている Mozkey の `IconFile` / `IconIndex` を更新し、タスクバーや IME 一覧に表示される IME アイコンへ反映します。

適用時には、管理者権限の確認が表示される場合があります。また、Windows 側のアイコン cache や入力方式一覧の更新タイミングにより、タスクバーや IME 一覧のアイコンがすぐに更新されない場合があります。その場合は Windows を再起動してください。

### Windows Search / immersive TSF 対応

Windows 版では、Windows Search の検索 UI のような immersive TSF 環境で、候補ウィンドウ、サジェストウィンドウ、ライブ変換中のルビ表示がホスト側の表示階層の背面へ隠れないように、TSF ホストプロセス内の専用 renderer UI thread で Win32 renderer を動かす経路を使用します。候補・サジェスト・ルビの各ウィンドウはホスト側の top-level window を owner として作成し、入力先のフォーカスを奪わない形で表示します。

通常の変換候補では、従来どおり mouse-down で候補をハイライトし、mouse-up でその候補を選択します。

一方、まだ候補がフォーカスされていない passive なサジェストでは、mouse-down だけでは候補のハイライトや通常変換状態への移行を行いません。押されたサジェストの candidate ID をその mouse gesture 中だけ保持し、mouse-up 時に `SUBMIT_CANDIDATE` として確定します。

ライブ変換中の passive Suggestion は、画面に表示する候補を通常の live-conversion context とは別の cloned `ImeContext` で生成します。そのため、表示された Suggestion と通常変換候補で同じ数値の candidate ID が別の候補を指す場合があります。Mozkey は表示した Suggestion を生成した cloned context も保持し、mouse-up 時の candidate ID をその同じ converter state で解決することで、画面上で押した Suggestion とは別の通常変換候補が確定されることを防ぎます。

サジェストを押したまま候補外へ移動して mouse-up した場合は確定せず、通常候補のクリック操作は従来の挙動を維持します。

### 候補ウィンドウ・サジェストウィンドウ・ルビ表示の外観設定（Windows / macOS）

Windows / macOS 版では、設定画面から候補ウィンドウ、サジェストウィンドウ、ライブ変換中のルビ表示の外観を調整できます。

候補ウィンドウ、サジェストウィンドウ、ルビ表示は、ライト / ダーク / 自動（システムテーマに合わせる）/ カスタム配色を選択できます。新規設定では3項目とも「自動」が既定です。「自動」では現在のシステム外観を参照し、OS のライト / ダークテーマを変更した場合は次の renderer 更新から新しい外観を反映します。

ライト / ダーク / カスタム配色を明示的に選択した場合は、システムテーマの変更には追従しません。サジェストウィンドウとルビ表示には「候補ウィンドウに追従」もあり、候補ウィンドウで解決された配色やカスタム配色を引き継ぎます。「自動」と「候補ウィンドウに追従」は別の設定です。

カスタム配色では、候補ウィンドウとサジェストウィンドウについて、背景、文字、選択背景、選択枠、枠線、ショートカット、説明、フッター、スクロールバーなどの色を調整できます。ルビ表示については、背景、文字、枠線の色を調整できます。

各ウィンドウの表示サイズ、角丸、透明度、影の広がり、濃さ、方向、距離も設定できます。影の方向は画面座標基準の角度で指定し、0° は右、45° は右下、90° は下を表します。影の距離を 0 にすると、全方向に均等な影になります。変換候補・用例などの候補系表示には候補ウィンドウ設定を使い、予測・サジェスト系表示にはサジェストウィンドウ設定を使います。

Windows 版のルビ表示では、左右の余白、上下の余白、入力文字との距離を個別に設定できます。これらは固定の物理ピクセル値ではなく、ルビ表示のサイズ設定と表示先モニターの DPI に応じて拡大縮小されます。複数モニター環境では、入力位置があるモニターの DPI を基準にフォント、余白、角丸、影、入力文字との距離を再計算し、TSF から得た入力位置も物理座標へ変換して配置します。

候補ウィンドウ、用例ウィンドウ、ライブ変換中のルビ表示に使うフォントも設定画面から変更できます。既定フォントに戻すこともでき、選択したフォントを候補表示に適用できない場合は、候補ウィンドウが消えないように既定フォントへフォールバックします。

候補ウィンドウ、サジェストウィンドウ、ライブ変換中のルビ表示については、主要テキストの太さもそれぞれ 100～900 の範囲で個別に設定できます。既定値は 400（標準 / Regular）です。候補番号、説明、フッター、用例ウィンドウ内の文字などの補助情報は対象外で、従来の太さを維持します。

ライブ変換中のルビ表示は、設定画面から ON/OFF を切り替えられます。

Windows 版の IME 切り替えインジケータは、設定画面から「システムテーマに合わせる / ダーク / ライト / カスタム」を選択できます。「システムテーマに合わせる」では Windows のライト / ダークテーマに追従し、「ダーク」「ライト」を明示した場合は Windows のテーマ変更に追従せず、その配色を維持します。「カスタム」では英数字・ひらがな・カタカナの背景色・枠線色・文字色・影色に加え、幅・高さ・角丸・文字サイズ・枠線幅・影のぼかし・濃さ・位置を調整できます。

入力位置がモニターの作業領域端付近にある場合は、インジケータが画面外にはみ出さないよう表示位置を自動調整します。外観設定の「リセット」はテーマとカスタム値を既定値へ戻しますが、インジケータの表示 ON/OFF 設定は変更しません。

### 縦書き対応（Windows / macOS）

Windows / macOS 版では、縦書き入力時に候補ウィンドウ、予測・サジェスト、用例表示、ライブ変換中のルビを縦組みに合わせて表示します。横書き時の既存動作は維持します。

縦書きの候補操作では、Windows / macOS のどちらでも、縦書き状態を session 側へ伝え、現在のキー設定が対応する既存コマンド割り当てに一致する場合に、視覚方向に合わせて矢印キーを解釈します。

- Suggestion では Left で候補へ移動
- Conversion / Prediction では Left / Right で候補の前後へ移動し、Up / Down で前後の文節へ移動
- Conversion では `Shift+Up` で文節幅を縮小、`Shift+Down` で文節幅を拡大
- 既存の `Shift+Left` / `Shift+Right` による文節幅変更も互換操作として維持

通常の横書き動作、通常 Composition 中のカーソル操作、`Ctrl+Shift` 系の操作、および対応するコマンド割り当てを持たないキー設定は従来動作を維持します。

#### Windows

Windows 版では、縦書きの入力位置を検出し、候補や用例の主要テキストを DirectWrite の縦書き描画で表示します。

候補ウィンドウは縦書きの入力位置に対して左側への配置を優先し、アプリケーションから渡される入力行の幅が狭い場合でも、入力文字と候補表示が近づきすぎないように配置を補正します。アプリケーション名ごとの個別分岐ではなく、入力位置の geometry に基づいて調整します。

縦書きの候補列は、候補ごとの文字量や説明の有無に応じた自然な幅・高さで配置します。長い候補がある場合でも、他の候補列まで一律に引き伸ばさず、それぞれの候補に必要な大きさを保ちます。選択中の候補は、文字列自体が短い場合でも候補本文領域の下端まで選択背景を伸ばし、縦書きの列として現在位置を確認しやすくしています。

連鎖候補ウィンドウは、現在選択している候補を基準に、縦書き本文から見て外側となる左側への展開を優先します。候補ウィンドウや入力位置との重なり、または画面端の制約により左側へ配置できない場合は右側へフォールバックします。通常の横書き時は従来の配置を維持します。

ライブ変換中のルビも縦書き composition に追従します。Word などで未確定文字列が複数の縦列へ折り返す場合は、すでに表示されている composition 列と重ならない位置へルビを配置します。

#### macOS

macOS 版では、IMK 側で取得した writing direction をレンダラーへ伝播し、候補・用例・ルビの主要テキストを Core Text の縦書き描画で表示します。

候補／サジェストでは、Core Text が全文を確実に描画できる技術的な text frame と、画面上で実際に見せる候補列の幅を分離しています。表示幅は CJK の標準的な列幅を下限としつつ、実際のグリフ幅がそれを超える場合だけ拡張します。これにより、日本語・ASCII・半角カナなどが混在しても、必要以上に太い候補列になりにくいようにしています。

縦書き時の候補／サジェストと本文との間隔は 6 pt とし、ライブ変換中のルビも実効 6 pt の間隔で配置します。横書き時の既存の間隔・配置は変更しません。

用例表示（Infolist）は縦書き時に列ごとの Core Text frame を使って描画し、縦組みの表示方向に合わせて配置します。ルビ表示も、描画用 frame を確保しつつ、可視部分は CJK の標準列幅と実際のグリフ幅に基づいて過不足なく配置します。

連鎖候補ウィンドウは、現在の候補位置を基準に、縦書き本文から見て外側となる左側への展開を優先します。候補ウィンドウや preedit との重なりを避けられない場合は右側へフォールバックし、同じ階層の連鎖候補同士も重ならないように配置します。

これらの縦書き処理は macOS 専用の layout / writing-direction ロジックとして分離し、候補レイアウト、Infolist レイアウト、writing direction、ウィンドウ配置の単体テストを追加しています。通常の横書き経路は従来動作を維持します。

### Windows 未確定文字の表示と表示色

Windows 版では、ローマ字入力時に「未確定のローマ字を薄く表示する」を有効にすると、変換規則の途中にある末尾の未確定ローマ字だけを通常の preedit と分けて表示できます。この機能は表示だけを変更し、入力文字列、変換用の key、確定結果は変更しません。

この設定は opt-in で、設定値がまだ存在しない新規・未設定のプロファイルでは既定 OFF です。すでに明示的な ON / OFF が保存されている場合は、そのユーザー設定を維持します。薄さは 0～90% の範囲を 1% 単位で設定でき、既定値は 75% です。値を大きくすると背景色に近づいてより薄く見え、0% は高いコントラスト、90% は非常に薄い表示になります。

未確定ローマ字の表示情報は通常の入力中だけでなく、ライブ変換や Zenz 補正を表示している経路でも、元の segment の key / value を壊さず安全に保持できる範囲で引き継ぎます。表示のために key を value から合成することはせず、安全に分割できない segment は元の key / value のまま扱います。

この表示は Windows TSF の専用 display attribute として適用します。Firefox などの Gecko 系ホストでは、編集面の背景を安全に取得できた場合だけ、その背景に合わせた薄い文字色を使用します。選択範囲、clipped な geometry、非均一な背景など信頼できない状態は背景色として固定せず、互換表示へフォールバックします。また、この機能が OFF のときは背景取得のための画面 sampling を行いません。

あわせて、Windows 版では設定画面から入力中の文字と変換中の文節について、それぞれ以下の表示色を個別に設定できます。

- 文字色
- 背景色
- 下線色

新規・未設定のプロファイルでは、入力中の下線色は `#30DCC8`、変換中の文節の下線色は `#C1A5AB` が既定です。すでに下線色やその有効 / 無効を明示的に保存している場合は、その既存設定を維持します。

日本語入力中・変換中の未確定文字を見やすくするための機能です。特に、視認性を高めたいユーザー向けのアクセシビリティ改善として追加しています。

これらの設定は Windows TSF の表示属性として反映されます。対応アプリでのみ有効です。Chrome や Edge など、一部のアプリでは反映されない場合があります。

system dictionary の強化
-----------------------

この fork では、外部辞書を元に Mozc の system dictionary を強化するための生成スクリプトを追加しています。

また、外部辞書とは別に、Mozkey 独自の小さな manual override 辞書で、日常語彙・実務語彙・外来語・英語綴り候補を段階的に補強します。通常の日本語語彙は自然な第一候補として、英語綴り候補は第一候補ではなく補助候補として出ることを重視します。評価方針と検証手順は [Koyasi Dictionary Data](src/data/dictionary_koyasi/README.md) から参照できます。

daily local 辞書は主に以下を元に生成できます。

- merge-ut-dictionaries
  - 地名
  - SudachiDict 由来語彙
- dic-nico-intersection-pixiv
  - 固有名詞、ネットスラング、作品名、キャラクター名、サブカル系語彙
  - 生成済み daily 辞書または Mozc 標準辞書に既に存在する key/value は除外
  - 末尾に `☆`、`★`、`♡`、`♪`、`※` などの装飾記号が付く表記は daily 変換ではノイズになりやすいため除外
- mozcdic-ut-personal-names
  - 人名・芸名・活動名などを含む人名辞書
  - 生成済み daily 辞書、nico/pixiv delta、Mozc 標準辞書に既に存在する key/value は除外
  - 短すぎる読み、長いカタカナ塊、グループ名風表記、ASCII 表記、記号を含む表記などは除外または弱める
- Mozkey syntax / expressive kana guard 辞書
  - 文節区切り崩れの影響が大きいケースだけを小さな生成辞書として補強
  - 例: `と打ちたいのに`、`に分ける`、`した方が`、`したにもかかわらず`、`にまで`、`までに`、`までも`、`肌身離さず`、`になってしまいます`、`になっちゃいます` のような経路を保護
  - 例: `うっそ`、`くっそ`、`やっば`、`ちっす`、`ほえ～`、`ほぇ～` のような完成済み expressive kana を自然なかな候補として補強

巨大な生成辞書ファイルは、このリポジトリには commit しません。`src/data/dictionary_koyasi/generated/` 以下にローカル生成します。

リリース MSI には、ローカル生成した `daily` system dictionary profile を同梱する場合があります。外部辞書の出典とライセンス note は [Third-party notices](THIRD_PARTY_NOTICES.md) および [Koyasi Dictionary Data](src/data/dictionary_koyasi/README.md) を参照してください。

強化辞書を有効にした状態で package build する前に、daily 辞書をローカルで再生成してください。

```powershell
.\tools\dictionary\prepare_daily_dictionary.ps1
```

詳細:

* [Koyasi Dictionary Data](src/data/dictionary_koyasi/README.md)
* [Third-party notices](THIRD_PARTY_NOTICES.md)


### macOS Zenz 対応 Universal PKG のビルド

配布用の macOS Zenz runtime は、追跡済みの
`src/mac/installer/zenz_runtime/build_universal_runtime.zsh` から生成し、
`stage_runtime_assets.zsh` で staging してから Universal PKG をビルドします。
`llama-server` と `mozc_zenz_scorer` はどちらも `arm64` / `x86_64`
を含む同一パスの Universal executable として扱います。

```bash
src/mac/installer/zenz_runtime/build_universal_runtime.zsh \
  "$HOME/Downloads/mozkey-macos-zenz-runtime"

src/mac/installer/zenz_runtime/stage_runtime_assets.zsh \
  "$HOME/Downloads/mozkey-macos-zenz-runtime"

cd src

bazelisk build \
  --config=oss_macos \
  --config=release_build \
  --macos_cpus=x86_64,arm64 \
  --define=macos_zenz_runtime=1 \
  //mac:package \
  --verbose_failures
```

`--define=macos_zenz_runtime=1` を付けない generic package build は、
Zenz runtime を含む配布用 macOS PKG としては扱いません。

正式な architecture/runtime gate では、最終 `.pkg` を Apple Silicon で検証し、
同一 SHA-256 の package artifact を native Intel runner に渡して、
同じ package-level verifier で `mozc_zenz_scorer -> llama-server -> model`
の実リクエストまで検証します。詳細な source pin、BUILD-CONTRACT、staging、
署名境界、検証手順は
[`src/mac/installer/zenz_runtime/README.md`](src/mac/installer/zenz_runtime/README.md)
を参照してください。

Note
----

This fork is primarily maintained for personal use.
Upstream-oriented changes are organized in `pr/*` branches.

この fork は主に個人利用向けに保守しています。
upstream 提案向けの変更は `pr/*` branches に整理しています。

<br>

# English

This repository is my personal fork of [google/mozc](https://github.com/google/mozc).

This fork is mainly maintained for my own Windows / macOS environments and adds input assistance, live conversion, context-aware conversion, local Zenz correction, system-theme-aware renderer appearance, and offline-distribution-oriented adjustments to Mozc.

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
The Zenz runtime is used for local inference and is not intended to expose an
external network service or send user input to external servers.

In the Windows Zenz-bundled configuration, `llama-server.exe` is used as a
local-only inference server and is expected to bind to `127.0.0.1`. On Windows,
the Zenz helper process communicates with the bundled `llama-server.exe` only
through a localhost endpoint. This local HTTP endpoint is used as an internal
process boundary for inference and is not intended for external network access.

The Zenz localhost transport is hardened so that it does not rely on a fixed
endpoint, and internal requests include protection against accidental or stale
local endpoint mismatches.

Zenz feedback learning stores only full-sequence local learning records such as
the complete reading, the complete candidate, coarse context class,
accepted/rejected counts, and reason markers. Raw left context is not stored.
Context used for feedback is reduced to a non-reversible class such as `empty`,
`japanese_only`, `japanese_with_punctuation`, `mixed_japanese_ascii`, or
`sensitive_like`. Segment-local or lexical-unit learning is intentionally not
stored in the Zenz feedback TSV; safe local learning belongs to Mozc history.

Additional release checks verify that Mozc core runtime binaries do not contain
hard-deny telemetry, updater, crash-upload, or usage-statistics markers.

Generic URL-like markers such as `http://`, `https://`, and `googleapis.com`
are reported for audit, but they are not treated as hard failures because they
can come from manifests, XML namespaces, comments, license files, or library
metadata.

Building from source may require network access to download build dependencies.
This is separate from runtime behavior of the installed IME.

On Windows, the installer also adds outbound Windows Firewall block rules for
Mozc runtime executables as an additional offline hardening layer. For
Zenz-bundled builds, the rules also cover `mozc_zenz_scorer.exe` and
`llama-server.exe`. These rules are removed during uninstall.

See also:

- [Secure Offline Guarantee](docs/security/offline_guarantee.md)
- [Secure Offline Release Checklist](docs/security/release_checklist.md)


Download / Install
------------------

At the moment, prebuilt packages published from Releases are available for Windows.

In addition to Windows, this fork is also tested on real macOS hardware. The macOS path is verified beyond successful builds, including actual package/install, runtime, input, and rendering workflows.

Linux is supported by upstream Mozc itself, but this fork-specific Zenz configuration and added features have not yet been tested on a real Linux environment.

Windows MSI packages are available from [Releases](https://github.com/koyasi777/mozkey/releases).

- On x64 Windows, use the latest `Mozkey_v*_x64.msi` from Releases.
- On Windows on Arm (ARM64), when a release contains an MSI explicitly labeled ARM64, use the ARM64 package. The ARM64 MSI packages Mozkey, `mozc_zenz_scorer.exe`, and `llama-server.exe` as native ARM64 payloads.
- The `universal` MSI is not a dual-native whole-product package containing native x64 and ARM64 versions of every executable. Prefer the architecture-specific x64 / ARM64 MSI for normal release installation.
- For the Zenz-bundled build, choose an MSI whose file name contains `zenz` or `zenz_offline`.
- Releases from this fork are published as personal experimental builds.
- Zenz-bundled builds are larger than the traditional offline MSI because they include a local inference runtime and a GGUF model.
- Windows release MSI packages may include the locally generated `daily` system dictionary profile.
- The `daily` profile is generated from the Mozc base dictionaries, merge-ut-dictionaries, dic-nico-intersection-pixiv, mozcdic-ut-personal-names, and the Mozkey syntax / expressive kana guard dictionary.
- See [Third-party notices](THIRD_PARTY_NOTICES.md) for source and license notes for bundled runtimes, model files, and generated dictionary data.

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
- Allows a custom romaji-table rule to produce output shorter than 1024 UTF-8 bytes while keeping the existing input and pending limits below 300 bytes
- Adds an option to directly commit punctuations and symbols with a single key press
- Allows choosing direct-commit punctuations and symbols from the config dialog
- Makes punctuation conversion and punctuation/symbol direct commit mutually exclusive
- Improves user-history learning behavior when a committed conversion is immediately corrected with Backspace or Cancel
- Learns a hiragana choice when an active conversion is canceled with Esc, Ctrl+Z, or another key bound to Cancel and the restored hiragana preedit is immediately committed with Enter or a direct-commit punctuation/symbol, similarly to F6 -> Enter
- For multi-segment conversions canceled back to hiragana, preserves conversion-time segment boundaries when possible and learns each hiragana segment as a spelling preference, rather than as a single whole restored preedit
- Keeps learning caused by direct-commit punctuations/symbols pending until the next real text input, reverts it on Backspace, Escape, cancel-equivalent keys such as Ctrl+Z, Revert, Reset, or Undo, and confirms it on IMEOff / MakeSureIMEOff
- Adds live conversion that automatically converts the current composition and shows a ruby-like overlay for the original reading
- Allows enabling/disabling live conversion and configuring its debounce delay and minimum start length from the config dialog
- Applies live conversion after a short debounce delay to avoid noisy intermediate conversions
- By default, suppresses live conversion for one-character compositions to avoid over-converting particles
- Suppresses live conversion for very short kana compositions with decorative trailing symbols such as `え~`, `えー`, or `ん？`
- Suppresses live-conversion flicker for unfinished expressive kana prefixes, while completed expressive forms such as `うっそ`, `くっそ`, `やっば`, `すっげぇ`, `めっちゃ`, `ちっす`, `ちょりっす`, `ほえ～`, `ほぇ～`, and `ほっほーん` are rescued as generated dictionary candidates
- Uses committed left context, previous segments, and limited right context to reduce unnatural homophone results in cases such as `mainにマージしました`, `githubには`, `彼になった`, `彼なのか`, `2名しかいない`, `追記したい`, `山梨県立美術館`, and `滋賀方面`
- Allows assigning multiple commands to a single key binding as an ordered command sequence
- Stores command sequences as `Commit|IMEOff` and shows them in the keymap editor as `Commit → IMEOff`
- In the MS-IME style keymap, pressing Space while committed text is selected reconverts that selection; with no selection, Space still inserts a normal space
- Allows assigning left/right Shift and left/right Ctrl separately on Windows
- Shows the IME mode indicator even when a key assigned to IMEOn or IMEOff is pressed while Mozc is already in that state
- Adds explicit Windows default IME controls to the config dialog, with restore support for the previous default IME setting
- Allows choosing the Windows Mozkey IME profile icon from Default, Monochrome (Black), and Monochrome (White) in the config dialog
- Supports candidate, suggestion, and live-conversion ruby rendering in immersive TSF hosts such as Windows Search through an in-process renderer path, keeping the UI above the host presentation layer and preserving correct mouse selection; an unfocused passive suggestion does not enter normal conversion on mouse-down, and mouse-up commits the suggestion that was actually clicked
- Allows configuring light/dark/auto (follow system theme)/custom color themes, size, corner radius, opacity, and shadow separately for the candidate window, suggestion window, and live-conversion ruby display on Windows and macOS; new configurations default all three theme selectors to Auto
- Makes the Windows ruby display use target-monitor DPI-aware positioning and scaling, and allows configuring its horizontal padding, vertical padding, and distance from the input text
- Allows the suggestion window and ruby display to either follow the candidate window color theme or use their own theme/custom colors; Auto follows the OS system theme directly and is distinct from Follow Candidate
- Allows changing the font used for the Windows candidate window, infolist window, and live-conversion ruby display from the config dialog
- Allows configuring the primary text weight independently from 100 to 900 for the Windows candidate window, suggestion window, and live-conversion ruby display
- Adds vertical-writing layouts for candidate, suggestion, infolist, and live-conversion ruby displays on Windows and macOS, with candidate and segment navigation aligned to the visual writing direction when the active keymap uses the supported command bindings
- On Windows and macOS, places cascading candidate windows outward from the focused vertical candidate, with fallback to the opposite side when necessary
- Allows enabling or disabling the ruby display shown during live conversion from the config dialog
- Allows customizing Windows preedit text color, background color, and underline color from the config dialog
- Adds an opt-in Windows feature for Roman input that dims only the unresolved trailing romaji still forming an incomplete conversion-rule prefix; it defaults to OFF and the dimness can be adjusted from 0% to 90% (default 75%)
- Allows choosing System theme, Dark, Light, or Custom for the Windows IME mode indicator, including per-mode colors and geometry/shadow customization, with automatic screen-edge repositioning
- Adds an enhanced system dictionary generation pipeline
- Adds a small tracked manual override dictionary for daily vocabulary, practical vocabulary, loanwords, and secondary English spelling candidates, with regression checks that keep Japanese candidates first
- Allows incorporating place names and SudachiDict-derived vocabulary from merge-ut-dictionaries into the system dictionary
- Allows adding internet/subculture proper nouns from dic-nico-intersection-pixiv as daily-dictionary differences
- Allows adding a weak personal-name dictionary from mozcdic-ut-personal-names after removing entries already covered by the generated daily dictionary, nico/pixiv delta dictionary, or base Mozc dictionaries
- Adds a syntax guard dictionary generation pipeline to reduce high-impact segmentation failures, including guarded paths such as `と打ちたいのに`, `に分ける`, `した方が`, `したにもかかわらず`, `にまで`, `までに`, `までも`, and `肌身離さず`
- Keeps large generated dictionary files out of Git and switches Bazel dictionary inputs to locally generated files
- Reduces over-conversion of natural functional kana sequences such as `には` and `してたの`
- Reduces cases where short two-character hiragana inputs such as `にじ` are split too aggressively
- Adds a local Zenz live correction pipeline based on llama.cpp
- Uses dedicated `zenz_preceding_text` / `zenz_following_text` fields for Zenz context without changing the normal Mozc `preceding_text` / `following_text` semantics
- Lets the Server request the required preceding / following lengths and lets Windows TSF / macOS IMK acquire only the requested directions and lengths
- Selects preceding and following Zenz context independently and applies shared Unicode-aware script analysis and privacy filtering
- On Windows, sends Zenz correction requests from `mozc_server` to `mozc_zenz_scorer.exe` through a named pipe and performs local inference through the localhost endpoint of `llama-server.exe`
- Allows configuring the Zenz correction debounce delay from the config dialog. The default is 1000 ms
- Allows configuring the minimum number of characters to start Zenz correction
- Adds an opt-in `Do not show the normal live-conversion result until Zenz correction returns` mode. While waiting, Mozkey preserves the previously stable Zenz-corrected presentation when possible and appends unresolved raw-romaji suffixes to that visible presentation; if Zenz is not adopted, it falls back to the current Mozc live-conversion result
- Adds a configurable deferred-presentation wait limit. The default is 300 ms after the Zenz correction request is submitted; when the deadline expires, that generation is discarded and Mozkey reveals the already-computed normal Mozc live-conversion result
- Commits the presentation that is actually visible to the user, rather than a hidden Mozc baseline, when Enter or Shift-based ASCII input ends a deferred presentation; Undo after Enter restores the same visible presentation
- Detects and rejects likely context echo where Zenz repeats a long suffix of already committed left context at the beginning of the current output, then falls back to the normal Mozc live-conversion result
- Adds optional local feedback learning for Zenz correction results
- Adds adaptive Zenz feedback auto-blocking. For the same full reading, context class, and correction value, auto-blocking activates only when both the minimum ordinary-reject count and the minimum reject percentage over accepted plus ordinary-rejected observations are met. For configurations without a saved value, auto-blocking defaults to enabled with a minimum of 1 rejection and 50%. Auto-blocking does not persist irreversible hard-reject rows; it is re-evaluated dynamically from the current settings and stored feedback.
- Keeps evaluating an auto-blocked Zenz result as a hidden shadow observation. If the final committed value for the same reading matches that hidden Zenz value, accepted feedback is added so the entry can recover automatically once its reject percentage falls below the threshold. A different final value for the same reading adds an ordinary rejection; a changed final reading is neutral. Explicit hard rejects are not eligible for shadow recovery.
- Stops reusing a Zenz feedback entry as a preferred candidate or live-correction fast path when ordinary rejected observations outnumber accepted observations for the same full reading, context class, and correction value. This is a neutral reject-count-dominant state, not a hard block, so it does not delete ordinary Mozc candidates or prevent newly produced Zenz corrections by itself.
- Does not store Zenz feedback just because a Zenz correction was displayed. A visible Zenz result becomes pending accepted feedback only when the user explicitly commits it, such as with Enter or a direct-commit punctuation/symbol
- Writes pending accepted feedback to the local TSV only if it is not canceled by Backspace, Escape, Revert, Undo, or similar correction actions before the next real text input. IMEOff / MakeSureIMEOff are treated as post-commit mode changes rather than cancellation
- Treats a visible Zenz correction as rejected feedback when the user moves to normal conversion operations such as Space or candidate movement. Ordinary rejected feedback from these operations is used as a negative ranking signal rather than as a hard command to suppress the candidate
- When Space restores the underlying Mozc normal conversion from a visible Zenz correction, the next text input commits that restored conversion before starting a new composition
- Adds a safe Zenz feedback management UI to the config dialog. Users can search, import, export, delete selected entries, and clear all entries without directly editing the TSV file
- Allows explicitly blocking selected Zenz corrections from the feedback management UI. Already blocked corrections cannot be blocked again; to unblock one, delete the corresponding feedback entry and relearn it if needed
- Reuses Zenz feedback for normal conversion candidate ranking. In single-segment conversions, stored feedback is scored, existing candidates receive feedback-adjusted costs, and missing feedback candidates may be inserted as synthetic candidates at a natural cost-based position. Accepted feedback raises the candidate, while ordinary rejected feedback lowers it without deleting it
- Does not apply Zenz feedback ranking to multi-segment normal conversions, to avoid collapsing phrase boundaries
- Reuses accepted Zenz feedback via the session-level live-correction fast path for multi-segment live conversion to preserve learned full-phrase corrections
- Does not reuse feedback obtained from `sensitive_like` context for ordinary-context candidate ranking
- Learns accepted Zenz candidates into Mozc user history as external conversion results when the runtime conditions allow it
- When an accepted Zenz correction can be safely reverse-projected onto the previous normal Mozc live-conversion segments, learns the projected segment sequence as an external multi-segment commit. If Mozc can reproduce the same key/value candidate through normal conversion, the candidate structure is reused so user history receives evidence closer to a normal conversion commit. Only segments actually changed by Zenz are marked as strong user-selected history.
- Protects user-dictionary candidates and ASCII / mixed-script surfaces that appear in the current normal Mozc live-conversion result before adopting Zenz live-correction output
- For ASCII / mixed-script surfaces, temporarily replaces the reading with a placeholder in the Zenz prompt when it can be identified safely, then restores the selected surface after the response, so entries such as `もずきー -> Mozkey` are not silently overwritten as `モズキー` while surrounding text can still be corrected
- Treats the surface selected by normal Mozc as an orthographic baseline. When Zenz alone introduces or mutates a Latin / technical spelling such as `東京 -> Tokyo` or `コンピュータ -> computer`, Mozkey restores the affected safely projectable segment to the Mozc surface; Latin / technical tokens already selected by Mozc, such as `GitHub`, `iPhone`, `AI`, `C++`, `GPT-5`, `UTF-8`, `HTTP/2`, and `Windows11`, remain valid
- Protects ASCII numeric literals, including signed integers and decimals, separately from Latin / technical tokens and leaves them visible in the Zenz prompt instead of replacing them with placeholders. The exact numeric spelling and non-Japanese boundaries are preserved, while adjacent Japanese text in expressions such as `1日`, `100円`, `スマートフォン1台`, `第1回`, and `3.14倍` remains eligible for Zenz correction
- Preserves user-visible punctuation style when adopting Zenz live-correction output, so brackets and symbols such as `（ ）` / `( )`, `？` / `?`, and `！` / `!` stay in the style chosen by the current composition or Mozc live-conversion result
- Suppresses only excess trailing sentence-final or expressive punctuation introduced by contextual Zenz output beyond what is already present at the end of the current composition or normal Mozc live-conversion result, while preserving the rest of the correction
- For Japanese-only user-dictionary surfaces, keeps the natural reading in the Zenz prompt and validates the selected surface boundaries after the response, accepting the result only when any extra kana attachment can be repaired safely
- Sanitizes left context before using it in Zenz prompts, and excludes sensitive-like context such as URLs, email addresses, file paths, tokens, and long digit sequences
- Stores only full-sequence Zenz feedback with non-reversible context classes, never raw left context or segment-local feedback
- Bundles third-party license notices for the Zenz model and llama.cpp runtime in the MSI
- Includes build adjustments for my own Windows / macOS development environments

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

### Long custom romaji-table outputs

A custom romaji-table rule can produce output shorter than 1024 UTF-8 bytes. The input and pending fields keep their existing limits below 300 bytes.

Previously, input, output, and pending all shared the same limit below 300 bytes. For typical Japanese characters encoded as three UTF-8 bytes, this limited one rule's output to the equivalent of 99 characters. This fork raises only the output limit, allowing up to 1023 bytes, or the equivalent of 341 three-byte characters.

This is a per-rule romaji-table output limit. It does not change the converter's whole-composition input-length limit.

### Live conversion

With live conversion enabled, Mozc automatically converts the current composition without committing it immediately.

To reduce distracting intermediate conversions, this fork applies live conversion after a short configurable debounce delay instead of converting every character immediately. By default, single-character compositions are not live-converted, because they are often particles such as `に`, `を`, or `が`. The minimum number of characters required to start live conversion can be changed from the config dialog.

Live conversion is also suppressed for very short kana compositions followed only by decorative trailing symbols, such as `え~`, `えー`, or `ん？`. This avoids noisy intermediate conversions such as `え~` becoming `絵～` while the user is still typing.

For short expressive kana utterances, colloquial evaluative forms, and casual greetings, this fork suppresses live-conversion flicker only while the user is still typing an unfinished prefix or a pending roman suffix. Completed expressions are allowed to reach the normal converter.

For completed expressive forms, the generated dictionary adds natural kana candidates such as `うっそ`, `くっそ`, `やっば`, `すっげぇ`, `めっちゃ`, `ちっす`, `ちょりっす`, `ほえ～`, `ほぇ～`, and `ほっほーん`. This avoids pathological kanji segmentation by default while still allowing explicit user selections, user history, and user dictionary entries such as `ウッソ`, `クッソ`, `ヤッバ`, `チッス`, or `ホェ～` to influence future ranking.

This does not disable normal conversion. Pressing Space still invokes ordinary conversion candidates, and completed expressive words remain ordinary converter candidates.

For example:

- Type `kyouha`
- After the debounce delay, the preedit can be shown as `今日は` before pressing Space
- Typing more characters keeps the same uncommitted composition and schedules another live conversion
- Romaji-table ellipsis rules such as `v. -> …` or `v, -> ‥` keep the visible live-converted prefix instead of falling back to raw kana
- When Shift-based ASCII input starts, the visible live conversion result is committed first, and then ASCII input begins
- Pressing Backspace or Delete updates the live conversion result immediately
- Pressing Enter commits the current live conversion result

During live conversion, this fork shows a small ruby-like overlay window above the preedit text so that the original reading remains visible while the converted text is shown.

The live conversion feature can be enabled or disabled from the config dialog. The debounce delay and the minimum number of characters required to start live conversion can also be configured there.

### Zenz live correction

When both live conversion and Zenz live correction are enabled, this fork
normally shows the Mozc live-conversion result and asynchronously asks a local
Zenz runtime to refine the visible preedit.

When the config option `Do not show the normal live-conversion result until
Zenz correction returns` is enabled, inputs that are eligible for Zenz do not
show a newly computed Mozc live-conversion result while the Zenz request is
pending. If a stable Zenz-corrected prefix is already visible, Mozkey preserves
that presentation when possible and appends unresolved raw-romaji suffixes to
it. Mozc resegmentation alone does not cause a previously stable Zenz
presentation to be split or rolled back. If Zenz fails, times out, is rejected
by validation, or produces the same value as Mozc and is therefore not adopted,
Mozkey falls back to the current normal Mozc live-conversion result.

Deferred presentation has a separate configurable wait limit measured from the
moment the Zenz correction request is actually submitted. The default is 300
ms. If the result is still unavailable at that deadline, Mozkey abandons that
generation and reveals the normal Mozc live-conversion result that was already
computed as the Zenz baseline. A late result from that generation is not
adopted. The debounce interval before the Zenz request is submitted is not
counted toward this 300 ms budget.

On Windows, the Zenz request is sent from `mozc_server` to
`mozc_zenz_scorer.exe` through a Windows named pipe. The scorer then calls the
bundled `llama-server.exe` on a localhost endpoint for local inference. The
localhost transport is hardened so that it does not rely on a fixed endpoint,
and internal requests include protection against accidental or stale local
endpoint mismatches.

The Windows Zenz named-pipe transport uses finite deadlines for connection and
request/response I/O, and explicitly interrupts pending I/O during shutdown.
A stalled scorer or peer therefore cannot leave the Zenz worker or scorer
waiting indefinitely, and the scorer can recover to serve subsequent requests.
The macOS Unix-domain socket path likewise wakes pending poll operations during
shutdown.

Surrounding context for Zenz uses dedicated fields separate from the normal Mozc
generic context. The Server reports the required preceding / following lengths
to the client; Windows TSF and macOS IMK then acquire only the requested
directions and lengths and attach them as `zenz_preceding_text` /
`zenz_following_text`. The normal Mozc surrounding-text semantics are left
unchanged.

Zenz correction is delayed by a configurable debounce interval. The default
delay is 1000 ms. The minimum number of characters required to start Zenz
correction can also be configured. If the current composition changes before
the Zenz result arrives, the old result is discarded by generation/key checks.

While a deferred presentation is waiting for Zenz, pressing Space first reveals
the current normal Mozc conversion result without advancing to the next
candidate. Enter commits exactly the presentation that is visible at that
moment, and Undo restores that same presentation as preedit. Shift-based ASCII
input likewise commits the visible presentation before starting ASCII input.
Backspace immediately reflects the post-deletion Mozc result; that Backspace
does not itself start a fresh Zenz correction, and normal deferred Zenz
scheduling resumes with subsequent real text input.

Zenz output is validated before display. Outputs that are empty, too short,
identical to the Mozc result, too long, malformed, or likely to contain unsafe
text are rejected. Mozkey also rejects likely context echo in which the output
starts by repeating an unusually long suffix of already committed left context
relative to the current reading/Mozc result. Short overlaps are not rejected
unconditionally, and legitimate repetition supported by the current reading is
still allowed. If validation fails, Mozkey falls back to the current normal
Mozc live-conversion result.

User-dictionary candidates and ASCII / mixed-script surfaces that appear in the
current normal Mozc live-conversion result are protected before Zenz output is
adopted. For ASCII / mixed-script surfaces, when the protected reading can be
identified safely, such as in `もずきー -> Mozkey`, the reading is temporarily
replaced with a placeholder in the Zenz prompt and restored to the selected
surface after the response. This prevents Zenz from silently overwriting the
protected word as `モズキー` while still allowing correction of the surrounding
sentence.

The surface selected by normal Mozc is also treated as an orthographic baseline
for Zenz adoption. If Zenz alone introduces a Latin spelling such as
`東京 -> Tokyo` or `コンピュータ -> computer`, or removes or mutates a Latin /
technical token already selected by Mozc, Mozkey restores only the affected
segment when the result can be projected safely back onto the Mozc segment
boundaries, preserving unrelated Zenz corrections. If safe projection is not
possible, or if the saved baseline segments are missing or inconsistent, a Zenz
result containing an unsafe orthographic transition is not adopted. Latin /
technical tokens already selected by normal Mozc, including `GitHub`, `iPhone`,
`AI`, `C++`, `GPT-5`, `UTF-8`, `HTTP/2`, and `Windows11`, remain valid baseline
surfaces.

ASCII numeric literals are handled separately from these Latin / technical identities.
Numeric spellings, including signed integers and decimals, remain visible in the Zenz
prompt instead of being replaced with placeholders, and the adopted output must preserve
the same spelling and occurrence count. For each numeric occurrence, when the normal Mozc
baseline has an adjacent Japanese character, the Zenz output may replace it with another
Japanese character. A non-Japanese adjacent character must remain exactly the same, and a
string boundary must remain a boundary. This allows corrections such as `1代 -> 1台` and
surrounding-context corrections while rejecting changes such as `100円 -> 100¥`,
`100% -> 100$`, `2026/09/23 -> 2026-09-23`, and `HTTP/2 -> HTTP 2`.

Zenz output may also normalize visible punctuation style. Before adoption,
Mozkey restores the symbol style from the current composition or normal Mozc
live-conversion result. This is preservation rather than fullwidth
normalization: `（test）` stays fullwidth when that was the source style, while
`(test)` stays ASCII. ASCII-token-like contexts such as code-like words, paths,
and URLs are handled conservatively to avoid unwanted widening.

Zenz can also carry sentence-final or expressive punctuation forward from
surrounding context even when the current composition did not request it.
Before adoption, Mozkey limits trailing symbols such as `！` / `!`, `？` / `?`,
`。`, `…`, and `〜` to the count already present at the end of the current
composition or normal Mozc live-conversion result. Only the excess trailing
symbols are removed, so semantic and orthographic corrections are preserved;
punctuation inside the candidate is not modified by this repair.

Japanese-only user-dictionary surfaces keep their natural reading in the Zenz
prompt. After the Zenz response, Mozkey validates the selected surface
boundaries. If extra kana is attached immediately after the protected surface,
the result is accepted only when the attachment can be repaired safely; otherwise
the normal Mozc live-conversion result remains visible.

If a Zenz response drops or changes a protected surface and placeholder
restoration or safe repair cannot preserve the required number of occurrences,
the response is rejected and the normal Mozc live conversion result remains
visible. This does not pin every candidate that merely exists in the user
dictionary. Protection is based on surfaces that actually appear in the current
normal live-conversion result.

Zenz live correction is disabled for password fields and for composition text
that has no Japanese-script signal, such as intermediate raw romaji input.
Japanese text mixed with ASCII can still be eligible when it passes the privacy
gate.

For Windows TSF password input scopes and macOS Secure Event Input, application
surrounding text is not acquired for Zenz and extended Zenz context acquisition
is skipped.

Zenz feedback learning is optional. When enabled, a displayed Zenz result is not
stored just because it was shown. It becomes a pending accepted feedback only
when the user explicitly commits the visible Zenz result, such as by pressing
Enter or by using a direct-commit punctuation/symbol.

Pending accepted feedback is written to the local TSV only if it is not canceled
by the next user action. Backspace, Escape, Revert, Undo, and similar correction
actions discard the pending feedback. IMEOff / MakeSureIMEOff are treated as
post-commit mode changes rather than cancellation, so the pending feedback is
confirmed. Moving from a visible Zenz correction to normal conversion operations,
such as Space or candidate movement, records the Zenz result as rejected feedback
instead. Ordinary rejected feedback from these operations is interpreted as a
negative ranking signal, not as a hard command to permanently suppress the
candidate.

Zenz feedback auto-blocking uses both an ordinary-reject count and a reject ratio.
For the same full-sequence feedback entry, an auto-block is active only when
`ordinary rejects >= minimum reject count` and
`ordinary rejects / (accepted + ordinary rejects) >= minimum reject percentage`
are both true. The defaults are 1 rejection and 50%. The comparison is inclusive,
so `accepted=1 / rejected=1` is blocked at exactly 50%. Explicit hard rejects are
handled separately as absolute blocks and are not part of this ratio decision.

While an entry is ordinarily auto-blocked, Mozkey still lets Zenz produce the
candidate internally but keeps it hidden as a shadow observation. If the final
commit for the same reading matches the hidden Zenz value, one accepted feedback
observation is added. This hidden match is used only for Zenz feedback recovery
and does not add another Mozc user-history learning event. Once matching
observations lower the reject percentage below the threshold, for example to
`accepted=2 / rejected=1` (33%), the auto-block clears automatically. If the same
reading is committed as a different value, one ordinary rejection is added. If
continued typing changes the final reading, the shadow observation is neutral and
does not update feedback. Explicit hard rejects do not create shadow observations
and therefore do not self-recover.

The feedback TSV is scoped to full Zenz sequences: a complete reading key, a
complete correction value, and a coarse non-reversible context class. It does not
store segment-local or lexical-unit feedback. Accepted Zenz corrections may still
be learned into Mozc user history as external conversion results, but that is a
separate Mozc-history path rather than an additional Zenz feedback-store record.
When the accepted Zenz result can be safely reverse-projected onto the previous
normal Mozc live-conversion segments, Mozkey learns the projected segment
sequence as an external multi-segment commit. If Mozc can reproduce the same
key/value candidate through normal conversion, the candidate structure is reused
so user history receives evidence closer to a normal conversion commit. Only
segments actually changed by Zenz are marked as strong user-selected commits. If
reverse projection fails, or if the privacy / password gates reject the text,
Mozkey falls back to full-sequence learning only.

Space is treated specifically as a candidate-change operation, not as a plain
cancel back into the live-conversion composition. If deferred presentation is
still waiting for Zenz, the first Space reveals the current normal Mozc
conversion result without advancing to the next candidate. When Space is pressed
while a Zenz correction is already visible, Mozkey restores the underlying Mozc
conversion as an ordinary conversion result without opening the candidate window
yet. If the user then types more text, the restored Mozc conversion is committed
first and the new text starts a fresh composition. Pressing Space again follows
the ordinary conversion path and opens the candidate window for the next
candidate.

Zenz feedback is reused differently for single-segment and multi-segment
conversions.

For single-segment normal conversion, Zenz feedback is reused as ranked
candidate augmentation inside the rewriter chain. Stored feedback is scored
from accepted counts and rejected reasons. If the feedback value already exists
in the candidate list, its cost is adjusted by that score. If it does not exist,
a synthetic candidate can be inserted at a natural feedback-adjusted cost
position instead of being forced to the top. Ordinary rejected feedback lowers
the candidate's ranking without deleting it; only explicit hard-reject reasons
act as strong suppression. ZenzFeedbackCandidateRewriter runs before
UserSegmentHistoryRewriter, so explicit user selection history can still make
the final ranking decision.

For multi-segment live conversion, the rewriter-chain feedback ranking does
not rewrite normal conversion candidates, because phrase boundaries are owned by
the converter and should not be collapsed. Instead, accepted feedback can still be replayed via
the session-level live-correction fast path. This preserves learned full-phrase
corrections such as `かれはてんてきです` -> `彼は天敵です`.

Feedback learned in a `sensitive_like` context is not reused for ordinary-context candidate ranking.

Zenz feedback data can be managed from the config dialog. The management dialog
shows learned entries in a read-only table and supports search, import, export,
single-entry deletion, and full deletion. This avoids requiring users to edit the
TSV file directly.

The management dialog can also explicitly block a selected Zenz correction.
Already blocked corrections cannot be blocked again. To unblock a correction,
delete the corresponding feedback entry and relearn it if needed.

#### Zenzai v3/v3.2 condition fields

Zenz live correction can pass additional condition fields using the special-token format expected by Zenzai v3/v3.2. These fields are not ChatGPT-style system prompts; they are short hints mapped to the conditional input format used by the Zenzai model.

- `profile`: passed as `U+EE03` profile for a short description of the writer or use case.
- `topic`: passed as `U+EE04` topic. This field is experimental.
- `style`: passed as `U+EE05` style. This field is experimental.
- `settings`: passed as `U+EE06` settings. This field is experimental.
- Right context: when dedicated Zenz text on the right side of the caret is available, it is passed as `U+EE07` right context for Zenzai v3.2.

Empty `profile`, `topic`, `style`, and `settings` fields are omitted from the prompt. Right context is not a fixed user-entered phrase. On Windows TSF / macOS IMK, the Server requests the required amount and the client automatically acquires text on the right side of the caret and supplies it as `zenz_following_text`.

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

It also protects functional-prefix and whole functional-chain paths immediately
after noun-like committed context. For example, after committing `彼`, typing
`になった` or `なのか` is biased toward `彼になった` / `彼なのか` instead of
content-node hijacks such as `彼担った` / `彼七日`. Conditional chains such as
`なら`, `ならば`, and `ならでは` are also protected, so after committing
`練習`, typing `なら` is biased toward `練習なら` instead of `練習奈良`.

It also protects common functional expressions such as `しか` followed by a
negative expression after a noun or quantity-like context. For example,
`2めいしかいない` is biased toward `2名しかいない` instead of intermediate
homophone paths such as `2名司会...`, `2名視界...`, or `2名士会内`.

It also protects some context-sensitive noun and suffix constructions. For
example, `追記したい` is biased away from homophone noun candidates such as
`追記死体`. After a committed sahen noun, `した`, `したの`, and `したん`
are also biased away from content-word hijacks such as `下`, `舌`, or
`シタン`. `山梨県立美術館` is biased away from broken suffix paths such as
`山梨県率美術館`, and place-suffix phrases such as `滋賀方面` or `佐賀空港`
are biased away from short `content + が` splits such as `子が方面`.

### Direct commit for punctuations/symbols

With the direct-commit option enabled, configured punctuations/symbols are committed immediately.

Examples:

- `tesuto.` → `てすと。`
- `tesuto?` → `てすと？`
- `kakko(` → `かっこ（`
- `tesuto/` → `てすと・` when the symbol style maps `/` to the middle dot

The selectable targets include periods, commas, question marks, exclamation marks, parentheses, corner brackets, and the middle dot. `「` and `」` are configured independently. The middle dot `・` is handled as an independent Japanese separator symbol.

You can choose which punctuations/symbols are committed directly in the config dialog.

However, period- and comma-like punctuation immediately following a number is excluded from direct commit so that numeric input can continue normally. This allows values such as `3.14` and `1,000` to be entered without being committed at the punctuation, while ordinary punctuation direct commit in Japanese text continues to work as before.

When live conversion is enabled, direct-commit punctuations/symbols commit the currently displayed live conversion result instead of committing the raw kana composition.

For direct-commit punctuations/symbols, learning caused by the immediately
committed conversion is also kept pending until the next real text input. If the
next action is Backspace, Escape, Revert, Reset, or Undo, the pending learning is
reverted instead of being kept. IMEOff / MakeSureIMEOff are treated as
post-commit mode changes rather than cancellation, so the pending learning is
confirmed.

When a cancel-equivalent key such as Ctrl+Z is pressed immediately after a
direct-commit punctuation/symbol and that key is echoed back to the application,
Mozkey also discards the pending learning. Whether the already-committed text is
actually undone depends on the application's own Undo behavior.

### Hiragana learning after conversion cancel

When an active conversion is canceled with Esc, Ctrl+Z, or another key bound to Cancel, the preedit returns to raw hiragana. If the user immediately commits that restored hiragana preedit with Enter, Commit → IMEOff, IMEOff / MakeSureIMEOff, or a direct-commit punctuation/symbol, this fork treats the hiragana spelling as an explicitly selected candidate.

For example, if `きょう` is converted to `今日`, then canceled back to `きょう` with Esc and committed with Enter or Commit → IMEOff, committed by IMEOff while returning to direct input, or committed as `きょう。` by direct-commit punctuation, the hiragana body `きょう` is learned in user segment history similarly to F6 -> Enter. This makes the hiragana candidate more likely to be promoted for the same reading later.

For multi-segment conversions, this fork preserves the conversion-time segment boundaries when possible. For example, if `おつかれぺん` had been converted as `お疲れ | ペン`, then canceled back to `おつかれぺん` and committed unchanged, the learning is recorded as segment-level hiragana preferences such as `おつかれ -> おつかれ` and `ぺん -> ぺん`, rather than as a single whole restored preedit.

This strong-learning path is intentionally narrow. It applies only when the preedit is unchanged after Cancel and is committed immediately with Enter, Commit → IMEOff, IMEOff / MakeSureIMEOff, or a direct-commit punctuation/symbol. For direct-commit punctuation/symbols, only the restored hiragana body is learned as the spelling preference; the punctuation/symbol suffix is not included in that user-segment history entry. The committed hiragana body must be at least two hiragana characters, the reading and committed value must be identical, and the input must not be from a password field. Plain raw preedit commits, commits after editing the canceled preedit, one-character hiragana commits, non-hiragana commits, and password fields are excluded.

With live conversion enabled, the same behavior applies when the user explicitly cancels back to hiragana and immediately commits it with Enter, Commit → IMEOff, IMEOff / MakeSureIMEOff, or a direct-commit punctuation/symbol. Internal live-conversion cancellation used only for continuing input does not trigger this strong-learning path.

### Partial revert of history learning after immediate correction

When a committed conversion is immediately followed by Backspace, or by a key assigned to Cancel in the current keymap, this fork treats it as a revert signal for the just-committed learning result.

For direct-commit punctuations/symbols, the committed text may already have been sent to the application without using Mozkey's normal Undo context. In that case, a cancel-equivalent key such as Ctrl+Z still discards Mozkey's pending learning, while the key itself is passed through to the application. Whether the visible text is actually undone depends on the application's own Undo behavior.

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

### Space reconversion for selected text (Windows / MS-IME style keymap)

In the Windows MS-IME style keymap, pressing Space while committed application text is selected reconverts that selected text. When no text is selected, the same key keeps the normal behavior and inserts a space.

In the keymap editor, this command is shown as `Reconvert selected text, or insert space if no text is selected`. It does not unconditionally intercept the Space key; it is enabled only when the MS-IME style keymap assigns this command to Space in the precomposition state. If the user has customized the Space key binding, that custom keymap takes precedence.

Users with an existing custom keymap can enable this behavior by selecting the MS-IME style keymap again or by manually assigning `Reconvert selected text, or insert space if no text is selected` to Space in the precomposition state.

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

### Windows IME icon setting

On Windows, the config dialog can switch the Mozkey IME icon.

The available styles are:

- Default
- Monochrome (Black)
- Monochrome (White)

This setting updates the `IconFile` / `IconIndex` values registered in the Windows TSF language profile for Mozkey, and applies to the IME icon shown in the taskbar and IME list.

Administrator approval may be required when applying this setting. Depending on the Windows icon cache or input-method list refresh timing, the taskbar or IME-list icon may not update immediately. If it does not update, restart Windows.

### Windows Search / immersive TSF support

On Windows, Mozkey uses an in-process Win32 renderer path for immersive TSF
hosts such as the Windows Search UI so that candidate, suggestion, and
live-conversion ruby windows are not hidden behind the host presentation layer.
The renderer runs on a dedicated UI thread inside the TSF host process, and the
renderer windows are created with the host top-level window as their owner while
avoiding focus activation.

Ordinary conversion candidates keep the existing mouse behavior: mouse-down
highlights the candidate and mouse-up selects it.

For an unfocused passive suggestion list, mouse-down does not highlight a
candidate or move the session into ordinary conversion. Mozkey keeps the
candidate ID pressed during that mouse gesture and submits it with
`SUBMIT_CANDIDATE` on mouse-up.

A passive Suggestion shown during live conversion is generated from a cloned
`ImeContext`, separately from the converter state used by the active live
conversion. The same numeric candidate ID can therefore refer to different
values in the visible Suggestion list and the normal conversion list. Mozkey
keeps the cloned context that produced the visible Suggestion and resolves the
mouse-up candidate ID against that same converter state, preventing an unrelated
normal-conversion candidate from being committed instead of the suggestion that
was actually clicked.

Dragging outside the suggestion list before releasing the mouse does not commit
a suggestion, while ordinary candidate clicking keeps its previous behavior.

### Candidate window, suggestion window, and ruby display appearance (Windows / macOS)

On Windows and macOS, the config dialog can customize the appearance of the
candidate window, the suggestion window, and the ruby display shown during live
conversion.

The candidate window, suggestion window, and ruby display can use Light, Dark,
Auto (follow system theme), or Custom colors. New configurations default all
three theme selectors to Auto. Auto resolves the current system appearance and
uses the corresponding light or dark renderer theme; after the OS appearance
changes, the new theme is picked up on the next renderer update.

Explicit Light, Dark, and Custom selections do not follow later system-theme
changes. The suggestion window and ruby display can also use Follow Candidate,
which inherits the candidate window's resolved or custom appearance. Auto and
Follow Candidate are separate settings.

Custom colors can be configured for the candidate and suggestion windows,
including the background, text, selected background, selected border, border,
shortcut, description, footer, and scrollbar colors. For the ruby display, the
background, text, and border colors can be customized.

The display size, corner radius, opacity, and shadow spread, opacity, angle,
and distance can also be configured for each window. The shadow angle uses
screen coordinates: 0° is right, 45° is down-right, and 90° is down. Set the
shadow distance to 0 for an even shadow on all sides.
Candidate-like displays such as conversion candidates and usage/infolist windows
use the candidate window settings, while prediction/suggestion displays use the
suggestion window settings.

On Windows, horizontal padding, vertical padding, and distance from the input
text can be configured independently for the ruby display. These are logical design values,
not fixed physical-pixel values; they scale with the ruby display size and the
DPI of the target monitor. In multi-monitor setups, the renderer recalculates
the font, padding, corner radius, shadow, and input-text gap for the monitor that
contains the composition target, and converts the TSF geometry to physical
coordinates before placing the window.

The font used for the candidate window, infolist window, and live-conversion
ruby display can also be changed from the config dialog. The setting can be
reset to the default font, and the renderer falls back to the default font if
the selected font cannot be used reliably for candidate rendering.

The primary text weight can also be configured independently from 100 to 900
for the candidate window, suggestion window, and live-conversion ruby display.
The default is 400 (Regular). Auxiliary text such as candidate shortcuts,
descriptions, footer labels, and infolist text keeps its existing weight.

The ruby display shown during live conversion can be enabled or disabled from
the config dialog.

On Windows, the IME mode indicator can use System theme, Dark, Light, or
Custom. System theme follows the Windows light/dark appearance; explicit Dark
or Light keeps that appearance even when the Windows theme changes. Custom
allows separate colors for alphanumeric, Hiragana, and Katakana indicators,
plus size, corner radius, border, and shadow adjustments.

When the input position is near an edge of the monitor work area, the indicator
is automatically repositioned to stay on-screen. Reset restores the indicator
theme and custom appearance values to their defaults without changing whether
the indicator itself is enabled.

### Vertical writing support (Windows / macOS)

On Windows and macOS, Mozkey lays out the candidate window, prediction/suggestion display, infolist, and live-conversion ruby for vertical writing while preserving the existing horizontal-writing behavior.

For candidate navigation in vertical writing, both Windows and macOS pass the vertical-writing state into the session layer. When the active keymap uses the supported existing command bindings, Mozkey reinterprets arrow keys to match the visual writing direction.

- In Suggestion state, Left enters the candidate list.
- In Conversion / Prediction state, Left / Right move through candidates while Up / Down move between segments.
- In Conversion state, `Shift+Up` shrinks the segment width and `Shift+Down` expands it.
- Existing `Shift+Left` / `Shift+Right` segment-width operations remain available for compatibility.

Horizontal writing, ordinary cursor movement during Composition, `Ctrl+Shift` combinations, and keymaps that do not use the supported command bindings retain their existing behavior.

#### Windows

On Windows, Mozkey detects vertical composition geometry and uses DirectWrite vertical text rendering for primary candidate and infolist text.

The candidate window prefers placement on the left side of a vertical composition. When an application reports a narrow input-line geometry, Mozkey adds placement clearance so that the candidate display does not sit too close to the input text. This adjustment is based on the reported composition geometry rather than application-specific name checks.

In vertical mode, candidate columns keep natural per-candidate geometry based on their own text and description content. A long candidate does not force unrelated candidate columns to expand to the same size. The selection background extends through the candidate body even when the visible text is short, making the currently selected vertical column easier to identify.

Cascading candidate windows are anchored to the focused candidate and prefer expanding to the left, outward from the vertical composition. If the left-side placement would collide with the candidate/preedit region or cannot fit within the working area, the renderer falls back to the right. The existing horizontal cascading placement is preserved.

The live-conversion ruby display also follows vertical composition geometry. When an uncommitted composition wraps across multiple vertical columns, such as in Word, the renderer keeps the ruby outside the already occupied composition span instead of placing it over an earlier column.

#### macOS

On macOS, Mozkey propagates the writing direction obtained by the IMK input side into the renderer and uses Core Text vertical text rendering for primary candidate, infolist, and ruby text.

For candidate and suggestion rows, the technical Core Text frame required to render the full string is kept separate from the visible candidate-column width. The visible width uses a nominal CJK column width as its floor and expands only when the actual glyph ink requires more space. This avoids unnecessarily wide columns for Japanese, ASCII, and half-width katakana while preserving enough room for genuinely wide glyphs.

The vertical gap between the candidate/suggestion display and the composition text is 6 pt. The live-conversion ruby is also placed with an effective 6 pt gap. Existing horizontal-writing spacing and placement remain unchanged.

The infolist uses compact per-column Core Text frames in vertical mode. The ruby display similarly keeps a sufficiently large technical text frame while sizing the visible pill from the nominal CJK column width and actual glyph ink.

Cascading candidate windows are anchored to the focused candidate and prefer expanding to the left, which is outward from the vertical composition. If that placement would collide with the candidate/preedit region or run out of room, the renderer falls back to the right side. Sibling cascade windows are also kept from overlapping each other.

The macOS vertical-writing implementation is separated into dedicated layout and writing-direction logic, with unit tests for candidate layout, infolist layout, writing direction, and window placement. The existing horizontal-writing path retains its previous behavior.

### Windows preedit presentation and display colors

On Windows, enabling `Dim unresolved Roman input` while using Roman input gives only the unresolved trailing romaji that is still an incomplete conversion-rule prefix a separate, subdued presentation. This is presentation metadata only: it does not change the input string, conversion key, or committed text.

The feature is opt-in. For a new or otherwise unset profile, the default is OFF. If an explicit ON / OFF value has already been stored, that existing user choice is preserved. Dimness is configurable from 0% to 90% in 1% steps, with a default of 75%. Higher values move the unresolved Roman text closer to the background color; 0% keeps high contrast and 90% is very faint.

Pending-romaji presentation metadata is also preserved through live-conversion and Zenz-correction display paths when the original segment key/value relationship can be kept safely. Presentation-only splitting never synthesizes segment keys from displayed values; if a segment cannot be split safely, its original key/value is kept unsplit.

The presentation is applied through a dedicated Windows TSF display attribute. In Gecko-based hosts such as Firefox, Mozkey uses a subdued text color matched to the editor background only when that background can be sampled safely. Selection-painted regions, clipped geometry, and non-uniform surfaces are not cached as editor backgrounds; Mozkey falls back to a compatibility presentation instead. When the feature is OFF, no screen sampling is performed for this feature.

The config dialog also allows the following display colors to be configured separately for input text and the converting segment:

- Text color
- Background color
- Underline color

For new or otherwise unset profiles, the default underline color is `#30DCC8` for input text and `#C1A5AB` for the converting segment. Explicitly stored underline-color choices, including whether custom underline colors are enabled, are preserved.

These options are intended to improve the visibility of uncommitted text, especially for users who need stronger visual contrast while composing or converting Japanese text.

The settings are applied through Windows TSF display attributes. They work only in applications that honor those attributes. Some applications, including Chrome and Edge, may ignore them.

Enhanced system dictionary
--------------------------

This fork includes scripts to build an enhanced Mozc system dictionary from
external dictionary sources.

Separately from the external generated dictionaries, Mozkey also maintains a
small tracked manual override dictionary for daily vocabulary, practical
vocabulary, loanwords, and English spelling candidates. Normal Japanese entries
are expected to remain natural first candidates, while English spelling entries
are treated as secondary candidates. See [Koyasi Dictionary Data](src/data/dictionary_koyasi/README.md) for the detailed policy and evaluation workflow.

The daily local dictionary can be generated from:

- merge-ut-dictionaries
  - place names
  - SudachiDict-derived vocabulary
- dic-nico-intersection-pixiv
  - additional proper nouns, internet slang, works, characters, and subculture terms
  - entries already covered by the generated daily dictionary or the base Mozc dictionaries are skipped
  - values ending with decorative symbols such as `☆`, `★`, `♡`, `♪`, or `※` are rejected because they tend to be noisy in daily conversion
- mozcdic-ut-personal-names
  - personal names, stage names, and activity names
  - entries already covered by the generated daily dictionary, nico/pixiv delta dictionary, or base Mozc dictionaries are skipped
  - risky short readings, long katakana-like names, group-like names, ASCII values, and punctuation-heavy values are filtered or demoted
- Mozkey syntax / expressive kana guard dictionary
  - small generated guard entries for high-impact segmentation failures
  - for example, protecting paths such as `と打ちたいのに`, `に分ける`, `した方が`, `したにもかかわらず`, `にまで`, `までに`, `までも`, `肌身離さず`, `になってしまいます`, and `になっちゃいます`
  - also adds natural kana candidates for completed expressive forms such as `うっそ`, `くっそ`, `やっば`, `ちっす`, `ほえ～`, `ほぇ～`, and `ほっほーん`

Large generated dictionary files are not committed to this repository.
They are generated locally under `src/data/dictionary_koyasi/generated/`.

Windows release MSI packages may include the locally generated `daily` system dictionary profile. See [Third-party notices](THIRD_PARTY_NOTICES.md) and [Koyasi Dictionary Data](src/data/dictionary_koyasi/README.md) for source and license notes.

Before building a package with the enhanced dictionary enabled, regenerate the daily dictionary locally:

```powershell
.\tools\dictionary\prepare_daily_dictionary.ps1
```

See:

* [Koyasi Dictionary Data](src/data/dictionary_koyasi/README.md)
* [Third-party notices](THIRD_PARTY_NOTICES.md)

### Building a Zenz-enabled Universal macOS PKG

The distributable macOS Zenz runtime is generated by the tracked
`src/mac/installer/zenz_runtime/build_universal_runtime.zsh` builder and staged
with `stage_runtime_assets.zsh` before building the Universal package.
Both `llama-server` and `mozc_zenz_scorer` are same-path Universal executables
containing `arm64` and `x86_64` slices.

```bash
src/mac/installer/zenz_runtime/build_universal_runtime.zsh \
  "$HOME/Downloads/mozkey-macos-zenz-runtime"

src/mac/installer/zenz_runtime/stage_runtime_assets.zsh \
  "$HOME/Downloads/mozkey-macos-zenz-runtime"

cd src

bazelisk build \
  --config=oss_macos \
  --config=release_build \
  --macos_cpus=x86_64,arm64 \
  --define=macos_zenz_runtime=1 \
  //mac:package \
  --verbose_failures
```

A generic package built without `--define=macos_zenz_runtime=1` should not be
treated as a distributable Zenz-enabled macOS PKG.

The formal architecture/runtime gate verifies the final `.pkg` on Apple
Silicon, transfers that exact SHA-256-identical package artifact to a native
Intel runner, and runs the same package-level verifier through a real
`mozc_zenz_scorer -> llama-server -> model` request. See
[`src/mac/installer/zenz_runtime/README.md`](src/mac/installer/zenz_runtime/README.md)
for the exact source pin, BUILD-CONTRACT, staging, signing boundary, and
verification contract.

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
