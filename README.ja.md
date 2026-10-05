# openscex-modchip

[English](README.md) | 日本語 | [中文](README.zh.md)

[![CI](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml/badge.svg)](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml)
[![MISRA C:2012](https://img.shields.io/badge/MISRA%20C%3A2012-0%20deviations-brightgreen)](AGENTS.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

初代ソニー PlayStation（フット機）と PSone 向けのリージョン解除ファームウェアです。ひとつのソースが ATtiny85（SCEx 注入）と ATtiny84（SCEx とブート ROM BIOS パッチ）向けにビルドされます。設定されたリージョン文字列を SUBQ リージョンチェックの窓の間に送出し、プレイ中はデータ線をハイインピーダンスにします。光学ドライブエミュレータではなく、PS2 や Saturn には対応せず、LibCrypt は突破しません。

## 概要

| 項目 | 値 |
|:-----|:---|
| 対象コンソール | PlayStation フット機 PU-7 から PU-23、PSone PM-41 と PM-41(2) |
| MCU | ATtiny85（8 ピン DIP）、ATtiny84（14 ピン DIP） |
| 方式 | 両チップで SCEx 注入。ATtiny84 はブート ROM も patch する |
| リージョン | ビルドごとに 1 つ、`REGION=jp|us|eu`（既定 us） |
| クロック | 内蔵 8 MHz RC、またはコンソール外部（ハードウェア前提） |
| 信号線 | 4 本（SQCK、SUBQ、DATA、WFCK）＋電源、任意の LED |
| ツールチェーン | C17、MISRA C:2012 逸脱ゼロ、ピン留め Docker イメージ |

## 実機で検証済み

実機でリージョン外起動した組み合わせ（SCEx リージョン解除）。それ以外の組み合わせはビルドされコンテナ内のゲートセットに合格しますが、実機では未確認です。

| 基板ファミリ | コンソール | クロック | 注入 | 状態 |
|:-------------|:-----------|:---------|:-----|:-----|
| PU-8 | フット、NTSC-U/C | 内蔵 | SCEx | Verified 2026-10-05 |
| PU-18 | フット、NTSC-U/C | 内蔵 | SCEx | Verified 2026-10-05 |
| PU-18 | フット、NTSC-U/C | 外部 | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | 内蔵 | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | 外部 | SCEx | Verified 2026-10-05 |

実機で未確認（ビルドとシミュレーションのみ）: PU-7、PU-20、PU-22、PU-23、ブート ROM BIOS パッチの全モデル、およびどのファミリでも PAL と NTSC-J。PU-8 と PU-18 機の正確な SCPH、PSone 機のリージョンと SCPH、および外部クロックビルドをどのチップで行ったかは記録していません。

上記の実機結果は `TIMING=fixed` で取得しました。旧来（PU-8、PU-18）の行は `TIMING` の影響を受けません。両ビルドとも MCU ディレイを使うためです。PM-41 のキャリア行は fixed タイミングで、adaptive 既定のキャリアビット（WFCK ロック）は実機で再試験されるまでシミュレーションのみです。検証済みバイナリは `make TIMING=fixed` で再ビルドできます。

## 機能

| 機能 | 詳細 |
|:-----|:-----|
| SCEx 注入 | 44 ビット LSB first のリージョン文字列、旧来の静的ゲート方式と PU-22 以降の WFCK キャリア方式 |
| 基板自動検出 | 起動時の WFCK 挙動で旧来またはキャリアモードを選択、ひとつのビルドが全ファミリに適合 |
| 適応タイミング | WFCK キャリア基板では注入ビットを WFCK 周期の計数で測り、コンソールクロックにロックして MCU 発振器のドリフトに影響されない。`TIMING=fixed` で MCU ディレイに戻す |
| 単一リージョン | `REGION` の 1 つだけを送出、3 つすべては出さない |
| ステルス | SUBQ リージョンチェックの窓の間だけ、武装ごとに上限付きで注入、その後 DATA をハイ Z、LED をオフ |
| ディスク交換の再武装 | 窓を出ると一発動作が再武装 |
| ブート ROM BIOS パッチ | ATtiny84 のみ、日本のフット機と PAL の PSone、二段階 SCPH-1000/3000 を含む全モデル |
| 任意の LED | ステータス出力、LED 無しでもファームウェアは正しい |
| 実地診断 | チップがアイドルになった後に 4 バイトのフライトレコーダを EEPROM へ書き込み（検出した基板、セッション数、注入回数）、avrdude で読み出す。既存の PS1 モッドチップは見たものを報告しない |
| 検証 | ホストテスト行・分岐カバレッジ 100%、発振器帯域全体の simavr モデル、ミューテーションテスト、再現可能ビルド |

## 確度タグ

| タグ | 意味 |
|:-----|:-----|
| Read | 一次資料から（PsNee ソース、consolemods、psdevwiki） |
| Concluded | 資料から推論、いずれも明言せず |
| Verified | 本プロジェクトが実機または simavr モデルで確認 |
| Unknown | どの資料も示さず、実機で測定が必要 |

| 事実 | タグ |
|:-----|:-----|
| MCU ピン割り当て | Read（PsNee `MCU.h`）、所有者が実地実証済みと確認 |
| コンソール側タップ点、信号レベル | Read、PsNee と Mayumi の施工で実地実証済み |
| メカコンのピン番号 | Unknown、フォーラム中継、基板図で再確認 |
| 各パッド電圧 | Unknown、測定が必要 |
| PU-8、PU-18、PSone の SCEx 解除 | Verified 2026-10-05（上表参照） |
| PU-18 と PSone の外部クロックビルド | Verified 2026-10-05、他の基板は前提ゲート |
| BIOS パッチのタイミング | PsNee から Read、simavr で動作確認、実機では未 Verified |

## コンソール別のビルド

BIOS モデルはコンソールの実際の BIOS バージョンに従い、これは SCPH 番号より重要です。

| コンソール | 基板 | `MCU` | `REGION` | `BIOS` | 配線 |
|:-----------|:-----|:------|:---------|:-------|:-----|
| フット US/カナダ、SCPH-100x1/550x1/700x1/900x1 | PU-7 から PU-23 | `attiny85` | `us` | `none` | SCEx、4 本 |
| フット PAL、SCPH-100x2/550x2/900x2 | PU-8 から PU-22 | `attiny85` | `eu` | `none` | SCEx、4 本 |
| PSone US/カナダ、SCPH-101 | PM-41 / PM-41(2) | `attiny85` | `us` | `none` | SCEx、4 本 |
| PSone PAL、SCPH-102 | PM-41 / PM-41(2) | `attiny84` | `eu` | `scph_102` | SCEx + BIOS パッチ |
| PSone 日本、SCPH-100 | PM-41 | `attiny84` | `jp` | `scph_100` | SCEx + BIOS パッチ |
| フット日本、SCPH-5000/5500/3500 | PU-18 / PU-8 | `attiny84` | `jp` | `scph_3500_5500` | SCEx + BIOS パッチ |
| フット日本、SCPH-7000/7500/9000 | PU-20 から PU-23 | `attiny84` | `jp` | `scph_7000_9000` | SCEx + BIOS パッチ |
| フット日本、SCPH-3000 | PU-8 | `attiny84` | `jp` | `scph_3000` | SCEx + 二段階 BIOS パッチ |
| フット日本、SCPH-1000 | PU-7 | `attiny84` | `jp` | `scph_1000` | SCEx + 二段階 BIOS パッチ |

BIOS モデルで未対応: アジア向け（SCPH-xxx3、例 SCPH-5003/5903）は 2 回目チェックのない英語 ROM だが NTSC-J の CD コントローラにバックドアがなく SCEx だけでは不十分。開発機（DTL-H120x、PU-9）は焼いたディスクをそのまま読むためチップ不要。

## 設定

ひとつのソースが全派生をビルド、ノブは `make` に渡します。

| ノブ | 値 | 既定 | 選ぶもの |
|:-----|:---|:-----|:---------|
| `MCU` | `attiny85`、`attiny84` | `attiny85` | 8 ピン（SCEx のみ）または 14 ピン（BIOS パッチ追加） |
| `REGION` | `jp`、`us`、`eu` | `us` | チップが送出する 1 つのリージョン文字列 |
| `CLOCK` | `internal`、`external` | `internal` | 内蔵 8 MHz RC、またはコンソールクロック（`CLOCK=external EXT_F_CPU=<hz>`）、そのクロックとピン電圧を先に測定することが前提 |
| `TIMING` | `adaptive`、`fixed` | `adaptive` | adaptive は WFCK キャリアの注入ビットを WFCK 周期の計数で測り、コンソールクロックにロックする。fixed はコンパイル時の MCU ディレイを使う、実機で動かした方のタイミング |
| `BIOS` | `none`、`scph_102`、`scph_100`、`scph_7000_9000`、`scph_3500_5500`、`scph_1000`、`scph_3000` | `none` | ATtiny84 のみ、その BIOS バージョン向けのブート ROM パッチ |

既定以外のリージョンは成果物名にタグを付けます。例 `openscex-modchip-attiny85-jp.hex`。

## MCU ピン配置

PsNee の実証済み ATtiny85（`ATTINY_X5`）割り当て、PsNee `MCU.h` から Read。物理ピン番号は標準 PDIP 配置、SOIC や QFN はデータシートで確認。

### ATtiny85、8 ピン DIP（SCEx のみ）

| DIP ピン | ポート | 信号 | 方向 | 接続先 |
|:-------:|:-----|:-----|:-----|:-------|
| 1 | PB5 | RESET | - | リセットのまま |
| 2 | PB3 | LED | 出力、任意 | 抵抗経由で LED、または未接続 |
| 3 | PB4 | WFCK | 入出力 | 旧来ゲート、または PU-22+ ライブキャリア |
| 4 | GND | GND | - | コンソールのグランド |
| 5 | PB0 | SQCK | 入力 | SUBQ シリアルクロック |
| 6 | PB1 | SUBQ | 入力 | SUBQ シリアルデータ |
| 7 | PB2 | DATA | 出力、ロー駆動またはハイ Z | メカコンへの SCEx 注入 |
| 8 | VCC | VCC | - | コンソールの 5 V、先に測定 |

### ATtiny84、14 ピン DIP（SCEx と BIOS パッチ）

SCEx 信号は 85 と同じ並びで PORTA。AX、AY、DX は日本のフット機と PAL の PSone でのみ配線。

| DIP ピン | ポート | 信号 | 方向 | 接続先 |
|:-------:|:-----|:-----|:-----|:-------|
| 1 | VCC | VCC | - | コンソールの 5 V、先に測定 |
| 2 | PB0 | - | - | 未使用、外部クロック時は XTAL1 |
| 3 | PB1 | - | - | 未使用、XTAL2 |
| 4 | PB3 | RESET | - | リセットのまま |
| 5 | PB2 | AX | 入力 | BIOS パッチ、1 本目のアドレス線、パルス計数 |
| 6 | PA7 | - | - | 未使用 |
| 7 | PA6 | AY | 入力 | BIOS パッチ、2 本目のアドレス線、二段階モデルのみ |
| 8 | PA5 | DX | 出力、ロー駆動またはハイ Z | BIOS パッチ、データバスのオーバーライド |
| 9 | PA4 | LED | 出力、任意 | 抵抗経由で LED、または未接続 |
| 10 | PA3 | WFCK | 入出力 | 旧来ゲート、または PU-22+ ライブキャリア |
| 11 | PA2 | DATA | 出力、ロー駆動またはハイ Z | メカコンへの SCEx 注入 |
| 12 | PA1 | SUBQ | 入力 | SUBQ シリアルデータ |
| 13 | PA0 | SQCK | 入力 | SUBQ シリアルクロック |
| 14 | GND | GND | - | コンソールのグランド |

SCEx のみのコンソールでは ATtiny84 は 85 と同じ 4 信号を使い、AX、AY、DX は未接続のままにします。

## コンソール側タップ点

DATA 線が SCEx ビットストリームを運び、WFCK がゲートまたはキャリア。これらは PsNee と Mayumi のタップ点。基板ファミリは自動検出されるのでチップは同一で、異なるのはタップ点だけです。

| 基板ファミリ | SCPH 世代 | DATA 注入点 | WFCK の役割 | 確度 |
|:-------------|:----------|:------------|:-----------|:-----|
| PU-7、PU-8 | 1000-5003 | 復調したウォブル NRZ シリアルをメカコンへ、歴史的に "point 6" | 静的ハイ | Read、点はフォーラム断片、基板で確認 |
| PU-18、PU-20 | 550x-750x | ウォブル ASIC のデジタル NRZ 出力をメカコンへ | 静的ゲート | Read |
| PU-22、PU-23 | 7500-900x | CD プロセッサのトラッキング線、WFCK を疑似キャリアに、三線プラスリンク | ライブクロック、同期が必要 | Read |
| PM-41、PM-41(2) | PSone 100-103 | 同じトラッキング線キャリア方式、PM-41(2) ではアイドル時にチップ I/O をフロート | ライブクロック | Read |

メカコンの SUBQ と SQCK ピンはフォーラム中継、psxdev.net が 2025 年 10 月以降オフラインのため Unknown: PU-22 以降は SUBQ がピン 24、SQCK がピン 26。PU-7 と初期 PU-8 は SUBQ がピン 39、SQCK がピン 41。切断前に consolemods の基板図で確認。

BIOS パッチのパッド（ATtiny84、日本フット機と PAL PSone）: MCU 側ピンは上表で固定、[`src/bios.c`](src/bios.c) と [`src/port.S`](src/port.S) が駆動。AX、AY、DX のコンソール側パッドは基板と BIOS リビジョンごとに異なり、本プロジェクトに検証済みのパッド対応はありません。お使いのモデルについて PsNee のメモと基板図で特定し、起動するまで未確認として扱ってください。

## ビルドと書き込み

あらゆるビルド・検査・テストはピン留め Docker ツールチェーンで `make` を通して走ります。ホストで動くのは `avrdude` だけです。

| ツール | 用途 |
|:-------|:-----|
| Docker | ピン留めツールチェーンを実行 |
| Git | リポジトリのクローン |
| avrdude | イメージをチップへ書き込む |

```bash
git clone https://github.com/gufranco/openscex-modchip.git
cd openscex-modchip
make REGION=us                                        # ATtiny85、アメリカ、内蔵クロック
make MCU=attiny84 REGION=eu BIOS=scph_102             # PAL PSone、BIOS パッチ付き
avrdude -c <programmer> -p attiny85 -U flash:w:openscex-modchip-attiny85.hex:i
```

Arduino as ISP を含め、どの ISP でも使えます。ファームウェアは起動時にクロックプリスケーラを 1 分周へ戻すので、工場出荷の CKDIV8 ヒューズはタイミングを変えません。

| ビルド | ヒューズ（low / high / extended） |
|:-------|:----------------------------------|
| 内蔵 8 MHz RC | `0xE2` / `0xDF` / `0xFF` |
| 外部クロック | 測定周波数に依存、チップのデータシートから読む |

外部クロックビルドは PU-18 と PSone で実機 Verified（2026-10-05）、他の基板では前提ゲートです。測定したメカコン周波数でビルド、`make CLOCK=external EXT_F_CPU=<hz>UL`、成果物名に `-extclk`。候補周波数は 4.2336 MHz（16.9344 MHz の 4 分周）、断片から Concluded でここでは未測定。クロックピンの電圧を測定する前に外部クロックヒューズを書き込まないこと。

検証:

```bash
make test        # ホストテスト、simavr コンソールモデル、静的解析、MISRA
make repro       # 2 回のフレッシュビルドがバイト単位で同一
make mutate      # ロジック層のミューテーションテスト
```

同じゲートが push と pull request のたびに CI でも走ります。定義は [`.github/workflows/ci.yml`](.github/workflows/ci.yml)。

## 診断

チップは注入を終えてアイドルになった後、4 バイトのフライトレコーダを EEPROM に書き込みます。これにより施工を推測ではなく診断できます。書き込みは起動時やリージョンチェックの窓の中では決して起こらないので注入タイミングに影響せず、1 電源サイクルにつき 1 回なので EEPROM 寿命も問題になりません。プログラマで読み出します:

```bash
avrdude -c <programmer> -p attiny85 -U eeprom:r:diag.bin:r
```

| バイト | 意味 |
|:-------|:-----|
| 0 | マジック `0x50`、それ以外はまだ記録なし |
| 1 | 検出した基板: `0` 旧来ゲート、`1` WFCK キャリア |
| 2 | アイドルに達したセッション数、255 で折り返す |
| 3 | 直近セッションで送出したリージョン文字列数 |

## 安全

- 配線前にすべてのタップ点で論理電圧を測定すること。各パッド電圧を示す資料はないため Unknown。フット機はおよそ 5 V、PSone PM-41(2) はより低くノイズに敏感と想定するが、想定は測定ではない。
- 測定前にコンソール信号をクロックピンへ入力しないこと。
- コンソールを開けて CD サブシステムにはんだ付けすると破壊の恐れがある。自己責任で組むこと。

## ライセンス

ファームウェアとドキュメントは [MIT](LICENSE)。
