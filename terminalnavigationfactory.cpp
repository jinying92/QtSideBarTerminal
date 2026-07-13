#include "terminalnavigationfactory.h"
#include "simpleterminal.h"
#include "qtsidebarterminaltr.h"

#include <utils/filepath.h>
#include <utils/terminalhooks.h>

#include <QDir>
#include <QFileInfo>

namespace QtSideBarTerminal::Internal {

/**
 * @brief 获取要启动的 Shell 路径
 *
 * Windows：优先级 PowerShell 7 → Windows PowerShell → cmd.exe。
 * PowerShell 7 路径：C:\Program Files\PowerShell\7\pwsh.exe
 */
static QString defaultShellPath()
{
#ifdef Q_OS_WIN
    // PowerShell 7（pwsh.exe）
    if (QFileInfo::exists(
            QStringLiteral("C:/Program Files/PowerShell/7/pwsh.exe")))
        return QStringLiteral("pwsh.exe");
    // Windows PowerShell（powershell.exe）
    if (QFileInfo::exists(
            QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")))
        return QStringLiteral("powershell.exe");
    return QStringLiteral("cmd.exe");
#else
    const auto shellPath = Utils::Terminal::defaultShellForDevice(
        Utils::FilePath::fromString(QDir::rootPath()));
    if (shellPath)
        return shellPath->toString();
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
