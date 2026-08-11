#include "ui/PreviewPage.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "settings/CurveCatalog.hpp"
#include "settings/Strings.hpp"
#include "ui/FontLibrary.hpp"

namespace weight::ui {
namespace {

using settings::Strings;

/// A small filled square in the curve's own colour.
///
/// Drawn rather than taken from an emoji, because an emoji can only ever
/// approximate three fixed colours: a curve added to the catalogue with any
/// r, g, b gets a swatch that matches the chart exactly.
QPixmap colourSwatch(const QColor& colour, int side) {
    const int edge = std::max(side, 6);
    QPixmap pixmap(edge, edge);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(QRectF(0.5, 0.5, edge - 1.0, edge - 1.0), 2.0, 2.0);
    return pixmap;
}

}  // namespace

PreviewPage::PreviewPage(const settings::Settings& settings, const FontLibrary& fonts,
                         QWidget* parent)
    : QWidget(parent), settings_(&settings) {
    coefficients_.assign(settings::curveCount(), std::nullopt);
    buildLayout(fonts);
    connectSignals();
    restoreDefaults();
}

void PreviewPage::buildLayout(const FontLibrary& fonts) {
    const settings::UiTheme& theme = settings_->ui;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme.mainPaddingHorizontal, theme.mainPaddingVertical,
                              theme.mainPaddingHorizontal, theme.mainPaddingVertical);
    outer->setSpacing(theme.paddingVertical);

    auto* title = new QLabel(Strings::get(QStringLiteral("page.preview.title")), this);
    title->setFont(fonts.label());
    title->setAlignment(Qt::AlignCenter);
    outer->addWidget(title);

    auto* hint = new QLabel(Strings::get(QStringLiteral("page.preview.hint")), this);
    hint->setFont(fonts.secondary());
    hint->setAlignment(Qt::AlignCenter);
    outer->addWidget(hint);

    auto* frame = new QFrame(this);
    frame->setFrameShape(QFrame::Box);
    frame->setFrameShadow(QFrame::Plain);
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(theme.previewFrameMargin, theme.previewFrameMargin,
                                    theme.previewFrameMargin, theme.previewFrameMargin);
    chartLabel_ = new QLabel(frame);
    chartLabel_->setAlignment(Qt::AlignCenter);
    frameLayout->addWidget(chartLabel_);
    outer->addWidget(frame);

    // ---------------------------------------------------------------------
    // Visibility controls, built entirely by walking the curve catalogue.
    // ---------------------------------------------------------------------
    auto* group = new QGroupBox(Strings::get(QStringLiteral("page.preview.options")), this);
    group->setFont(fonts.label());
    auto* grid = new QGridLayout(group);
    grid->setContentsMargins(8, 6, 8, 6);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(2);

    connectorBox_ = new QCheckBox(Strings::get(QStringLiteral("page.preview.connector")), group);
    samplesBox_ = new QCheckBox(Strings::get(QStringLiteral("page.preview.samples")), group);
    grid->addWidget(connectorBox_, 0, 0);
    grid->addWidget(samplesBox_, 0, 1);

    const std::span<const settings::CurveDescriptor> catalogue = settings::curveCatalogue();
    curveBoxes_.assign(catalogue.size(), nullptr);
    derivativeBoxes_.assign(catalogue.size(), nullptr);

    const int swatchSide = QFontMetrics(fonts.label()).height() - 2;

    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        const QString letter = QString(catalogue[i].letter);
        const QString shortName = settings_->shortNameFor(i);
        const QIcon swatch(colourSwatch(settings_->colourFor(i), swatchSide));

        curveBoxes_[i] = new QCheckBox(
            Strings::get(QStringLiteral("page.preview.curve"), letter, shortName), group);
        curveBoxes_[i]->setToolTip(settings_->longNameFor(i));
        curveBoxes_[i]->setIcon(swatch);

        derivativeBoxes_[i] = new QCheckBox(
            Strings::get(QStringLiteral("page.preview.derivative"), letter), group);
        derivativeBoxes_[i]->setToolTip(
            Strings::get(QStringLiteral("page.preview.derivative.tip"), letter, shortName));
        derivativeBoxes_[i]->setIcon(swatch);

        grid->addWidget(curveBoxes_[i], static_cast<int>(i) + 1, 0);
        grid->addWidget(derivativeBoxes_[i], static_cast<int>(i) + 1, 1);
    }
    outer->addWidget(group);

    auto* sliderColumn = new QVBoxLayout();
    sliderColumn->setSpacing(2);
    outer->addLayout(sliderColumn);

    auto* header = new QHBoxLayout();
    sliderColumn->addLayout(header);

    smoothnessLabel_ = new QLabel(QString(), this);
    smoothnessLabel_->setFont(fonts.label());
    header->addWidget(smoothnessLabel_);
    header->addStretch(1);

    coefficientLabel_ = new QLabel(QString(), this);
    coefficientLabel_->setFont(fonts.secondary());
    coefficientLabel_->setTextFormat(Qt::RichText);
    coefficientLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    coefficientLabel_->setToolTip(Strings::get(QStringLiteral("score.tooltip")));
    header->addWidget(coefficientLabel_);

    const settings::SmoothnessSlider& setup = settings_->slider;
    const int steps =
        static_cast<int>(std::lround((setup.maximum - setup.minimum) / setup.resolution));
    const int tickSteps =
        std::max(1, static_cast<int>(std::lround(setup.tickInterval / setup.resolution)));

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setMinimum(0);
    slider_->setMaximum(std::max(1, steps));
    slider_->setSingleStep(1);
    slider_->setPageStep(tickSteps);
    slider_->setTickPosition(QSlider::TicksBelow);
    slider_->setTickInterval(tickSteps);
    slider_->setMinimumWidth(std::max(theme.minimumSliderWidth,
                                      settings_->plot.geometry.previewWidthPixels
                                          - theme.sliderWidthMargin));
    sliderColumn->addWidget(slider_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(theme.buttonSpacing);
    outer->addLayout(buttons);
    buttons->addStretch(1);

    const auto addButton = [&](const QString& caption, int characters, bool primary) {
        auto* button = new QPushButton(caption, this);
        button->setFont(fonts.button());
        button->setMinimumWidth(fonts.widthForCharacters(fonts.button(), characters));
        button->setDefault(primary);
        button->setAutoDefault(primary);
        buttons->addWidget(button);
        return button;
    };

    cancelButton_ = addButton(Strings::get(QStringLiteral("button.cancel")),
                              theme.smallButtonCharacters, false);
    resetButton_ = addButton(Strings::get(QStringLiteral("button.reset")),
                             theme.smallButtonCharacters, false);
    backButton_ = addButton(Strings::get(QStringLiteral("button.back")),
                            theme.smallButtonCharacters, false);
    saveButton_ = addButton(Strings::get(QStringLiteral("button.save")),
                            theme.largeButtonCharacters, true);
    buttons->addStretch(1);
}

void PreviewPage::connectSignals() {
    connect(connectorBox_, &QCheckBox::toggled, this, &PreviewPage::optionsChanged);
    connect(samplesBox_, &QCheckBox::toggled, this, &PreviewPage::optionsChanged);

    const auto onCurveToggled = [this] {
        refreshSliderEnabled();
        refreshCoefficientLabel();
        emit optionsChanged();
    };
    for (std::size_t i = 0; i < curveBoxes_.size(); ++i) {
        connect(curveBoxes_[i], &QCheckBox::toggled, this, onCurveToggled);
        connect(derivativeBoxes_[i], &QCheckBox::toggled, this, onCurveToggled);
    }

    connect(slider_, &QSlider::valueChanged, this, [this] { refreshSmoothnessLabel(); });
    connect(slider_, &QSlider::sliderReleased, this, [this] {
        if (anyTrendActive()) {
            emit smoothnessCommitted();
        }
    });
    // Arrow and page keys emit actionTriggered rather than sliderReleased.
    // Collapsing a burst into one request keeps a held key responsive; the
    // render engine coalesces anything that still arrives too fast.
    connect(slider_, &QSlider::actionTriggered, this, [this] {
        if (rerenderPending_) {
            return;
        }
        rerenderPending_ = true;
        QTimer::singleShot(0, this, [this] {
            rerenderPending_ = false;
            if (anyTrendActive()) {
                emit smoothnessCommitted();
            }
        });
    });

    connect(cancelButton_, &QPushButton::clicked, this, &PreviewPage::cancelRequested);
    connect(resetButton_, &QPushButton::clicked, this, &PreviewPage::resetRequested);
    connect(backButton_, &QPushButton::clicked, this, &PreviewPage::backRequested);
    connect(saveButton_, &QPushButton::clicked, this, &PreviewPage::saveRequested);

    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* shortcut = new QShortcut(QKeySequence(key), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, &PreviewPage::saveRequested);
    }
}

settings::RenderOptions PreviewPage::renderOptions() const {
    settings::RenderOptions options = settings_->render;
    options.showTrendCurve.assign(curveBoxes_.size(), false);
    options.showDerivative.assign(derivativeBoxes_.size(), false);
    options.showSampleConnector = connectorBox_->isChecked();
    options.showSamples = samplesBox_->isChecked();
    for (std::size_t i = 0; i < curveBoxes_.size(); ++i) {
        options.showTrendCurve[i] = curveBoxes_[i]->isChecked();
        options.showDerivative[i] = derivativeBoxes_[i]->isChecked();
    }
    return options;
}

double PreviewPage::quantise(double value) const {
    const settings::SmoothnessSlider& setup = settings_->slider;
    const double clamped = std::clamp(value, setup.minimum, setup.maximum);
    const double snapped = setup.minimum
                           + std::round((clamped - setup.minimum) / setup.resolution)
                                 * setup.resolution;
    return std::clamp(snapped, setup.minimum, setup.maximum);
}

int PreviewPage::toSliderPosition(double value) const {
    const settings::SmoothnessSlider& setup = settings_->slider;
    const double clamped = std::clamp(value, setup.minimum, setup.maximum);
    return static_cast<int>(std::lround((clamped - setup.minimum) / setup.resolution));
}

double PreviewPage::fromSliderPosition(int position) const {
    const settings::SmoothnessSlider& setup = settings_->slider;
    return quantise(setup.minimum + position * setup.resolution);
}

double PreviewPage::smoothness() const { return fromSliderPosition(slider_->value()); }

void PreviewPage::applyState(const settings::RenderOptions& options, double smoothness) {
    const QSignalBlocker blockConnector(connectorBox_);
    const QSignalBlocker blockSamples(samplesBox_);
    const QSignalBlocker blockSlider(slider_);

    connectorBox_->setChecked(options.showSampleConnector);
    samplesBox_->setChecked(options.showSamples);
    for (std::size_t i = 0; i < curveBoxes_.size(); ++i) {
        const QSignalBlocker blockCurve(curveBoxes_[i]);
        const QSignalBlocker blockDerivative(derivativeBoxes_[i]);
        curveBoxes_[i]->setChecked(i < options.showTrendCurve.size()
                                   && options.showTrendCurve[i]);
        derivativeBoxes_[i]->setChecked(i < options.showDerivative.size()
                                        && options.showDerivative[i]);
    }
    slider_->setValue(toSliderPosition(quantise(smoothness)));

    refreshSmoothnessLabel();
    refreshSliderEnabled();
    refreshCoefficientLabel();
}

void PreviewPage::restoreDefaults() {
    settings::RenderOptions defaults = settings_->render;
    defaults.resizeToCatalogue();
    applyState(defaults, settings_->trend.common.generalSmoothness);
}

void PreviewPage::showChart(const QPixmap& chart) {
    chartLabel_->setPixmap(chart);
    // Pinning the label to the pixmap makes the surrounding layout grow to the
    // image instead of quietly cropping it.
    chartLabel_->setFixedSize(chart.size());
}

void PreviewPage::updateCoefficients(const std::vector<std::optional<double>>& values) {
    for (std::size_t i = 0; i < coefficients_.size() && i < values.size(); ++i) {
        if (values[i].has_value()) {
            coefficients_[i] = values[i];
        }
    }
    refreshCoefficientLabel();
}

void PreviewPage::refreshCoefficientLabel() {
    QStringList parts;
    for (std::size_t i = 0; i < curveBoxes_.size(); ++i) {
        if (!curveBoxes_[i]->isChecked()) {
            continue;
        }
        const QString shown = (i < coefficients_.size() && coefficients_[i].has_value())
                                  ? QString::number(*coefficients_[i], 'f', 4)
                                  : Strings::get(QStringLiteral("score.unavailable"));
        parts.append(QStringLiteral(
                         "<span style=\"color:%1;\">(R\u00b2<sub>%2</sub>&nbsp;=&nbsp;%3)</span>")
                         .arg(settings_->colourFor(i).name())
                         .arg(settings_->letterFor(i))
                         .arg(shown));
    }
    coefficientLabel_->setText(parts.join(QStringLiteral(" ; ")));
}

void PreviewPage::refreshSmoothnessLabel() {
    smoothnessLabel_->setText(
        Strings::get(QStringLiteral("slider.smoothness"),
                     QString::number(smoothness(), 'f', settings_->slider.displayedDecimals)));
}

bool PreviewPage::anyTrendActive() const {
    for (std::size_t i = 0; i < curveBoxes_.size(); ++i) {
        if (curveBoxes_[i]->isChecked() || derivativeBoxes_[i]->isChecked()) {
            return true;
        }
    }
    return false;
}

void PreviewPage::refreshSliderEnabled() { slider_->setEnabled(anyTrendActive()); }

void PreviewPage::setControlsEnabled(bool enabled) {
    for (QWidget* widget : interactiveWidgets()) {
        widget->setEnabled(enabled);
    }
    if (enabled) {
        refreshSliderEnabled();
    }
}

void PreviewPage::setBusy(bool busy) {
    // Controls stay live while a render is in flight. The engine coalesces
    // requests, so the person can keep moving the slider and simply sees the
    // newest chart when it lands; disabling the controls would make a
    // responsive design feel slower than it is.
    setCursor(busy ? Qt::BusyCursor : Qt::ArrowCursor);
}

QList<QWidget*> PreviewPage::interactiveWidgets() const {
    QList<QWidget*> widgets{connectorBox_, samplesBox_, slider_,      cancelButton_,
                            resetButton_,  backButton_, saveButton_};
    for (QCheckBox* box : curveBoxes_) {
        widgets.append(box);
    }
    for (QCheckBox* box : derivativeBoxes_) {
        widgets.append(box);
    }
    return widgets;
}

void PreviewPage::focusPrimaryButton() { saveButton_->setFocus(Qt::OtherFocusReason); }

}  // namespace weight::ui
