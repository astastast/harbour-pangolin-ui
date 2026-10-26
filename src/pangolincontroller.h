#ifndef PANGOLINCONTROLLER_H
#define PANGOLINCONTROLLER_H

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

/*
 * Wraps the Pangolin CLI (https://github.com/fosrl/cli) with QProcess.
 *
 * Commands used (all verified against fosrl/cli @ 8bcb8db, v0.19.0 release):
 *   pangolin up --attach [--override-dns=false] [extra flags]   (cmd/up/client/client.go)
 *   pangolin down                                               (cmd/down/client/client.go)
 *   pangolin status --json                                      (cmd/status/client/client.go)
 *   pangolin version                                            (cmd/version/version.go)
 *   pangolin auth status                                        (cmd/auth/status/status.go)
 *   pangolin login <hostname>                                   (cmd/auth/login/login.go)
 *
 * The CLI is bundled in the RPM as /usr/bin/pangolin. Only "up" needs root
 * (TUN interface creation). Start/Stop go through the systemd *user* unit
 * harbour-pangolin.service, which runs the setuid-root helper
 * /usr/libexec/harbour-pangolin-ui/pangolin-helper (`up --attach ...` only).
 * The Top Menu toggle
 * (/usr/share/jolla-settings/pages/harbour-pangolin-ui/PangolinToggle.qml)
 * starts/stops that same unit over D-Bus, so both always agree.
 */
class PangolinController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString binaryPath READ binaryPath CONSTANT)
    Q_PROPERTY(bool binaryFound READ binaryFound CONSTANT)
    Q_PROPERTY(bool helperOk READ helperOk CONSTANT)
    Q_PROPERTY(bool overrideDns READ overrideDns WRITE setOverrideDns NOTIFY overrideDnsChanged)
    Q_PROPERTY(QString extraUpArgs READ extraUpArgs WRITE setExtraUpArgs NOTIFY extraUpArgsChanged)
    Q_PROPERTY(bool isRoot READ isRoot CONSTANT)
    Q_PROPERTY(QString cpuArch READ cpuArch CONSTANT)
    Q_PROPERTY(QString clientLogPath READ clientLogPath CONSTANT)
    Q_PROPERTY(QString serviceLogPath READ serviceLogPath CONSTANT)
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable CONSTANT)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString currentAction READ currentAction NOTIFY busyChanged)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString statusSummary READ statusSummary NOTIFY statusChanged)
    Q_PROPERTY(QString orgId READ orgId NOTIFY statusChanged)
    Q_PROPERTY(QString agentVersion READ agentVersion NOTIFY statusChanged)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY statusChanged)
    Q_PROPERTY(QString cliVersion READ cliVersion NOTIFY cliVersionChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(bool polling READ polling WRITE setPolling NOTIFY pollingChanged)

    Q_PROPERTY(bool loginActive READ loginActive NOTIFY loginChanged)
    Q_PROPERTY(QString loginCode READ loginCode NOTIFY loginChanged)
    Q_PROPERTY(QString loginUrl READ loginUrl NOTIFY loginChanged)

public:
    explicit PangolinController(QObject *parent = 0);
    ~PangolinController();

    QString binaryPath() const;
    bool binaryFound() const;
    // The setuid helper exists, is owned by root and has the setuid bit.
    bool helperOk() const;
    bool overrideDns() const { return m_overrideDns; }
    void setOverrideDns(bool v);
    QString extraUpArgs() const { return m_extraUpArgs; }
    void setExtraUpArgs(const QString &v);
    bool isRoot() const;
    QString cpuArch() const;
    QString clientLogPath() const;
    QString serviceLogPath() const;
    bool serviceAvailable() const;

    bool busy() const { return m_action != 0; }
    QString currentAction() const { return m_actionName; }
    QString state() const { return m_state; }
    QString statusSummary() const { return m_statusSummary; }
    QString orgId() const { return m_orgId; }
    QString agentVersion() const { return m_agentVersion; }
    QVariantList peers() const { return m_peers; }
    QString cliVersion() const { return m_cliVersion; }
    QString output() const { return m_output; }
    bool polling() const { return m_pollTimer.isActive(); }
    void setPolling(bool on);

    bool loginActive() const { return m_login != 0; }
    QString loginCode() const { return m_loginCode; }
    QString loginUrl() const { return m_loginUrl; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void checkVersion();
    Q_INVOKABLE void authStatus();
    Q_INVOKABLE void login(const QString &hostname);
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void cancelAction();
    Q_INVOKABLE void clearOutput();
    Q_INVOKABLE QString readClientLog(int maxLines) const;
    // Output of harbour-pangolin.service (`up --attach` and the helper), see serviceLogPath.
    Q_INVOKABLE QString readServiceLog(int maxLines) const;

signals:
    void overrideDnsChanged();
    void extraUpArgsChanged();
    void busyChanged();
    void statusChanged();
    void cliVersionChanged();
    void outputChanged();
    void pollingChanged();
    void loginChanged();
    void actionFinished(const QString &action, bool ok, const QString &hint);

private slots:
    void onActionReadyRead();
    void onActionFinished(int exitCode, QProcess::ExitStatus status);
    void onActionError(QProcess::ProcessError error);
    void onStatusFinished(int exitCode, QProcess::ExitStatus status);
    void onLoginReadyRead();
    void onLoginFinished(int exitCode, QProcess::ExitStatus status);

private:
    bool runAction(const QString &name, const QStringList &args);
    bool startProcess(const QString &name, const QString &program, const QStringList &fullArgs,
                      const QString &display);
    bool runSystemctl(const QString &name, const QStringList &args);
    bool serviceActive() const;
    void checkServiceFailed();
    bool writeServiceEnv();
    QStringList upFlags() const;
    void finishAction(bool ok, const QString &hint);
    void appendOutput(const QString &text);
    void parseStatus(const QByteArray &out, int exitCode);
    void setState(const QString &state, const QString &summary);
    QProcessEnvironment cliEnvironment() const;
    static QString stripAnsi(const QString &s);
    QString tailFile(const QString &path, int maxLines) const;
    void saveSettings();

    bool m_overrideDns;
    QString m_extraUpArgs;
    bool m_daemonReloaded;
    // Set by start(): watch the unit during the status burst and report
    // once if it fails (systemctl start returns before `up` can fail).
    bool m_watchService;

    QProcess *m_action;
    QString m_actionName;
    QByteArray m_actionOut;
    bool m_actionErrored;

    QProcess *m_status;
    QTimer m_pollTimer;
    QTimer m_burstTimer;
    int m_burstLeft;

    QProcess *m_login;
    QString m_loginCode;
    QString m_loginUrl;

    QString m_state;
    QString m_statusSummary;
    QString m_orgId;
    QString m_agentVersion;
    QVariantList m_peers;
    QString m_cliVersion;
    QString m_output;
};

#endif // PANGOLINCONTROLLER_H
