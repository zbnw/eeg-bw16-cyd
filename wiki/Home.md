# EEG-bw16-cyd：BW16 无线脑电采集与 ESP32-CYD 显示原型

这是一个开源的**单通道 EEG（脑电）链路原型**。它把 EEG 模块输出的串口数据交给电池供电的 BW16，经 BLE 无线送到 ESP32-CYD（ESP32-2432S028R 类开发板），在板载 LCD 上显示原始波形，并可通过 CYD 的 USB 串口送到电脑。电脑端提供采集与分析工具、本机实时网关和网页监视器。

项目适合学习嵌入式数据链路、验证模块接线与 BLE 传输、观察原始波形及调试丢包和断流。它**不是医疗器械**，不提供诊断、治疗建议或经校准的脑电测量。网页的演示曲线是合成数据，不能当作真实 EEG。人体接触测试必须先阅读[原型安全边界](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E5%8E%9F%E5%9E%8B%E5%AE%89%E5%85%A8%E8%BE%B9%E7%95%8C)。

当前源码版本为 **0.2.0（2026-09-26）**。项目设备曾上传该版本固件，但其他硬件需要自行构建、烧录和验证；源码构建通过不等于每块板上的运行结果均已确认。完整升级与验收步骤见[0.2.0 升级、回退与验收](https://github.com/zbnw/eeg-bw16-cyd/wiki/0.2%E5%8D%87%E7%BA%A7%E4%B8%8E%E9%AA%8C%E6%94%B6)。

完整中文说明也已整理在[项目 Wiki](https://github.com/zbnw/eeg-bw16-cyd/wiki)，可以从首页按“入门、数据与排障、协议与开发、硬件与维护”逐步阅读。

## 它是如何工作的

```text
电极 → EEG V6.1 模块 ──57600 UART──→ BW16 ──BLE──→ ESP32-CYD
                                                     ├─ LCD：原始波形、信号与链路状态
                                                     └─ 115200 USB：二进制包或 JSONL
                                                                         ↓
                                                        电脑采集工具 / 本机网关 / 网页
```

EEG 模块产生原始样本和模块指标包；BW16 负责串口接收、包校验、排队和 BLE 通知；CYD 负责 BLE 接收、重组、波形绘制和 USB 转发。电脑端可直接保存原始 BIN、解析历史文件，或启动实时网关让网页展示当前数据。CYD 不需要连接 Wi-Fi，网页网关默认只监听本机。

| 组成 | 本项目中的任务 | 对使用者的意义 |
| --- | --- | --- |
| Sichiray EEG V6.1 | 输出单通道串口原始样本与指标包 | 数据源；供电、电极与接口必须先核实 |
| BW16 | UART → BLE 发送端 | 佩戴端可由电池供电，并与 CYD 无线通信 |
| ESP32-CYD | BLE 接收、LCD 显示、USB 输出 | 不接电脑也能现场看波形；接电脑可采集 |
| Python 工具 | 构建、采集、解析、网关和备份 | 保存原始数据并定位链路问题 |
| 网页 | 演示、实时监视、历史可视化与 CSV 导出 | 以图形方式查看会话和链路统计 |

底板设计仍处于连接规范阶段，目标是固定 EEG/BW16 模块、可靠引出电源、UART 和电极连接，尚无可直接生产的 PCB 文件。不要把文档中的模块脚位当作未经复核的成品接线图。

## 本版的主要能力

- 两块板共用有界协议解析器：损坏包逐字节重新同步、残包超时、指标字段完整校验后更新。
- BW16 使用 32 条发送队列、发送节奏控制、指标双段成对处理，并约每秒报告版本与健康计数。
- CYD 使用 96 条接收队列、断流恢复和指标重组；LCD 显示版本、原始波形、信号质量与错误计数，同时保留此板已使用的直接 ILI9341 SPI 驱动与引脚。
- CYD USB 默认输出 `115200` 二进制 EEG 包。发送 ASCII `json\n` 切换为逐行 JSON，发送 `binary\n` 恢复二进制。
- 电脑网关支持真实 CYD 串口或明确标记的历史 BIN 回放，可保存包含诊断状态的 JSONL；网页按**新样本**判断实时性，状态心跳不会掩盖断流。
- 采集和构建工具默认保护已有文件，构建输出包含日志及 SHA-256 清单；构建脚本本身不会烧录设备。

这些计数用于定位问题，不等于医学准确性或无线链路无损保证。波形纵轴为模块原始数值，不是微伏；采样时间和网页横轴是近似值。详情见[协议与数据含义](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E5%8D%8F%E8%AE%AE%E4%B8%8E%E6%A0%B8%E5%BF%83%E9%80%BB%E8%BE%91)和[网页协议](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E7%BD%91%E9%A1%B5%E4%B8%8E%E7%BD%91%E5%85%B3%E5%8D%8F%E8%AE%AE)。

## 第一次使用：先选路径

**只想看界面、暂时没有硬件：**可以直接打开 [web/index.html](https://github.com/zbnw/eeg-bw16-cyd/blob/main/web/index.html)。默认进入演示模式，显示浏览器生成的合成信号。页面的 `DEMO` 标识用于提醒它不是真实测量；无需烧录或连接设备。

**已有两块板，要看到实时波形：**先阅读[接线与上电](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E6%8E%A5%E7%BA%BF%E4%B8%8E%E4%B8%8A%E7%94%B5)，确认 EEG 与 BW16 的供电及两线 UART，再按[升级与验收](https://github.com/zbnw/eeg-bw16-cyd/wiki/0.2%E5%8D%87%E7%BA%A7%E4%B8%8E%E9%AA%8C%E6%94%B6)先烧录 CYD、后烧录 BW16。上电后 CYD 应从 `BLE SEARCH` 转到接收状态；若只看到 `BLE NO DATA`，应先检查 EEG UART 与供电。然后按下一节启动电脑网关。

**只想调试 EEG 串口：**可临时烧录 [BW16 原始字节转发诊断程序](https://github.com/zbnw/eeg-bw16-cyd/wiki/BW16%E4%B8%B2%E5%8F%A3%E8%AF%8A%E6%96%AD)，用十六进制串口终端检查数据包。此程序不运行 BLE 主链路，诊断完成后要重新烧录 `sketches/bw16_ble_eeg`。

## 电脑实时监视与保存数据

在项目根目录安装 Python 3.11+，创建虚拟环境并安装网关依赖。以下为 Windows PowerShell 示例；`COMx` 应换成 CYD 当前显示的串口号，不能沿用旧会话的端口号。

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r tools/requirements.txt
./.venv/Scripts/python.exe tools/eeg_gateway.py --port COMx --record recordings/session-001.jsonl
```

网关会打印网页地址，默认类似 `http://127.0.0.1:8080/?ws=ws://127.0.0.1:8765/ws`。请打开**终端打印的完整地址**；直接打开 `web/index.html` 或缺少 `ws` 参数时仍是演示模式。网页出现 `LIVE` 只表示最近收到新的设备样本；`STALE` 表示样本暂停，`REPLAY` 表示历史文件回放。结束时在终端按 `Ctrl+C`，网关会尽量让 CYD 恢复默认二进制输出。

记录文件为 JSONL，适合保留会话、设备版本、接收时间和健康状态；网页 CSV 只保留最近最多 100000 个点。完整采集与导出说明见[采集与数据处理](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E9%87%87%E9%9B%86%E4%B8%8E%E6%95%B0%E6%8D%AE%E5%A4%84%E7%90%86)和[网页使用说明](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E7%BD%91%E9%A1%B5%E4%B8%8E%E7%BD%91%E5%85%B3%E4%BD%BF%E7%94%A8)。同一串口不能同时由网关、串口监视器和其它采集软件占用。

需要离线展示历史文件时，可在已有本地 BIN 文件的前提下运行：

```powershell
./.venv/Scripts/python.exe tools/eeg_gateway.py --replay .build/captures/sample.bin --sample-rate 248
```

这里的路径只是示例；仓库**不附带个人 EEG 采样**。回放会明确标记为 `REPLAY`，给定采样率仅用于近似时间轴，不能把回放当成当前实时数据。

## 构建与烧录

本项目的权威构建目标为 Arduino CLI 的 `esp32:esp32:jczn_2432s028r` 和 `realtek:AmebaD:Ai-Thinker_BW16`。本地构建曾使用 ESP32 core 3.1.1、AmebaD 3.1.7；新电脑需先安装 Arduino CLI、对应开发板平台及其厂商工具。`tools/build_firmware.py` 会优先使用项目本地 CLI 副本（若存在），否则寻找系统 `PATH` 中的 `arduino-cli`。

```powershell
python tools/build_firmware.py --target all
```

每次生成新的 `.build/release-日期时间/` 目录，内含构建日志、固件文件和 `manifest.json`。手工编译须显式指定项目库：

```powershell
arduino-cli compile --fqbn esp32:esp32:jczn_2432s028r --libraries libraries --build-path .build/local-cyd sketches/esp32_cyd_starter
arduino-cli compile --fqbn realtek:AmebaD:Ai-Thinker_BW16 --libraries libraries --build-path .build/local-bw16 sketches/bw16_ble_eeg
```

**构建不执行烧录。** 固件镜像必须按本机新生成的清单选取，尤其 BW16 厂商工具会从自己的工具目录读取镜像，其旧文件可能被其它草图覆盖。烧录前关闭所有占用串口的程序，并按[升级、镜像核对和验收步骤](https://github.com/zbnw/eeg-bw16-cyd/wiki/0.2%E5%8D%87%E7%BA%A7%E4%B8%8E%E9%AA%8C%E6%94%B6)操作。PlatformIO 配置是备用入口，不替代上述板型的构建和实机验收。

## 文档导航

| 想解决的问题 | 阅读 |
| --- | --- |
| 从接线到首次上电 | [接线与上电](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E6%8E%A5%E7%BA%BF%E4%B8%8E%E4%B8%8A%E7%94%B5)、[详细接线](https://github.com/zbnw/eeg-bw16-cyd/wiki/EEG%E6%8E%A5%E7%BA%BF%E8%AF%A6%E8%A7%A3) |
| 从环境安装到烧录与使用 | [安装与使用](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E5%AE%89%E8%A3%85%E4%B8%8E%E4%BD%BF%E7%94%A8)、[升级与验收](https://github.com/zbnw/eeg-bw16-cyd/wiki/0.2%E5%8D%87%E7%BA%A7%E4%B8%8E%E9%AA%8C%E6%94%B6) |
| 理解 BLE、USB 与网页数据 | [BLE 通信协议](https://github.com/zbnw/eeg-bw16-cyd/wiki/BLE%E9%80%9A%E4%BF%A1%E5%8D%8F%E8%AE%AE)、[协议与核心逻辑](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E5%8D%8F%E8%AE%AE%E4%B8%8E%E6%A0%B8%E5%BF%83%E9%80%BB%E8%BE%91)、[网页协议](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E7%BD%91%E9%A1%B5%E4%B8%8E%E7%BD%91%E5%85%B3%E5%8D%8F%E8%AE%AE) |
| 采集、保存、分析、导出 | [采集与数据处理](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E9%87%87%E9%9B%86%E4%B8%8E%E6%95%B0%E6%8D%AE%E5%A4%84%E7%90%86)、[网页使用说明](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E7%BD%91%E9%A1%B5%E4%B8%8E%E7%BD%91%E5%85%B3%E4%BD%BF%E7%94%A8) |
| 排查断流、串口和编译问题 | [故障排查](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E6%95%85%E9%9A%9C%E6%8E%92%E6%9F%A5) |
| 开发、测试和保护原始资料 | [开发维护](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E5%BC%80%E5%8F%91%E7%BB%B4%E6%8A%A4)、[测试记录](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E6%B5%8B%E8%AF%95%E8%AE%B0%E5%BD%95)、[备份与复原](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E5%A4%87%E4%BB%BD%E4%B8%8E%E5%A4%8D%E5%8E%9F) |
| 底板设计与安全 | [模块集成](https://github.com/zbnw/eeg-bw16-cyd/wiki/EEG%E6%A8%A1%E5%9D%97%E9%9B%86%E6%88%90)、[底板连接规范](https://github.com/zbnw/eeg-bw16-cyd/wiki/BW16%E5%BA%95%E6%9D%BF%E8%BF%9E%E6%8E%A5%E8%A7%84%E8%8C%83)、[安全边界](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E5%8E%9F%E5%9E%8B%E5%AE%89%E5%85%A8%E8%BE%B9%E7%95%8C) |

## 代码目录与资料边界

| 路径 | 内容 |
| --- | --- |
| `libraries/EegCore/` | CYD 与 BW16 共用的 EEG 包解析代码 |
| `sketches/esp32_cyd_starter/` | CYD 固件与板级配置 |
| `sketches/bw16_ble_eeg/` | BW16 BLE 固件与配置 |
| `sketches/bw16_eeg_reader/` | BW16 原始字节转发诊断程序 |
| `tools/`、`tests/` | 网关、采集、构建、分析与合成测试 |
| `web/` | 演示、实时和历史回放共用界面 |
| `docs/`、`hardware/` | 使用、协议、硬件与安全说明 |
| `wiki/` | 从仓库文档生成并发布到 GitHub Wiki 的中文页面 |

`.build/`、工具链、虚拟环境、个人采集、烧录日志和备份不随公开源码发布；`AGENTS.md` 仅供本地使用。发布边界见[GitHub 发布检查](https://github.com/zbnw/eeg-bw16-cyd/wiki/%E5%8F%91%E5%B8%83%E4%B8%8E%E8%B5%84%E6%96%99%E8%BE%B9%E7%95%8C)。代码与文档按仓库中的 [GNU GPL v3 许可证](https://github.com/zbnw/eeg-bw16-cyd/blob/main/LICENSE) 发布。

---

本页与[仓库原文](https://github.com/zbnw/eeg-bw16-cyd/blob/main/README.md)同步。
