#include "simpleterminal.h"

#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/terminalhooks.h>
#include <utils/theme/theme.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projecttree.h>

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QList>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>
#include <QWheelEvent>

#include <coreplugin/editormanager/editormanager.h>
#include <utils/link.h>

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

    // 终端内容区域四周留 10px 边距
    setViewportMargins(10, 10, 10, 10);
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
 * KeyPress：Ctrl+退格 发 \x17（删前一词）；Ctrl+V 调粘贴。
 */
bool SimpleTerminalWidget::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape
            || (keyEvent->modifiers() & Qt::ControlModifier)) {
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::KeyPress: {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Backspace
            && (keyEvent->modifiers() & Qt::ControlModifier)) {
            // Ctrl+退格 → 删前一词
            writeToPty(QByteArrayLiteral("\x17"));
            return true;
        }
        if (keyEvent->key() == Qt::Key_V
            && (keyEvent->modifiers() & Qt::ControlModifier)
            && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            // Ctrl+V → 粘贴（读取剪贴板写入 PTY）
            pasteFromClipboard();
            return true;
        }
        // Home / End → 委托 TerminalView 发送给终端
        if (keyEvent->key() == Qt::Key_Home
            || keyEvent->key() == Qt::Key_End) {
            TerminalSolution::TerminalView::keyPressEvent(keyEvent);
            updateMicroFocus();
            return true;
        }
        // Tab / Shift+Tab → 直接写 PTY（libvterm 对 Shift+Tab 映射不准）
        if (keyEvent->key() == Qt::Key_Tab) {
            if (keyEvent->modifiers() & Qt::ShiftModifier)
                writeToPty(QByteArrayLiteral("\x1b[Z"));  // CSI 反向制表
            else
                writeToPty(QByteArrayLiteral("\x09"));    // HT
            updateMicroFocus();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return TerminalSolution::TerminalView::event(event);
}

/**
 * @brief 鼠标按键（日志右键 selection 状态，左键直接激活链接）
 */
void SimpleTerminalWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        const bool hasSel = (selection() != std::nullopt);
        logToFile(QString("mousePressEvent RIGHT click: hasSelection=%1").arg(hasSel));
    }

    // 左键无 Ctrl：直接读取链接文本并激活（避免构造假事件导致崩溃）
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier)) {
        const TextAndOffsets hit = textAt(event->pos());
        if (hit.text.size() > 0) {
            const QString t = QString::fromUcs4(hit.text.c_str(), hit.text.size()).trimmed();
            const auto link = toLink(t);
            if (link) {
                linkActivated(*link);
                return;
            }
        }
    }

    TerminalSolution::TerminalView::mousePressEvent(event);
}

/// @reimp 鼠标悬停时始终检测超链接（不依赖 Ctrl 键）
void SimpleTerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    // 始终检测链接（TerminalView 默认只在 Ctrl 按下时检测）
    const bool hasLink = checkLinkAt(event->pos());

    if (event->buttons() & Qt::LeftButton) {
        // 正在选择文本 → 由基类处理选择逻辑
        TerminalSolution::TerminalView::mouseMoveEvent(event);
    }
    // 不调用基类（基类会在无 Ctrl 时清除链接检测结果）

    // 手动设置光标：有链接→手型，否则 IBeam
    setCursor(hasLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

/// @reimp Ctrl+滚轮缩放字体
void SimpleTerminalWidget::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        QFont f = font();
        int newSize = f.pointSize() + (event->angleDelta().y() > 0 ? 1 : -1);
        newSize = qBound(6, newSize, 72);
        f.setPointSize(newSize);
        setFont(f);
        applySizeChange();
        event->accept();
        return;
    }
    TerminalSolution::TerminalView::wheelEvent(event);
}

/// @reimp 输入法光标位置查询（中文候选字定位）
QVariant SimpleTerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle) {
        auto termCursor = surface()->cursor();
        QPointF pos = gridToGlobal(termCursor.position);
        QFontMetrics fm(font());
        return QRect(pos.toPoint(), QSize(fm.averageCharWidth(), fm.height()));
    }
    return TerminalSolution::TerminalView::inputMethodQuery(query);
}

/// @reimp 将选中文本写入系统剪贴板（TerminalView 默认实现为空）
void SimpleTerminalWidget::setClipboard(const QString &text)
{
    logToFile(QString("setClipboard: text=%1 chars").arg(text.size()));
    QApplication::clipboard()->setText(text);
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

    // 辅助：从 Qt Creator 主题读取颜色，失败用暗色硬编码回退
    auto themeColor = [](Utils::Theme::Color role, const QColor &fallback) -> QColor {
        QColor c = Utils::creatorColor(role);
        return c.isValid() ? c : fallback;
    };

    // ANSI 0-15（优先主题，回退 Tango Dark）
    colors[0]  = themeColor(Utils::Theme::TerminalAnsi0,  QColor("#2e3436"));
    colors[1]  = themeColor(Utils::Theme::TerminalAnsi1,  QColor("#cc0000"));
    colors[2]  = themeColor(Utils::Theme::TerminalAnsi2,  QColor("#4e9a06"));
    colors[3]  = themeColor(Utils::Theme::TerminalAnsi3,  QColor("#c4a000"));
    colors[4]  = themeColor(Utils::Theme::TerminalAnsi4,  QColor("#3465a4"));
    colors[5]  = themeColor(Utils::Theme::TerminalAnsi5,  QColor("#75507b"));
    colors[6]  = themeColor(Utils::Theme::TerminalAnsi6,  QColor("#06989a"));
    colors[7]  = themeColor(Utils::Theme::TerminalAnsi7,  QColor("#d3d7cf"));
    colors[8]  = themeColor(Utils::Theme::TerminalAnsi8,  QColor("#555753"));
    colors[9]  = themeColor(Utils::Theme::TerminalAnsi9,  QColor("#ef2929"));
    colors[10] = themeColor(Utils::Theme::TerminalAnsi10, QColor("#8ae234"));
    colors[11] = themeColor(Utils::Theme::TerminalAnsi11, QColor("#fce94f"));
    colors[12] = themeColor(Utils::Theme::TerminalAnsi12, QColor("#729fcf"));
    colors[13] = themeColor(Utils::Theme::TerminalAnsi13, QColor("#ad7fa8"));
    colors[14] = themeColor(Utils::Theme::TerminalAnsi14, QColor("#34e2e2"));
    colors[15] = themeColor(Utils::Theme::TerminalAnsi15, QColor("#eeeeec"));

    // Widget 颜色（优先主题，回退 Qt Creator 深色主题默认）
    colors[16] = themeColor(Utils::Theme::TerminalForeground, QColor("#d4d4d4"));
    colors[17] = themeColor(Utils::Theme::TerminalBackground, QColor("#1e1e1e"));
    colors[18] = themeColor(Utils::Theme::TerminalSelection,  QColor("#264f78"));
    colors[19] = themeColor(Utils::Theme::TerminalFindMatch,  QColor("#6c6c6c"));

    setColors(colors);

    // 边距填充色：终端背景色 HSL 亮度降低 ~15% 以产生内部边框效果
    QColor marginBg = colors[17].toHsl();
    marginBg.setHsl(marginBg.hslHue(), marginBg.hslSaturation(),
                    qMax(0, marginBg.lightness() - 38));
    setAutoFillBackground(true);
    setStyleSheet(QString("background-color: %1;").arg(marginBg.name()));
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

/// @reimp 将终端文本映射为可点击链接（URL / 文件路径）
std::optional<SimpleTerminalWidget::Link> SimpleTerminalWidget::toLink(const QString &text)
{
    // URL 检测
    static const QRegularExpression urlRe(
        R"(^(https?|ftp|file)://[^\s:;,.!?)\]]+)");
    if (urlRe.match(text).hasMatch())
        return Link{text, 0, 0};

    // 文件路径检测：支持 line:column 尾缀（如 main.cpp:42:10）
    // 匹配：<路径>.<扩展名>[:行[:列]]
    static const QRegularExpression fileRe(
        R"(^(.+)\.(\w+)(?::(\d+))?(?::(\d+))?$)");
    auto m = fileRe.match(text);
    if (m.hasMatch()) {
        int line = m.captured(3).toInt();
        int col  = m.captured(4).toInt();
        return Link{text, line, col};
    }

    return std::nullopt;
}

/// @reimp 点击链接后的操作
void SimpleTerminalWidget::linkActivated(const Link &link)
{
    const QString text = link.text;

    // URL → 系统浏览器
    static const QRegularExpression urlRe(
        R"(^(https?|ftp|file)://)", QRegularExpression::CaseInsensitiveOption);
    if (urlRe.match(text).hasMatch()) {
        QDesktopServices::openUrl(QUrl(text));
        return;
    }

    // 从文本中提取裸路径（移除 :行:列 尾缀）
    // 注意：toLink 返回的 link.targetLine/Column 已设置，但路径字串仍含 ":42:10"
    QString path = text;
    int line = 0, col = 0;

    static const QRegularExpression lineColSuffix(R"((.*\.\w+):(\d+)(?::(\d+))?$)");
    auto m = lineColSuffix.match(text);
    if (m.hasMatch()) {
        path = m.captured(1);                  // 裸路径
        line = m.captured(2).toInt();           // toLink 中解析的行号
        col  = m.captured(3).toInt();           // toLink 中解析的列号
    }
    // fallback: 使用 link 提供的值
    if (line == 0) line = link.targetLine;
    if (col  == 0) col  = link.targetColumn;

    const Utils::FilePath fp = Utils::FilePath::fromString(path);

    // 可执行文件：系统打开而非编辑器中打开
    static const QStringList exeExts = {"exe", "bat", "cmd", "com", "msi"};
    if (exeExts.contains(fp.suffix(), Qt::CaseInsensitive)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fp.toFSPathString()));
        return;
    }

    // 尝试打开文件路径（支持相对路径 → 拼上项目目录）
    Utils::FilePath resolved = fp;
    if (!resolved.exists() && !resolved.isAbsolutePath()) {
        // 尝试用当前项目目录解析相对路径
        if (auto *proj = ProjectExplorer::ProjectTree::currentProject()) {
            resolved = proj->projectDirectory().resolvePath(fp);
        }
    }
    // 如果仍未找到，再尝试 CWD（Shell 启动时也设置了项目目录）
    if (!resolved.exists())
        resolved = Utils::FilePath::fromString(
            QDir::current().absoluteFilePath(path));

    // 仍不存在 → 在项目目录中递归搜索文件名匹配
    if (!resolved.exists()) {
        if (auto *proj = ProjectExplorer::ProjectTree::currentProject()) {
            const QString baseDir = proj->projectDirectory().toFSPathString();
            const QString fileName = Utils::FilePath::fromString(path).fileName();
            if (!fileName.isEmpty()) {
                QDirIterator it(baseDir, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next();
                    if (it.fileName().compare(fileName, Qt::CaseInsensitive) == 0) {
                        resolved = Utils::FilePath::fromString(it.filePath());
                        break;  // 只取第一个匹配
                    }
                }
            }
        }
    }

    if (resolved.exists())
        Core::EditorManager::openEditorAt(Utils::Link(resolved, line, col));
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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

/// 向首个活跃终端发送文本
void SimpleTerminalWidget::sendToActiveTerminal(const QString &text)
{
    for (SimpleTerminalWidget *w : std::as_const(s_activeWidgets)) {
        if (w) {
            w->writeToPty(text.toUtf8());
            return;
        }
    }
}

} // namespace QtSideBarTerminal::Internal
