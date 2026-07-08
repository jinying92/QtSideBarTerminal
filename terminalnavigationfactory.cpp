#include "terminalnavigationfactory.h"
#include "simpleterminal.h"
#include "qtsidebarterminaltr.h"

#include <utils/filepath.h>
#include <utils/terminalhooks.h>

#include <QDir>

namespace QtSideBarTerminal::Internal {

/**
 * @brief 获取系统默认 Shell 路径
 *
 * 通过 Utils::Terminal::defaultShellForDevice 查询设备默认 Shell，
 * 失败时按平台回退到 cmd.exe / /bin/sh。
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

TerminalNavigationFactory::TerminalNavigationFactory()
{
    setDisplayName(Tr::tr("Terminal"));
    setId("QtSideBarTerminal.Terminal");
    setPriority(0);
}

Core::NavigationView TerminalNavigationFactory::createWidget()
{
    auto widget = new SimpleTerminalWidget(nullptr, defaultShellPath());
    return {widget, {}};
}

} // namespace QtSideBarTerminal::Internal
