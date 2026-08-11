#include "platform/SingleInstance.hpp"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>

#include "core/Logging.hpp"

namespace weight::platform {
namespace {

/// Payload the owner sends to anyone who connects: just its process id.
QByteArray processIdPayload() {
    return QByteArray::number(QCoreApplication::applicationPid()) + '\n';
}

/// Connects to an existing owner and reads the process id it offers.
///
/// Returns true when somebody answered, which is the authoritative liveness
/// test: a process that no longer exists cannot accept a connection, whatever
/// it may have left on disk. `pid` is filled in only when the handshake also
/// produced a readable number; failing to get it is not fatal, only less
/// informative.
bool probeOwner(const QString& key, int timeoutMs, qint64* pid) {
    QLocalSocket probe;
    probe.connectToServer(key);
    if (!probe.waitForConnected(timeoutMs)) {
        return false;
    }
    if (probe.waitForReadyRead(timeoutMs)) {
        bool ok = false;
        const qint64 answered = probe.readAll().trimmed().toLongLong(&ok);
        if (ok && answered > 0 && pid != nullptr) {
            *pid = answered;
        }
    }
    probe.disconnectFromServer();
    return true;
}

}  // namespace

SingleInstance::SingleInstance(QString key, QObject* parent)
    : QObject(parent), key_(std::move(key)) {}

SingleInstance::~SingleInstance() { stopResponder(); }

bool SingleInstance::tryAcquire() {
    // Step 1: ask whether anybody is listening.
    if (probeOwner(key_, timeoutMs_, &runningPid_)) {
        return false;
    }

    // Step 2: nobody answered. On Unix a socket file may still be sitting
    // there from a killed process; removing it is safe precisely because step
    // 1 proved no one is listening on it. On Windows this is a no-op, because
    // a named pipe ceases to exist with the process that owned it.
    QLocalServer::removeServer(key_);

    startResponder();
    if (server_ != nullptr) {
        return true;
    }

    // Listening failed. A race is possible: another copy may have taken the
    // name between our probe and our listen. That is "already running" from
    // the person's point of view, not a failure.
    if (probeOwner(key_, timeoutMs_, &runningPid_)) {
        stopResponder();
        return false;
    }

    // The mechanism itself failed — an exhausted file-descriptor table, a
    // read-only runtime directory. Refusing to start over that would be worse
    // than the duplicate it is meant to prevent, so the person is let through.
    core::log::warning(
        QStringLiteral("Could not claim the single-instance name; continuing without the check."));
    stopResponder();
    return true;
}

void SingleInstance::startResponder() {
    responderThread_ = std::make_unique<QThread>();
    responderThread_->setObjectName(QStringLiteral("weight-instance-guard"));

    // A plain QObject is enough to give the server a home on the other thread
    // and to act as the receiver context for the connection. Moving it before
    // the thread starts is what fixes its affinity.
    responderContext_ = std::make_unique<QObject>();
    responderContext_->moveToThread(responderThread_.get());
    responderThread_->start();

    // The server is created, connected and set listening *on the responder
    // thread*: a QLocalServer binds its socket notifier to the thread that
    // calls listen(), and creating it here and moving it afterwards is exactly
    // the mistake that leaves notifications undelivered.
    QMetaObject::invokeMethod(
        responderContext_.get(),
        [this] {
            auto* server = new QLocalServer(responderContext_.get());
            // Refuse connections from other users: the check is per user, and
            // a socket readable by everyone would let one account block
            // another.
            server->setSocketOptions(QLocalServer::UserAccessOption);
            QObject::connect(server, &QLocalServer::newConnection, server, [server] {
                while (QLocalSocket* client = server->nextPendingConnection()) {
                    QObject::connect(client, &QLocalSocket::disconnected, client,
                                     &QLocalSocket::deleteLater);
                    client->write(processIdPayload());
                    client->flush();
                    client->disconnectFromServer();
                }
            });
            if (server->listen(key_)) {
                server_ = server;
            } else {
                delete server;
                server_ = nullptr;
            }
        },
        Qt::BlockingQueuedConnection);
}

void SingleInstance::stopResponder() {
    if (!responderThread_) {
        return;
    }
    if (responderContext_) {
        // Closing and deleting must also happen on the owning thread, for the
        // same reason creating did.
        QMetaObject::invokeMethod(
            responderContext_.get(),
            [this] {
                if (server_ != nullptr) {
                    server_->close();
                    delete server_;
                    server_ = nullptr;
                }
            },
            Qt::BlockingQueuedConnection);
    }
    responderThread_->quit();
    if (!responderThread_->wait(2000)) {
        core::log::warning(QStringLiteral("The single-instance guard did not stop in time."));
        responderThread_->terminate();
        responderThread_->wait(500);
    }
    responderContext_.reset();
    responderThread_.reset();
}

}  // namespace weight::platform
