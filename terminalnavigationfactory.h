#pragma once

#include <coreplugin/inavigationwidgetfactory.h>

namespace QtSideBarTerminal::Internal {

/**
 * @brief 将终端作为选项卡注册到 Qt Creator 右侧栏
 *
 * 实现 Core::INavigationWidgetFactory：构造时自动加入全局工厂列表，
 * Qt Creator 的 NavigationWidget 会把它作为"项目/文件系统/大纲"旁边的
 * 一个新选项卡。createWidget() 返回一个 SimpleTerminalWidget（自动启动 Shell）。
 */
class TerminalNavigationFactory : public Core::INavigationWidgetFactory
{
    Q_OBJECT

public:
    TerminalNavigationFactory();
    ~TerminalNavigationFactory() override = default;

    Core::NavigationView createWidget() override;
};

} // namespace QtSideBarTerminal::Internal
