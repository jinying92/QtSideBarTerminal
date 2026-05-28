#include "qtsidebarterminalconstants.h"
#include "qtsidebarterminaltr.h"
#include "sidebarterminal.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/rightpane.h>

#include <extensionsystem/iplugin.h>

#include <QAction>
#include <QMainWindow>
#include <QMenu>

using namespace Core;

namespace QtSideBarTerminal::Internal {

/**
 * @brief QtSideBarTerminalPlugin - Qt Creator 侧边栏终端插件
 *
 * 将终端(TerminalWidget)嵌入到 Qt Creator 右侧面板(RightPaneWidget)中，
 * 实现"打开 -> 使用 -> 关闭"的最简单终端交互。
 *
 * 生命周期：
 *   initialize()          — 创建 SideBarTerminal，注册 RightPanePlaceHolder 和菜单
 *   extensionsInitialized() — 所有依赖插件就绪（无需额外操作）
 *   aboutToShutdown()     — 销毁终端，清理资源
 */
class QtSideBarTerminalPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "QtSideBarTerminal.json")

public:
    QtSideBarTerminalPlugin() = default;

    ~QtSideBarTerminalPlugin() final = default;

    /**
     * @brief 插件初始化
     *
     * 1. 创建 SideBarTerminal 管理对象
     * 2. 注册 RightPanePlaceHolder(MODE_EDIT)，确保编辑模式下右侧面板可用
     * 3. 在工具菜单注册 Toggle Sidebar Terminal 动作
     */
    void initialize() final
    {
        m_sidebarTerminal = new SideBarTerminal(this);

        // 在编辑模式注册 RightPanePlaceHolder，确保右侧面板可用
        // 仅在切换到编辑模式时面板才关联到 RightPaneWidget
        new RightPanePlaceHolder(Core::Constants::MODE_EDIT);

        // 注册工具菜单项
        ActionContainer *toolsMenu = ActionManager::actionContainer(Core::Constants::M_TOOLS);

        ActionBuilder(this, Constants::TOGGLE_ACTION_ID)
            .setText(Tr::tr("Toggle Sidebar Terminal"))
            .setDefaultKeySequence(Tr::tr("Ctrl+Alt+T"))
            .addToContainer(Core::Constants::M_TOOLS)
            .addOnTriggered(this, &QtSideBarTerminalPlugin::toggleTerminal);
    }

    void extensionsInitialized() final
    {
        // 所有依赖插件已就绪，无需额外操作
    }

    /**
     * @brief 插件关闭前清理
     *
     * 删除 SideBarTerminal（析构函数中自动关闭终端进程）。
     */
    ShutdownFlag aboutToShutdown() final
    {
        delete m_sidebarTerminal;
        m_sidebarTerminal = nullptr;
        return SynchronousShutdown;
    }

private:
    /// 菜单动作触发：切换终端显示/隐藏
    void toggleTerminal()
    {
        if (m_sidebarTerminal) {
            m_sidebarTerminal->toggleTerminal();
        }
    }

    SideBarTerminal *m_sidebarTerminal = nullptr;
};

} // namespace QtSideBarTerminal::Internal

#include <qtsidebarterminal.moc>
