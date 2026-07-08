# QtSideBarTerminal

在 Qt Creator 右侧栏（项目/文件系统/大纲）中嵌入一个**真正的终端**，支持交互式 Shell 和 PTY（ConPTY on Windows）。最终目标是在侧边栏中运行 **AI Agent CLI**（如 Claude Code、Copilot CLI 等），在编码过程中随时调用 AI 助手。

---
![Uploading img_v3_0213d_4ba8b052-6219-4d86-93ae-45359a8611eg.jpg…]()

## 最终目标

在 Qt Creator 的右侧边栏中嵌入一个完整的终端，用于运行 **AI Agent 命令行工具**：

- 在编码时随时召唤 AI 辅助（代码生成、重构、审查）
- AI Agent 可以直接读取当前项目文件、执行 Shell 命令
- 无需切换到独立的终端窗口，保持心流状态

> 当前版本实现了基础的终端嵌入（Shell 可交互、颜色正常、工作目录联动），后续将专注于 AI Agent CLI 的集成体验。

---

## 已实现的功能

- ✅ **Qt Creator 右侧栏选项卡** — 终端作为选项卡出现在"项目/文件系统/大纲"旁边
- ✅ **Windows ConPTY / Linux PTY** — 真正的伪终端支持，交互式 Shell 完全正常（提示符、回显、输入、ANSI 颜色）
- ✅ **自动识别项目目录** — 终端启动时工作目录自动设置为 Qt Creator 当前项目目录
- ✅ **深色终端配色** — 默认使用与 Qt Creator 深色主题一致的配色方案（浅灰文字、深灰背景）
- ✅ **延迟启动** — Shell 进程在终端选项卡首次显示时启动，避免 Qt Creator 启动时不必要的 fork

---

## 构建要求

| 组件 | 版本 |
|------|------|
| Qt Creator | 19.0.2（基于 `qt-creator-opensource-src-19.0.2`） |
| Qt | 6.10.3 |
| CMake | ≥ 3.16 |
| 编译器 | MSVC 2022 (Windows) / GCC (Linux) |

### 目录结构参考

```
# Qt Creator 源码（用于参考 API）
E:\1_SourceCode\ComponentProjects\qt-creator-opensource-src-19.0.2\

# Qt Creator 安装路径（CMake 引用）
H:\Qt\Qt6\Tools\QtCreator\
```

---

## 构建步骤

```bash
# 1. 配置
cmake -DCMAKE_PREFIX_PATH=H:/Qt/Qt6/Tools/QtCreator \
      -DCMAKE_BUILD_TYPE=Release \
      -B build/Qt_6

# 2. 编译
cmake --build build/Qt_6 --target ALL_BUILD --config Release

# 3. 安装（替换为 build 输出的路径）
# 将 QtSideBarTerminal.dll + QtSideBarTerminal.json 复制到
# Qt Creator 的 plugins/Release/ 目录
```

构建完成后，将编译输出的 `QtSideBarTerminal.dll`（或 `.so`）与 `QtSideBarTerminal.json` 复制到 Qt Creator 的插件目录（通常为 `<QtCreator>/lib/qtcreator/plugins/`）。

---

## 架构

```
QtSideBarTerminalPlugin (IPlugin)
  └─ TerminalNavigationFactory (INavigationWidgetFactory)
       └─ SimpleTerminalWidget (TerminalSolution::TerminalView)
            └─ Utils::Process + PtY (ConPTY) → cmd.exe / bash
```

| 组件 | 来源 | 作用 |
|------|------|------|
| `TerminalLib` | Qt Creator SDK（`QtCreator::TerminalLib`） | `TerminalView` 终端渲染 + `TerminalSurface` libvterm 仿真 |
| `Utils::Process` | Qt Creator SDK（`QtCreator::Utils`） | 进程管理，`setPtyData()` 启用 ConPTY/PTY |
| `INavigationWidgetFactory` | Qt Creator Core API | 注册右侧栏选项卡 |
| `ProjectExplorer::ProjectTree` | Qt Creator SDK | 获取当前项目目录 |

### 数据流

```
用户按键 → TerminalView → TerminalSurface → writeToPty() [override]
  → Utils::Process::writeRaw() → PTY → Shell (cmd/powershell/bash)

Shell 输出 → PTY → Utils::Process → readAllRawStandardOutput()
  → surface()->dataFromPty() → TerminalSurface → TerminalView 渲染
```

---

## 相关文档

- [设计文档](Docs/sidebarterminal-design.md) — 详细架构说明和设计决策

---

## 许可

本项目基于 Qt Creator 兼容的许可证发布（GPL-3.0）。
`TerminalLib` 和 `Utils::Process` 来自 Qt Creator 开源项目，遵循其原始许可证。
