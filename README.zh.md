# openscex-modchip

[English](README.md) | [日本語](README.ja.md) | 中文

[![CI](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml/badge.svg)](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml)
[![MISRA C:2012](https://img.shields.io/badge/MISRA%20C%3A2012-0%20deviations-brightgreen)](AGENTS.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

面向初代索尼 PlayStation（厚机）与 PSone 的区域解锁固件。同一套源码可为 ATtiny85（SCEx 注入）与 ATtiny84（SCEx 加引导 ROM BIOS 补丁）编译。它在 SUBQ 区域检查窗口内送出配置好的区域字符串，游戏运行时让数据线保持高阻。它不是光驱模拟器，不支持 PS2 或 Saturn，也不破解 LibCrypt。

## 概览

| 项目 | 值 |
|:-----|:---|
| 目标主机 | PlayStation 厚机 PU-7 至 PU-23，PSone PM-41 与 PM-41(2) |
| MCU | ATtiny85（8 脚 DIP）、ATtiny84（14 脚 DIP） |
| 方式 | 两种芯片均做 SCEx 注入；ATtiny84 还 patch 引导 ROM |
| 区域 | 每次构建一个，`REGION=jp|us|eu`（默认 us） |
| 时钟 | 内部 8 MHz RC，或主机外部（以硬件为前提） |
| 信号线 | 4 根（SQCK、SUBQ、DATA、WFCK）加电源，可选 LED |
| 工具链 | C17、MISRA C:2012 零偏差、固定 Docker 镜像 |

## 实机已验证

在真实主机上跨区启动的组合（SCEx 区域解锁）。其余组合可编译并通过容器内关卡集，但未在硬件上确认。

| 主板系列 | 主机 | 时钟 | 注入 | 状态 |
|:---------|:-----|:-----|:-----|:-----|
| PU-8 | 厚机，NTSC-U/C | 内部 | SCEx | Verified 2026-10-05 |
| PU-18 | 厚机，NTSC-U/C | 内部 | SCEx | Verified 2026-10-05 |
| PU-18 | 厚机，NTSC-U/C | 外部 | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | 内部 | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | 外部 | SCEx | Verified 2026-10-05 |

硬件上未确认（仅编译与仿真）: PU-7、PU-20、PU-22、PU-23、引导 ROM BIOS 补丁的所有型号，以及任何系列的 PAL 或 NTSC-J。PU-8 与 PU-18 机的确切 SCPH、PSone 机的区域与 SCPH，以及外部时钟构建用的是哪种芯片，均未记录。

上述硬件结果是在 `TIMING=fixed` 下取得的。旧式（PU-8、PU-18）各行不受 `TIMING` 影响，因为两种构建都用 MCU 延时。PM-41 载波各行用的是 fixed 时序；adaptive 默认的载波位（锁定 WFCK）在主机重新测试前仅经仿真。可用 `make TIMING=fixed` 重建已验证的二进制。

各主机的验证由社区驱动。在你的主机上试过后，请开一个 [兼容性报告](../../issues/new?template=compatibility.yml)，此表将从确认的安装中成长。

## 功能

| 功能 | 细节 |
|:-----|:-----|
| SCEx 注入 | 44 位 LSB first 区域字符串，旧式静态门控法与 PU-22 及以后的 WFCK 载波法 |
| 主板自动检测 | 启动时按 WFCK 行为选择旧式或载波模式，一套构建适配所有系列 |
| 自适应时序 | 在 WFCK 载波主板上，注入位以计数 WFCK 周期来计时，锁定到主机时钟，不受 MCU 振荡器漂移影响；`TIMING=fixed` 退回 MCU 延时 |
| 单一区域 | 只送出 `REGION` 一个，不送三个 |
| 隐身 | 只在 SUBQ 区域检查窗口内、每次武装有上限地注入，之后 DATA 高阻、LED 关闭 |
| 换盘重新武装 | 离开窗口后一次性动作重新武装 |
| 引导 ROM BIOS 补丁 | 仅 ATtiny84，日本厚机与 PAL 的 PSone，包含两段式 SCPH-1000/3000 的所有型号 |
| 可选 LED | 状态输出，无 LED 时固件也正确 |
| 实地诊断 | 芯片空闲后向 EEPROM 写入 4 字节飞行记录（检测到的主板、会话数、注入次数），用 avrdude 读回；既有 PS1 改机芯片都不报告其所见 |
| 验证 | 主机测试行与分支覆盖率 100%，跨振荡器带的 simavr 模型，变异测试，可复现构建 |

## 置信度标签

| 标签 | 含义 |
|:-----|:-----|
| Read | 取自一手资料（PsNee 源码、consolemods、psdevwiki） |
| Concluded | 从资料推断，无一明确陈述 |
| Verified | 本项目在真实硬件或 simavr 模型中确认 |
| Unknown | 无资料给出，须在主机上测量 |

| 事实 | 标签 |
|:-----|:-----|
| MCU 引脚分配 | Read（PsNee `MCU.h`），所有者确认为现场验证 |
| 主机侧搭接点，信号层面 | Read，PsNee 与 Mayumi 安装现场验证 |
| mechacon 引脚号 | Unknown，论坛转述，对照板图再确认 |
| 各焊盘电压 | 从 PsNee 与 Mayumi 的安装 Concluded（厚机约 5 V，PSone 更低且对噪声敏感）；测量以确认 |
| PU-8、PU-18、PSone 的 SCEx 解锁 | Verified 2026-10-05（见上表） |
| PU-18 与 PSone 的外部时钟构建 | Verified 2026-10-05，其他主板为前提门控 |
| BIOS 补丁时序 | Read 自 PsNee 并在 simavr 演练，硬件上未 Verified |

## 各主机对应的构建

BIOS 型号依据主机实际的 BIOS 版本，这比 SCPH 编号更重要。

| 主机 | 主板 | `MCU` | `REGION` | `BIOS` | 接线 |
|:-----|:-----|:------|:---------|:-------|:-----|
| 厚机 US/加拿大，SCPH-100x1/550x1/700x1/900x1 | PU-7 至 PU-23 | `attiny85` | `us` | `none` | SCEx，4 根 |
| 厚机 PAL，SCPH-100x2/550x2/900x2 | PU-8 至 PU-22 | `attiny85` | `eu` | `none` | SCEx，4 根 |
| PSone US/加拿大，SCPH-101 | PM-41 / PM-41(2) | `attiny85` | `us` | `none` | SCEx，4 根 |
| PSone PAL，SCPH-102 | PM-41 / PM-41(2) | `attiny84` | `eu` | `scph_102` | SCEx + BIOS 补丁 |
| PSone 日本，SCPH-100 | PM-41 | `attiny84` | `jp` | `scph_100` | SCEx + BIOS 补丁 |
| 厚机日本，SCPH-5000/5500/3500 | PU-18 / PU-8 | `attiny84` | `jp` | `scph_3500_5500` | SCEx + BIOS 补丁 |
| 厚机日本，SCPH-7000/7500/9000 | PU-20 至 PU-23 | `attiny84` | `jp` | `scph_7000_9000` | SCEx + BIOS 补丁 |
| 厚机日本，SCPH-3000 | PU-8 | `attiny84` | `jp` | `scph_3000` | SCEx + 两段 BIOS 补丁 |
| 厚机日本，SCPH-1000 | PU-7 | `attiny84` | `jp` | `scph_1000` | SCEx + 两段 BIOS 补丁 |

BIOS 型号未覆盖: 亚洲型号（SCPH-xxx3，例 SCPH-5003/5903）带无第二道检查的英文 ROM，但 NTSC-J 的 CD 控制器无后门，仅 SCEx 不足；开发机（DTL-H120x、PU-9）原生即可读刻录盘，无需芯片。

## 配置

一套源码构建所有变体，开关传给 `make`。

| 开关 | 取值 | 默认 | 选择 |
|:-----|:-----|:-----|:-----|
| `MCU` | `attiny85`、`attiny84` | `attiny85` | 8 脚（仅 SCEx）或 14 脚（加 BIOS 补丁） |
| `REGION` | `jp`、`us`、`eu` | `us` | 芯片送出的那一个区域字符串 |
| `CLOCK` | `internal`、`external` | `internal` | 内部 8 MHz RC，或主机时钟（`CLOCK=external EXT_F_CPU=<hz>`），前提是先测量该时钟与引脚电压 |
| `TIMING` | `adaptive`、`fixed` | `adaptive` | adaptive 以计数 WFCK 周期来计时 WFCK 载波注入位，使其锁定到主机时钟；fixed 使用编译期 MCU 延时，即在硬件上跑过的那套时序 |
| `BIOS` | `none`、`scph_102`、`scph_100`、`scph_7000_9000`、`scph_3500_5500`、`scph_1000`、`scph_3000` | `none` | 仅 ATtiny84，针对该 BIOS 版本的引导 ROM 补丁 |

非默认区域会在产物名上加标签，例如 `openscex-modchip-attiny85-jp.hex`。

## MCU 引脚图

PsNee 经验证的 ATtiny85（`ATTINY_X5`）分配，Read 自 PsNee `MCU.h`。物理引脚号为标准 PDIP 配置，SOIC 或 QFN 请查数据手册。

### ATtiny85，8 脚 DIP（仅 SCEx）

| DIP 脚 | 端口 | 信号 | 方向 | 接到 |
|:------:|:-----|:-----|:-----|:-----|
| 1 | PB5 | RESET | - | 保持为复位 |
| 2 | PB3 | LED | 输出，可选 | 经电阻接 LED，或空置 |
| 3 | PB4 | WFCK | 输入输出 | 旧板门控，或 PU-22+ 活动载波 |
| 4 | GND | GND | - | 主机地 |
| 5 | PB0 | SQCK | 输入 | SUBQ 串行时钟 |
| 6 | PB1 | SUBQ | 输入 | SUBQ 串行数据 |
| 7 | PB2 | DATA | 输出，拉低或高阻 | 向 mechacon 注入 SCEx |
| 8 | VCC | VCC | - | 主机 5 V，先测量 |

### ATtiny84，14 脚 DIP（SCEx 加 BIOS 补丁）

SCEx 信号在 PORTA 上与 85 同序。AX、AY、DX 仅在日本厚机与 PAL 的 PSone 上接线。

| DIP 脚 | 端口 | 信号 | 方向 | 接到 |
|:------:|:-----|:-----|:-----|:-----|
| 1 | VCC | VCC | - | 主机 5 V，先测量 |
| 2 | PB0 | - | - | 未使用，启用外部时钟时为 XTAL1 |
| 3 | PB1 | - | - | 未使用，XTAL2 |
| 4 | PB3 | RESET | - | 保持为复位 |
| 5 | PB2 | AX | 输入 | BIOS 补丁，第一根地址线，计数脉冲 |
| 6 | PA7 | - | - | 未使用 |
| 7 | PA6 | AY | 输入 | BIOS 补丁，第二根地址线，仅两段式型号 |
| 8 | PA5 | DX | 输出，拉低或高阻 | BIOS 补丁，数据总线覆写 |
| 9 | PA4 | LED | 输出，可选 | 经电阻接 LED，或空置 |
| 10 | PA3 | WFCK | 输入输出 | 旧板门控，或 PU-22+ 活动载波 |
| 11 | PA2 | DATA | 输出，拉低或高阻 | 向 mechacon 注入 SCEx |
| 12 | PA1 | SUBQ | 输入 | SUBQ 串行数据 |
| 13 | PA0 | SQCK | 输入 | SUBQ 串行时钟 |
| 14 | GND | GND | - | 主机地 |

在仅需 SCEx 的主机上，ATtiny84 使用与 85 相同的四路信号，AX、AY、DX 保持空置。

## 主机侧搭接点

DATA 线承载 SCEx 比特流，WFCK 为门控或载波。这些是 PsNee 与 Mayumi 的搭接点。主板系列自动检测，故芯片相同，仅搭接点不同。

| 主板系列 | SCPH 世代 | DATA 注入点 | WFCK 角色 | 置信度 |
|:---------|:----------|:------------|:----------|:-------|
| PU-7、PU-8 | 1000-5003 | 解调后的摆动 NRZ 串行送入 mechacon，历史上称 "point 6" | 静态高 | Read，点来自论坛片段，请在板上确认 |
| PU-18、PU-20 | 550x-750x | 摆动 ASIC 的数字 NRZ 输出送入 mechacon | 静态门控 | Read |
| PU-22、PU-23 | 7500-900x | CD 处理器跟踪线，WFCK 作假载波，三线加链接 | 活动时钟，需要同步 | Read |
| PM-41、PM-41(2) | PSone 100-103 | 同样的跟踪线载波法，PM-41(2) 空闲时让芯片 I/O 浮空 | 活动时钟 | Read |

mechacon 的 SUBQ 与 SQCK 引脚为论坛转述，因 psxdev.net 自 2025 年 10 月起离线故为 Unknown: PU-22 及以后，SUBQ 在第 24 脚，SQCK 在第 26 脚；PU-7 与早期 PU-8，SUBQ 在第 39 脚，SQCK 在第 41 脚。动刀前对照 consolemods 板图确认。

BIOS 补丁焊盘（ATtiny84，日本厚机与 PAL PSone）: MCU 侧引脚如上表固定，由 [`src/bios.c`](src/bios.c) 与 [`src/port.S`](src/port.S) 驱动。AX、AY、DX 的主机侧焊盘因主板与 BIOS 修订而异，本项目无经验证的逐焊盘映射。请按你的型号对照 PsNee 的说明与板图定位，并在启动成功前视为未确认。

## 构建与烧录

所有构建、检查与测试都在固定 Docker 工具链中通过 `make` 运行。主机上只运行 `avrdude`。

| 工具 | 用途 |
|:-----|:-----|
| Docker | 运行固定工具链 |
| Git | 克隆仓库 |
| avrdude | 把镜像烧录到芯片 |

```bash
git clone https://github.com/gufranco/openscex-modchip.git
cd openscex-modchip
make REGION=us                                        # ATtiny85，美洲，内部时钟
make MCU=attiny84 REGION=eu BIOS=scph_102             # PAL PSone，带 BIOS 补丁
avrdude -c <programmer> -p attiny85 -U flash:w:openscex-modchip-attiny85.hex:i
```

任何 ISP 均可，包括用 Arduino 作 ISP。固件在启动时把时钟预分频器复位为一分频，故出厂的 CKDIV8 熔丝不改变时序。

| 构建 | 熔丝（low / high / extended） |
|:-----|:------------------------------|
| 内部 8 MHz RC | `0xE2` / `0xDF` / `0xFF` |
| 外部时钟 | 取决于测得频率，从芯片数据手册读取 |

外部时钟构建已在 PU-18 与 PSone 实机 Verified（2026-10-05），在其他主板上为前提门控。用测得的 mechacon 频率构建，`make CLOCK=external EXT_F_CPU=<hz>UL`，产物名带 `-extclk`。候选频率为 4.2336 MHz（16.9344 MHz 除以四），从片段 Concluded，此处未实测。在测量时钟引脚电压之前，不要烧录外部时钟熔丝。

验证:

```bash
make test        # 主机测试、simavr 主机模型、静态分析、MISRA
make repro       # 两次全新构建，逐字节一致
make mutate      # 逻辑层变异测试
```

同样的关卡在每次 push 与 pull request 时也在 CI 中运行，定义见 [`.github/workflows/ci.yml`](.github/workflows/ci.yml)。

## 诊断

芯片在完成注入并空闲后，向 EEPROM 写入一条 4 字节飞行记录，使安装可被诊断而非猜测。写入绝不发生在上电时或区域检查窗口内，故不影响注入时序；每个上电周期仅写一次，故 EEPROM 寿命无虞。用编程器读回:

```bash
avrdude -c <programmer> -p attiny85 -U eeprom:r:diag.bin:r
```

| 字节 | 含义 |
|:-----|:-----|
| 0 | 魔数 `0x50`，其他值表示尚无记录 |
| 1 | 检测到的主板: `0` 旧式门控，`1` WFCK 载波 |
| 2 | 达到空闲的会话数，到 255 回绕 |
| 3 | 最近一次会话送出的区域字符串数 |

## 安全

- 接线前在每个搭接点测量逻辑电压。数值取自既定的 PsNee 与 Mayumi 安装（厚机约 5 V，PSone PM-41(2) 更低且对噪声敏感），但假定不是测量，故在把信号接入时钟引脚之前请在你的板上确认。
- 测量之前不要把主机信号接入时钟引脚。
- 打开主机并焊接到 CD 子系统可能将其损坏。自担风险制作。

## 许可

固件与文档采用 [MIT](LICENSE)。
