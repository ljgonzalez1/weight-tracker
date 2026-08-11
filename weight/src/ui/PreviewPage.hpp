#pragma once

#include <optional>
#include <vector>

#include <QPixmap>
#include <QWidget>

#include "settings/Settings.hpp"

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

namespace weight::ui {

class FontLibrary;

/// Page two: the chart, the layers to show and the smoothness slider.
///
/// Every curve control is built by walking the curve catalogue, so a curve
/// added there gains a checkbox, a tooltip, a colour swatch and a place in the
/// R-squared readout without this file being touched.
class PreviewPage final : public QWidget {
    Q_OBJECT

public:
    PreviewPage(const settings::Settings& settings, const FontLibrary& fonts,
                QWidget* parent = nullptr);

    [[nodiscard]] settings::RenderOptions renderOptions() const;
    [[nodiscard]] double smoothness() const;

    /// Applies a saved state without emitting change signals.
    void applyState(const settings::RenderOptions& options, double smoothness);

    void restoreDefaults();
    void showChart(const QPixmap& chart);
    void updateCoefficients(const std::vector<std::optional<double>>& values);
    void setControlsEnabled(bool enabled);
    void setBusy(bool busy);

    [[nodiscard]] bool anyTrendActive() const;
    [[nodiscard]] QList<QWidget*> interactiveWidgets() const;
    void focusPrimaryButton();

signals:
    /// A visibility checkbox changed. Carries the full state so the window can
    /// persist it immediately, which is what makes config.txt live.
    void optionsChanged();
    void smoothnessCommitted();
    void saveRequested();
    void backRequested();
    void resetRequested();
    void cancelRequested();

private:
    void buildLayout(const FontLibrary& fonts);
    void connectSignals();
    void refreshSmoothnessLabel();
    void refreshSliderEnabled();
    void refreshCoefficientLabel();

    [[nodiscard]] int toSliderPosition(double value) const;
    [[nodiscard]] double fromSliderPosition(int position) const;
    [[nodiscard]] double quantise(double value) const;

    const settings::Settings* settings_;

    QLabel* chartLabel_ = nullptr;
    QLabel* smoothnessLabel_ = nullptr;
    QLabel* coefficientLabel_ = nullptr;
    QSlider* slider_ = nullptr;

    QCheckBox* connectorBox_ = nullptr;
    QCheckBox* samplesBox_ = nullptr;
    std::vector<QCheckBox*> curveBoxes_;
    std::vector<QCheckBox*> derivativeBoxes_;

    QPushButton* cancelButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;

    std::vector<std::optional<double>> coefficients_;
    bool rerenderPending_ = false;
};

}  // namespace weight::ui
