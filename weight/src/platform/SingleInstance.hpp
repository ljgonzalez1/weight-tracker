#pragma once

#include <memory>

#include <QObject>
#include <QString>

class QLocalServer;
class QThread;

namespace weight::platform {

/// Ensures only one copy of the program runs at a time.
///
/// Why not a lock file: a lock file records an intention, not a fact. If the
/// process is killed with SIGKILL, crashes, or the machine loses power, the
/// file survives and every later run refuses to start until someone deletes it
/// by hand. Writing the pid into it and checking whether that pid is alive is
/// better but still wrong, because pids are recycled.
///
/// This uses a **named local socket** instead, which is the standard mechanism
/// on all three platforms and is owned by the kernel rather than by the
/// filesystem:
///
///   * Windows — a named pipe. The pipe ceases to exist the moment the owning
///     process ends, however it ends. There is nothing to leave behind.
///   * Linux, macOS — a Unix domain socket. The socket *file* can survive a
///     crash, but a connection attempt to it fails immediately with
///     ConnectionRefused, which is exactly the signal that the owner is gone.
///     The stale entry is then removed and this process takes ownership.
///
/// So the liveness question is answered by trying to talk to the other
/// instance, not by inspecting a file. A dead instance cannot answer.
///
/// The running instance also replies with its process id, so the second copy
/// can name it in the message it shows.
class SingleInstance : public QObject {
    Q_OBJECT

public:
    /// `key` must be stable across runs and unique to this application.
    explicit SingleInstance(QString key, QObject* parent = nullptr);
    ~SingleInstance() override;

    /// Attempts to become the one running instance.
    ///
    /// Returns true when this process is now the owner. Returns false when
    /// another instance is already running, in which case `runningProcessId()`
    /// holds its pid when it could be obtained.
    [[nodiscard]] bool tryAcquire();

    /// Process id of the instance already running, or 0 when it could not be
    /// determined. Only meaningful after tryAcquire() returned false.
    [[nodiscard]] qint64 runningProcessId() const noexcept { return runningPid_; }

    /// Milliseconds allowed for the handshake with the other instance.
    void setHandshakeTimeout(int milliseconds) noexcept { timeoutMs_ = milliseconds; }

private:
    /// Serves the process id to anyone who connects.
    ///
    /// It runs on its own thread, with its own event loop, and that is not
    /// decoration. QLocalServer delivers newConnection through a socket
    /// notifier, which only fires while an event loop is spinning. If the
    /// server lived on the main thread, then any moment the main thread was
    /// busy — a long fit, a modal dialog, a full-resolution export — a second
    /// copy would be told "already running" with no process id, because the
    /// owner never got round to answering. Giving the guard a thread of its
    /// own makes the answer independent of whatever the application is doing.
    void startResponder();
    void stopResponder();

    QString key_;

    /// Owned by responderContext_, so it is created and destroyed on the
    /// responder thread. A QLocalServer must not be touched from another
    /// thread once it is listening.
    QLocalServer* server_ = nullptr;
    std::unique_ptr<QThread> responderThread_;
    std::unique_ptr<QObject> responderContext_;

    qint64 runningPid_ = 0;
    int timeoutMs_ = 500;
};

}  // namespace weight::platform
