# GitHub 发布与资料边界

源码仓库为 `zbnw/eeg-bw16-cyd`。远端已有的 [LICENSE](../LICENSE) 是 GNU GPL v3，合并时保留原文。
Arduino 开发板平台、厂商工具和 Python 依赖由使用者按各自来源安装，本仓库不附带它们。

## 已排除的本地资料

`.gitignore` 排除了本机工作说明和代理状态、聊天记录、个人 EEG 采样、构建镜像、工具链安装、虚拟环境及备份快照。
公开仓库只包含源码、通用接线/安全说明、可复现的操作文档和合成协议测试数据。

推送前请逐项查看 `git status` 与 `git ls-files`，确认新建文件没有个人采样、导出的 CSV/JSONL、串口日志、截图或本机配置。
不要用 `git add -f` 绕过忽略规则来添加聊天或采集资料。

## 后续发布维护

- 使用、修改或再发布前请阅读 GNU GPL v3 的完整授权文本。
- 后续提交前再次检查 `docs/`、`hardware/` 与 README 中没有本机路径或私人测量内容。
- 需要发布固件二进制时，单独确定哪些目标、构建来源、哈希和许可证可以公开；当前 `.build/` 被忽略，不会随源码提交。

## 本地复核命令

在项目根目录检查将要提交和已配置的远端：

```powershell
git status --short --branch
git ls-files
git remote -v
git check-ignore -v AGENTS.md docs/CHAT_HISTORY.json .build/sample.bin
```

个人采集数据、聊天、烧录日志和本机工具链继续只保留在本地。
