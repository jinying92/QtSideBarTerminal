# QtSideBarTerminal — 最小单终端侧边栏设计文档

## 1. 概述

将 Qt Creator 底部 Terminal 以**最小单终端**形式移植到右侧面板（RightPane），实现"打开 → 使用 → 关闭"的最简交互。

### 1.1 目标

- 在 Qt Creator 右侧面板嵌入一个独立 Terminal Widget
- 支持默认 Shell 自动启动
- 右侧面板可折叠/展开
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
| `Terminal::TerminalWidget` | `terminal/terminalwidget.h` | 终端控件本体，独立 QWidget |
| `Utils::Terminal::OpenTerminalParameters` | `utils/terminalhooks.h` | 终端创建参数 |
| `Utils::Terminal::defaultShellForDevice` | `utils/terminalhooks.h` | 获取系统默认 Shell |
| `Core::RightPaneWidget` | `coreplugin/rightpane.h` | 右侧面板容器，`setWidget()` 嵌入任意 QWidget |
| `Core::RightPanePlaceHolder` | `coreplugin/rightpane.h` | 注册到 Mode，控制面板可见性 |
| `Core::ICore` | `coreplugin/icore.h` | 获取 `RightPaneWidget::instance()` |

### 2.1 TerminalWidget 关键签名

```cpp
namespace Terminal {

class TerminalWidget : public Core::SearchableTerminal  // → TerminalView → QWidget
{
public:
    // 构造即可用，openParameters 为空时使用系统默认 Shell
    TerminalWidget(QWidget *parent = nullptr,
                   const Utils::Terminal::OpenTerminalParameters &openParameters = {});

    void closeTerminal();
    void restart(const Utils::Terminal::OpenTerminalParameters &openParameters);

    QString title() const;
    Utils::FilePath cwd() const;
    QProcess::ProcessState processState() const;

signals:
    void started(qint64 pid);
    void finished(int exitCode);
    void cwdChanged(const Utils::FilePath &cwd);
    void titleChanged();
};
}
```

### 2.2 OpenTerminalParameters 关键字段

```cpp
namespace Utils::Terminal {

struct OpenTerminalParameters {
    std::optional<CommandLine> shellCommand;       // 指定 Shell，空则用系统默认
    std::optional<FilePath> workingDirectory;       // 工作目录，空则用项目根目录
    std::optional<Environment> environment;         // 环境变量
    ExitBehavior m_exitBehavior{ExitBehavior::Close}; // 退出行为
    std::optional<Id> identifier;                   // 终端标识符
};

// ExitBehavior 枚举：
enum class ExitBehavior { Close, Restart, Keep };

}
```

### 2.3 RightPaneWidget 关键签名

```cpp
namespace Core {

class RightPaneWidget : public QWidget
{
public:
    static RightPaneWidget *instance();       // 全局单例
    void setWidget(QWidget *widget);          // 嵌入任意 QWidget
    QWidget *widget() const;                  // 当前嵌入的 widget
    bool isShown() const;                    // 是否展开
    void setShown(bool b);                   // 折叠/展开
};

class RightPanePlaceHolder : public QWidget
{
public:
    explicit RightPanePlaceHolder(Utils::Id mode, QWidget *parent = nullptr);
    // mode: 对应 IMode 的 ID，如 Constants::MODE_EDIT
    // 当切换到该 mode 时自动关联到 RightPaneWidget
};

}
```

---

## 3. 架构设计

### 3.1 类图

```
┌──────────────────────────────────────┐
│  SideBarTerminalPlugin (IPlugin)     │
│  + initialize()                      │
│  + extensionsInitialized()           │
│  - m_sidebarTerminal*               │
└──────────┬───────────────────────────┘
           │ 创建并持有
           ▼
┌──────────────────────────────────────┐
│  SideBarTerminal (QObject)           │
│  + toggleTerminal()                  │
│  + isTerminalVisible()               │
│  - m_terminal: TerminalWidget*       │
│  - m_isCreated: bool                 │
└──────────┬───────────────────────────┘
           │ 创建并嵌入
           ▼
┌──────────────────────────────────────┐
│  TerminalWidget (SearchableTerminal) │ ← Qt Creator SDK 提供
│  完整的终端控件                       │
└──────────────────────────────────────┘
           │ 通过 setWidget() 放入
           ▼
┌──────────────────────────────────────┐
│  RightPaneWidget (单例)              │ ← Qt Creator SDK 提供
│  Qt Creator 右侧面板容器              │
└──────────────────────────────────────┘
```

### 3.2 生命周期

```
插件加载
  → initialize()
    → 创建 SideBarTerminal（不立即创建 TerminalWidget）
    → 注册 RightPanePlaceHolder(Constants::MODE_EDIT)
    → 注册菜单项 Toggle Terminal

用户点击菜单 / 快捷键
  → SideBarTerminal::toggleTerminal()
    → 首次调用：创建 TerminalWidget → RightPaneWidget::setWidget()
    → 调用 RightPaneWidget::setShown(true/false)

用户关闭 Qt Creator
  → aboutToShutdown()
    → 关闭终端进程
    → 清理资源
```

---

## 4. 详细实现方案

### 4.1 插件骨架（CMakeLists.txt）

```cmake
project(QtSideBarTerminal LANGUAGES CXX)

find_package(QtCreator REQUIRED COMPONENTS Core)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

add_qtc_plugin(QtSideBarTerminal
  PLUGIN_DEPENDS
    QtCreator::Core
    QtCreator::Terminal        # Link against Terminal plugin
  DEPENDS
    Qt::Widgets
    QtCreator::ExtensionSystem
    QtCreator::Utils
  SOURCES
    sidebarterminalplugin.cpp
    sidebarterminal.cpp
)
```

### 4.2 SideBarTerminal 类

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

    Terminal::TerminalWidget *m_terminal = nullptr;
    bool m_isCreated = false;
};
```

### 4.3 创建终端流程

```cpp
void SideBarTerminal::createTerminal()
{
    if (m_isCreated)
        return;

    // 获取系统默认 Shell
    const auto shellPath = Utils::Terminal::defaultShellForDevice(
        Utils::FilePath::fromString(QDir::rootPath()));

    // 构建启动参数
    Utils::Terminal::OpenTerminalParameters params;
    if (shellPath) {
        params.shellCommand = Utils::CommandLine(*shellPath);
    }
    params.m_exitBehavior = Utils::Terminal::ExitBehavior::Close;

    // 创建 TerminalWidget（默认参数自动使用系统 Shell）
    m_terminal = new Terminal::TerminalWidget(nullptr, params);

    // 信号连接
    connect(m_terminal, &Terminal::TerminalWidget::started,
            this, [](qint64 pid) {
        // 日志：终端已启动，PID=xxx
    });
    connect(m_terminal, &Terminal::TerminalWidget::finished,
            this, [this](int exitCode) {
        // 终端退出后关闭右侧面板
        Core::RightPaneWidget::instance()->setShown(false);
    });

    // 嵌入到右侧面板
    Core::RightPaneWidget::instance()->setWidget(m_terminal);

    m_isCreated = true;
}
```

### 4.4 插件初始化

```cpp
void SideBarTerminalPlugin::initialize()
{
    m_sidebarTerminal = new SideBarTerminal(this);

    // 在编辑模式注册 RightPanePlaceHolder，确保右侧面板可用
    new Core::RightPanePlaceHolder(Core::Constants::MODE_EDIT);

    // 注册菜单项 / 快捷键
    // 通过 ActionBuilder 添加 Toggle Terminal 操作
}

void SideBarTerminalPlugin::extensionsInitialized()
{
    // 所有插件就绪后的初始化（可选）
}
```

### 4.5 菜单/快捷键注册

```cpp
// 注册动作
auto action = new QAction(Tr::tr("Toggle Sidebar Terminal"), this);
connect(action, &QAction::triggered, m_sidebarTerminal,
        &SideBarTerminal::toggleTerminal);

Core::ActionManager::registerAction(
    action, "QtSideBarTerminal.Toggle",
    Core::Context(Core::Constants::C_GLOBAL));
```

---

## 5. 关键决策与约束

### 5.1 单终端 vs 多终端

**选择：单终端**

| 因素 | 影响 |
|------|------|
| 右侧面板宽度有限（通常 300-400px） | 不适合 TabBar + 终端（空间不足） |
| TerminalPane 已有完整多标签方案 | 不需重复造轮子 |
| 最小系统目标 | 快速验证可行性 |

### 5.2 创建时机

**选择：延迟创建（首次 show 时创建）**

| 因素 | 影响 |
|------|------|
| 启动性能 | 避免 Qt Creator 启动时就 fork 终端进程 |
| 资源占用 | 不使用时零开销 |

### 5.3 工作目录

**选择：用户家目录（默认），可后续扩展为项目根目录**

```cpp
// 暂不传入 workingDirectory，TerminalWidget 默认使用 QDir::homePath()
// 后续可扩展为：params.workingDirectory = ProjectTree::currentProject()->projectDirectory();
```

### 5.4 关闭行为

**选择：`ExitBehavior::Close`**

终端退出后不自动重启，仅关闭右侧面板。

---

## 6. 文件清单

```
QtSideBarTerminal/
├── CMakeLists.txt
├── QtSideBarTerminal.json.in
├── sidebarterminalplugin.h        # IPlugin 实现
├── sidebarterminalplugin.cpp
├── sidebarterminal.h              # SideBarTerminal 管理类
├── sidebarterminal.cpp
├── sidebarterminalconstants.h     # ID 常量
├── sidebarterminaltr.h            # 翻译工具类
└── Docs/
    └── sidebarterminal-design.md  # 本文档
```

---

## 7. 风险与限制

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| `TerminalWidget` 在窄侧边栏可能布局异常 | 终端宽度不足，命令行换行体验差 | 用户可拖拽调整面板宽度 |
| `RightPaneWidget::setWidget()` 替换已有内容 | 其他插件可能也用右侧面板 | 通过 PlaceHolder 的 mode 机制隔离（仅在编辑模式显示） |
| `Terminal::defaultShellForDevice` 在 Windows 返回空 | 无法获取默认 Shell | 回退到 `cmd.exe` 或 `pwsh.exe` |
| 终端进程未清理 | Qt Creator 关闭时残留进程 | `aboutToShutdown()` 中显式调用 `closeTerminal()` |
| 非编辑模式下右侧面板不可用 | Toggle 无需响应 | PlaceHolder 绑定到 `MODE_EDIT` 即可 |

---

## 8. 后续扩展可能

1. **工作目录联动** — 自动使用 `%{CurrentProject:Path}` 作为工作目录
2. **工具栏按钮** — 在侧边栏顶部添加"新建终端"、"关闭终端"按钮
3. **Shell 选择** — 添加右键菜单选择 cmd/powershell/bash
4. **快捷键传递** — Ctrl+C / Ctrl+D 等键盘快捷键正确传递到终端
5. **拖拽调整** — 支持在底部/侧边栏之间拖拽切换位置
