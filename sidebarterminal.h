#pragma once

#include <QObject>

namespace Terminal {
class TerminalWidget;
}

namespace Core {
class RightPaneWidget;
}

namespace Utils::Terminal {
struct OpenTerminalParameters;
}

namespace QtSideBarTerminal::Internal {

/**
 * @brief SideBarTerminal 管理类
 *
 * 负责终端(TerminalWidget)的完整生命周期：
 * - 延迟创建：首次 toggle 时才创建终端进程，避免启动时 fork
 * - 嵌入右侧面板：通过 RightPaneWidget::setWidget() 将终端放入 Qt Creator 右侧面板
 * - 自动关闭：终端进程退出时自动收起右侧面板
 */
class SideBarTerminal : public QObject
{
    Q_OBJECT

public:
    explicit SideBarTerminal(QObject *parent = nullptr);
    ~SideBarTerminal() override;

    /// 切换终端显示/隐藏，首次调用时创建终端
    void toggleTerminal();

    /// 返回终端当前是否可见
    bool isTerminalVisible() const;

private:
    /// 创建 TerminalWidget 并嵌入 RightPaneWidget，仅首次调用有效
    void createTerminal();

    /// 销毁终端控件并清理资源
    void destroyTerminal();

    Terminal::TerminalWidget *m_terminal = nullptr;
    bool m_isCreated = false;
};

} // namespace QtSideBarTerminal::Internal
