#include "sidebarterminal.h"
#include "simpleterminal.h"

#include <coreplugin/icore.h>

#include <utils/filepath.h>
#include <utils/terminalhooks.h>

#include <QDockWidget>
#include <QMainWindow>
#include <QDir>
#include <QProcess>

namespace QtSideBarTerminal::Internal {

/**
 * @brief 获取系统默认 Shell 路径
 */
static QString defaultShellPath()
{
    const auto shellPath = Utils::Terminal::defaultShellForDevice(
        Utils::FilePath::fromString(QDir::rootPath()));
    if (shellPath)
        return shellPath->toString();

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

/**
 * @brief 检查终端是否可见
 *
 * 通过 DockWidget 的可见性和小部件的存在性判断。
 */
bool SideBarTerminal::isTerminalVisible() const
{
    return m_terminal
           && m_terminal->parentWidget()
           && m_terminal->parentWidget()->isVisible();
}

/**
 * @brief 切换终端显示/隐藏
 *
 * 首次调用时创建终端和 DockWidget。之后每次调用切换 DockWidget 的可见性。
 * 如果终端进程已退出，重新创建终端。
 */
void SideBarTerminal::toggleTerminal()
{
    if (!m_isCreated) {
        createTerminal();
        return;
    }

    // 如果终端进程已退出（非运行状态），重新创建
    if (m_terminal && m_terminal->processState() != QProcess::Running) {
        destroyTerminal();
        createTerminal();
        return;
    }

    // 切换 DockWidget 可见性
    if (m_terminal && m_terminal->parentWidget()) {
        QDockWidget *dock = qobject_cast<QDockWidget *>(m_terminal->parentWidget());
        if (dock) {
            dock->setVisible(!dock->isVisible());
            if (dock->isVisible())
                m_terminal->setFocus();
        }
    }
}

/**
 * @brief 创建终端控件和 DockWidget
 *
 * 使用系统默认 Shell 创建 SimpleTerminalWidget，放入 QDockWidget
 * 并添加到主窗口的右侧 Dock 区域。
 */
void SideBarTerminal::createTerminal()
{
    if (m_isCreated)
        return;

    QMainWindow *mainWindow = Core::ICore::mainWindow();
    if (!mainWindow)
        return;

    const QString shell = defaultShellPath();

    // 创建终端控件
    m_terminal = new SimpleTerminalWidget(nullptr, shell);

    // 终端启动成功
    QObject::connect(m_terminal, &SimpleTerminalWidget::started,
                     this, [](qint64 pid) {
        Q_UNUSED(pid)
    });

    // 终端进程退出时自动隐藏 DockWidget
    QObject::connect(m_terminal, &SimpleTerminalWidget::finished,
                     this, [this](int exitCode) {
        Q_UNUSED(exitCode)
        QDockWidget *dock = m_terminal
            ? qobject_cast<QDockWidget *>(m_terminal->parentWidget())
            : nullptr;
        if (dock && dock->isVisible()) {
            dock->hide();
        }
    });

    // 创建 DockWidget 并嵌入终端
    auto *dock = new QDockWidget(tr("Terminal"), mainWindow);
    dock->setWidget(m_terminal);
    dock->setFeatures(QDockWidget::DockWidgetMovable
                      | QDockWidget::DockWidgetClosable);
    dock->setAllowedAreas(Qt::RightDockWidgetArea
                          | Qt::LeftDockWidgetArea
                          | Qt::BottomDockWidgetArea);
    dock->setMinimumWidth(200);

    // 添加到主窗口右侧
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->show();
    m_terminal->setFocus();

    m_isCreated = true;
}

/**
 * @brief 销毁终端控件和 DockWidget
 */
void SideBarTerminal::destroyTerminal()
{
    if (m_terminal) {
        // 先获取并销毁 DockWidget
        QDockWidget *dock = qobject_cast<QDockWidget *>(m_terminal->parentWidget());
        if (dock) {
            dock->hide();
            dock->deleteLater();
        }

        m_terminal->closeTerminal();
        m_terminal->deleteLater();
        m_terminal = nullptr;
    }
    m_isCreated = false;
}

} // namespace QtSideBarTerminal::Internal
