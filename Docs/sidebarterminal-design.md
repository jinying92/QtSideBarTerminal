# QtSideBarTerminal — 最小单终端侧边栏设计文档

## 1. 概述

将 Qt Creator 底部 Terminal 以**最小单终端**形式移植到右侧 DockWidget 中，实现"打开 → 使用 → 关闭"的最简交互。

### 1.1 目标

- 在 Qt Creator 右侧停靠区域嵌入一个独立终端
- 支持默认 Shell 自动启动
- DockWidget 可折叠/展开/拖拽
- 不修改 Qt Creator 原有代码，纯插件实现

### 1.2 非目标（不在本阶段实现）

- 多标签终端管理（使用底部的 TerminalPane 即可）
- 自定义 Shell 选择菜单
- 多终端实例
- TerminalPane 原有的工具栏按钮（新建/关闭/锁键盘等）

---

## 2. 核心 API 依赖

| API | 头文件 | 用途 |
|-----|--------|------|
| `TerminalSolution::TerminalView` | `solutions/terminal/terminalview.h` | 终端渲染控件（TerminalLib 库） |
| `TerminalSolution::TerminalSurface` | `solutions/terminal/terminalsurface.h` | 终端仿真核心（基于 libvterm） |
| `Utils::Terminal::defaultShellForDevice` | `utils/terminalhooks.h` | 获取系统默认 Shell（Utils 库） |
| `Core::ICore` | `coreplugin/icore.h` | 获取 `mainWindow()` 用于 QDockWidget 嵌入 |
| `QDockWidget` | `QDockWidget` | Qt 标准停靠窗口控件 |

### 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 终端控件来源 | `TerminalView`（TerminalLib 库） | Terminal 插件在 Windows 上不导出 `TerminalWidget` 符号 |
| 嵌入方式 | `QDockWidget` | `RightPaneWidget::setWidget()` 在 QtCreator 19.0.2 上行为异常 |
| Shell 管理 | `QProcess` 直接管理 | 避免依赖 `TerminalProcessImpl`（插件内部 API） |

---

## 3. 架构设计

### 3.1 类图

```
┌────────────────────────────────────────┐
│  QtSideBarTerminalPlugin (IPlugin)     │
│  + initialize()                        │
│  + extensionsInitialized()             │
│  - m_sidebarTerminal*                  │
└──────────┬─────────────────────────────┘
           │ 创建并持有
           ▼
┌────────────────────────────────────────┐
│  SideBarTerminal (QObject)             │
│  + toggleTerminal()                    │
│  + isTerminalVisible()                 │
│  - m_terminal: SimpleTerminalWidget*   │
│  - m_isCreated: bool                   │
└──────────┬─────────────────────────────┘
           │ 创建并放入 DockWidget
           ▼
┌────────────────────────────────────────┐
│  SimpleTerminalWidget (TerminalView)   │ ← TerminalLib 库
│  继承 TerminalView，桥接 QProcess      │
│  - m_process: QProcess*                │
└──────────┬─────────────────────────────┘
           │ 通过 QDockWidget 停靠到
           ▼
┌────────────────────────────────────────┐
│  QDockWidget (Qt 标准控件)             │
│  Qt Creator 主窗口右侧停靠区域          │
└────────────────────────────────────────┘
```

### 3.2 数据流

```
用户按键 → TerminalView → TerminalSurface → writeToPty() [override]
  → QProcess::write() → Shell
                               ↑
Shell 输出 → QProcess → onReadyReadStdout/Stderr
  → surface()->dataFromPty() → TerminalSurface → TerminalView 渲染
```

### 3.3 生命周期

```
插件加载
  → initialize()
    → 创建 SideBarTerminal（不立即创建终端）
    → 注册菜单项 Toggle Terminal (Ctrl+Alt+T)

用户 Ctrl+Alt+T 首次触发
  → SideBarTerminal::toggleTerminal()
    → 创建 SimpleTerminalWidget → 启动 QProcess(Shell)
    → 创建 QDockWidget → 停靠到 mainWindow 右侧 → show()
    → 设置焦点

用户再次 Ctrl+Alt+T
  → 切换 DockWidget 可见性

Shell 进程退出
  → finished 信号 → 自动隐藏 DockWidget

用户关闭 Qt Creator
  → aboutToShutdown()
    → 关闭终端进程 → 销毁 DockWidget
```

---

## 4. 类设计

### 4.1 SimpleTerminalWidget

```cpp
class SimpleTerminalWidget : public TerminalSolution::TerminalView
{
    Q_OBJECT
public:
    explicit SimpleTerminalWidget(QWidget *parent, const QString &shell);
    ~SimpleTerminalWidget() override;

    void closeTerminal();
    QProcess::ProcessState processState() const;

signals:
    void started(qint64 pid);
    void finished(int exitCode);

protected:
    qint64 writeToPty(const QByteArray &data) override;

private:
    void setupProcess(const QString &shell);
    void onReadyReadStdout();
    void onReadyReadStderr();

    QProcess *m_process = nullptr;
};
```

### 4.2 SideBarTerminal

```cpp
class SideBarTerminal : public QObject
{
    Q_OBJECT
public:
    explicit SideBarTerminal(QObject *parent = nullptr);
    ~SideBarTerminal() override;

    void toggleTerminal();
    bool isTerminalVisible() const;

private:
    void createTerminal();
    void destroyTerminal();

    SimpleTerminalWidget *m_terminal = nullptr;
    bool m_isCreated = false;
};
```

---

## 5. 文件清单

```
QtSideBarTerminal/
├── CMakeLists.txt
├── QtSideBarTerminal.json.in
├── qtsidebarterminal.cpp           # IPlugin 实现
├── qtsidebarterminalconstants.h    # ID 常量
├── qtsidebarterminaltr.h          # 翻译工具
├── sidebarterminal.h              # SideBarTerminal 管理类
├── sidebarterminal.cpp
├── simpleterminal.h               # SimpleTerminalWidget
├── simpleterminal.cpp
├── .gitignore
└── Docs/
    └── sidebarterminal-design.md  # 本文档
```

---

## 6. 关键决策与约束

### 6.1 终端控件来源

**选择：继承 TerminalLib 的 TerminalView**

| 因素 | 影响 |
|------|------|
| Terminal 插件不导出 Widget 符号 | Windows DLL 仅导出 `qt_plugin_instance` 和 `qt_plugin_query_metadata_v2` |
| TerminalLib 是可链接的库 | `add_qtc_library(TerminalLib)`，有 `TERMINAL_EXPORT` |
| TerminalView 有完整终端渲染 | 基于 libvterm，支持 ANSI 色彩、光标等 |

### 6.2 嵌入方式：QDockWidget vs RightPaneWidget

| 因素 | QDockWidget | RightPaneWidget |
|------|-------------|-----------------|
| 可靠性 | Qt 标准控件，行为稳定 | QtCreator 版本间 API 行为不一致 |
| 可拖拽 | 用户可自由移动/拆分为浮动窗口 | 固定位置 |
| 依赖 | 纯 Qt，无 Creator 内部 API | 需要 RightPanePlaceHolder 配合 |

### 6.3 创建时机

**选择：延迟创建（首次 toggle 时创建）**

| 因素 | 影响 |
|------|------|
| 启动性能 | 避免 Qt Creator 启动时就 fork 终端进程 |
| 资源占用 | 不使用时零开销 |

### 6.4 Shell 管理

**选择：QProcess 直接管理**

不使用 `Utils::TerminalInterface`（其 Stub Creator 机制过于复杂），直接在构造函数中 `QProcess::start(shell)` 启动，`writeToPty()` 重写中调用 `QProcess::write()`。

---

## 7. 风险与限制

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| `TerminalView` 在窄侧边栏布局异常 | 终端宽度不足，命令行换行体验差 | 用户可拖拽调整 DockWidget 宽度 |
| `QProcess` 在 Windows 上无 PTY | 部分交互式命令行为与真实终端不同 | 后期可用 winpty 替换 |
| `defaultShellForDevice` 在 Windows 返回空 | 无法获取默认 Shell | 回退到 `cmd.exe` |
| 终端进程未清理 | Qt Creator 关闭时残留进程 | `aboutToShutdown()` 中显式调用 `closeTerminal()` |
| QDockWidget 浮动后可能脱离主窗口 | 用户体验问题 | 无缓解（用户自由选择） |

---

## 8. 后续扩展可能

1. **工作目录联动** — 自动使用 `%{CurrentProject:Path}` 作为工作目录
2. **工具栏按钮** — 在 DockWidget 标题栏添加"新建终端"、"关闭终端"按钮
3. **Shell 选择** — 添加右键菜单选择 cmd/powershell/bash
4. **PTY 支持** — 在 Windows 上使用 winpty 提供真正的 PTY 支持
5. **快捷键传递** — Ctrl+C / Ctrl+D 等键盘快捷键正确传递到终端
