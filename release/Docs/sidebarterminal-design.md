# QtSideBarTerminal — 最小单终端侧边栏设计文档

## 1. 概述

将 Qt Creator 自带的终端能力以**最小单终端**形式，作为**右侧栏的一个选项卡**嵌入
（与"项目 / 文件系统 / 大纲"并列），实现"打开 → 使用 → 关闭"的最简交互。

> 可行性分析结论：Qt Creator 自带的 `Terminal` 插件（`src/plugins/terminal/`）把终端放在
> **底部输出面板**（`TerminalPane : Core::IOutputPane`），而非右侧栏。本项目目标是在**右侧栏**
> 出现选项卡，因此采用 Qt Creator 暴露的公开扩展点 `Core::INavigationWidgetFactory` 自行注册，
> 并复用可链接的 `TerminalLib`（`TerminalView` / `TerminalSurface`）做渲染、复用 `Utils` 的
> `Process` + `Pty` 做进程桥接（Windows 走 ConPTY，Linux 走系统 PTY）。

### 1.1 目标

- 在 Qt Creator 右侧栏出现"Terminal"选项卡
- 自动启动系统默认 Shell（Windows: cmd/powershell，Linux: /bin/sh 等）
- 完整终端交互（提示符、回显、输入、ANSI 色彩）—— 依赖真正的 PTY
- 不修改 Qt Creator 原有代码，纯插件实现

### 1.2 非目标（不在本阶段实现）

- 多标签终端管理
- 自定义 Shell 选择菜单 / 工具栏按钮
- shell 集成（clink /  fancy PowerShell 提示符）—— 需移植 `ShellIntegration`
- 搜索、链接点击等高级功能

---

## 2. 核心 API 依赖

| API | 头文件 | 用途 | 是否可链接 |
|-----|--------|------|-----------|
| `TerminalSolution::TerminalView` | `solutions/terminal/terminalview.h` | 终端渲染控件（TerminalLib 库，继承 `QAbstractScrollArea`） | ✅ `QtCreator::TerminalLib` |
| `TerminalSolution::TerminalSurface` | `solutions/terminal/terminalsurface.h` | 终端仿真核心（基于 libvterm） | ✅ `QtCreator::TerminalLib` |
| `TerminalView::surface()` / `dataFromPty()` | 同上 | 向仿真器喂入 Shell 输出 | ✅ |
| `TerminalView::writeToPty()` / `resizePty()` | 同上 | 虚函数，由我们重写桥接输入与尺寸 | ✅ |
| `Utils::Process` | `utils/qtcprocess.h` | 进程管理，支持 PTY 模式 | ✅ `QtCreator::Utils` |
| `Utils::Pty::Data` | `utils/processinterface.h` | PTY 配置（启用 ConPTY / 系统 PTY） | ✅ `QtCreator::Utils` |
| `Utils::Terminal::defaultShellForDevice` | `utils/terminalhooks.h` | 获取系统默认 Shell | ✅ `QtCreator::Utils` |
| `Core::INavigationWidgetFactory` | `coreplugin/inavigationwidgetfactory.h` | 注册右侧栏选项卡 | ✅ `QtCreator::Core` |

### 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 终端渲染 | `TerminalView`（TerminalLib 库） | `Terminal` 插件在 Windows 上不导出 `TerminalWidget` 符号 |
| 进程桥接 | `Utils::Process` + `Utils::Pty` | 裸 `QProcess` 使用管道，Windows 上 Shell 进入非交互模式（无提示符/不回显）；`Utils::Process` 设置 `Pty::Data` 后启用真正的 PTY（ConPTY），交互正常 |
| 右侧栏集成 | `Core::INavigationWidgetFactory` | 这是"项目/文件系统/大纲"选项卡的扩展点；`QDockWidget` 会生成独立可拖拽窗口而非栏内选项卡 |
| Shell 管理 | 直接 `Process::start(shell)` | 避免依赖 `TerminalProcessImpl`（插件内部 API） |

---

## 3. 架构设计

### 3.1 类图

```
┌──────────────────────────────────────────────┐
│  QtSideBarTerminalPlugin (IPlugin)            │
│  + initialize()  →  new TerminalNavigationFactory│
└──────────┬───────────────────────────────────┘
           │ 构造时自动加入全局工厂列表
           ▼
┌──────────────────────────────────────────────┐
│  TerminalNavigationFactory                    │
│   (Core::INavigationWidgetFactory)            │
│  + createWidget() → new SimpleTerminalWidget  │
│  (构造即注册，Qt Creator 右栏出现 "Terminal" 选项卡)│
└──────────┬───────────────────────────────────┘
           │ NavigationWidget 调用 createWidget()
           ▼
┌──────────────────────────────────────────────┐
│  SimpleTerminalWidget (TerminalView)          │ ← TerminalLib 库
│  继承 TerminalView，桥接 Utils::Process(PTY)  │
│  - m_process: Utils::Process*                 │
│  + writeToPty()  [override]  → Process.writeRaw│
│  + resizePty()  [override]  → Pty::Data.resize│
└──────────┬───────────────────────────────────┘
           │ 由 NavigationWidget 嵌入到
           ▼
┌──────────────────────────────────────────────┐
│  Core::NavigationWidget (Qt Creator 内置)      │
│  右侧栏："项目 | 文件系统 | 大纲 | Terminal"   │
└──────────────────────────────────────────────┘
```

### 3.2 数据流

```
用户按键 → TerminalView → TerminalSurface → writeToPty() [override]
  → Utils::Process::writeRaw() → PTY → Shell
                                     ↑
Shell 输出 → PTY → Utils::Process → readAllRawStandardOutput()
  → surface()->dataFromPty() → TerminalSurface → TerminalView 渲染

尺寸变化 → TerminalView::applySizeChange()
  → resizePty(QSize 行列数) [override] → Pty::Data::resize()
```

### 3.3 生命周期

```
插件加载
  → initialize()
    → new TerminalNavigationFactory
        （构造时加入 INavigationWidgetFactory 全局列表）

用户点击右侧栏 "Terminal" 选项卡
  → NavigationWidget 调用 createWidget()
    → new SimpleTerminalWidget(nullptr, defaultShellPath())
        → 构造中 setupPty() 启动 Shell（PTY 模式）
        → Shell 就绪后 started(pid) 信号

用户切换/收起选项卡
  → SimpleTerminalWidget 仅隐藏，Shell 继续运行（最小化开销）

用户关闭 Qt Creator
  → NavigationWidget 销毁控件 → ~SimpleTerminalWidget
    → closeTerminal()（terminate / kill 进程）
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
    qint64 writeToPty(const QByteArray &data) override;  // 键盘输入 → Process
    bool  resizePty(QSize newSize) override;             // 尺寸 → PTY

private:
    void setupPty(const QString &shell);  // Utils::Process + Pty::Data 启动 Shell
    Utils::Process *m_process = nullptr;
};
```

`setupPty` 关键步骤（参考 `src/plugins/terminal/terminalwidget.cpp` 的 `setupPty`）：

```cpp
m_process = new Utils::Process(this);
m_process->setProcessMode(Utils::ProcessMode::Writer);
m_process->setPtyData(Utils::Pty::Data{});          // 启用 PTY（Win: ConPTY, Linux: PTY）
m_process->setCommand(Utils::CommandLine{shell});
Utils::Environment env = Utils::Environment::systemEnvironment();
env.set("TERM", "xterm-256color");                  // 避免某些发行版默认 dumb
m_process->setEnvironment(env);
connect(m_process, &Utils::Process::readyReadStandardOutput, this, [this]{
    surface()->dataFromPty(m_process->readAllRawStandardOutput());
});
connect(m_process, &Utils::Process::started, this, [this]{ emit started(m_process->processId()); });
connect(m_process, &Utils::Process::done,    this, [this]{ emit finished(m_process->exitCode()); });
m_process->start();
```

### 4.2 TerminalNavigationFactory

```cpp
class TerminalNavigationFactory : public Core::INavigationWidgetFactory
{
    Q_OBJECT
public:
    TerminalNavigationFactory();
    Core::NavigationView createWidget() override;  // 返回 {new SimpleTerminalWidget(...), {}}
};
```

`INavigationWidgetFactory` 构造时自动把自己加入全局工厂列表（`g_navigationWidgetFactories`），
因此 `createWidget()` 会在用户点击右侧栏选项卡时被 `NavigationWidget` 调用。

---

## 5. 文件清单

```
QtSideBarTerminal/
├── CMakeLists.txt
├── QtSideBarTerminal.json.in
├── qtsidebarterminal.cpp              # IPlugin 实现（注册导航工厂）
├── qtsidebarterminaltr.h             # 国际化 Tr::tr
├── terminalnavigationfactory.h/.cpp  # 右侧栏工厂（INavigationWidgetFactory）
├── simpleterminal.h/.cpp             # SimpleTerminalWidget（TerminalView + Utils::Process/PTY）
├── .gitignore
└── Docs/
    └── sidebarterminal-design.md      # 本文档
```

`CMakeLists.txt` 的 `add_qtc_plugin` 关键依赖：
```
PLUGIN_DEPENDS QtCreator::Core
DEPENDS Qt::Widgets QtCreator::ExtensionSystem QtCreator::Utils QtCreator::TerminalLib
```

---

## 6. 关键决策与约束

### 6.1 终端控件来源

**选择：继承 TerminalLib 的 TerminalView**

| 因素 | 影响 |
|------|------|
| `Terminal` 插件不导出 Widget 符号 | Windows DLL 仅导出 `qt_plugin_instance` 和 `qt_plugin_query_metadata_v2` |
| TerminalLib 是可链接的库 | `add_qtc_library(TerminalLib)`，含 `TERMINAL_EXPORT` |
| `TerminalView` 有完整渲染 | 基于 libvterm，支持 ANSI 色彩、光标、选择、滚动 |

### 6.2 右侧栏集成：INavigationWidgetFactory vs QDockWidget

| 因素 | INavigationWidgetFactory（采用） | QDockWidget（已弃用） |
|------|--------------------------------|----------------------|
| 外观 | 作为右侧栏内的一个选项卡 | 独立的、可拖拽浮动的停靠窗口（出现在侧栏右方） |
| 符合需求 | ✅ 与"项目/文件系统/大纲"并列 | ❌ 用户明确要求栏内选项卡 |
| 依赖 | 纯 Core 公开 API | 纯 Qt，但位置不符合预期 |

### 6.3 Windows PTY（核心修复点）

**选择：Utils::Process + Utils::Pty**

裸 `QProcess` 使用匿名管道，Windows 上 `cmd.exe` / `powershell` 检测到 stdin 非控制台即进入
非交互模式（无提示符、不回显、`write()` 输入无效）。`Utils::Process` 在 `setPtyData()` 后于
Windows 使用 **ConPTY**、Linux 使用系统 PTY，Shell 获得真正的 TTY，交互完全正常。

### 6.4 创建时机

`createWidget()` 在用户首次点击"Terminal"选项卡时调用（构造即启动 Shell）。控件隐藏时 Shell
仍运行，零额外开销且随时可恢复；Qt Creator 关闭时随控件析构被清理。

---

## 7. 风险与限制

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 窄侧边栏下终端宽度不足 | 命令行换行体验一般 | 用户可拖拽调整右栏宽度 |
| `defaultShellForDevice` 返回空 | 无法获取默认 Shell | 按平台回退 `cmd.exe` / `/bin/sh` |
| 终端进程未清理 | Qt Creator 关闭时残留 | 控件析构 `~SimpleTerminalWidget` 中 `closeTerminal()` |
| 每次点击选项卡可能新建 Shell | 多次点击产生多个实例 | 当前为最小化实现，可接受；后续可缓存单例 |

---

## 8. 后续扩展可能

1. **单例缓存** — `createWidget()` 复用同一 `SimpleTerminalWidget`，避免重复 Shell
2. **工作目录联动** — 使用当前项目/文件目录作为 Shell 工作目录
3. **Shell 集成** — 移植 `ShellIntegration` 支持 clink / 高级 PowerShell 提示符
4. **工具栏按钮** — 在选项卡标题栏添加"新建/关闭/清屏"
5. **多标签** — 仿 `TerminalPane` 实现多终端 Tab
