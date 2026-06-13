#include "simpleterminal.h"

#include <utils/terminalhooks.h>

namespace QtSideBarTerminal::Internal {

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
 */
void SimpleTerminalWidget::setupProcess(const QString &shell)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    // Shell 输出 → TerminalSurface::dataFromPty()
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &SimpleTerminalWidget::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &SimpleTerminalWidget::onReadyReadStderr);

    // Shell 退出 → 发射 finished 信号
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        emit finished(exitCode);
    });

    m_process->start(shell);

    if (m_process->state() == QProcess::Running)
        emit started(m_process->processId());
}

/**
 * @brief 关闭终端进程
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
 * @brief 重写：TerminalView/TerminalSurface 需要向 PTY 写入键盘输入等数据时调用
 */
qint64 SimpleTerminalWidget::writeToPty(const QByteArray &data)
{
    if (!m_process || m_process->state() != QProcess::Running)
        return -1;
    return m_process->write(data);
}

void SimpleTerminalWidget::onReadyReadStdout()
{
    surface()->dataFromPty(m_process->readAllStandardOutput());
}

void SimpleTerminalWidget::onReadyReadStderr()
{
    surface()->dataFromPty(m_process->readAllStandardError());
}

} // namespace QtSideBarTerminal::Internal
