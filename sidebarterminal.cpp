#include "sidebarterminal.h"
#include "simpleterminal.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>
#include <coreplugin/rightpane.h>

#include <utils/commandline.h>
#include <utils/filepath.h>
#include <utils/terminalhooks.h>

#include <QDir>
#include <QProcess>

namespace QtSideBarTerminal::Internal {

/**
 * @brief 获取系统默认 Shell 路径
 *
 * 在 Windows 上返回 cmd.exe，Linux 上返回用户默认 shell。
 */
static QString defaultShellPath()
{
    const auto shellPath = Utils::Terminal::defaultShellForDevice(
        Utils::FilePath::fromString(QDir::rootPath()));
    if (shellPath)
        return shellPath->toString();
    // 回退：Windows 用 cmd.exe，Linux 用 /bin/sh
#ifdef Q_OS_WIN
    return QStringLiteral("cmd.exe");
#else
    return QStringLiteral("/bin/sh");
#endif
}

SideBarTerminal::SideBarTerminal(QObject *parent)
    : QObject(parent)
{
}

SideBarTerminal::~SideBarTerminal()
{
    destroyTerminal();
}

bool SideBarTerminal::isTerminalVisible() const
{
    return Core::RightPaneWidget::instance()->isShown()
           && Core::RightPaneWidget::instance()->widget() == m_terminal;
}

/**
 * @brief 切换终端显示/隐藏
 *
 * 首次调用时创建终端控件。之后每次调用切换右侧面板的展开/折叠状态。
 * 如果终端进程已退出，重新创建终端。
 */
void SideBarTerminal::toggleTerminal()
{
    if (!m_isCreated) {
        createTerminal();
        Core::RightPaneWidget::instance()->setShown(true);
        if (m_terminal)
            m_terminal->setFocus();
        return;
    }

    // 如果终端进程已退出（非运行状态），重新创建
    if (m_terminal && m_terminal->processState() != QProcess::Running) {
        destroyTerminal();
        createTerminal();
        Core::RightPaneWidget::instance()->setShown(true);
        if (m_terminal)
            m_terminal->setFocus();
        return;
    }

    // 切换面板可见性
    bool visible = Core::RightPaneWidget::instance()->isShown()
                   && Core::RightPaneWidget::instance()->widget() == m_terminal;
    Core::RightPaneWidget::instance()->setShown(!visible);
}

/**
 * @brief 创建终端控件
 *
 * 使用系统默认 Shell 创建 SimpleTerminalWidget，嵌入到 RightPaneWidget。
 * 连接 started 和 finished 信号以管理面板状态。
 */
void SideBarTerminal::createTerminal()
{
    if (m_isCreated)
        return;

    const QString shell = defaultShellPath();

    // 创建简易终端控件（继承 TerminalView 并桥接 QProcess）
    m_terminal = new SimpleTerminalWidget(nullptr, shell);
    m_terminal->setFocus();

    // 终端启动成功
    QObject::connect(m_terminal, &SimpleTerminalWidget::started,
                     this, [](qint64 pid) {
        Q_UNUSED(pid)
    });

    // 终端进程退出时自动收起右侧面板
    QObject::connect(m_terminal, &SimpleTerminalWidget::finished,
                     this, [this](int exitCode) {
        Q_UNUSED(exitCode)
        if (Core::RightPaneWidget::instance()->isShown()) {
            Core::RightPaneWidget::instance()->setShown(false);
        }
    });

    // 嵌入到 Qt Creator 右侧面板
    Core::RightPaneWidget::instance()->setWidget(m_terminal);
    m_isCreated = true;
}

/**
 * @brief 销毁终端控件
 *
 * 关闭终端进程并清理 Widget。
 */
void SideBarTerminal::destroyTerminal()
{
    if (m_terminal) {
        m_terminal->closeTerminal();
        m_terminal->deleteLater();
        m_terminal = nullptr;
    }
    m_isCreated = false;
}

} // namespace QtSideBarTerminal::Internal
