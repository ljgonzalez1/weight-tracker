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
};

}  // namespace weight::ui
