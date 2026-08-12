#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <QDateTime>
#include <QString>

#include "core/Result.hpp"
#include "core/SessionConfig.hpp"
#include "data/CsvSampleRepository.hpp"
#include "settings/Settings.hpp"

namespace weight::app {

/// The history as it would be written, plus whether writing it is intended.
struct PendingSession {
    std::vector<data::CsvRecord> records;
    std::vector<math::SamplePoint> samples;
    bool writeHistory = true;
    std::optional<QDateTime> measurementMoment;
};

/// Every operation the interface can ask for, with no widget in sight.
///
/// Holding the use cases here keeps the window thin, lets each operation be
/// exercised without a display server, and puts the rule about when the
/// history may be written in one place instead of spread across button
/// handlers.
class ApplicationController {
public:
    ApplicationController(settings::Settings settings, QString workspace);
    ~ApplicationController();

    [[nodiscard]] const settings::Settings& settings() const noexcept { return settings_; }
    [[nodiscard]] settings::Settings& settings() noexcept { return settings_; }
    [[nodiscard]] const QString& workspace() const noexcept { return workspace_; }
    [[nodiscard]] QString historyPath() const;
    [[nodiscard]] QString imagesDirectory() const;

    /// Creates the workspace layout: the history file and the images folder.
    [[nodiscard]] core::Status prepareStorage();

    /// The saved checkbox and slider state, or the defaults on first run.
    [[nodiscard]] core::SessionState sessionState() const { return session_; }

    /// Persists the state. Returns immediately; the write happens on a worker,
    /// which is what lets a checkbox update config.txt without any perceptible
    /// delay.
    void persistSession(const core::SessionState& state);

    /// Waits for any pending config write. Called before exit.
    void flushSession();

    [[nodiscard]] core::Result<PendingSession> buildSessionWithMeasurement(
        const QDateTime& moment, double mass);
    [[nodiscard]] core::Result<PendingSession> buildSessionForViewing();

    /// How many valid measurements the history currently holds.
    ///
    /// Answers the question "is there anything to plot?", which is what
    /// decides whether "View chart only" is offered at all. Zero on a first
    /// run, and zero for a file that exists but contains only a header or only
    /// unreadable lines — a chart drawn from those would be an empty pair of
    /// axes, which is exactly the outcome worth preventing.
    ///
    /// Read from disk on each call rather than cached. The file belongs to the
    /// person, who may well edit it in a text editor while this window is
    /// open, and it is a few kilobytes: a cache here would buy nothing and
    /// could disagree with what they can see on screen.
    [[nodiscard]] int storedMeasurementCount();

    /// Convenience for the caller that only needs the yes/no.
    [[nodiscard]] bool hasStoredMeasurements() { return storedMeasurementCount() > 0; }

    /// Writes the history when the session calls for it, then the image.
    ///
    /// The order matters: a measurement that is recorded but not pictured can
    /// be re-plotted at any time, whereas an image saved from a measurement
    /// that was never stored is misleading.
    [[nodiscard]] core::Status commit(const PendingSession& session, const QImage& image,
                                      const QString& imagePath);

    [[nodiscard]] QString defaultImagePath(double smoothness, const QDateTime& moment) const;
    [[nodiscard]] QString suggestedImageName(double smoothness, const QDateTime& moment) const;

private:
    settings::Settings settings_;
    QString workspace_;
    std::unique_ptr<data::CsvSampleRepository> repository_;
    std::unique_ptr<core::SessionConfigStore> sessionStore_;
    core::SessionState session_;
};

}  // namespace weight::app
