# MKB2 Naginata・HRM・Space 仕様

## 1. 目的

この文書は、MKB2のNaginataレイヤーで使用する以下の動作仕様と実装をまとめたものです。

- Naginata文字入力と同時押し
- Home Row Modifier（HRM）
- Naginata SpaceとNum／Arrレイヤー
- Modifier使用中の英数キー入力
- トラックボールのスクロール切り替え
- 回帰テストとビルド方法

対象は現在の`feature/zmk-naginata-v18`ブランチです。

## 2. 用語

| 用語 | 意味 |
|---|---|
| Nagi | Naginata入力レイヤー（レイヤー1） |
| Def | 通常の英数レイヤー（レイヤー0） |
| HRM | 文字キーのタップとModifierのホールドを兼用するHome Row Modifier |
| 同じ側 | HRMキーと同じ手側にあるキー |
| 反対側 | HRMキーと反対の手側にあるキー |
| Tap | Naginata文字または通常キーとして処理する状態 |
| Hold | Modifierとして処理する状態 |
| Pending | TapかHoldか未確定の状態 |
| shortcut経路 | Naginata変換を通さず、`&kp`で英数レイヤー相当のキーコードを送る経路 |

## 3. 対象キー

### 3.1 HRMキー

NagiレイヤーのホームポジションをHRMとして使用します。

| 手 | キー | Hold時のModifier | behavior |
|---|---|---|---|
| 左 | A | Left Control | `ng_mt_l` |
| 左 | S | Left Alt | `ng_mt_l` |
| 左 | D | Left Shift | `ng_mt_l` |
| 左 | F | Left Command | `ng_mt_l` |
| 右 | J | Right Command | `ng_mt_r` |
| 右 | K | Right Shift | `ng_mt_r` |
| 右 | L | Right Alt | `ng_mt_r` |
| 右 | `;`相当位置 | Right Control | `ng_mt_r` |

### 3.2 Space兼レイヤーキー

| 物理位置 | behavior | Hold時 | Tap時 |
|---|---|---|---|
| 左Space・位置39 | `ng_num` | Numレイヤー2 | Naginata Space |
| 右Space・位置42 | `ng_sands` | Arrレイヤー5 | Naginata Space |

### 3.3 左右位置

現在のキーマップ位置番号では、HRMの左右判定を次のように設定しています。

- 左HRMがHoldを許可する反対側位置：`6–11`, `18–23`, `30–35`, `42–45`
- 右HRMがHoldを許可する反対側位置：`0–5`, `12–17`, `24–29`, `36–41`
- 左HRMに対するNaginata Spaceトリガー：右Spaceの位置42
- 右HRMに対するNaginata Spaceトリガー：左Spaceの位置39

位置番号は現在のMKB2 physical layout／matrix transformを前提にしています。

## 4. HRM仕様

### 4.1 基本判定

HRMの判定時間は100msです。

| HRMを押した後の操作 | 経過時間 | 判定 | 結果 |
|---|---:|---|---|
| HRMを単独で離す | 任意 | Tap | Naginata文字を入力 |
| 同じ側キーを押す | 任意 | Tap | Modifierを適用しない |
| 反対側キーを押す | 100ms未満 | Tap | Naginata同時押しとして処理 |
| 反対側キーを押す | 100ms以上 | Hold | Modifierを適用 |
| 反対側Spaceを押す | 100ms未満 | Tap | Naginata Space同時押し |
| 反対側Spaceを押す | 100ms以上 | Hold | Modifier＋通常Space |

100msちょうどはHold側として扱います。

### 4.2 同じ側キー

同じ側キーでは、HRMを長く押していてもModifierにしません。

例：右HRMの`J`を押し続けてから右側の`K`を押しても、`J`はRight Commandになりません。`J`のNaginata文字と`K`のNaginata文字として処理します。

これは、同じ手のロール入力で意図しないModifierが発動することを防ぐための仕様です。

### 4.3 反対側キーとNaginata同時押し

反対側キーでも、100ms以内ならModifierではなくNaginata同時押しを優先します。

例：

```text
J↓ → 100ms未満 → W↓
```

`J + W`をNaginata同時押しとして処理し、「ぎ」に対応する`G`, `I`を出力します。

この判定により、HRMキーを含む濁点・半濁点・拗音などの同時押しを維持します。

### 4.4 反対側キーとModifier

Modifierとして使用するときは、HRMを100ms以上先に保持します。

例：

```text
J↓ → 100ms以上 → W↓
```

この場合は次の順で処理します。

1. `J`をRight Commandとして押下
2. `W`をNaginata変換へ渡さず、通常の`W`キーとして押下
3. `W`を解放
4. `J`を解放するとRight Commandを解放

Modifier確定後は、対象キーを押した時点で英数キーコードを送るため、対象キーとHRMキーを離す順番に依存しません。

### 4.5 HRM単独長押し

左右位置判定を持つHRMは、100ms経過だけではModifierを送信しません。

- HRMを押す：Pending
- 100ms経過：Pendingのまま
- 反対側キーを押す：Holdへ確定
- 同じ側キーを押す、またはHRMを単独で離す：Tapへ確定

これにより、HRMを長く押しただけでModifierが一時的に有効になることを防ぎます。

## 5. Modifier使用中の英数キー仕様

### 5.1 入力言語は変更しない

Modifier使用中に送るのはModifierと通常キーコードだけです。

- `LANG1`を送らない
- `LANG2`を送らない
- NagiレイヤーからDefレイヤーへ移動しない
- OSの入力ソースを変更しない

「英数入力」は入力言語の切り替えではなく、Naginata変換を通さない物理キーコード出力を意味します。

### 5.2 shortcut経路

通常のNaginata入力では`&ng`を使用します。ModifierがHoldへ確定している間は`shortcut-binding = <&kp>`へ切り替えます。

| 状態 | 使用する経路 |
|---|---|
| 通常のNaginata入力 | `&ng` |
| 100ms以内のNaginata同時押し | `&ng` |
| Modifier確定中の文字入力 | `&kp` |
| Modifier確定中のSpace | `&kp SPACE` |

### 5.3 NagiとDefで配置が異なるキー

大部分のNaginataキーは、Naginataへ渡すキーコードとDefレイヤーの物理キーコードが同じです。異なる2か所は明示的に補正します。

| 位置 | Nagiで使用するキーコード | Modifier中に送るDef相当キーコード |
|---:|---|---|
| 7 | `U` | `BSPC` |
| 22 | `SEMI` | `U` |

実装では`shortcut-key-position`と`shortcut-keycode`で補正します。

## 6. Naginata Space仕様

### 6.1 基本状態

Naginata Space behaviorは次の4状態を持ちます。

| 状態 | 意味 |
|---|---|
| `UNUSED` | 未使用 |
| `HOLD` | Num／Arrレイヤーを保持中 |
| `TAP` | Naginata Space同時押しとして確定 |
| `SHORTCUT` | Modifier＋通常Spaceとして確定 |

Spaceの単独タップ／レイヤー判定時間は200msです。

### 6.2 Spaceを先に押した場合

```text
Space↓ → 反対側文字↓
```

Spaceを先に押した場合は経過時間に制限を設けません。

1. Space押下時にNumまたはArrレイヤーを一時的に有効化
2. 反対側のNaginata文字位置が押されたらレイヤーHoldを解除
3. SpaceをNaginata Tapとして押下
4. 後続文字をNaginataへ渡す

したがって、Spaceを200ms以上保持してから反対側文字を押しても、Naginata Space同時押しへ切り替わります。

### 6.3 文字を先に押した場合

Naginata本体は、文字の後にSpaceをそのまま渡すと文字を先に確定します。たとえば`J → Space`をそのまま送ると、`J`単独の「あ」とSpaceの変換に分かれます。

これを避けるため、100ms以内の逆順入力は内部イベント順を補正します。

```text
物理入力: J → Space
内部入力: Space → J
```

この補正により、Naginata本体には`Space + J`として見え、「の」を出力できます。

100ms以上経過している場合は逆順補正を行わず、HRMをModifierへ確定して通常Spaceを送ります。

### 6.4 Space単独とレイヤー使用

| 操作 | 結果 |
|---|---|
| 200ms未満でSpaceだけを押して離す | Naginata Spaceを1回送る |
| 200ms以上保持して離す | レイヤーを解除し、Spaceは送らない |
| Space保持中にレイヤー上のキーを使用する | レイヤーを解除し、Spaceは送らない |
| Space保持中に反対側Naginata文字を押す | Naginata Space同時押しへ切り替える |

Num／Arr使用後に余分なSpaceを送らないため、非トリガーのキー入力があった場合は`interrupted = true`として記録します。

### 6.5 解放順

Naginata同時押しとしてTapへ確定した後は、解放順で判定を変更しません。

どちらも同じ結果です。

```text
J↓ → Space↓ → Space↑ → J↑
J↓ → Space↓ → J↑ → Space↑
```

100ms以内なら、どちらも「の」になります。

## 7. Naginata重なり判定

左右ファームウェアで次を設定しています。

```text
CONFIG_NAGINATA_MIN_OVERLAP_MS=0
```

物理的な重なりが1回でも存在すれば、Naginata同時押し候補として扱います。最小重なり時間は要求しません。

HRMの100msは「Naginata同時押しかModifierか」を決める時間であり、Naginata本体の最小重なり時間とは別の設定です。

## 8. Num／Arrとポインティングデバイス

### 8.1 トラックボール

`snippets/Default/Default.overlay`で、トラックボールのスクロール変換をNumレイヤー2に設定しています。

```text
scroll.layers = <2>
```

- Numレイヤー以外：通常のポインター移動
- Numレイヤー2：スクロール入力へ変換

### 8.2 トラックパッド

トラックパッドのスクロール変換はArrレイヤー5です。

```text
scroller.layers = <5>
```

### 8.3 センサー入力に関する制約

Naginata Spaceの`interrupted`判定は物理キー位置イベントを監視します。ポインティングデバイスの移動イベント自体は監視していません。

そのため、Space兼Num／Arrキーを200ms未満だけ保持し、その間にキーを一切押さず、ポインティングデバイスだけを操作してすぐ離した場合は、Space単独タップとして扱われる可能性があります。200ms以上保持した場合はSpaceを送りません。

## 9. 実装構成

### 9.1 モジュール登録

ルート`CMakeLists.txt`から、2つのcustom behaviorをZMK applicationへ追加します。

```cmake
target_sources(app PRIVATE boards/shields/MKB/behavior_naginata_hold_tap.c)
target_sources(app PRIVATE boards/shields/MKB/behavior_naginata_space.c)
```

`zephyr/module.yml`はリポジトリルートをCMake、board、DTS、snippetのrootとして登録します。

### 9.2 HRM／Naginataキー実装

| ファイル | 役割 |
|---|---|
| `boards/shields/MKB/behavior_naginata_hold_tap.c` | HRM、Naginata通常キー、左右位置、100ms判定、Modifier中の英数経路 |
| `boards/shields/MKB/behavior_naginata_hold_tap.h` | Space behaviorと共有するHRM状態API |
| `dts/bindings/behaviors/zmk,behavior-naginata-hold-tap.yaml` | HRM behaviorのDTS binding |
| `dts/bindings/behaviors/zmk,behavior-naginata-key.yaml` | 通常NaginataキーwrapperのDTS binding |

最大16個のactive HRM／Naginataキーを固定配列で管理します。動的メモリは使用しません。

#### HRMの状態遷移

```text
UNUSED
  └─ HRM press → PENDING
       ├─ same-side key → TAP
       ├─ opposite key before 100ms → TAP
       ├─ opposite key at/after 100ms → HOLD
       ├─ opposite Space before 100ms → TAP
       ├─ opposite Space at/after 100ms → HOLD
       └─ HRM release → TAP

TAP  ── physical release → UNUSED
HOLD ── physical release → modifier release → UNUSED
```

`hold-trigger-key-positions`を持つホームポジションHRMでは、タイマーだけでHoldへ移行しません。反対側キーイベントが来た時点で、押下開始時刻との差を比較します。

`hold-trigger-key-positions`を持たない`ng_lt`は従来どおりタイマー型です。200ms経過でSymbolレイヤーHoldへ移行します。

### 9.3 Space実装

| ファイル | 役割 |
|---|---|
| `boards/shields/MKB/behavior_naginata_space.c` | Space、Num／Arr、逆順補正、Modifier＋Space |
| `dts/bindings/behaviors/zmk,behavior-naginata-space.yaml` | Space behaviorのDTS binding |

左右2個のSpaceを固定配列で管理します。動的メモリは使用しません。

Space behaviorはHRM共有APIを使用して、反対側HRMの状態を次のように分類します。

| HRM状態 | Space押下時の処理 |
|---|---|
| Pendingかつ100ms未満 | Spaceを先にNaginataへ押し、HRMをTapへ確定 |
| Pendingかつ100ms以上 | HRMをHoldへ確定し、通常Spaceを押下 |
| Tap | Naginata Spaceを押下 |
| Hold | 通常Spaceを押下 |
| 該当HRMなし | Num／ArrレイヤーHoldを開始 |

### 9.4 DTS設定

#### `zmk,behavior-naginata-hold-tap`

| プロパティ | 用途 |
|---|---|
| `bindings` | Hold behaviorとTap behavior |
| `tapping-term-ms` | HRM判定時間、または非位置型behaviorのHold時間 |
| `tap-trigger-key-positions` | Space同時押しを強制する位置 |
| `hold-trigger-key-positions` | Holdを許可する反対側位置 |
| `shortcut-binding` | Modifier中に使う通常キーbehavior（`&kp`） |
| `shortcut-key-position` | NagiとDefでキーコードが異なる位置 |
| `shortcut-keycode` | 上記位置で送るDef相当キーコード |

#### `zmk,behavior-naginata-space`

| プロパティ | 用途 |
|---|---|
| `bindings` | レイヤーHold behaviorとNaginata Space behavior |
| `tapping-term-ms` | Space単独タップとレイヤー使用の判定時間 |
| `tap-trigger-key-positions` | Naginata Space同時押しへ切り替える反対側文字位置 |
| `shortcut-binding` | Modifier確定時の通常Space behavior（`&kp`） |

#### `zmk,behavior-naginata-key`

| プロパティ | 用途 |
|---|---|
| `binding` | 通常時のNaginata behavior（`&ng`） |
| `shortcut-binding` | Modifier中の通常キーbehavior（`&kp`） |
| `shortcut-key-position` | NagiとDefでキーコードが異なる位置 |
| `shortcut-keycode` | 上記位置で送るDef相当キーコード |

## 10. 変更点

### 10.1 追加したファイル

- `CMakeLists.txt`
- `boards/shields/MKB/behavior_naginata_hold_tap.c`
- `boards/shields/MKB/behavior_naginata_hold_tap.h`
- `boards/shields/MKB/behavior_naginata_space.c`
- `dts/bindings/behaviors/zmk,behavior-naginata-hold-tap.yaml`
- `dts/bindings/behaviors/zmk,behavior-naginata-key.yaml`
- `dts/bindings/behaviors/zmk,behavior-naginata-space.yaml`
- `tests/naginata-hold-tap/`以下の回帰ケース
- `tests/naginata-space/`以下の回帰ケース

### 10.2 更新した設定

- `config/MKB.keymap`
  - Naginata文字を`ng_key`へ変更
  - 左右HRMを`ng_mt_l`／`ng_mt_r`へ分割
  - 左右Spaceを`ng_num`／`ng_sands`へ変更
  - 左右位置、100ms、200ms、shortcut経路を設定
  - 位置7のBackspaceと位置22の`U`を英数配置として補正
- `boards/shields/MKB/MKB_L_Base.conf`
  - `CONFIG_NAGINATA_MIN_OVERLAP_MS=0`
- `boards/shields/MKB/MKB_R_Base.conf`
  - `CONFIG_NAGINATA_MIN_OVERLAP_MS=0`
- `snippets/Default/Default.overlay`
  - トラックボールのスクロール対象をNumレイヤー2へ移動
- `zephyr/module.yml`
  - root CMake／DTS／snippetをmoduleとして登録

### 10.3 解決した問題

- `Space → J`は「の」になるが、`J → Space`は「あ＋変換」になる問題
- Num／Arr使用後に余分なSpaceが入力される問題
- HRMが同じ側キーでもModifierになる問題
- HRMキーを含む濁点同時押しがModifierに奪われる問題
- Modifier中の文字がNaginata変換される問題
- Modifier中に入力言語を変更せず、英数レイヤー相当キーを使いたい要求
- NagiとDefで異なるBackspace／`U`位置の英数キー補正

## 11. 回帰ケース

| ディレクトリ | 固定する動作 |
|---|---|
| `tests/naginata-hold-tap/same-side-taps` | 同じ側キーでは100ms経過後もModifierにしない |
| `tests/naginata-hold-tap/opposite-side-holds` | 100ms以上保持後の反対側キーでModifierを適用 |
| `tests/naginata-hold-tap/dakuten-chord` | 100ms以内の`J + W`で「ぎ」相当の`G`, `I`を出力 |
| `tests/naginata-hold-tap/modifier-uses-raw-key` | Modifier中はNaginata変換せず通常`W`と英数配置補正キーを出力 |
| `tests/naginata-space/opposite-space-held` | Space先行、文字先行、両解放順でNaginata Space同時押し |
| `tests/naginata-space/hrm-hold-shortcut` | 100ms以上保持したHRM＋SpaceでModifier＋通常Space |
| `tests/naginata-space/layer-hold-no-space` | Num／Arr使用後と長押し後に余分なSpaceを出力しない |

これらはZMKの`native_posix_64`形式です。`native_posix_64`はLinux専用のため、macOS上では実行できません。現在のmacOS環境では回帰ケースを定義済みですが、native testの実行確認はしていません。

## 12. ビルドと生成物

### 12.1 ビルドコマンド

`zmk-workspace`で次を実行します。

```bash
nix develop --command just build all
```

対象：

- `MKB_L_MODULE_ENC`
- `MKB_R_MODULE_TBv4`

### 12.2 UF2出力

```text
/Users/Hashimoto/Develop/workshop/zmk-workspace/firmware/zmk-config-MKB2/feature-zmk-naginata-v18/MKB_L_MODULE_ENC.uf2
/Users/Hashimoto/Develop/workshop/zmk-workspace/firmware/zmk-config-MKB2/feature-zmk-naginata-v18/MKB_R_MODULE_TBv4.uf2
```

左右のhardware firmware buildは成功しています。

## 13. 意図した制約

1. 反対側のNaginata同時押しは100ms以内に押す必要があります。100ms以上HRMを先に保持するとModifierとして解釈します。
2. 同じ側キーでは、HRMをどれだけ長く保持してもModifierになりません。
3. Space先行のNaginata同時押しには時間制限がありません。Spaceをレイヤーとして保持中でも、反対側Naginata文字を押すとNaginata Spaceへ切り替わります。
4. Modifier中の英数経路は入力言語を変更しません。入力ソースの状態は操作前のままです。
5. ポインティングデバイスだけを200ms未満操作した場合は、Space behaviorが使用を検知できない可能性があります。
6. 位置番号と英数キー補正は現在のMKB2キーマップを前提とします。physical layoutを変更した場合は左右位置と`shortcut-key-position`を再確認する必要があります。
