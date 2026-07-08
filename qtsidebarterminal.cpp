#include "qtsidebarterminaltr.h"
#include "simpleterminal.h"
#include "terminalnavigationfactory.h"

#include <extensionsystem/iplugin.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace QtSideBarTerminal::Internal {

/// 写日志到临时目录
static void pluginLog(const QString &msg)
{
    QFile file(QDir::tempPath() + "/QtSideBarTerminal.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " "
            << "[plugin] " << msg << "\n";
    }
}

/**
 * @brief QtSideBarTerminalPlugin - Qt Creator 侧边栏终端插件
 *
 * 通过注册 TerminalNavigationFactory（Core::INavigationWidgetFactory），
 * 将终端作为选项卡嵌入 Qt Creator 右侧栏（与"项目/文件系统/大纲"并列）。
 *
 * 生命周期：
 *   initialize()           — 创建并注册 TerminalNavigationFactory
 *   aboutToShutdown()      — 强制终止所有活跃的 Shell 进程
 */
class QtSideBarTerminalPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "QtSideBarTerminal.json")

public:
    QtSideBarTerminalPlugin() = default;
    ~QtSideBarTerminalPlugin() final = default;

    void initialize() final
    {
        new TerminalNavigationFactory;
    }

    /**
     * @brief 插件关闭前强制终止所有 Shell 进程
     *
     * 在 Qt Creator 关闭时，NavigationWidget 可能不销毁 Terminal widget，
     * 导致 Utils::Process 持有的 ConPTY 句柄未能释放，阻塞进程退出。
     * 此处主动 kill 所有活跃进程，释放句柄。
     */
    ShutdownFlag aboutToShutdown() final
    {
        pluginLog("aboutToShutdown entered");
        SimpleTerminalWidget::killAllProcesses();
        // 刷新挂起事件（如 deferred delete）确保清理完成
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        pluginLog("aboutToShutdown done");
        return SynchronousShutdown;
    }
};

} // namespace QtSideBarTerminal::Internal

#include <qtsidebarterminal.moc>
