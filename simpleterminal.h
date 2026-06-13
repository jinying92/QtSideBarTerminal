#pragma once

#include <solutions/terminal/terminalview.h>

#include <QProcess>

namespace QtSideBarTerminal::Internal {

/**
 * @brief SimpleTerminalWidget - 基于 TerminalLib 的简易终端控件
 *
 * 继承 TerminalView（来自 TerminalLib 库，可链接），覆盖 writeToPty() 
 * 桥接到 QProcess，实现 Shell 的输入输出。不再依赖 Terminal 插件的 
 * TerminalWidget（该符号在 Windows 上未导出）。
 *
 * 数据流：
 *   TerminalView ←→ TerminalSurface  ←→ QProcess (Shell)
 *      渲染        libvterm 仿真        writeToPty() / dataFromPty()
 *
 * TerminalView 内部自动处理：
 *   - ANSI 转义码解析（通过 libvterm）
 *   - 键盘/鼠标事件转发到 TerminalSurface
 *   - 终端内容渲染（颜色、光标、选择等）
 *   - 滚动条管理、复制粘贴
 *
 * 平台注意：
 *   Windows: QProcess 使用管道通信，cmd.exe 检测到非交互式 stdin
 *   后不输出提示符。考虑后期引入 winpty 或 ConPTY。
 *   Linux: PTY 自动获得完整终端交互。
 */
class SimpleTerminalWidget : public TerminalSolution::TerminalView
{
    Q_OBJECT

public:
    /// @param parent  父控件
    /// @param shell   要启动的 Shell 可执行程序路径
    explicit SimpleTerminalWidget(QWidget *parent,
                                  const QString &shell);

    ~SimpleTerminalWidget() override;

    /// 关闭终端进程
    void closeTerminal();

    /// 获取底层进程状态
    QProcess::ProcessState processState() const;

signals:
    /// 终端进程已启动（首次或重启后）
    void started(qint64 pid);

    /// 终端进程已退出
    void finished(int exitCode);

protected:
    /// @reimp TerminalView::writeToPty
    /// TerminalSurface 需要向 PTY 写入数据时调用，此处转发到 QProcess
    qint64 writeToPty(const QByteArray &data) override;

private:
    /// 启动 Shell 进程并连接信号
    void setupProcess(const QString &shell);

    QProcess *m_process = nullptr;
};

} // namespace QtSideBarTerminal::Internal
