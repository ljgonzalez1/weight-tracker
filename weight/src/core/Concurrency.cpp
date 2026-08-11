#include "core/Concurrency.hpp"

#include <QThread>
#include <QThreadPool>

#include <algorithm>

#include "settings/Strings.hpp"

namespace weight::core {
namespace {
int g_configured = 0;
}

void Concurrency::configure(int curveCount, int auxiliaryThreads, int minimumThreads) {
    const int wanted = std::max(minimumThreads, curveCount + auxiliaryThreads);
    g_configured = wanted;

    // Deliberately independent of QThread::idealThreadCount(): the pool is
    // allowed to exceed the hardware thread count so that every curve gets a
    // task immediately and the kernel does the multiplexing.
    QThreadPool::globalInstance()->setMaxThreadCount(wanted);

    // Keep idle workers around briefly. Re-creating threads for every slider
    // movement would cost more than the fits themselves.
    QThreadPool::globalInstance()->setExpiryTimeout(30000);
}

int Concurrency::maxThreadCount() {
    return g_configured > 0 ? g_configured : QThreadPool::globalInstance()->maxThreadCount();
}

int Concurrency::hardwareThreads() { return QThread::idealThreadCount(); }

QString Concurrency::describe() {
    return settings::Strings::get(QStringLiteral("log.threads"),
                                  QString::number(maxThreadCount()),
                                  QString::number(hardwareThreads()));
}
}  // namespace weight::core
