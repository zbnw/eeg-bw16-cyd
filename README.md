# eeg-bw16-cyd

这是一个基于 BW16 与 ESP32-CYD 的 EEG 原型项目，包含 BLE 数据传输、LCD 波形显示、串口采集工具和网页监视器。

当前源码版本：**0.2.0，2026-09-26**。主链路为 EEG V6.1 → BW16 UART → BLE → ESP32-CYD LCD / USB → 电脑。
0.2.0 固件已为本项目的 CYD 和 BW16 设备上传；请在自己的硬件上重新按升级步骤确认版本和启动状态。先阅读[升级、回退与验收](docs/09_v0.2升级与验收.md)，再阅读[项目导览](docs/00_项目导览.md)。GitHub 准备状态见[发布检查](docs/GITHUB发布检查.md)。

## 本版提供什么

- 两块板共用有界解析器：损坏包逐字节重新同步、残包超时、指标字段按类型解析。
- BW16：32 条通知发送队列、发送节奏控制、订阅切换时清理旧批次，每秒发送版本及健康统计。
- CYD：96 条接收队列、断流恢复、完整指标重组、版本和错误计数显示；保留已验证的低级 ILI9341 初始化和全部 LCD 引脚。
- CYD USB 默认仍为 115200 二进制 EEG 包；发送 ASCII 命令 `json\n` 可切换 JSON Lines，`binary\n` 恢复二进制。
- 电脑网关连接真实串口，或明确标记地回放历史 BIN；可以记录含诊断数据的 JSONL。
- 网页按样本而非心跳消息判断实时性，显示链路统计、保留波形极值，CSV 保留采集时的设备和会话信息。
- 构建工具保留日志、源文件与固件 SHA-256 清单，不执行烧录。

## 快速开始

以下命令在项目根目录运行，COM 号按实际设备修改。Python 工具要求 Python 3.11+。

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r tools/requirements.txt

# 无硬件：对本地捕获的 BIN 回放，网页会标记 REPLAY（仓库不附带采样）
./.venv/Scripts/python.exe tools/eeg_gateway.py --replay .build/captures/sample.bin --sample-rate 248

# 烧录 CYD 后：真实数据和链路诊断，COMx 按实际端口替换
./.venv/Scripts/python.exe tools/eeg_gateway.py --port COMx --record recordings/session-001.jsonl
```

网关打印网页地址，默认是 `http://127.0.0.1:8080/?ws=ws://127.0.0.1:8765/ws`。
将 `COMx` 换成当前操作系统枚举到的串口号。
不带 `ws` 参数的网页仍为 DEMO；网关仅监听本机，Ctrl+C 退出。同一串口不能同时被网关和采集软件占用。
没有硬件采样时间戳；网页时间轴和导出时间必须按近似量理解。

## 构建

权威目标为 Arduino CLI 的 CYD 板型和已安装的 AmebaD BW16 板型。
本机验证使用 ESP32 core 3.1.1、AmebaD 3.1.7。新环境还需先安装 Arduino CLI 与这两个开发板平台，并将 Arduino CLI 加入 PATH；BW16 平台需按芯片厂商说明配置。

```powershell
python tools/build_firmware.py --target all
```

输出目录为新的 `.build/release-日期时间/`，含日志和 `manifest.json`。
单独编译必须加 `--libraries libraries`：

```powershell
arduino-cli compile --fqbn esp32:esp32:jczn_2432s028r --libraries libraries --build-path .build/local-cyd sketches/esp32_cyd_starter
arduino-cli compile --fqbn realtek:AmebaD:Ai-Thinker_BW16 --libraries libraries --build-path .build/local-bw16 sketches/bw16_ble_eeg
```

BW16 工具链会写入安装目录的中间文件；遇到权限错误，即使最终返回 0 也不算成功。构建工具会检查错误文字和新镜像。
PlatformIO 是备用入口，尚不代表最终硬件验证。烧录命令及 BW16 镜像准备见 [升级说明](docs/09_v0.2升级与验收.md)。

## 文件导航

| 路径 | 用途 |
| --- | --- |
| `libraries/EegCore/` | 两块板共用的协议代码 |
| `sketches/esp32_cyd_starter/` | CYD 固件及板级配置 |
| `sketches/bw16_ble_eeg/` | BW16 BLE 固件及配置 |
| `sketches/bw16_eeg_reader/` | 独立的原始 UART 字节直通诊断固件 |
| `tools/` | 采集、解析、实时网关、构建、备份 |
| `tests/` | C++ 核心、Python 协议/网关、JavaScript 状态回归 |
| `web/` | DEMO / 实时 / 历史回放共用界面 |
| `hardware/` | 模块底板连接约束，尚非生产 PCB |
| `docs/` | 中文说明、历史证据、升级验收 |
| `.build/` | 本地构建输出和私有采集文件（不发布） |

接线仍按 [EEG_WIRING.md](EEG_WIRING.md) 和 [安全说明](hardware/SAFETY.md) 执行。
佩戴端沿用电池供电、与电脑仅经 BLE 相连的项目边界；本次未修改或重新核验电气设计。
`AGENTS.md` 仅供本地使用，已加入忽略规则。
