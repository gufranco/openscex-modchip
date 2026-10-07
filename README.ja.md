[English](README.md) | 日本語 | [中文](README.zh.md)

<div align="center">

<h1>openscex-modchip</h1>

<strong>PlayStation と PSone のためのステルス SCEx リージョン解除。コンソール自身のクロックで動く ATtiny84 に載ります。</strong>

<br><br>

[![CI](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml/badge.svg)](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/gufranco/openscex-modchip)](https://github.com/gufranco/openscex-modchip/releases)
[![MISRA C:2012](https://img.shields.io/badge/MISRA%20C%3A2012-0%20deviations-brightgreen)](AGENTS.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

</div>

<p align="center">
  <a href="#クイックスタート">クイックスタート</a> &nbsp;|&nbsp;
  <a href="#コンソールのタップ位置">配線</a> &nbsp;|&nbsp;
  <a href="#ステータス-led">ステータス LED</a> &nbsp;|&nbsp;
  <a href="../../issues/new?template=compatibility.yml">あなたの機種を報告</a>
</p>

フラッシュ **2652** バイト · 基板 **6** 系統、PU-18 から PM-41(2) · リージョン **3** 種 · MISRA 逸脱 **0** · ホストの行と分岐カバレッジ **100%** · ミュータント **88/88** 撃破

```bash
gh release download --repo gufranco/openscex-modchip --pattern 'openscex-modchip-attiny84.hex' --pattern SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing
avrdude -c <programmer> -p attiny84 -U flash:w:openscex-modchip-attiny84.hex:i
```

> [!IMPORTANT]
> ATtiny84 への再設計（v0.3.0）はシミュレーションのゲートをすべて通過していますが、まだ実機では動かしていません。配線前にクロックと蓋の点を測ってください。

初代ソニー PlayStation（フット機）と PSone 向けのリージョン解除ファームウェアで、コンソール自身のクロックで動く ATtiny84 に載ります。設定されたリージョン文字列を SUBQ リージョンチェックの窓の間だけ送り、コンソールが受け入れた瞬間に止め、プレイ中はデータ線をハイインピーダンスに保ちます。蓋スイッチへの配線で、ディスク交換を正確に知ります。ブート ROM はパッチしないため、日本のフット機と PAL の PSone は 2 回目のリージョンチェックを残します。それを回避したい場合はパッチ済み BIOS を入れてください。光学ドライブエミュレータではなく、PS2 やサターンには対応せず、LibCrypt も回避しません。

| | |
|:--|:--|
| **受け入れ後は沈黙**<br>最初のプログラム領域フレームで注入を止め（文字列の途中でも）、蓋が開くまで沈黙します。 | **正確なディスク交換**<br>蓋線は開いてから 4 ms 以内に文字列を止め、閉じると再武装します。マルチディスクのゲームの全ディスクに対応。 |
| **コンソール同期のタイミング**<br>チップはコンソールの 4.2336 MHz クロックで動き、自前の発振器は使いません。 | **1 リージョン、1 つの窓**<br>設定したリージョン文字列だけを、SUBQ リージョンチェックの窓の間だけ、武装ごとに最大 16 回。 |
| **ステータス LED コード**<br>LED 1 個で起動の各段階、ディスクごとの結果、配線の異常を点滅回数で示し、何も保存しない。 | **コードで実証**<br>MISRA C:2012 準拠、ホストカバレッジ 100%、simavr コンソールモデル、ミューテーションテスト、バイト一致の再ビルド。 |

## 仕組み

```mermaid
graph LR
    subgraph Console
        CD[CD サブシステム]
        CLK[4.2336 MHz クロック]
        LID[蓋スイッチ]
        WF[WFCK]
        MECH[メカコン]
    end
    subgraph ATtiny84
        CAP[SUBQ 取得]
        DET[リージョンチェック検出]
        ST[ステルス状態機械]
        INJ[SCEx 注入]
        LED[ステータス LED]
    end
    CD -->|SQCK, SUBQ| CAP
    CAP --> DET --> ST --> INJ
    ST --> LED
    LID -->|蓋線| ST
    WF -->|ゲートまたはキャリア| INJ
    CLK -->|CLKI| ATtiny84
    INJ -->|DATA| MECH
```

## 他のチップとの比較

| 能力 | openscex | PsNee V9 | Mayumi V4 | MM3 |
|:-----|:---------|:---------|:----------|:----|
| ステルスの契機 | SUBQ チェック窓と受け入れラッチ | SUBQ デコード | センス線と蓋線 | Mayumi V4 と同じプログラム |
| ディスク交換の検出 | 蓋線 | SUBQ カウンタの減衰 | 蓋線 | 蓋線 |
| クロック | コンソール | 内蔵 | コンソール | 内蔵 RC |
| ブート ROM の BIOS パッチ | なし、パッチ済み BIOS を使う | あり、ATmega 版 | なし | なし |
| 基板 | PU-18 から PM-41(2) | PU-7 から PM-41(2) | PU-18 以降 | PU-7 以降 |
| 診断 | LED の段階と結果コード | シリアルデバッグ | なし | なし |
| テストと静的解析 | ホスト、simavr、ミューテーション、MISRA | なし | なし | なし |
| 実績 | 2 基板、以前のファームウェア | 数年 | 数十年 | 数十年 |

## 概要

| 項目 | 値 |
|:-----|:---|
| 対象機種 | PlayStation フット機 PU-18 から PU-23、PSone PM-41 と PM-41(2) |
| MCU | ATtiny84（14 ピン DIP） |
| 方式 | SCEx 注入 |
| リージョン | ビルドごとに 1 つ、`REGION=jp|us|eu`（既定 us） |
| クロック | コンソールの 4.2336 MHz クロックを CLKI に入力、内蔵発振器は使わない |
| 配線 | 信号 6 本（SQCK、SUBQ、DATA、WFCK、クロック、蓋）と電源、LED は任意 |
| ツールチェーン | C17、MISRA C:2012 逸脱ゼロ、バージョン固定の Docker イメージ |

## 実機で検証済み

実機でリージョン外のディスクが起動した組み合わせ（SCEx リージョン解除）、2026-10-05 時点のファームウェア。

| 基板 | 機種 | クロック | 注入 | 状態 |
|:-----|:-----|:---------|:-----|:-----|
| PU-18 | フット機、NTSC-U/C | コンソール | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | コンソール | SCEx | Verified 2026-10-05 |

これらの結果は ATtiny84 専用への再設計より前のものです。必須の蓋線、唯一のクロックとしてのコンソールクロック、受け入れラッチ、上限付きの SUBQ 待ち、2026-10-06 の全変更を含みません。再設計はシミュレーションのゲートをすべて通過していますが、まだ実機では動かしていません。2026-10-05 のコンソールクロック版を載せたチップ、各個体の正確な SCPH、ビルド時の周波数は記録されていません。実機未確認: PU-20、PU-22、PU-23、PM-41(2)、どの基板でも PAL と NTSC-J。

機種ごとの検証はコミュニティで進めます。あなたの機種で試しましたか？ [互換性レポート](../../issues/new?template=compatibility.yml)を開いてください。確認された取り付けでこの表が育ちます。

## 含まれるもの

| 機能 | 内容 |
|:-----|:-----|
| SCEx 注入 | 44 ビット LSB ファーストのリージョン文字列。PU-18 と PU-20 の静的ゲート方式と、PU-22 以降の WFCK キャリア方式 |
| 基板の自動判別 | 起動時の WFCK の振る舞いでゲートかキャリアかを選ぶ。1 つのビルドが全基板に合う |
| コンソールクロック | チップはコンソール自身の 4.2336 MHz クロックで動くので、すべての遅延がコンソールの水晶に同期する。Mayumi V4 と同じ |
| 蓋線 | 蓋スイッチへの必須配線。蓋が開くと 4 ms のビットセル 1 つ以内に文字列を止め、閉じると次のディスクに向けて再武装する。マルチディスクのゲームの交換はすべて推測ではなく検出される |
| ステルス | SUBQ リージョンチェックの窓の間だけ、武装ごとに上限付きで注入し、その後 DATA をハイ Z、LED をオフ。コンソールがプログラム領域を読んだ瞬間に止め、蓋が開くまでは以後のリードイン読み取りでも沈黙 |
| 単一リージョン | `REGION` だけを送り、3 つ全部は送らない |
| 適応タイミング | WFCK キャリアの基板では注入ビットを WFCK の周期を数えて計時する。`TIMING=fixed` はコンソールクロックで計時した遅延を使う |
| 自己回復 | 各 SUBQ 取得はフレーム間の隙間で再同期し、30 ms で諦める。注入中に WFCK キャリアが止まるとウォッチドッグが DATA を解放する。蓋線が外れていると開いたと読むので、チップは盲目的に注入せず沈黙する |
| ステータス LED | 専用ピンの任意の LED が起動の段階、ディスクごとの結果、現在の異常を点滅回数で示す。ファームウェアは LED を待たず、LED が無くても正しく動く |
| 現場診断 | プログラマ不要。LED のコードが、蓋の配線が無い場合からリージョンチェックが来ない SUBQ まで、失敗した段階を示す。ファームウェアは EEPROM に書かない |
| 閉ループ確認 | 注入後、SUBQ でプログラム領域のフレーム（実在のトラック番号）を待つ。メカコンはリージョン文字列を受け入れた後でしかそれを許さないので、リージョンチェックが通ったかを示す |
| 検証 | ホストテストの行と分岐カバレッジ 100%、コンソールクロックでの simavr コンソールモデル、ミューテーションテスト、再現可能ビルド |

## 確度タグ

| タグ | 意味 |
|:-----|:-----|
| Read | 一次資料から取得（PsNee のソース、Mayumi V4 のバイナリ、quade.co、consolemods、psdevwiki、ATtiny24A/44A/84A のデータシート） |
| Concluded | 資料から推論したもので、どれか 1 つが述べているわけではない |
| Verified | 本プロジェクトが実機または simavr モデルで確認 |
| Unknown | どの資料にもない。実機で測ること |

| 事実 | タグ |
|:-----|:-----|
| SCEx のピン順 | Read（PsNee `MCU.h`）、実績ありとオーナー確認済み |
| クロックと蓋のタップ位置 | Read、quade.co の Mayumi V4 の 2 番と 7 番。PM-41(2) のページは "Clock: Pin 2" と "CD Door: Pin 7" と明記 |
| 蓋の極性、開いている間 high | Read、Mayumi V4 のバイナリはドア入力が high の間待つ |
| コンソールクロック 4.2336 MHz | Concluded、16.9344 MHz の 4 分の 1。Mayumi V4 の遅延ループと整合（MM3 が 4 MHz RC で 170 回のところ 182 回） |
| SQCK と SUBQ のタップ位置 | Read、対応する全基板の PsNee の写真に表示。その裏のメカコンのピン番号は Unknown のまま |
| パッドごとの電圧 | PsNee と Mayumi の取り付けから Concluded（フット機は約 5 V、PSone は低めでノイズに敏感）。測って確認 |
| PU-18 と PSone での SCEx 解除 | 以前のファームウェアで Verified 2026-10-05（上の表を参照） |

## 機種ごとのビルド

| 機種 | 基板 | `REGION` | 2 回目のリージョンチェック |
|:-----|:-----|:---------|:---------------------------|
| フット US/カナダ、SCPH-550x1/700x1/900x1 | PU-18 から PU-23 | `us` | なし |
| フット PAL、SCPH-550x2/900x2 | PU-18 から PU-22 | `eu` | なし |
| PSone US/カナダ、SCPH-101 | PM-41 / PM-41(2) | `us` | なし |
| PSone PAL、SCPH-102 | PM-41 / PM-41(2) | `eu` | ブート ROM 内。パッチ済み BIOS が必要 |
| PSone 日本、SCPH-100 | PM-41 | `jp` | ブート ROM 内。パッチ済み BIOS が必要 |
| フット日本、SCPH-5000/5500/7000/7500/9000 | PU-18 から PU-23 | `jp` | ブート ROM 内。パッチ済み BIOS が必要 |
| アジア、SCPH-xxx3 | 未記録 | `jp` | なし |
| アジア ビデオ CD、SCPH-5903 | 未記録 | `jp` + `VCD_FILTER=on` | なし |

PU-18 より古い基板（PU-7 と PU-8、SCPH-1000 から SCPH-500x）は対象外です。ブート ROM のチェックはこのチップでは扱いません。上で示した機種では、パッチ済み BIOS を入れるまで輸入ソフトが拒否されることがあります。アジア向けモデルはパッチ不要です。PsNee V9.0 は SCPH-xxx3 と SCPH-5903 を NTSC-J の文字列だけで対象にしています。SCPH-5903 はビデオ CD も再生するため、そのビルドには `VCD_FILTER=on` を加え、注入をゲームのリードインでのみ起動し、ビデオ CD では起動しません。アジア向けのどちらの行もここではまだ実機で確認していません。開発機（DTL-H120x、PU-9）は焼いたディスクをそのまま読むためチップ不要です。

## 設定

ひとつのソースから全バリアントをビルドします。ノブは `make` に渡します。

| ノブ | 値 | 既定 | 選ぶもの |
|:-----|:---|:-----|:---------|
| `REGION` | `jp`、`us`、`eu` | `us` | チップが送る唯一のリージョン文字列 |
| `TIMING` | `adaptive`、`fixed` | `adaptive` | adaptive は WFCK キャリアの注入ビットを WFCK の周期を数えて計時する。fixed はコンパイル時の遅延を使い、これもコンソールクロックに同期する |
| `VCD_FILTER` | `off`、`on` | `off` | SCPH-5903 専用で on。注入はゲームのリードイン TOC でのみ起動し、ビデオ CD では起動しない。PsNee V9.0 の SCPH-5903 フィルタに準拠 |

既定以外のリージョンやフィルタは成果物名にタグを付けます。例 `openscex-modchip-attiny84-jp.hex` や `openscex-modchip-attiny84-jp-vcd.hex`。

## MCU ピン配置

SCEx の信号は PsNee の実証済みの順序を保ちます（PsNee `MCU.h` から Read）。クロックと蓋は PORTB にあります。物理ピン番号は標準 PDIP 配置です。SOIC や QFN はデータシートで確認してください。

| DIP ピン | ポート | 信号 | 方向 | 接続先 |
|:--------:|:-------|:-----|:-----|:-------|
| 1 | VCC | VCC | - | コンソール電源、先に測る |
| 2 | PB0 | CLKI | 入力 | コンソールクロック、Mayumi V4 の 2 番。この線を最も短くする |
| 3 | PB1 | LID | 入力、プルアップ | 蓋スイッチ、Mayumi V4 の 7 番 |
| 4 | PB3 | RESET | - | リセットのまま |
| 5 | PB2 | - | - | 未使用 |
| 6 | PA7 | - | - | 未使用 |
| 7 | PA6 | - | - | 未使用 |
| 8 | PA5 | - | - | 未使用 |
| 9 | PA4 | LED | 出力、任意 | 1 kΩ の抵抗経由の状態 LED、または付けない |
| 10 | PA3 | WFCK | 入出力 | 静的ゲート、または PU-22 以降のライブキャリア |
| 11 | PA2 | DATA | 出力、Low 駆動またはハイ Z | メカコンへの SCEx 注入 |
| 12 | PA1 | SUBQ | 入力 | SUBQ シリアルデータ |
| 13 | PA0 | SQCK | 入力 | SUBQ シリアルクロック |
| 14 | GND | GND | - | コンソールのグランド |

## コンソールのタップ位置

DATA は SCEx ビット列を運び、WFCK はゲートまたはキャリアです。これらは PsNee と Mayumi のタップ位置です。クロックと蓋の線は、Mayumi V4 チップが 2 番と 7 番のピンを付ける場所に付けます。基板ごとの位置は quade.co の図にあります: [PU-18](https://quade.co/ps1-modchip-guide/mayumi-v4/pu-18/)、[PU-20](https://quade.co/ps1-modchip-guide/mayumi-v4/pu-20/)、[PU-22](https://quade.co/ps1-modchip-guide/mayumi-v4/pu-22/)、[PU-23](https://quade.co/ps1-modchip-guide/mayumi-v4/pu-23/)、[PM-41](https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41/)、[PM-41(2)](https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41-2/)。

これらの図では 2 番がコンソールクロック、7 番が蓋線で、このチップが図から使うのはこの 2 点だけです。1 番と 8 番は電源とグランド、5 番と 6 番はこのチップも使う WFCK と DATA の点、3 番と 4 番は Mayumi 独自のステルスとリセットの配線で、このチップは使いません。SQCK と SUBQ はこれらの図にありません。さらに下の PsNee の写真に示されています。

<table>
<tr><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-18/"><img src="https://quade.co/wp-content/uploads/2018/02/PU18L.jpg" alt="William Quade による PU-18 の Mayumi V4 取り付け図" width="240"></a><br><sub><b>PU-18</b>。図: William Quade、<a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-18/">quade.co</a></sub></td><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-20/"><img src="https://quade.co/wp-content/uploads/2018/02/PU20L.jpg" alt="William Quade による PU-20 の Mayumi V4 取り付け図" width="240"></a><br><sub><b>PU-20</b>。図: William Quade、<a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-20/">quade.co</a></sub></td><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-22/"><img src="https://quade.co/wp-content/uploads/2018/02/PU22L.jpg" alt="William Quade による PU-22 の Mayumi V4 取り付け図" width="240"></a><br><sub><b>PU-22</b>。図: William Quade、<a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-22/">quade.co</a></sub></td></tr>
<tr><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-23/"><img src="https://quade.co/wp-content/uploads/2018/01/pu23l.jpg" alt="William Quade による PU-23 の Mayumi V4 取り付け図" width="240"></a><br><sub><b>PU-23</b>。図: William Quade、<a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-23/">quade.co</a></sub></td><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41/"><img src="https://quade.co/wp-content/uploads/2018/11/pm-41-mayumiv4.jpg" alt="William Quade による PM-41 の Mayumi V4 取り付け図" width="240"></a><br><sub><b>PM-41</b>。図: William Quade、<a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41/">quade.co</a></sub></td><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41-2/"><img src="https://quade.co/wp-content/uploads/2020/05/pm-412-m4.jpg" alt="William Quade による PM-41(2) の Mayumi V4 取り付け図" width="240"></a><br><sub><b>PM-41(2)</b>。図: William Quade、<a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41-2/">quade.co</a></sub></td></tr>
</table>

6 枚の図は William Quade のもので、クレジットを付けて quade.co から表示しています。このリポジトリにも MIT ライセンスにも含まれません。図をクリックすると元のページとコメントが開きます。

残りの点はすべて下の写真に名前付きで示されています。各基板で SQCK、SUBQ、DATA、WFCK、VCC、GND が表示されています。クロックと蓋は上の図の 2 番と 7 番の位置に、それ以外はこれらの写真が示す位置にはんだ付けしてください。PU-18 の写真は基板の裏面です。AX、DX、RESET も表示されていますが PsNee のブート ROM パッチ用で、このチップにはないので接続しないでください。

<table>
<tr><td align="center" width="33%"><a href="assets/psnee/pu-18.jpg"><img src="assets/psnee/pu-18.jpg" alt="PsNee による、SQCK、SUBQ、DATA、WFCK、VCC、GND の点を示した PU-18 基板" width="240"></a><br><sub><b>PU-18</b>。写真: PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pu-20.jpg"><img src="assets/psnee/pu-20.jpg" alt="PsNee による、SQCK、SUBQ、DATA、WFCK、VCC、GND の点を示した PU-20 基板" width="240"></a><br><sub><b>PU-20</b>。写真: PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pu-22.jpg"><img src="assets/psnee/pu-22.jpg" alt="PsNee による、SQCK、SUBQ、DATA、WFCK、VCC、GND の点を示した PU-22 基板" width="240"></a><br><sub><b>PU-22</b>。写真: PsNee</sub></td></tr>
<tr><td align="center" width="33%"><a href="assets/psnee/pu-23.jpg"><img src="assets/psnee/pu-23.jpg" alt="PsNee による、SQCK、SUBQ、DATA、WFCK、VCC、GND の点を示した PU-23 基板" width="240"></a><br><sub><b>PU-23</b>。写真: PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pm-41.jpg"><img src="assets/psnee/pm-41.jpg" alt="PsNee による、SQCK、SUBQ、DATA、WFCK、VCC、GND の点を示した PM-41 基板" width="240"></a><br><sub><b>PM-41</b>。写真: PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pm-41-2.jpg"><img src="assets/psnee/pm-41-2.jpg" alt="PsNee による、SQCK、SUBQ、DATA、WFCK、VCC、GND の点を示した PM-41(2) 基板" width="240"></a><br><sub><b>PM-41(2)</b>。写真: PsNee</sub></td></tr>
</table>

この 6 枚の写真は kalymos とコントリビュータによる [PsNee](https://github.com/kalymos/PsNee) V9.0 のもので、[Unlicense](LICENSES/Unlicense.txt) でパブリックドメインに置かれており、縮小してここに複製しています。これと上の図で、チップのすべての配線に取り付け位置の画像があります。

| 基板 | SCPH 世代 | DATA 注入点 | WFCK の役割 | 確度 |
|:-----|:----------|:------------|:------------|:-----|
| PU-18、PU-20 | 550x-750x | ウォブル ASIC からメカコンへのデジタル NRZ 出力 | 静的ゲート | Read |
| PU-22、PU-23 | 7500-900x | CD プロセッサのトラッキング線、WFCK を偽キャリアに、3 線プラスリンク | ライブクロック、同期必須 | Read |
| PM-41、PM-41(2) | PSone 100-103 | 同じトラッキング線キャリア方式。PM-41(2) では待機中にチップの I/O を浮かせる | ライブクロック | Read |

クロック線はコンソールの 4.2336 MHz クロックをチップに運ぶので、長さが重要です。quade.co は Mayumi V4 の不具合をこの線が拾うノイズに帰しており、最も短くするよう勧めています。チップはクロック点の近くに置いてください。データシートは、1 サイクルごとに 2% を超えて変動するクロックはチップの予測不能な動作を招くと警告しています。

上の写真は対応する全基板で SQCK と SUBQ を示しているので、それに従ってください。フォーラムの伝聞による PU-22 以降のメカコンのピン番号、SUBQ が 24 番、SQCK が 26 番は、psxdev.net が 2025 年 10 月から停止中のため Unknown のままです。

## クイックスタート

機種別のビルド済み `.hex` は各[リリース](../../releases)に添付されるので、ツールチェーンを飛ばして下の `avrdude` 手順で書き込めます。各リリースには ATtiny84 のイメージ 3 種（`us`、`eu`、`jp`）、SCPH-5903 用イメージ（`jp-vcd`）、`SHA256SUMS` ファイル、ライセンス、ビルド来歴のアテステーションが付きます。v0.2.0 までのリリースには以前の 4 線設計の ATtiny85 イメージが付いていました。書き込む前にダウンロードを確認してください:

```bash
sha256sum -c SHA256SUMS --ignore-missing
gh attestation verify openscex-modchip-attiny84.hex --repo gufranco/openscex-modchip
```

リリースはコミット履歴から自動でバージョンが付き、CI が通ったコミットからのみ作られます。`0.x` 系はファームウェアが実機検証前であることを示します。ソースからビルドする場合: ビルド、チェック、テストはすべてバージョン固定の Docker ツールチェーン内で `make` を通して実行し、ホストで動くのは `avrdude` だけです。

| ツール | 用途 |
|:-------|:-----|
| Docker | 固定ツールチェーンを動かす |
| Git | リポジトリを取得する |
| avrdude | イメージをチップに書き込む |

```bash
git clone https://github.com/gufranco/openscex-modchip.git
cd openscex-modchip
make REGION=us                                        # アメリカ
make REGION=jp VCD_FILTER=on                          # SCPH-5903
avrdude -c <programmer> -p attiny84 -U flash:w:openscex-modchip-attiny84.hex:i
avrdude -c <programmer> -p attiny84 -U lfuse:w:0xE0:m -U hfuse:w:0xDF:m -U efuse:w:0xFF:m
```

ISP は何でも使えます。Arduino as ISP も可。フラッシュを先に、ヒューズを最後に書きます。low ヒューズ `0xE0` は CLKI の外部クロック、緩やかな電源立ち上がり向けの起動遅延、クロック分周なしを選びます（Read: ATtiny24A/44A/84A データシート DS40002269A、Table 19-5 と Table 6-3、CKSEL 0000、SUT 10）。以後チップは自前のクロックを持たないので、読み出しや書き直しはコンソールの電源を入れた状態で載せたまま行うか、プログラマから 2 番ピンにクロックを入れてください。ファームウェアは起動時にクロックプリスケーラもクリアするので、CKDIV8 ヒューズで遅くなることはありません。

| ビルド | ヒューズ（low / high / extended） |
|:-------|:----------------------------------|
| コンソールクロック | `0xE0` / `0xDF` / `0xFF` |
| コンソールクロック、2.7 V のブラウンアウト検出付き | `0xE0` / `0xDD` / `0xFF` |

ブラウンアウト検出は電源が 2.7 V を下回る間チップをリセットに保つので、電源を切るときに崩れていく電源でチップが動くことはありません。任意の設定で、実機ではまだ確かめていません。ATtiny84 は速度グレード上 1.8 V 以上で 4.2336 MHz で動くため（Read: 同データシート、1.8 V で 0 から 4 MHz、2.7 V で 0 から 10 MHz）、PSone の低めの電源も範囲内です。

検証:

```bash
make test        # ホストテスト、simavr コンソールモデル、静的解析、MISRA
make repro       # 2 回の新規ビルドがバイト単位で一致
make mutate      # ロジック層のミューテーションテスト
```

同じゲートが push とプルリクエストごとに CI で走ります。定義は [`.github/workflows/ci.yml`](.github/workflows/ci.yml)。コントリビュータは `make hooks` を一度実行すると、コミットメッセージと整形のチェックをローカルで有効にできます。

## ステータス LED

LED は任意で、チップ唯一の診断手段です。いまどの段階にいるか、各ディスクがリージョンチェックを通ったか、問題があればどの配線を見るべきかを示します。どの機能も遅らせず妨げず、何も保存しないので、コードは常に現在の状態です。

| 部品 | 選び方 |
|:-----|:-------|
| LED | 3 mm か 5 mm の赤、橙、黄、緑の LED、順方向電圧約 2 V。青や白は順方向電圧が 3 V あり、PSone の低めの電源では抵抗にほとんど電圧が残らないので不可 |
| 抵抗 | 1 kΩ、ワット数は問わない。5 V で約 3 mA、3.5 V で約 1.5 mA。室内では十分明るく、ピンの絶対最大定格 40 mA を大きく下回る（Read: ATtiny24A/44A/84A データシート DS40002269A） |
| 配線 | 9 番ピン（PA4）から抵抗、抵抗から LED のアノード（長い足）、カソード（平らな側）を GND へ |

| 段階 | LED の動き |
|:-----|:-----------|
| 基板判別 | 電源投入後約 0.4 s 点灯 |
| 基板確定 | 静的ゲート基板（PU-18、PU-20）なら 300 ms の点滅 1 回、WFCK キャリア基板（PU-22 以降）なら 2 回 |
| ディスク待ち | 2 s ごとに 40 ms の短い点灯 |
| 注入中 | リージョン文字列 1 本ごとに 90 から 181 ms の点灯 |
| 結果 | コードを 3 回示し、その後プレイ中は消灯 |

コードは 700 ms の長い点灯を 300 ms 間隔で数え、2 s 休んでから繰り返します。

| コード | 意味 | 確認先 |
|:------:|:-----|:-------|
| 1 | コンソールがリージョン文字列を受け入れた | 何もしない、ディスクは動く |
| 2 | 文字列を送ったがコンソールがプログラム領域に達しなかった | DATA と WFCK の配線、ビルドのリージョンがディスクと合っているか |
| 3 | 蓋が開いている、または蓋の配線が無い。続く間は繰り返す | 蓋の配線とその接続点 |
| 4 | 蓋が閉じたまま 5 s SUBQ フレームが無い。続く間は繰り返す | クロック配線、SQCK、電源と GND |
| 5 | フレームは来るが 20 s リージョンチェックが無い。続く間は繰り返す | SUBQ。ディスク無しや音楽 CD でも正常に出る |
| 6 | ウォッチドッグがチップをリセットした。次の起動で 1 回示す | 注入中に止まった WFCK |

## 安全

- 配線前に、クロックと蓋の点も含めすべてのタップ位置の論理電圧を測ってください。値は確立した PsNee と Mayumi の取り付けから取っています（フット機は約 5 V、PSone PM-41(2) は低めでノイズに敏感）が、想定は測定ではありません。
- クロックピンには、電圧と周波数を測る前にコンソールの信号を入れないでください。
- コンソールを開けて CD サブシステムにはんだ付けすると壊すことがあります。自己責任で作ってください。

## バージョニング

リリースは `0.x` 系の[セマンティックバージョニング](https://semver.org/)に従います。マイナーリリースでも互換性が壊れることがあり、v0.3.0 は ATtiny85 の 4 線設計を ATtiny84 に置き換えました。各リリースはタグ付けされ、CI が通ったコミットからビルドされます。注記は[リリース](../../releases)を参照してください。

## サポート

| 用件 | 窓口 |
|:-----|:-----|
| バグ報告 | [バグ用テンプレート](../../issues/new?template=bug.yml) |
| あなたの機種での結果 | [互換性レポート](../../issues/new?template=compatibility.yml) |
| セキュリティ報告 | [セキュリティポリシー](SECURITY.md)、非公開で報告 |
| 貢献 | [コントリビューションガイド](CONTRIBUTING.md) |

## ライセンス

ファームウェアとドキュメントは [MIT](LICENSE)。
