#pragma once

#include <optional>

#include <QDateTime>
#include <QWidget>

#include "settings/Settings.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace weight::ui {

class FontLibrary;

/// Page one: the time, date and mass of a single measurement.
///
/// The page validates its own fields and reports a finished measurement; it
/// knows nothing about files, charts or estimators. That keeps it testable and
/// keeps the window free of field-level detail.
class InputPage final : public QWidget {
    Q_OBJECT

public:
    InputPage(const settings::Settings& configuration, const FontLibrary& fonts,
              QWidget* parent = nullptr);

    /// Fills every field with the current date and time.
    void resetToNow();

    /// Reads and validates all six fields.
    ///
    /// Returns the measurement, or std::nullopt after showing the first problem
    /// it found in the inline error label. Reporting one problem at a time is
    /// deliberate: a list of six complaints is harder to act on than the single
    /// field that needs attention.
    [[nodiscard]] std::optional<std::pair<QDateTime, double>> readMeasurement();

    void clearError();
    void showError(const QString& message);

    /// Widgets disabled while the application is busy or has failed.
    [[nodiscard]] QList<QWidget*> interactiveWidgets() const;

    /// Enables or disables every field and button on the page at once.
    ///
    /// Preferred over looping over `interactiveWidgets()` at the call site,
    /// because "View chart only" is not an ordinary widget: it must stay
    /// disabled while the history is empty, and a caller that enables the list
    /// blindly would switch it back on. Putting the rule here means there is
    /// one place that can get it wrong, and it is this one.
    void setInputsEnabled(bool enabled);

    /// States whether there is anything in the history to draw.
    ///
    /// "View chart only" reads the stored measurements and plots them without
    /// recording anything. With an empty history that produces a chart with no
    /// data in it — an empty pair of axes, which looks like a failure and is
    /// not — so the button is disabled instead, and its tooltip says why.
    void setHistoryAvailable(bool available);

    [[nodiscard]] bool historyAvailable() const noexcept { return historyAvailable_; }

    /// Exposed so a test can assert the gate without reaching into Qt's
    /// widget internals.
    [[nodiscard]] bool viewChartButtonEnabled() const;

    /// Moves the caret to the mass field, which is the one field that always
    /// needs typing.
    void focusPrimaryField();

signals:
    void continueRequested();
    void viewChartOnlyRequested();
    void cancelRequested();

private:
    void buildLayout(const FontLibrary& fonts);
    void connectSignals();

    [[nodiscard]] int selectedMonth() const;
    [[nodiscard]] int selectedYear() const;
    [[nodiscard]] int daysInSelectedMonth() const;

    void refreshWeekday();
    void clampDayToMonth();

    /// Puts "View chart only" into the state the two conditions imply, and
    /// sets the tooltip that explains it.
    void applyHistoryGate();

    [[nodiscard]] std::optional<int> readIntegerField(const QLineEdit* field, int minimum,
                                                      int maximum, const QString& errorMessage);

    const settings::Settings* configuration_;

    QLineEdit* hourField_ = nullptr;
    QLineEdit* minuteField_ = nullptr;
    QLineEdit* dayField_ = nullptr;
    QComboBox* monthBox_ = nullptr;
    QLineEdit* yearField_ = nullptr;
    QLineEdit* massField_ = nullptr;

    QLabel* weekdayLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;

    QPushButton* cancelButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
    QPushButton* continueButton_ = nullptr;
    QPushButton* viewChartButton_ = nullptr;

    /// Starts false, so the button is disabled from the first paint rather
    /// than being enabled and then corrected a moment later — a button that
    /// flickers from usable to unusable reads as a bug.
    bool historyAvailable_ = false;
};

}  // namespace weight::ui
