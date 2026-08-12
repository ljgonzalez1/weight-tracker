#include "app/ApplicationController.hpp"

#include <QDir>
#include <QFileInfo>

#include "core/Logging.hpp"
#include "data/CalendarUtils.hpp"
#include "platform/Workspace.hpp"
#include "plot/PlotImageWriter.hpp"
#include "settings/Strings.hpp"

namespace weight::app {

ApplicationController::ApplicationController(settings::Settings settings, QString workspace)
    : settings_(std::move(settings)), workspace_(std::move(workspace)) {
    repository_ = std::make_unique<data::CsvSampleRepository>(historyPath(), settings_);
    sessionStore_ = std::make_unique<core::SessionConfigStore>(
        platform::Workspace::configPath(workspace_, settings_.storage.settingsFileName),
        settings_);

    core::Result<core::SessionState> loaded = sessionStore_->load();
    session_ = loaded ? loaded.value() : core::SessionState::fromDefaults(settings_);
    if (!loaded) {
        core::log::warning(loaded.error().toString());
    }
}

ApplicationController::~ApplicationController() { flushSession(); }

QString ApplicationController::historyPath() const {
    return platform::Workspace::historyPath(workspace_, settings_.csv.defaultFileName);
}

QString ApplicationController::imagesDirectory() const {
    return platform::Workspace::imagesPath(workspace_, settings_.storage.imagesSubdirectory);
}

core::Status ApplicationController::prepareStorage() {
    if (!QDir().mkpath(imagesDirectory())) {
        return core::makeError(core::ErrorCode::PermissionDenied,
                               QStringLiteral("Preparing the workspace"),
                               QStringLiteral("%1 could not be created").arg(imagesDirectory()));
    }
    return repository_->ensureExists();
}

void ApplicationController::persistSession(const core::SessionState& state) {
    session_ = state;
    sessionStore_->scheduleSave(state);
}

void ApplicationController::flushSession() {
    if (sessionStore_) {
        sessionStore_->flush();
    }
}

core::Result<PendingSession> ApplicationController::buildSessionForViewing() {
    core::Result<data::LoadedHistory> loaded = repository_->load();
    if (!loaded) {
        return loaded.error();
    }
    PendingSession session;
    session.records = data::CsvSampleRepository::sortForWriting(std::move(loaded.value().records),
                                                                settings_.csv);
    for (const data::CsvRecord& record : session.records) {
        if (record.isValid()) {
            session.samples.push_back(record.toSample());
        }
    }
    session.writeHistory = false;
    return session;
}

int ApplicationController::storedMeasurementCount() {
    core::Result<data::LoadedHistory> loaded = repository_->load();
    if (!loaded) {
        // An unreadable history is not the same as an empty one, but for this
        // question the answer is the same: there is nothing that can be
        // plotted, so the button stays off. The error itself is reported by
        // whichever operation actually needs the file.
        return 0;
    }
    int valid = 0;
    for (const data::CsvRecord& record : loaded.value().records) {
        if (record.isValid()) {
            ++valid;
        }
    }
    return valid;
}

core::Result<PendingSession> ApplicationController::buildSessionWithMeasurement(
    const QDateTime& moment, double mass) {
    core::Result<data::LoadedHistory> loaded = repository_->load();
    if (!loaded) {
        return loaded.error();
    }

    const double day = data::CalendarUtils::fractionalDay(
        moment, settings_.plot.geometry.originDate, settings_.csv);
    const QString timestamp = moment.toString(settings_.csv.timestampFormat);

    std::vector<data::CsvRecord> records = std::move(loaded.value().records);
    records.push_back(data::CsvRecord::makeValid(day, mass, timestamp));

    PendingSession session;
    QStringList repairs;
    session.records =
        data::CsvSampleRepository::sortForWriting(std::move(records), settings_.csv, &repairs);
    for (const QString& repair : repairs) {
        core::log::repair(repair);
    }
    for (const data::CsvRecord& record : session.records) {
        if (record.isValid()) {
            session.samples.push_back(record.toSample());
        }
    }
    session.writeHistory = true;
    session.measurementMoment = moment;
    return session;
}

core::Status ApplicationController::commit(const PendingSession& session, const QImage& image,
                                           const QString& imagePath) {
    if (session.writeHistory) {
        if (const core::Status status = repository_->save(session.records); !status) {
            return status;
        }
    }
    if (!QDir().mkpath(QFileInfo(imagePath).absolutePath())) {
        return core::makeError(core::ErrorCode::PermissionDenied, QStringLiteral("Saving"),
                               QStringLiteral("%1 could not be created")
                                   .arg(QFileInfo(imagePath).absolutePath()));
    }
    return plot::PlotImageWriter::writePng(image, imagePath);
}

QString ApplicationController::suggestedImageName(double smoothness,
                                                  const QDateTime& moment) const {
    return plot::PlotImageWriter::buildFileName(settings_, smoothness, moment);
}

QString ApplicationController::defaultImagePath(double smoothness,
                                                const QDateTime& moment) const {
    return QDir(imagesDirectory()).filePath(suggestedImageName(smoothness, moment));
}

}  // namespace weight::app
