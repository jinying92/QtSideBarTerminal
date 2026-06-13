#include "simpleterminal.h"

#include <utils/terminalhooks.h>

#include <QOperatingSystemVersion>

namespace QtSideBarTerminal::Internal {

/**
 * @brief 构建 Windows Shell 命令行参数
 *
 * Windows 上用 QProcess 的管道启动 cmd.exe 时，cmd.exe 会检测到
 * stdin 不是控制台，进入批处理模式（不显示提示符、不回显输入）。
 * 使用 powershell.exe 会有类似问题，但可以用 -NoExit 强制交互。
 *
 * Linux 上通过 PTY 自动获得完整的终端交互。
 */
static QStringList shellArguments(const QString &shell)
{
    QStringList args;
#ifdef Q_OS_WIN
    if (shell.contains("cmd", Qt::CaseInsensitive)) {
        args << "/Q";    // 关闭回显
        // /K 是默认行为，不额外追加
    } else if (shell.contains("powershell", Qt::CaseInsensitive)) {
        args << "-NoExit" << "-Command" << "Write-Host ''";
    }
#else
    Q_UNUSED(shell)
#endif
    return args;
}

SimpleTerminalWidget::SimpleTerminalWidget(QWidget *parent,
                                           const QString &shell)
    : TerminalSolution::TerminalView(parent)
{
    setMinimumSize(200, 100);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    setupProcess(shell);
}

SimpleTerminalWidget::~SimpleTerminalWidget()
{
    closeTerminal();
}

/**
 * @brief 启动 Shell 进程并连接 IO 信号
 *
 * 合并 stdout/stderr(MergedChannels)，因为终端中两者都应显示。
 * 使用 QProcess::started 信号确保进程完全就绪后再发射
 * SimpleTerminalWidget::started。
 */
void SimpleTerminalWidget::setupProcess(const QString &shell)
{
    m_process = new QProcess(this);
    // MergedChannels: 将 stderr 合并到 stdout 统一读取
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    // Shell 标准输出 → TerminalSurface
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, [this]() {
        surface()->dataFromPty(m_process->readAllStandardOutput());
    });

    // Shell 进程真正启动后发射信号
    connect(m_process, &QProcess::started,
            this, [this]() {
        emit started(m_process->processId());
    });

    // Shell 退出
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        emit finished(exitCode);
    });

    m_process->start(shell, shellArguments(shell));
}

/**
 * @brief 强制关闭
 */
void SimpleTerminalWidget::closeTerminal()
{
    if (!m_process)
        return;

    m_process->terminate();
    if (!m_process->waitForFinished(3000))
        m_process->kill();
    m_process->deleteLater();
    m_process = nullptr;
}

QProcess::ProcessState SimpleTerminalWidget::processState() const
{
    return m_process ? m_process->state() : QProcess::NotRunning;
}

/**
 * @brief 重写：键盘输入/终端控制 → QProcess stdin
 *
 * TerminalSurface 处理用户按键后调用此函数将数据写入 PTY，
 * 此处重写为写入 QProcess 的 stdin 管道。
 */
qint64 SimpleTerminalWidget::writeToPty(const QByteArray &data)
{
    if (!m_process || m_process->state() != QProcess::Running)
        return -1;
    return m_process->write(data);
}

} // namespace QtSideBarTerminal::Internal
