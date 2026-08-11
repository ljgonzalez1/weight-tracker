#pragma once

#include <QObject>

#include <memory>
#include <QString>

#include <vector>

#include "core/Result.hpp"
#include "settings/Settings.hpp"

namespace weight::core {

/// The part of the state that belongs to the person rather than to the
/// program: which layers they last had switched on, and where they left the
/// smoothness slider.
struct SessionState {
    bool showSampleConnector = true;
    bool showSamples = true;
    std::vector<bool> showCurve;
    std::vector<bool> showDerivative;
    double smoothness = 0.5;

    /// Sizes the vectors from the curve catalogue and fills them with the
    /// catalogue's own defaults.
    static SessionState fromDefaults(const settings::Settings& settings);

    /// Applies this state onto a RenderOptions, ignoring any length mismatch
    /// so a config written before a curve was added still loads.
    void applyTo(settings::RenderOptions& options) const;

    /// Captures the state from a RenderOptions and a slider position.
    static SessionState capture(const settings::RenderOptions& options, double smoothness);
};

/// Reads and writes `config.txt` in the workspace folder.
///
/// The file is deliberately trivial to read and edit by hand: one
/// `key = value` per line, `#` or `;` for comments, unknown keys ignored. It is
/// not the same thing as the tuning file: this one holds only what the person
/// changed by clicking, and it is rewritten the moment they click.
///
/// Curve visibility is keyed by the curve's stable **id**, not by its position,
/// so inserting a curve in the middle of the catalogue does not silently
/// reassign someone's saved choices to the wrong curves.
///
/// Saving happens off the UI thread. Every write goes through the atomic
/// writer, so a config file is never found half-written, and writes are
/// coalesced: clicking four checkboxes quickly performs one write, not four.
class SessionConfigStore : public QObject {
    Q_OBJECT

public:
    SessionConfigStore(QString path, const settings::Settings& settings,
                       QObject* parent = nullptr);
    ~SessionConfigStore() override;

    [[nodiscard]] const QString& path() const noexcept { return path_; }

    /// Loads the file. A missing file is not an error: it means this is the
    /// first run, and the caller keeps the defaults.
    [[nodiscard]] Result<SessionState> load() const;

    /// Requests a save. Returns immediately; the write happens on a worker.
    void scheduleSave(const SessionState& state);

    /// Writes synchronously. Used at shutdown and by the tests.
    [[nodiscard]] Status saveNow(const SessionState& state) const;

    /// Blocks until any scheduled save has completed. Called before exit so a
    /// click made a moment earlier is not lost.
    void flush();

    /// Renders the state as the exact bytes that would be written, so the
    /// format can be tested without touching a disk.
    [[nodiscard]] static QByteArray serialise(const SessionState& state);

    /// Parses file contents. Unknown keys and malformed values are skipped.
    [[nodiscard]] static SessionState parse(const QString& text, const SessionState& fallback);

private:
    QString path_;
    const settings::Settings* settings_;

    struct Pending;
    std::unique_ptr<Pending> pending_;
};

}  // namespace weight::core
