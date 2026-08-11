#pragma once

#include <QString>

#include "settings/Settings.hpp"

class QWidget;

namespace weight::ui {

/// Builds the application's dialogs, centred on their parent and worded from
/// the configuration.
///
/// Collecting them here keeps the window free of dialog plumbing and makes
/// every dialog behave the same way: modal to its parent, centred on it rather
/// than on the desktop, and cancel-by-default so an accidental return keeps the
/// user's work.
class DialogFactory {
public:
    DialogFactory(const settings::Settings& configuration, QWidget* parent);

    /// Asks whether the generated chart may be discarded.
    /// Returns true only if the user explicitly confirms.
    [[nodiscard]] bool confirmDiscard() const;

    /// Asks where to save a chart. Returns an empty string when cancelled.
    /// The `.png` suffix is enforced on the result.
    [[nodiscard]] QString askImageDestination(const QString& directory,
                                              const QString& suggestedName) const;

    /// Tells the user another copy is already running, naming its process id
    /// in small type. Static because it must work before any window exists.
    static void reportAlreadyRunning(qint64 processId);

    /// Reports a failure the user must know about but cannot fix from here.
    void reportError(const QString& title, const QString& message) const;

    /// Centres a widget on the primary screen's available area, which excludes
    /// panels and docks. Qt resolves "primary screen" identically on X11,
    /// Wayland and macOS, so no platform-specific code is needed.
    static void centreOnPrimaryScreen(QWidget* widget);

    /// Centres a widget on another widget's frame.
    static void centreOnParent(QWidget* widget, const QWidget* parent);

private:
    const settings::Settings* settings_;
    QWidget* parent_;
};

}  // namespace weight::ui
