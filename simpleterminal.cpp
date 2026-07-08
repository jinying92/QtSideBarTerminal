#include "simpleterminal.h"

#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/terminalhooks.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projecttree.h>

#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QList>
#include <QTextStream>

namespace QtSideBarTerminal::Internal {

/// 写日志到临时目录（QtSideBarTerminal.log），帮助排查崩溃问题
static void logToFile(const QString &msg)
{
    QFile file(QDir::tempPath() + "/QtSideBarTerminal.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " "
            << msg << "\n";
    }
}

/// 所有活跃终端控件的指针列表，供插件 aboutToShutdown 时强制清理
static QList<SimpleTerminalWidget *> s_activeWidgets;

SimpleTerminalWidget::SimpleTerminalWidget(QWidget *parent,
                                           const QString &shell)
    : TerminalSolution::TerminalView(parent)
    , m_shellPath(shell)
{
    logToFile(QString("SimpleTerminalWidget constructor, shell=%1").arg(shell));
    s_activeWidgets.append(this);
    setMinimumSize(200, 100);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    setupDefaultColors();
    // Shell 延迟到 showEvent 启动，确保项目已加载
    logToFile("SimpleTerminalWidget constructor done");
}

SimpleTerminalWidget::~SimpleTerminalWidget()
{
    logToFile("~SimpleTerminalWidget");
    s_activeWidgets.removeOne(this);
    forceKillProcess();
}

/**
}

/**
 * @brief 延迟启动 Shell（首次显示时）
 *
 * Qt Creator 启动时可能先恢复侧栏状态（触发 createWidget），此时项目
 * 尚未加载完成，ProjectTree::currentProject() 返回空值。将 setupPty()
 * 延迟到 showEvent 执行，确保项目已就绪，Shell 的工作目录正确。
 */
void SimpleTerminalWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (!m_process) {
        if (m_processWasKilled) {
            logToFile("showEvent: skipping, process was killed (shutdown in progress)");
        } else {
            logToFile("showEvent: first show, starting process");
            setupPty();
            applySizeChange();
        }
    }
}

/**
 * @brief 拦截事件，防止 Qt Creator 全局快捷键吞掉终端按键
 *
 * ShortcutOverride：阻止 Qt 应用层拦截 ESC 和 Ctrl 组合键，
 *   使其到达 TerminalView 的正常按键处理。
 * KeyPress：Ctrl+退格 直接发送 \x17（删除前一词），
 *   因为 libvterm 对 Ctrl+Backspace 的映射不正确。
 * ContextMenu：阻止 Qt 默认右键菜单干扰 TerminalView 自带的
 *   右键复制/粘贴逻辑。
 */
bool SimpleTerminalWidget::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        // 任一条件成立则拦截：① ESC  ② 带 Ctrl 修饰符的任意按键
        if (keyEvent->key() == Qt::Key_Escape
            || (keyEvent->modifiers() & Qt::ControlModifier)) {
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::KeyPress: {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        // Ctrl+Backspace → 标准"删除前一词"序列
        if (keyEvent->key() == Qt::Key_Backspace
            && (keyEvent->modifiers() & Qt::ControlModifier)) {
            writeToPty(QByteArrayLiteral("\x17"));
            return true;
        }
        break;
    }
    case QEvent::ContextMenu:
        // 阻止默认上下文菜单，让 TerminalView::mousePressEvent 自处理
        event->accept();
        return true;
    default:
        break;
    }
    return TerminalSolution::TerminalView::event(event);
}

/**
 * @brief 以 PTY 模式启动 Shell 进程
 *
 * 关键点：使用 Utils::Process 并调用 setPtyData() 启用伪终端，
 * 而非裸 QProcess 的管道。Windows 上 Utils 内部使用 ConPTY，Shell
 * 检测到自己是 TTY 后才会输出提示符并回显输入（裸管道会导致非交互模式）。
 *
 * 输出以原始字节读取（readAllRawStandardOutput）后喂给 TerminalSurface；
 * 输入由 writeToPty() 通过 writeRaw() 写入。TERM 强制为 xterm-256color，
 * 避免部分发行版默认 "dumb" 导致 clear 等命令异常。
 */
void SimpleTerminalWidget::setupPty()
{
    logToFile(QString("setupPty: starting shell=%1").arg(m_shellPath));
    m_process = new Utils::Process(this);
    // Writer 模式：以原始字节写入，不经过 Qt 的文本编解码
    m_process->setProcessMode(Utils::ProcessMode::Writer);

    // 启用 PTY（Windows 走 ConPTY，Linux 走系统 PTY）
    m_process->setPtyData(Utils::Pty::Data{});

    m_process->setCommand(Utils::CommandLine{Utils::FilePath::fromString(m_shellPath)});

    Utils::Environment env = Utils::Environment::systemEnvironment();
    env.set("TERM", "xterm-256color");
    m_process->setEnvironment(env);

    // 设置起始目录为 Qt Creator 当前项目目录（若有）
    ProjectExplorer::Project *project = ProjectExplorer::ProjectTree::currentProject();
    if (project) {
        const Utils::FilePath projectDir = project->projectDirectory();
        if (projectDir.isDir())
            m_process->setWorkingDirectory(projectDir);
    }

    // Shell 标准输出 → TerminalSurface
    connect(m_process, &Utils::Process::readyReadStandardOutput,
            this, [this]() {
        logToFile("readyReadStandardOutput signal");
        surface()->dataFromPty(m_process->readAllRawStandardOutput());
    });

    // Shell 进程真正启动后发射信号
    connect(m_process, &Utils::Process::started,
            this, [this]() {
        logToFile(QString("Process started, pid=%1").arg(m_process->processId()));
        emit started(m_process->processId());
    });

    // Shell 退出
    connect(m_process, &Utils::Process::done,
            this, [this]() {
        logToFile(QString("Process done, exitCode=%1").arg(m_process->exitCode()));
        emit finished(m_process->exitCode());
    });

    m_process->start();
    logToFile("setupPty: process started");
}

/**
 * @brief 强制关闭 Shell 进程（非阻塞）
 *
 * 关键逻辑：
 * 1. 先断开所有信号连接，防止 done/readyRead 回调访问已释放的成员
 * 2. 发送 terminate 信号请求进程退出
 * 3. 通过 deleteLater 调度异步清理，不阻塞主线程
 *    （Process 析构时会自动 kill 子进程，无需 waitForFinished）
 */
void SimpleTerminalWidget::closeTerminal()
{
    if (!m_process) {
        logToFile("closeTerminal: m_process is null, returning");
        return;
    }

    logToFile("closeTerminal: disconnecting and terminating process");
    // 断开所有信号（防止 done 回调中访问 m_process 空指针）
    m_process->disconnect(this);

    m_process->terminate();
    m_process->deleteLater();
    m_process = nullptr;
    logToFile("closeTerminal: done");
}

QProcess::ProcessState SimpleTerminalWidget::processState() const
{
    return m_process ? m_process->state() : QProcess::NotRunning;
}

/**
 * @brief 设置终端默认颜色方案（深色终端配色）
 *
 * TerminalView 构造时 m_currentColors 为全黑（QColor 默认构造），
 * 导致前景色=背景色=黑色，文字不可见。此处设置标准的深色终端配色，
 * 与 Qt Creator 默认终端主题（TerminalForeground / TerminalBackground）
 * 保持一致。颜色表索引说明：
 *   0-15: ANSI 16 色
 *   16:   WidgetColorIdx::Foreground（前景，文字颜色）
 *   17:   WidgetColorIdx::Background（背景）
 *   18:   WidgetColorIdx::Selection（选中背景）
 *   19:   WidgetColorIdx::FindMatch（搜索高亮）
 */
void SimpleTerminalWidget::setupDefaultColors()
{
    std::array<QColor, 20> colors;

    // ANSI 0-15: 标准 16 色（与 xterm-256color / Tango Dark 兼容）
    colors[0]  = QColor("#2e3436");   //  0: Black
    colors[1]  = QColor("#cc0000");   //  1: Red
    colors[2]  = QColor("#4e9a06");   //  2: Green
    colors[3]  = QColor("#c4a000");   //  3: Yellow
    colors[4]  = QColor("#3465a4");   //  4: Blue
    colors[5]  = QColor("#75507b");   //  5: Magenta
    colors[6]  = QColor("#06989a");   //  6: Cyan
    colors[7]  = QColor("#d3d7cf");   //  7: White
    colors[8]  = QColor("#555753");   //  8: Bright Black
    colors[9]  = QColor("#ef2929");   //  9: Bright Red
    colors[10] = QColor("#8ae234");   // 10: Bright Green
    colors[11] = QColor("#fce94f");   // 11: Bright Yellow
    colors[12] = QColor("#729fcf");   // 12: Bright Blue
    colors[13] = QColor("#ad7fa8");   // 13: Bright Magenta
    colors[14] = QColor("#34e2e2");   // 14: Bright Cyan
    colors[15] = QColor("#eeeeec");   // 15: Bright White

    // Widget 颜色（Qt Creator 深色主题默认）
    colors[16] = QColor("#d4d4d4");   // Foreground — 浅灰文字
    colors[17] = QColor("#1e1e1e");   // Background — 深灰背景
    colors[18] = QColor("#264f78");   // Selection  — 蓝色选择区
    colors[19] = QColor("#6c6c6c");   // FindMatch  — 中灰搜索高亮

    setColors(colors);
}

/**
 * @brief 重写：键盘输入/终端控制 → PTY
 *
 * TerminalSurface 处理用户按键后调用此函数将数据写入 PTY，
 * 此处重写为写入 Utils::Process 的 PTY 通道。
 */
qint64 SimpleTerminalWidget::writeToPty(const QByteArray &data)
{
    if (!m_process || !m_process->isRunning()) {
        logToFile("writeToPty: process not running, skipping");
        return -1;
    }
    return m_process->writeRaw(data);
}

/**
 * @brief 重写：终端尺寸变化（行列数）→ 通知 PTY 调整
 *
 * TerminalView 在显示/缩放时通过 applySizeChange() 调用本函数。
 * 将新的行列尺寸下发给 PTY，使 Shell 正确换行与重绘。
 */
bool SimpleTerminalWidget::resizePty(QSize newSize)
{
    if (!m_process || !m_process->ptyData() || !m_process->isRunning()) {
        logToFile(QString("resizePty: not running, size=%1x%2").arg(newSize.width()).arg(newSize.height()));
        return false;
    }
    m_process->ptyData()->resize(newSize);
    logToFile(QString("resizePty: resized to %1x%2").arg(newSize.width()).arg(newSize.height()));
    return true;
}

/// 强制终止并销毁本终端进程，释放 ConPTY 句柄
void SimpleTerminalWidget::forceKillProcess()
{
    if (!m_process)
        return;
    logToFile("forceKillProcess: disconnecting, killing and deleting process");
    m_processWasKilled = true;
    m_process->disconnect();
    // 先清除 PTY 数据（释放 ConPTY 伪控制台句柄），再杀进程
    m_process->setPtyData(std::nullopt);

    logToFile(QString("forceKillProcess: running before kill=%1").arg(m_process->isRunning()));
    m_process->kill();
    // 短等待确保 OS 处理 TerminateProcess，避免 ~Process 堵塞
    bool exited = m_process->waitForFinished(QDeadlineTimer(300));
    logToFile(QString("forceKillProcess: after kill, exitWait=%1 stillRunning=%2")
                  .arg(exited).arg(m_process->isRunning()));

    // 同步删除 Process 对象
    delete m_process;
    m_process = nullptr;
    logToFile("forceKillProcess: done");
}

/// 终止所有活跃的终端进程（供插件 aboutToShutdown 调用）
void SimpleTerminalWidget::killAllProcesses()
{
    logToFile(QString("killAllProcesses: %1 widgets in list").arg(s_activeWidgets.size()));
    for (SimpleTerminalWidget *w : std::as_const(s_activeWidgets)) {
        if (w) {
            logToFile("killAllProcesses: calling forceKillProcess on widget");
            w->forceKillProcess();
        }
    }
    s_activeWidgets.clear();
    logToFile("killAllProcesses: done");
}

} // namespace QtSideBarTerminal::Internal
