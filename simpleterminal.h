#pragma once

#include <solutions/terminal/terminalview.h>

#include <utils/processinterface.h> // Utils::Pty::Data
#include <utils/qtcprocess.h>        // Utils::Process

#include <QColor>
#include <QProcess>

#include <array>

namespace QtSideBarTerminal::Internal {

/**
 * @brief SimpleTerminalWidget - 基于 TerminalLib 的简易终端控件
 *
 * 继承 TerminalView（来自可链接的 TerminalLib 库），重写 writeToPty() /
 * resizePty() 桥接到 Utils::Process。Utils::Process 在调用 setPtyData()
 * 后会启用伪终端：
 *   - Windows: Utils 内部使用 ConPTY，Shell 检测到 TTY 后进入交互模式
 *   - Linux:   使用系统 PTY
 * 从而获得完整的终端交互（提示符、回显、输入均正常）。
 *
 * 数据流：
 *   键盘/鼠标 → TerminalSurface → writeToPty()  → Utils::Process(PTY) → Shell
 *   Shell 输出 → Utils::Process → readAllRawStandardOutput() → dataFromPty()
 *
 * TerminalView 内部自动处理：
 *   - ANSI 转义码解析（libvterm）
 *   - 键盘/鼠标事件转发到 TerminalSurface
 *   - 内容渲染（颜色、光标、选择）、滚动条、复制粘贴
 *   - 尺寸变化通过 applySizeChange() → resizePty() 下发
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

    /// 关闭底层 Shell 进程
    void closeTerminal();

    /// 获取进程状态
    QProcess::ProcessState processState() const;

signals:
    /// 终端进程已启动（首次或重启后）
    void started(qint64 pid);

    /// 终端进程已退出
    void finished(int exitCode);

protected:
    /// @reimp 终端按键数据 → 写入 PTY
    qint64 writeToPty(const QByteArray &data) override;

    /// @reimp 终端尺寸变化（行列数）→ 通知 PTY 调整
    bool resizePty(QSize newSize) override;

    /// @reimp 延迟启动 Shell（到首次显示时，确保项目已加载）
    void showEvent(QShowEvent *event) override;

    /// @reimp 将选中文本写入系统剪贴板
    void setClipboard(const QString &text) override;

    /// @reimp 将终端文本映射为可点击链接（URL/文件路径）
    std::optional<Link> toLink(const QString &text) override;

    /// @reimp 点击链接后的操作（浏览器打开 URL / Qt Creator 打开文件）
    void linkActivated(const Link &link) override;

    /// @reimp 拦截事件，防止 Qt Creator 全局快捷键吞掉终端按键
    bool event(QEvent *event) override;

    /// @reimp 鼠标按键（日志右键 selection 状态，用于调试复制）
    void mousePressEvent(QMouseEvent *event) override;

    /// @reimp 鼠标悬停时始终检测超链接（不依赖 Ctrl 键）
    void mouseMoveEvent(QMouseEvent *event) override;

    /// @reimp Ctrl+滚轮缩放字体
    void wheelEvent(QWheelEvent *event) override;

    /// @reimp 输入法光标位置查询（中文候选字定位）
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    /// 以 PTY 模式启动 Shell 进程
    void setupPty();

    /// 设置终端颜色方案（默认深色终端配色）
    void setupDefaultColors();

    /// Shell 可执行程序路径（构造时保存，显示时延迟启动）
    QString m_shellPath;

    /// 是否已被强制终止（防止 shutdown 中 showEvent 误重启）
    bool m_processWasKilled = false;

    Utils::Process *m_process = nullptr;

    /// 强制终止并销毁本终端进程，同时清理 ConPTY 句柄
    void forceKillProcess();

public:
    /// 终止所有活跃的终端进程（供插件 aboutToShutdown 调用）
    static void killAllProcesses();

    /// 向首个活跃终端发送文本（供编辑器右键菜单调用）
    static void sendToActiveTerminal(const QString &text);
};

} // namespace QtSideBarTerminal::Internal
