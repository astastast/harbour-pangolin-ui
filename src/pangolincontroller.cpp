#include "pangolincontroller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

#include <sys/stat.h>
#include <unistd.h>

namespace {
// Bundled in the RPM (rpm/harbour-pangolin-ui.spec).
const char *kBinary = "/usr/bin/pangolin";
// setuid-root helper from helper/pangolin-helper.c, run by the unit.
const char *kHelper = "/usr/libexec/harbour-pangolin-ui/pangolin-helper";
const int kMaxOutputChars = 200000;
// systemd user unit shipped in this package (systemd/harbour-pangolin.service).
const char *kServiceName = "harbour-pangolin.service";

QString findTool(const QString &name)
{
    QString p = QStandardPaths::findExecutable(name);
    if (p.isEmpty())
        p = QStandardPaths::findExecutable(name, QStringList()
                                           << QStringLiteral("/usr/bin")
                                           << QStringLiteral("/bin")
                                           << QStringLiteral("/usr/sbin")
                                           << QStringLiteral("/sbin"));
    return p;
}
} // namespace

PangolinController::PangolinController(QObject *parent)
    : QObject(parent)
    , m_overrideDns(true)
    , m_daemonReloaded(false)
    , m_watchService(false)
    , m_action(0)
    , m_actionErrored(false)
    , m_status(0)
    , m_burstLeft(0)
    , m_login(0)
    , m_state(QStringLiteral("unknown"))
{
    QSettings s;
    // DNS override (CLI default: on) is needed for private resource aliases.
    // On Sailfish (systemd-resolved 238) it relies on the bundled CLI's
    // pangolin-cli/olm-systemd238.patch.
    m_overrideDns = s.value(QStringLiteral("overrideDns"), true).toBool();
    m_extraUpArgs = s.value(QStringLiteral("extraUpArgs")).toString();

    m_pollTimer.setInterval(5000);
    connect(&m_pollTimer, &QTimer::timeout, this, &PangolinController::refreshStatus);

    m_burstTimer.setInterval(1000);
    connect(&m_burstTimer, &QTimer::timeout, this, [this]() {
        refreshStatus();
        if (m_watchService)
            checkServiceFailed();
        if (--m_burstLeft <= 0) {
            m_burstTimer.stop();
            m_watchService = false;
        }
    });
}

PangolinController::~PangolinController()
{
    // Only GUI-side helper processes are killed; the tunnel itself runs
    // detached (nohup) and survives the app, just like on a desktop.
    if (m_login) { m_login->kill(); m_login->waitForFinished(1000); }
    if (m_status) { m_status->kill(); m_status->waitForFinished(1000); }
}

// ---------------------------------------------------------------- settings

void PangolinController::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("overrideDns"), m_overrideDns);
    s.setValue(QStringLiteral("extraUpArgs"), m_extraUpArgs);
    if (serviceAvailable())
        writeServiceEnv();
}

// ---------------------------------------------------------------- service mode

bool PangolinController::serviceAvailable() const
{
    return QFileInfo::exists(QStringLiteral("/usr/lib/systemd/user/") + QLatin1String(kServiceName))
        || QFileInfo::exists(QDir::homePath() + QStringLiteral("/.config/systemd/user/") + QLatin1String(kServiceName));
}

// Writes ~/.config/harbour-pangolin-ui/service.env, read by the unit's
// EnvironmentFile=, so the Top Menu toggle uses the same flags.
bool PangolinController::writeServiceEnv()
{
    const QString dir = QDir::homePath() + QStringLiteral("/.config/harbour-pangolin-ui");
    QDir().mkpath(dir);
    QFile f(dir + QStringLiteral("/service.env"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QString flags = upFlags().join(QLatin1Char(' '));
    flags.remove(QLatin1Char('"'));
    QTextStream ts(&f);
    ts << "# Written by harbour-pangolin-ui; read by harbour-pangolin.service\n"
       << "PANGOLIN_UP_ARGS=\"" << flags << "\"\n";
    return true;
}

bool PangolinController::serviceActive() const
{
    const QString systemctl = findTool(QStringLiteral("systemctl"));
    if (systemctl.isEmpty()) return false;
    QProcess p;
    p.start(systemctl, QStringList() << QStringLiteral("--user") << QStringLiteral("is-active")
                                     << QStringLiteral("--quiet") << QLatin1String(kServiceName));
    if (!p.waitForFinished(3000)) { p.kill(); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

bool PangolinController::runSystemctl(const QString &name, const QStringList &args)
{
    const QString systemctl = findTool(QStringLiteral("systemctl"));
    if (systemctl.isEmpty()) {
        appendOutput(tr("systemctl not found."));
        emit actionFinished(name, false, QString());
        return false;
    }
    QStringList full = QStringList() << QStringLiteral("--user") << args << QLatin1String(kServiceName);
    return startProcess(name, systemctl, full,
                        QStringLiteral("systemctl ") + full.join(QLatin1Char(' ')));
}

// Called once a second during the status burst after start(): systemctl
// start returns as soon as the helper is spawned, so a failing `up`
// (e.g. not logged in, helper not setuid) only shows in the unit state.
void PangolinController::checkServiceFailed()
{
    const QString systemctl = findTool(QStringLiteral("systemctl"));
    if (systemctl.isEmpty()) { m_watchService = false; return; }
    QProcess p;
    p.start(systemctl, QStringList() << QStringLiteral("--user") << QStringLiteral("show")
                                     << QStringLiteral("-p") << QStringLiteral("ActiveState")
                                     << QStringLiteral("-p") << QStringLiteral("ExecMainStatus")
                                     << QLatin1String(kServiceName));
    if (!p.waitForFinished(2000)) { p.kill(); return; }
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    if (!out.contains(QStringLiteral("ActiveState=failed"))) {
        if (out.contains(QStringLiteral("ActiveState=active")) && m_state == QLatin1String("connected"))
            m_watchService = false;
        return;
    }
    m_watchService = false;
    static const QRegularExpression statusRe(QStringLiteral("ExecMainStatus=(\\d+)"));
    const int code = statusRe.match(out).captured(1).toInt();
    appendOutput(tr("[service failed, exit code %1]").arg(code));
    appendOutput(readServiceLog(15));
    QString hint = QStringLiteral("service-failed");
    if (code == 2 && !helperOk()) {
        appendOutput(tr("Hint: %1 is not setuid root. Reinstall the package as root.").arg(QLatin1String(kHelper)));
    } else if (code == 2) {
        appendOutput(tr("Hint: the helper rejected a flag. Check \"Extra 'up' flags\" in Settings."));
    } else if (code == 127) {
        appendOutput(tr("Hint: the helper or %1 is missing. Reinstall the package.").arg(binaryPath()));
    }
    setState(QStringLiteral("error"), tr("Service failed (exit code %1)").arg(code));
    emit actionFinished(QStringLiteral("up"), false, hint);
}

QString PangolinController::serviceLogPath() const
{
    // The unit's StandardOutput=file:%t/harbour-pangolin.log (%t = $XDG_RUNTIME_DIR).
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
         + QStringLiteral("/harbour-pangolin.log");
}

QString PangolinController::readServiceLog(int maxLines) const
{
    // On Sailfish the user cannot read the user journal, so the unit logs to
    // a file; journalctl is only a fallback for an older unit file.
    if (QFileInfo::exists(serviceLogPath()))
        return tailFile(serviceLogPath(), maxLines);
    const QString journalctl = findTool(QStringLiteral("journalctl"));
    if (journalctl.isEmpty())
        return tr("journalctl not found.");
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(journalctl, QStringList() << QStringLiteral("--user") << QStringLiteral("-u")
                                      << QLatin1String(kServiceName) << QStringLiteral("-n")
                                      << QString::number(maxLines > 0 ? maxLines : 400)
                                      << QStringLiteral("--no-pager") << QStringLiteral("-o")
                                      << QStringLiteral("short"));
    if (!p.waitForFinished(5000)) { p.kill(); return tr("journalctl timed out."); }
    return stripAnsi(QString::fromUtf8(p.readAll()));
}

QString PangolinController::binaryPath() const { return QString::fromLatin1(kBinary); }

bool PangolinController::binaryFound() const
{
    QFileInfo fi(binaryPath());
    return fi.exists() && fi.isFile() && fi.isExecutable();
}

bool PangolinController::helperOk() const
{
    struct stat st;
    return stat(kHelper, &st) == 0 && st.st_uid == 0 && (st.st_mode & S_ISUID);
}

void PangolinController::setOverrideDns(bool v)
{
    if (v == m_overrideDns) return;
    m_overrideDns = v;
    saveSettings();
    emit overrideDnsChanged();
}

void PangolinController::setExtraUpArgs(const QString &v)
{
    if (v == m_extraUpArgs) return;
    m_extraUpArgs = v;
    saveSettings();
    emit extraUpArgsChanged();
}

bool PangolinController::isRoot() const { return geteuid() == 0; }

QString PangolinController::cpuArch() const { return QSysInfo::currentCpuArchitecture(); }

QString PangolinController::clientLogPath() const
{
    // internal/config/config.go defaultLogPath(): ~/.config/pangolin/logs/client.log
    // (can be changed with `pangolin config set log_file ...`).
    return QDir::homePath() + QStringLiteral("/.config/pangolin/logs/client.log");
}

void PangolinController::setPolling(bool on)
{
    if (on == m_pollTimer.isActive()) return;
    if (on) { m_pollTimer.start(); refreshStatus(); }
    else m_pollTimer.stop();
    emit pollingChanged();
}

// ---------------------------------------------------------------- helpers

QProcessEnvironment PangolinController::cliEnvironment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    return env;
}

QString PangolinController::stripAnsi(const QString &s)
{
    static const QRegularExpression ansi(QStringLiteral("\x1B\\[[0-9;?]*[ -/]*[@-~]|\x1B\\][^\x07]*\x07|\r"));
    QString out = s;
    out.remove(ansi);
    return out;
}

void PangolinController::appendOutput(const QString &text)
{
    if (text.isEmpty()) return;
    m_output += stripAnsi(text);
    if (!m_output.endsWith(QLatin1Char('\n'))) m_output += QLatin1Char('\n');
    if (m_output.size() > kMaxOutputChars)
        m_output = m_output.right(kMaxOutputChars);
    emit outputChanged();
}

void PangolinController::clearOutput()
{
    m_output.clear();
    emit outputChanged();
}

void PangolinController::setState(const QString &state, const QString &summary)
{
    m_state = state;
    m_statusSummary = summary;
    emit statusChanged();
}

// ---------------------------------------------------------------- actions

bool PangolinController::runAction(const QString &name, const QStringList &args)
{
    if (m_action) {
        appendOutput(tr("Another command is still running (%1).").arg(m_actionName));
        return false;
    }
    if (!binaryFound()) {
        appendOutput(tr("Pangolin CLI not found at %1. Reinstall the harbour-pangolin-ui package.")
                         .arg(binaryPath()));
        emit actionFinished(name, false, QStringLiteral("missing-binary"));
        return false;
    }
    return startProcess(name, binaryPath(), args,
                        QStringLiteral("pangolin ") + args.join(QLatin1Char(' ')));
}

bool PangolinController::startProcess(const QString &name, const QString &program,
                                      const QStringList &fullArgs, const QString &display)
{
    if (m_action) {
        appendOutput(tr("Another command is still running (%1).").arg(m_actionName));
        return false;
    }
    m_action = new QProcess(this);
    m_action->setProcessEnvironment(cliEnvironment());
    m_action->setProcessChannelMode(QProcess::MergedChannels);
    m_actionName = name;
    m_actionOut.clear();
    m_actionErrored = false;

    connect(m_action, &QProcess::readyRead, this, &PangolinController::onActionReadyRead);
    connect(m_action, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &PangolinController::onActionFinished);
    connect(m_action, &QProcess::errorOccurred, this, &PangolinController::onActionError);

    appendOutput(QStringLiteral("$ ") + display);

    m_action->start(program, fullArgs);
    // Keep stdin open but empty for the CLI (`login` reads it in a goroutine).
    emit busyChanged();
    return true;
}

void PangolinController::onActionReadyRead()
{
    if (!m_action) return;
    const QByteArray chunk = m_action->readAll();
    m_actionOut += chunk;
    appendOutput(QString::fromUtf8(chunk));
}

void PangolinController::onActionError(QProcess::ProcessError error)
{
    if (!m_action) return;
    if (error == QProcess::FailedToStart) {
        m_actionErrored = true;
        appendOutput(tr("Failed to start: %1").arg(m_action->errorString()));
        finishAction(false, QStringLiteral("failed-to-start"));
    }
}

void PangolinController::onActionFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_action || m_actionErrored) return;
    onActionReadyRead();
    const bool ok = status == QProcess::NormalExit && exitCode == 0;
    const QString out = QString::fromUtf8(m_actionOut);
    QString hint;

    if (out.contains(QStringLiteral("pangolin login")))
        hint = QStringLiteral("login");

    if (m_actionName == QLatin1String("version")) {
        // Output (cmd/version): the bare version line, surrounded by optional
        // "A new version is available: X (current: Y)" notices.
        const QStringList lines = out.split(QLatin1Char('\n'), QString::SkipEmptyParts);
        static const QRegularExpression verRe(QStringLiteral("^v?\\d+\\.\\d+(\\.\\d+)?\\S*$"));
        static const QRegularExpression newRe(QStringLiteral("new version is available:\\s*(\\S+)"));
        QString ver, newer;
        for (const QString &l : lines) {
            const QString t = stripAnsi(l).trimmed();
            if (ver.isEmpty() && verRe.match(t).hasMatch())
                ver = t;
            const QRegularExpressionMatch m = newRe.match(t);
            if (m.hasMatch())
                newer = m.captured(1);
        }
        if (!ver.isEmpty()) {
            m_cliVersion = newer.isEmpty() ? ver : tr("%1 (%2 available)").arg(ver, newer);
            emit cliVersionChanged();
        }
    }

    appendOutput(ok ? tr("[%1 finished]").arg(m_actionName)
                    : tr("[%1 failed, exit code %2]").arg(m_actionName).arg(exitCode));

    // `down` sends the exit request over /var/run/olm.sock first and then
    // shows a TUI log preview; without a TTY that preview can fail even
    // though the client is stopping. Status polling is the source of truth.
    finishAction(ok, hint);
}

void PangolinController::finishAction(bool ok, const QString &hint)
{
    const QString name = m_actionName;
    if (m_action) {
        m_action->disconnect(this);
        m_action->deleteLater();
        m_action = 0;
    }
    m_actionName.clear();
    emit busyChanged();
    emit actionFinished(name, ok, hint);

    if (name == QLatin1String("up") || name == QLatin1String("down")) {
        m_burstLeft = 15;
        m_burstTimer.start();
        refreshStatus();
    }
}

void PangolinController::cancelAction()
{
    if (m_action) {
        appendOutput(tr("Cancelling %1…").arg(m_actionName));
        m_action->kill();
    }
}

QStringList PangolinController::upFlags() const
{
    QStringList args;
    if (!m_overrideDns)
        args << QStringLiteral("--override-dns=false");
    const QString extra = m_extraUpArgs.trimmed();
    if (!extra.isEmpty())
        args << extra.split(QRegularExpression(QStringLiteral("\\s+")), QString::SkipEmptyParts);
    return args;
}

void PangolinController::start()
{
    if (isRoot()) {
        // Started as root (e.g. from devel-su): there is no user systemd
        // instance, so run the client detached directly. --silent skips
        // the TUI, which cannot run without a terminal.
        if (runAction(QStringLiteral("up"), QStringList() << QStringLiteral("up")
                                                          << QStringLiteral("--silent") << upFlags()))
            setState(QStringLiteral("starting"), tr("Starting…"));
        return;
    }
    // The unit runs `pangolin-helper up --attach $PANGOLIN_UP_ARGS`
    // (foreground, so systemd tracks it); the helper is setuid root.
    if (!binaryFound() || !serviceAvailable()) {
        appendOutput(tr("%1 or harbour-pangolin.service is missing. Reinstall the harbour-pangolin-ui package.")
                         .arg(binaryPath()));
        emit actionFinished(QStringLiteral("up"), false, QStringLiteral("missing-binary"));
        return;
    }
    if (!helperOk())
        appendOutput(tr("Warning: %1 is missing or not setuid root; starting will fail.").arg(QLatin1String(kHelper)));
    if (!writeServiceEnv())
        appendOutput(tr("Warning: could not write ~/.config/harbour-pangolin-ui/service.env"));
    if (!m_daemonReloaded) {
        // Make the user manager see a freshly installed/updated unit file.
        m_daemonReloaded = true;
        const QString systemctl = findTool(QStringLiteral("systemctl"));
        if (!systemctl.isEmpty()) {
            QProcess p;
            p.start(systemctl, QStringList() << QStringLiteral("--user") << QStringLiteral("daemon-reload"));
            if (!p.waitForFinished(5000)) p.kill();
        }
    }
    if (runSystemctl(QStringLiteral("up"), QStringList() << QStringLiteral("start"))) {
        m_watchService = true;
        setState(QStringLiteral("starting"), tr("Starting…"));
    }
}

void PangolinController::stop()
{
    // If the tunnel was started by the systemd user unit (app in service
    // mode or the Top Menu toggle), stop the unit so systemd's state is right.
    if (serviceAvailable() && serviceActive()) {
        if (runSystemctl(QStringLiteral("down"), QStringList() << QStringLiteral("stop")))
            setState(QStringLiteral("stopping"), tr("Stopping…"));
        return;
    }
    // Otherwise (detached instance): the olm API socket is created with mode
    // 0666, so `down` works as a normal user (fosrl/olm api/api_unix.go).
    if (runAction(QStringLiteral("down"), QStringList() << QStringLiteral("down")))
        setState(QStringLiteral("stopping"), tr("Stopping…"));
}

void PangolinController::checkVersion()
{
    runAction(QStringLiteral("version"), QStringList() << QStringLiteral("version"));
}

void PangolinController::authStatus()
{
    runAction(QStringLiteral("auth status"), QStringList() << QStringLiteral("auth") << QStringLiteral("status"));
}

// ---------------------------------------------------------------- status

void PangolinController::refreshStatus()
{
    if (m_status || !binaryFound()) {
        if (!binaryFound())
            setState(QStringLiteral("nobinary"), tr("Pangolin CLI not found"));
        return;
    }
    m_status = new QProcess(this);
    m_status->setProcessEnvironment(cliEnvironment());
    m_status->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_status, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &PangolinController::onStatusFinished);
    connect(m_status, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && m_status) {
            setState(QStringLiteral("error"), m_status->errorString());
            m_status->deleteLater();
            m_status = 0;
        }
    });
    m_status->start(binaryPath(), QStringList() << QStringLiteral("status") << QStringLiteral("--json"));
}

void PangolinController::onStatusFinished(int exitCode, QProcess::ExitStatus)
{
    if (!m_status) return;
    const QByteArray out = m_status->readAll();
    m_status->deleteLater();
    m_status = 0;
    parseStatus(out, exitCode);
}

void PangolinController::parseStatus(const QByteArray &raw, int exitCode)
{
    const QString text = stripAnsi(QString::fromUtf8(raw));
    const int brace = text.indexOf(QLatin1Char('{'));

    // When nothing runs the CLI prints "No client is currently running"
    // (plain text, exit 0) even with --json.
    if (brace < 0) {
        m_peers.clear();
        m_orgId.clear();
        m_agentVersion.clear();
        if (text.contains(QStringLiteral("No client is currently running")))
            setState(QStringLiteral("stopped"), tr("Not running"));
        else
            setState(QStringLiteral("error"),
                     exitCode == 0 ? text.trimmed() : tr("status failed: %1").arg(text.trimmed()));
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(text.mid(brace).toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        setState(QStringLiteral("error"), tr("Could not parse status JSON"));
        return;
    }
    const QJsonObject o = doc.object();
    const bool connected = o.value(QStringLiteral("connected")).toBool();
    const bool registered = o.value(QStringLiteral("registered")).toBool();
    m_orgId = o.value(QStringLiteral("orgId")).toString();
    m_agentVersion = o.value(QStringLiteral("agent")).toString() + QLatin1Char(' ')
                   + o.value(QStringLiteral("version")).toString();

    m_peers.clear();
    const QJsonObject exitNode = o.value(QStringLiteral("exitNode")).toObject();
    if (!exitNode.isEmpty()) {
        QVariantMap p;
        p.insert(QStringLiteral("name"), tr("Pangolin Server"));
        p.insert(QStringLiteral("endpoint"), exitNode.value(QStringLiteral("endpoint")).toString());
        p.insert(QStringLiteral("connected"), exitNode.value(QStringLiteral("connected")).toBool());
        p.insert(QStringLiteral("mode"), QStringLiteral("Direct"));
        p.insert(QStringLiteral("rttMs"), exitNode.value(QStringLiteral("rtt")).toDouble() / 1e6);
        m_peers.append(p);
    }
    const QJsonObject peers = o.value(QStringLiteral("peers")).toObject();
    for (QJsonObject::const_iterator it = peers.begin(); it != peers.end(); ++it) {
        const QJsonObject po = it.value().toObject();
        QVariantMap p;
        p.insert(QStringLiteral("name"), po.value(QStringLiteral("name")).toString());
        p.insert(QStringLiteral("endpoint"), po.value(QStringLiteral("endpoint")).toString());
        p.insert(QStringLiteral("connected"), po.value(QStringLiteral("connected")).toBool());
        const bool local = po.value(QStringLiteral("isLocal")).toBool();
        const bool relay = po.value(QStringLiteral("isRelay")).toBool();
        p.insert(QStringLiteral("mode"), local ? QStringLiteral("Local")
                                               : relay ? QStringLiteral("Relay") : QStringLiteral("Direct"));
        p.insert(QStringLiteral("rttMs"), po.value(QStringLiteral("rtt")).toDouble() / 1e6);
        m_peers.append(p);
    }

    const QJsonObject e = o.value(QStringLiteral("error")).toObject();
    if (!e.isEmpty()) {
        setState(QStringLiteral("error"), tr("Error: %1").arg(e.value(QStringLiteral("message")).toString()));
    } else if (connected && registered) {
        setState(QStringLiteral("connected"), tr("Connected"));
    } else if (registered) {
        setState(QStringLiteral("registered"), tr("Registered, connecting…"));
    } else {
        setState(QStringLiteral("disconnected"), tr("Running, not connected"));
    }
}

// ---------------------------------------------------------------- login

void PangolinController::login(const QString &hostname)
{
    if (m_login) return;
    if (!binaryFound()) {
        appendOutput(tr("Pangolin CLI not found at %1").arg(binaryPath()));
        return;
    }
    const QString host = hostname.trimmed().isEmpty() ? QStringLiteral("app.pangolin.net") : hostname.trimmed();
    m_loginCode.clear();
    m_loginUrl.clear();
    m_login = new QProcess(this);
    m_login->setProcessEnvironment(cliEnvironment());
    m_login->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_login, &QProcess::readyRead, this, &PangolinController::onLoginReadyRead);
    connect(m_login, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &PangolinController::onLoginFinished);
    appendOutput(QStringLiteral("$ pangolin login ") + host);
    // Passing the hostname skips the interactive "hosting option" form
    // (cmd/auth/login/login.go). The org picker may still be interactive
    // if the account has several orgs - then log in from Terminal once.
    m_login->start(binaryPath(), QStringList() << QStringLiteral("login") << host);
    emit loginChanged();
}

void PangolinController::onLoginReadyRead()
{
    if (!m_login) return;
    const QString chunk = stripAnsi(QString::fromUtf8(m_login->readAll()));
    appendOutput(chunk);
    static const QRegularExpression codeRe(QStringLiteral("one-time code:\\s*(\\S+)"));
    static const QRegularExpression urlRe(QStringLiteral("open\\s+(https?://\\S+)\\s+in your browser"));
    QRegularExpressionMatch m = codeRe.match(chunk);
    bool changed = false;
    if (m.hasMatch()) { m_loginCode = m.captured(1); changed = true; }
    m = urlRe.match(chunk);
    if (m.hasMatch()) { m_loginUrl = m.captured(1); changed = true; }
    if (changed) emit loginChanged();
}

void PangolinController::onLoginFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_login) return;
    onLoginReadyRead();
    const bool ok = status == QProcess::NormalExit && exitCode == 0;
    appendOutput(ok ? tr("[login finished]") : tr("[login failed, exit code %1]").arg(exitCode));
    m_login->deleteLater();
    m_login = 0;
    emit loginChanged();
    emit actionFinished(QStringLiteral("login"), ok, QString());
}

void PangolinController::cancelLogin()
{
    if (m_login) m_login->kill();
}

// ---------------------------------------------------------------- logs

QString PangolinController::readClientLog(int maxLines) const
{
    return tailFile(clientLogPath(), maxLines);
}

QString PangolinController::tailFile(const QString &path, int maxLines) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return tr("Cannot read %1: %2").arg(path, f.errorString());
    // Read at most the last 256 KiB.
    const qint64 maxBytes = 256 * 1024;
    if (f.size() > maxBytes) f.seek(f.size() - maxBytes);
    QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    if (maxLines > 0 && lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return stripAnsi(lines.join(QLatin1Char('\n')));
}
