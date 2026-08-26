# MKB2 Naginata実装 引き継ぎ

## 1. 現在地

- リポジトリ: `https://github.com/hashi-yu/zmk-config-MKB2.git`
- ブランチ: `feature/zmk-naginata-v18`
- 作業ディレクトリ: `/Users/Hashimoto/Develop/workshop/zmk-config-MKB2`
- ZMK workspace: `/Users/Hashimoto/Develop/workshop/zmk-workspace`
- 最新実装コミット: `d5aa7de Refine Naginata HRM chord handling`
- 関連コミット:
  - `bc5ad2a Sync Naginata with language keys`
  - `b5a612f Fix Naginata chord and layer handling`
  - `d5aa7de Refine Naginata HRM chord handling`
- 詳細仕様: [`NAGINATA_SPEC.md`](./NAGINATA_SPEC.md)

実装・設定・回帰ケース・詳細な判定表は`NAGINATA_SPEC.md`を正とする。この文書は、次の担当者が作業を再開するための状態、検証状況、残作業をまとめる。

## 2. 実装済みの動作

### 2.1 Naginata HRM

NagiレイヤーのホームポジションをHRMとして使用する。

| 側 | 物理キー | Hold |
|---|---|---|
| 左 | A / S / D / F | LCtrl / LAlt / LShift / LCommand |
| 右 | J / K / L / セミコロン位置 | RCommand / RShift / RAlt / RCtrl |

判定時間は100ms。

- 同じ側のキーを続けて押した場合は、保持時間に関係なくNaginata Tapとして処理する。
- 反対側のキーを100ms未満で押した場合は、Naginata同時押しとして処理する。
- 反対側のキーを100ms以降に押した場合は、HRMをModifier Holdとして処理する。
- 左右位置判定を持つHRMは、単独で100msを超えて保持してもModifierを押下しない。単独解放時はTapになる。
- Tap／Hold確定後は、キーの解放順で結果を変更しない。

### 2.2 Modifier中の通常キー

HRMがModifier Holdへ確定している間、対象キーは`&ng`ではなく`shortcut-binding = <&kp>`を使う。

- `LANG1`／`LANG2`は送らない。
- レイヤーやOSの入力言語は変更しない。
- Nagi配置と通常英数配置が異なる次の2か所は、物理位置ごとのoverrideを使用する。
  - 位置7: Nagiでは`U`、通常配列では`BSPC`
  - 位置22: Nagiでは`SEMI`、通常配列では`U`

### 2.3 Naginata Space

- 左Space、位置39: `ng_num`。HoldはNumレイヤー2。
- 右Space、位置42: `ng_sands`。HoldはArrレイヤー5。
- SpaceのTap／Layer判定時間は200ms。
- Spaceを先に押してから反対側文字を押した場合は、時間制限なしでレイヤーを解除し、Naginata Space同時押しへ変換する。
- 文字を先に押してから反対側Spaceを100ms未満で押した場合は、内部的にSpaceを先に送ってから保留中HRMをTapへ確定する。
- 文字を先に100ms以上保持してから反対側Spaceを押した場合は、HRM Modifierと通常Spaceになる。
- Space単独を200ms未満で解放した場合はNaginata Spaceになる。
- Spaceを200ms以上保持した場合、またはレイヤーキーとして使用した場合は、解放時にSpaceを出さない。

### 2.4 ポインティングデバイス

- トラックボールのスクロール変換: Numレイヤー2。
- トラックパッドのスクロール変換: Arrレイヤー5。
- Space behaviorの割り込み判定は物理キー位置イベントを監視する。ポインター移動イベント自体は監視しないため、Spaceを200ms未満だけ保持してポインターだけを操作すると、解放時にSpaceが出る可能性がある。これは現時点の既知の制約。

## 3. 主な実装ファイル

| ファイル | 役割 |
|---|---|
| `boards/shields/MKB/behavior_naginata_hold_tap.c` | HRM、Naginata Tap、Modifier中のshortcut処理 |
| `boards/shields/MKB/behavior_naginata_hold_tap.h` | HRMとSpace間の内部連携API |
| `boards/shields/MKB/behavior_naginata_space.c` | Space／Num／Arrの状態遷移とイベント順補正 |
| `config/MKB.keymap` | behaviorインスタンス、左右位置、キーコードoverride |
| `dts/bindings/behaviors/zmk,behavior-naginata-hold-tap.yaml` | HRM DTS binding |
| `dts/bindings/behaviors/zmk,behavior-naginata-key.yaml` | Naginata通常キー DTS binding |
| `dts/bindings/behaviors/zmk,behavior-naginata-space.yaml` | Space DTS binding |
| `snippets/Default/Default.overlay` | ポインティングデバイスのレイヤー設定 |
| `CMakeLists.txt` | custom behaviorのコンパイル登録 |
| `zephyr/module.yml` | ZMK module、DTS、board、snippet登録 |

`CONFIG_NAGINATA_MIN_OVERLAP_MS=0`は左右のベース設定に入っている。

## 4. 回帰ケース

以下を追加・更新済み。

### HRM

- `tests/naginata-hold-tap/same-side-taps`
- `tests/naginata-hold-tap/opposite-side-holds`
- `tests/naginata-hold-tap/dakuten-chord`
- `tests/naginata-hold-tap/modifier-uses-raw-key`

### Space

- `tests/naginata-space/opposite-space-held`
- `tests/naginata-space/hrm-hold-shortcut`
- `tests/naginata-space/layer-hold-no-space`

これらはテスト定義として追加されているが、このMacでは実行していない。ZMKの`native_posix_64`テストはLinux環境が必要。

## 5. 検証状況

### 完了

最終実装後、次のファームウェアビルドが成功した。

```sh
cd /Users/Hashimoto/Develop/workshop/zmk-workspace
nix develop --command just build all
```

確認対象:

- `MKB_L_MODULE_ENC`
- `MKB_R_MODULE_TBv4`

生成物:

```text
/Users/Hashimoto/Develop/workshop/zmk-workspace/firmware/zmk-config-MKB2/feature-zmk-naginata-v18/MKB_L_MODULE_ENC.uf2
/Users/Hashimoto/Develop/workshop/zmk-workspace/firmware/zmk-config-MKB2/feature-zmk-naginata-v18/MKB_R_MODULE_TBv4.uf2
```

### 未完了

- 実機へUF2を書き込み、左右分割キーボードで最終確認する。
- Linux環境で回帰ケースを実行する。

ファームウェアのコンパイル成功は確認済みだが、実機でのキー入力結果は未確認。次の担当者は、実機確認前に動作確定とみなさないこと。

## 6. 実機確認チェックリスト

### HRM Tap／同時押し

- 同じ側の組み合わせが、長押しを含めてModifierにならない。
- `J + W`などのNaginata濁点同時押しが100ms未満で成立する。
- 反対側キーを100ms以降に押すとModifierとして働く。
- HRMを単独で長押しして解放するとNaginata文字になる。
- HRMと対象キーの解放順を変えても結果が変わらない。

### Modifier

- 左右のCtrl／Alt／Shift／Commandが期待どおり働く。
- Modifier中に入力言語が切り替わらない。
- Modifierとの組み合わせで、位置7がBackspace、位置22が`U`として働く。

### Space／Layer

- `Space → 反対側文字`がNaginata Space同時押しになる。
- `反対側文字 → Space`を100ms未満で行っても同じ結果になる。
- HRMを100ms以上保持してSpaceを押すと、Modifier + 通常Spaceになる。
- Num／Arrレイヤー使用後、Space解放時に余分なSpaceが入力されない。
- Space単独の短押しはNaginata Spaceになる。
- Space単独の200ms以上長押しではSpaceが入力されない。
- Numレイヤーでトラックボールがスクロールになる。
- Arrレイヤーでトラックパッドがスクロールになる。

## 7. 次に問題が出た場合の調査順

1. 操作順、押下間隔、解放順、左右位置を記録する。
2. `NAGINATA_SPEC.md`の判定表と照合する。
3. `behavior_naginata_hold_tap.c`でHRMがTap／Holdのどちらへ確定したか確認する。
4. Spaceを含む場合は`behavior_naginata_space.c`でSpace先行補正とレイヤー解除を確認する。
5. Modifier中のキーが誤る場合は、`config/MKB.keymap`の`shortcut-key-position`と`shortcut-keycode`を確認する。
6. ソース修正後は左右両ターゲットをビルドする。
7. ビルドだけで完了とせず、同じ実機操作で再確認する。

症状だけを例外処理するのではなく、Tap／Hold確定またはイベント順の原因を直す。

## 8. 作業上の注意

- `.DS_Store`は未追跡であり、コミット対象にしない。
- `NAGINATA_SPEC.md`と実装が食い違う場合は、意図を確認して両方を同じコミットで更新する。
- clean cutoverを維持し、旧behaviorのaliasや互換shimを追加しない。
- 実機確認で問題が出た場合、操作手順を再現ケースとして残してから修正する。
