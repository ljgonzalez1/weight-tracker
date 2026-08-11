#include "core/SessionConfig.hpp"

#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <cmath>
#include <QThreadPool>
#include <QWaitCondition>
#include <QtConcurrent/QtConcurrentRun>

#include "core/Logging.hpp"
#include "data/AtomicFileWriter.hpp"
#include "settings/CurveCatalog.hpp"

namespace weight::core {
namespace {

QString boolText(bool value) {
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

std::optional<bool> parseBool(const QString& text) {
    const QString lowered = text.trimmed().toLower();
    if (lowered == QLatin1String("true") || lowered == QLatin1String("1")
        || lowered == QLatin1String("yes") || lowered == QLatin1String("on")) {
        return true;
    }
    if (lowered == QLatin1String("false") || lowered == QLatin1String("0")
        || lowered == QLatin1String("no") || lowered == QLatin1String("off")) {
        return false;
    }
    return std::nullopt;
}

}  // namespace

SessionState SessionState::fromDefaults(const settings::Settings& settings) {
    SessionState state;
    state.showSampleConnector = settings.render.showSampleConnector;
    state.showSamples = settings.render.showSamples;
    state.showCurve = settings.render.showTrendCurve;
    state.showDerivative = settings.render.showDerivative;
    state.smoothness = settings.trend.common.generalSmoothness;
    return state;
}

void SessionState::applyTo(settings::RenderOptions& options) const {
    options.showSampleConnector = showSampleConnector;
    options.showSamples = showSamples;
    // Copy element by element rather than wholesale: a config written before a
    // curve was added is shorter than the catalogue, and the extra curves must
    // keep their own defaults rather than becoming false by accident.
    for (std::size_t i = 0; i < options.showTrendCurve.size() && i < showCurve.size(); ++i) {
        options.showTrendCurve[i] = showCurve[i];
    }
    for (std::size_t i = 0; i < options.showDerivative.size() && i < showDerivative.size(); ++i) {
        options.showDerivative[i] = showDerivative[i];
    }
}

SessionState SessionState::capture(const settings::RenderOptions& options, double smoothness) {
    SessionState state;
    state.showSampleConnector = options.showSampleConnector;
    state.showSamples = options.showSamples;
    state.showCurve = options.showTrendCurve;
    state.showDerivative = options.showDerivative;
    state.smoothness = smoothness;
    return state;
}

/// Holds the most recent state waiting to be written, plus the flag that stops
/// several writers piling up.
struct SessionConfigStore::Pending {
    QMutex mutex;
    QWaitCondition idle;
    SessionState state;
    bool hasState = false;
    bool writing = false;
};

SessionConfigStore::SessionConfigStore(QString path, const settings::Settings& settings,
                                       QObject* parent)
    : QObject(parent),
      path_(std::move(path)),
      settings_(&settings),
      pending_(std::make_unique<Pending>()) {}

SessionConfigStore::~SessionConfigStore() { flush(); }

QByteArray SessionConfigStore::serialise(const SessionState& state) {
    const std::span<const settings::CurveDescriptor> catalogue = settings::curveCatalogue();

    QString text;
    text += QStringLiteral("; Weight — what you last had switched on.\n");
    text += QStringLiteral("; Rewritten automatically whenever you change the chart.\n");
    text += QStringLiteral("; Lines starting with ; or # are ignored, as are unknown keys.\n\n");

    text += QStringLiteral("smoothness = %1\n").arg(state.smoothness, 0, 'f', 2);
    text += QStringLiteral("show.connector = %1\n").arg(boolText(state.showSampleConnector));
    text += QStringLiteral("show.samples = %1\n\n").arg(boolText(state.showSamples));

    // Keyed by stable id, never by position: inserting a curve in the middle of
    // the catalogue must not reassign someone's saved choices.
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        const bool curve = i < state.showCurve.size() && state.showCurve[i];
        const bool derivative = i < state.showDerivative.size() && state.showDerivative[i];
        text += QStringLiteral("curve.%1 = %2\n").arg(catalogue[i].id, boolText(curve));
        text += QStringLiteral("derivative.%1 = %2\n").arg(catalogue[i].id, boolText(derivative));
    }
    return text.toUtf8();
}

SessionState SessionConfigStore::parse(const QString& text, const SessionState& fallback) {
    SessionState state = fallback;
    const std::span<const settings::CurveDescriptor> catalogue = settings::curveCatalogue();
    state.showCurve.resize(catalogue.size(), false);
    state.showDerivative.resize(catalogue.size(), false);

    const QStringList lines =
        text.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")), Qt::SkipEmptyParts);

    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();

        if (key == QLatin1String("smoothness")) {
            bool ok = false;
            const double parsed = value.toDouble(&ok);
            if (ok && std::isfinite(parsed)) {
                state.smoothness = parsed;
            }
            continue;
        }
        if (key == QLatin1String("show.connector")) {
            if (const auto parsed = parseBool(value)) {
                state.showSampleConnector = *parsed;
            }
            continue;
        }
        if (key == QLatin1String("show.samples")) {
            if (const auto parsed = parseBool(value)) {
                state.showSamples = *parsed;
            }
            continue;
        }

        const bool isCurve = key.startsWith(QLatin1String("curve."));
        const bool isDerivative = key.startsWith(QLatin1String("derivative."));
        if (!isCurve && !isDerivative) {
            continue;  // unknown key: ignored, never fatal
        }
        const QString id = key.section(QLatin1Char('.'), 1);
        const std::optional<std::size_t> index = settings::curveIndex(id);
        if (!index.has_value()) {
            continue;  // a curve that no longer exists; harmless
        }
        const auto parsed = parseBool(value);
        if (!parsed.has_value()) {
            continue;
        }
        if (isCurve) {
            state.showCurve[*index] = *parsed;
        } else {
            state.showDerivative[*index] = *parsed;
        }
    }
    return state;
}

Result<SessionState> SessionConfigStore::load() const {
    const SessionState fallback = SessionState::fromDefaults(*settings_);

    QFile file(path_);
    if (!QFileInfo::exists(path_)) {
        return fallback;  // first run
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return makeError(ErrorCode::ReadFailed, QStringLiteral("Reading config.txt"),
                         QStringLiteral("%1: %2").arg(path_, file.errorString()));
    }
    return parse(QString::fromUtf8(file.readAll()), fallback);
}

Status SessionConfigStore::saveNow(const SessionState& state) const {
    return data::AtomicFileWriter::write(path_, serialise(state), /*verify=*/false);
}

void SessionConfigStore::scheduleSave(const SessionState& state) {
    bool startWorker = false;
    {
        const QMutexLocker locker(&pending_->mutex);
        // Keep only the newest state. Four rapid clicks leave one write to do,
        // not four, and the one that runs is the one the user ended on.
        pending_->state = state;
        pending_->hasState = true;
        if (!pending_->writing) {
            pending_->writing = true;
            startWorker = true;
        }
    }
    if (!startWorker) {
        return;  // a worker is already running and will pick up the new state
    }

    // Detached from the UI thread: a slow or full disk must never make a
    // checkbox feel sticky.
    static_cast<void>(QtConcurrent::run([this] {
        for (;;) {
            SessionState toWrite;
            {
                const QMutexLocker locker(&pending_->mutex);
                if (!pending_->hasState) {
                    pending_->writing = false;
                    pending_->idle.wakeAll();
                    return;
                }
                toWrite = pending_->state;
                pending_->hasState = false;
            }
            if (const Status status = saveNow(toWrite); !status) {
                log::warning(QStringLiteral("Could not save config.txt: %1")
                                 .arg(status.error().toString()));
            }
        }
    }));
}

void SessionConfigStore::flush() {
    QMutexLocker locker(&pending_->mutex);
    while (pending_->writing) {
        // Waits for the worker to drain; bounded because each pass either
        // writes or exits.
        if (!pending_->idle.wait(&pending_->mutex, 5000)) {
            log::warning(QStringLiteral("Timed out waiting for config.txt to be written."));
            return;
        }
    }
}

}  // namespace weight::core
