#include "qtsidebarterminaltr.h"
#include "simpleterminal.h"
#include "terminalnavigationfactory.h"

#include <extensionsystem/iplugin.h>

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>

#include <texteditor/texteditor.h>
#include <texteditor/texteditorconstants.h>

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
        registerSendToTerminalAction();
    }

    /// 注册"发送到终端"到编辑器右键菜单
    void registerSendToTerminalAction()
    {
        const Utils::Id actionId("QtSideBarTerminal.SendToTerminal");
        Core::ActionBuilder builder(this, actionId);
        builder.setText(Tr::tr("Send to Terminal"))
            .setContext(Core::Context(TextEditor::Constants::C_TEXTEDITOR))
            .addToContainer(Utils::Id(TextEditor::Constants::M_STANDARDCONTEXTMENU))
            .addOnTriggered(this, [] {
                auto *editor = Core::EditorManager::currentEditor();
                if (!editor)
                    return;
                // TextEditorWidget 不是 IEditor::widget() 的直接返回结果
                auto *baseEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
                if (!baseEditor)
                    return;
                const QString text = baseEditor->editorWidget()->textCursor().selectedText();
                if (!text.isEmpty())
                    SimpleTerminalWidget::sendToActiveTerminal(text);
            });
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
