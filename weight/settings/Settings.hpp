#pragma once

#include <QColor>
#include <QDate>
#include <QString>
#include <QStringList>

#include <array>
#include <vector>

#include "math/common/TrendParameters.hpp"

/// \file Settings.hpp
/// Declares the complete, strongly typed configuration of the application.
///
/// Every value the program depends on is a field of one of the structs below.
/// The defaults live in a single place, src/core/Settings.cpp, which is
/// the file to edit in order to change behaviour without touching logic.
/// Users can override any of them at run time through the INI file managed by
/// SettingsStore, and a subset through command line options.
namespace weight::settings {

// ---------------------------------------------------------------------------
// Input validation
// ---------------------------------------------------------------------------

/// Bounds enforced both by the GUI keystroke filters and by the CSV parser.
/// A value outside these bounds is rejected as invalid rather than clamped,
/// because silently altering a recorded measurement would corrupt the history.
struct InputLimits {
    int minimumHour;
    int maximumHour;
    int minimumMinute;
    int maximumMinute;
    int minimumDayOfMonth;
    int maximumDayOfMonth;  ///< Absolute ceiling; narrowed per month/year.
    int minimumYear;
    int maximumYear;
    double minimumMass;  ///< kilograms
    double maximumMass;  ///< kilograms
};

// ---------------------------------------------------------------------------
// CSV persistence
// ---------------------------------------------------------------------------

/// Layout and tolerances of the semicolon-separated history file.
///
/// The canonical record is `day;mass;timestamp` where `day` is a fractional
/// offset from the plot origin (negative values are legal and mean "before the
/// origin"), `mass` is in kilograms and `timestamp` is informational.
struct CsvFormat {
    QString defaultFileName;
    QString header;              ///< Written when creating or rewriting a file.
    QStringList legacyHeaders;   ///< Recognised on read for backwards compatibility.
    QString separator;           ///< Canonical delimiter used when writing.
    QStringList candidateSeparators;  ///< Tried on read, in preference order.
    int dayFractionDecimals;     ///< Precision of the day column.
    int massDecimals;            ///< Precision of the mass column.
    double secondsPerDay;        ///< Length of a mean solar day.
    QString timestampFormat;     ///< Qt format string of the third column.
    int separatorVoteSampleLines;  ///< Lines inspected when guessing the delimiter.
    QStringList headerTokens;    ///< Words that identify a header row.
};

// ---------------------------------------------------------------------------
// Storage locations
// ---------------------------------------------------------------------------

/// Where user data lives. Nothing is ever written next to the executable or
/// inside the installation prefix; the platform conventions are resolved
/// through QStandardPaths.
/// Thread policy. Deliberately expressed as counts of *work*, not of cores.
struct ConcurrencyPolicy {
    /// Threads on top of one per curve: the renderer and the config writer.
    int auxiliaryThreads = 3;
    /// Floor, so a single-curve build still has room to overlap work.
    int minimumThreads = 6;
};

struct StorageLayout {
    QString applicationName;      ///< Also the data directory name.
    QString organisationDomain;   ///< Reverse DNS, used for the macOS bundle id.
    /// Folder created inside whichever Documents directory wins. Everything
    /// the program owns lives under it: the history, the images and config.txt.
    QString folderName;
    QString imagesSubdirectory;   ///< Relative to the workspace folder.
    QString settingsFileName;     ///< Plain-text file holding the session state.
};

// ---------------------------------------------------------------------------
// Output image naming
// ---------------------------------------------------------------------------

struct OutputNaming {
    QString filePrefix;
    QString timestampFormat;
    QString smoothnessInfix;
    int smoothnessDecimals;
    QString fileExtension;
};

// ---------------------------------------------------------------------------
// Plot geometry and styling
// ---------------------------------------------------------------------------

/// Axis ranges, tick steps and image size of the generated chart.
struct PlotGeometry {
    /// Epoch the CSV day column counts from. Fixed, because the stored day
    /// values are offsets from it: changing it would reinterpret every record.
    /// The visible window is derived from the data instead, not from this.
    QDate originDate;

    /// How far right the axis reaches. Whichever of the two is further away
    /// wins, so a young record still gets a full year of context and a long one
    /// still gets room ahead of the last measurement.
    double horizonFromFirstDays;   ///< One year, by default.
    double horizonFromLastDays;    ///< Three months, by default.

    /// Tick spacings tried in order until the label count fits. Multiples of a
    /// week, so every label lands on the same weekday.
    std::vector<double> labelStepCandidatesDays;
    int maximumXLabels;            ///< Above this the axis becomes unreadable.  ///< Calendar date mapped to day 0.0 on the X axis.

    double xMinimum;
    double xMaximum;
    double yMinimum;  ///< kilograms
    double yMaximum;  ///< kilograms

    double xLabelStepDays;
    double yLabelStep;  ///< Major tick spacing on the mass axis, kg.
    double yGridStep;   ///< Minor grid spacing on the mass axis, kg.

    int widthPixels;   ///< Final exported image.
    int heightPixels;
    int dotsPerInch;

    int previewWidthPixels;  ///< In-window preview; height follows the aspect ratio.
    int previewDotsPerInch;

    /// Axes rectangle expressed as fractions of the figure, matching the
    /// convention of the original implementation so that the exported image
    /// keeps the same proportions.
    double marginLeft;
    double marginRight;
    double marginTop;
    double marginBottom;

    /// Right margin used when the secondary (rate of change) axis is visible,
    /// leaving room for its tick labels.
    double marginRightWithSecondaryAxis;
};

/// Colours of every drawable element.
struct PlotPalette {
    QColor background;
    QColor axisLine;
    QColor axisText;
    QColor titleText;
    QColor majorGrid;
    QColor minorGrid;
    QColor samplePoints;
    /// Curve colours are no longer stored here: they come from the curve
    /// catalogue, where each entry states its own r, g, b. Keeping one source
    /// avoids the two lists drifting apart when a curve is added.
    QColor derivativeZeroLine;
};

/// Font sizes in typographic points; converted to pixels using the DPI of the
/// target image, so the chart looks identical at any resolution.
struct PlotTypography {
    QString fontFamily;
    double titlePointSize;
    double axisLabelPointSize;
    double tickLabelPointSize;
    double xTickRotationDegrees;
};

/// Line widths and opacities, in typographic points where relevant.
struct PlotLineStyle {
    double trendCurveWidth;
    double trendCurveOpacity;
    double trendInnerBandHalfWidth;  ///< kg
    double trendOuterBandHalfWidth;  ///< kg
    double trendInnerBandOpacity;
    double trendOuterBandOpacity;

    double sampleMarkerArea;  ///< points squared, matching scatter semantics
    double sampleMarkerOpacity;
    double sampleConnectorWidth;
    double sampleConnectorOpacity;

    double majorGridWidth;
    double minorGridWidth;
    double axisLineWidth;
    double tickLength;
    double tickWidth;
    double axisLabelPadding;
    double tickLabelPadding;
    double titlePadding;
};

/// Secondary axis carrying the rate of change of each active trend curve.
struct DerivativeAxisStyle {
    double yMinimum;
    double yMaximum;
    double yTickStep;
    double lineWidthRatio;  ///< Relative to trendCurveWidth.
    double lineOpacity;
    QVector<double> lineDashPattern;      ///< In units of the pen width.
    QVector<double> zeroLineDashPattern;
    double zeroLineOpacity;
    double zeroLineWidth;
    QString axisLabel;
};

/// Everything the renderer needs, grouped for convenience.
struct PlotStyle {
    PlotGeometry geometry;
    PlotPalette palette;
    PlotTypography typography;
    PlotLineStyle lines;
    DerivativeAxisStyle derivativeAxis;
    QString title;
    QString xAxisLabel;
    QString yAxisLabel;
};

// ---------------------------------------------------------------------------
// Visibility of the chart elements
// ---------------------------------------------------------------------------

/// Which layers are drawn. These are also the initial states of the preview
/// checkboxes, so the GUI and the renderer cannot drift apart.
/// The vectors are sized from the curve catalogue at construction, so a new
/// curve appears here automatically and no fixed-size array has to be widened.
struct RenderOptions {
    bool showSampleConnector = true;
    bool showSamples = true;
    std::vector<bool> showTrendCurve;
    std::vector<bool> showDerivative;

    /// Sizes the vectors and fills them from the catalogue's own defaults.
    void resizeToCatalogue();

    /// True when the curve is needed at all: either the curve itself or its
    /// rate of change is visible.
    [[nodiscard]] bool requiresCurve(std::size_t index) const noexcept {
        return index < showTrendCurve.size()
               && (showTrendCurve[index] || showDerivative[index]);
    }

    [[nodiscard]] bool anyTrendActive() const noexcept {
        for (std::size_t i = 0; i < showTrendCurve.size(); ++i) {
            if (showTrendCurve[i] || showDerivative[i]) {
                return true;
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// Trend method presentation
// ---------------------------------------------------------------------------

// Curve names now come from the localisation catalogue, keyed by the
// shortNameKey and longNameKey each catalogue entry carries, so the legend and
// the tooltips are translated along with everything else.

// ---------------------------------------------------------------------------
// User interface
// ---------------------------------------------------------------------------

/// A font described independently of the platform: family, point size, weight.
struct FontSpec {
    QString family;
    int pointSize;
    bool bold;
};

/// Fonts, colours and metrics of the window.
struct UiTheme {
    FontSpec labelFont;
    FontSpec entryFont;
    FontSpec largeEntryFont;
    FontSpec buttonFont;
    FontSpec errorFont;
    FontSpec secondaryFont;
    FontSpec monospaceFont;

    QColor errorTextColor;
    QColor fatalBackgroundColor;
    QColor fatalTextColor;

    /// Field widths expressed in character cells; converted with QFontMetrics
    /// so they stay correct with any font or scaling factor.
    int timeFieldCharacters;
    int dayFieldCharacters;
    int yearFieldCharacters;
    int massFieldCharacters;
    int monthComboCharacters;
    int smallButtonCharacters;
    int largeButtonCharacters;
    int fieldWidthPaddingPixels;
    int tracebackVisibleLines;

    int paddingHorizontal;
    int paddingVertical;
    int mainPaddingHorizontal;
    int mainPaddingVertical;
    int buttonSpacing;
    int previewFrameMargin;
    int windowSafetyPadding;  ///< Guards against 1 px clipping from rounding.
    int minimumSliderWidth;
    int sliderWidthMargin;
};

/// Range and granularity of the smoothness slider.
struct SmoothnessSlider {
    double minimum;
    double maximum;
    double resolution;   ///< Distance between two adjacent slider positions.
    double tickInterval;
    int displayedDecimals;
};

/// Every user-visible string. Kept in one struct so the interface language can
/// be changed in a single place.
struct UiStrings {
    QString windowTitle;

    QString buttonCancel;
    QString buttonResetDateTime;
    QString buttonContinue;
    QString buttonViewChartOnly;
    QString buttonSaveImage;
    QString buttonBack;
    QString buttonReset;

    QString labelTimeOfDay;
    QString labelDate;
    QString labelMass;
    QString labelKilograms;
    QString labelColon;
    QString labelOf;

    QString previewTitle;
    QString previewHint;
    QString previewOptionsGroup;
    QString previewShowSampleConnector;
    QString previewShowSamples;
    QString previewCurveTemplate;       ///< %1 letter, %2 method name
    QString previewDerivativeTemplate;  ///< %1 letter
    QString previewCurveTooltipTemplate;
    QString previewDerivativeTooltipTemplate;
    QString smoothnessLabelTemplate;  ///< %1 current value
    QString coefficientUnavailable;
    QString coefficientTooltip;

    QString errorInvalidHour;
    QString errorInvalidMinute;
    QString errorInvalidYear;
    QString errorInvalidMonth;
    QString errorInvalidDay;
    QString errorInvalidTimestamp;
    QString errorEmptyMass;
    QString errorInvalidMass;
    QString errorFatal;
    QString errorNoPreview;

    QString discardTitle;
    QString discardText;
    QString discardQuestion;
    QString discardConfirm;
    QString discardCancel;

    QString saveDialogTitle;
    QString saveDialogFilter;

    QStringList monthNames;
    QStringList monthAbbreviations;
    QStringList weekdayNames;  ///< Monday first, matching QDate::dayOfWeek().
};

// ---------------------------------------------------------------------------
// Aggregate
// ---------------------------------------------------------------------------

/// The complete configuration of one run of the application.
struct Settings {
    InputLimits limits;
    CsvFormat csv;
    StorageLayout storage;
    ConcurrencyPolicy concurrency;
    OutputNaming output;
    PlotStyle plot;
    RenderOptions render;
    math::TrendParameters trend;
    UiTheme ui;
    SmoothnessSlider slider;
    UiStrings strings;

    /// Returns the configuration shipped with the program: the documented
    /// defaults defined in src/core/Settings.cpp.
    [[nodiscard]] static Settings defaults();

    /// Clamps mutually dependent fields into a consistent state and reports
    /// what had to be corrected. Called after loading external overrides so a
    /// damaged settings file can never produce a nonsensical chart.
    QStringList sanitise();

    /// Line colour of a curve, taken from the catalogue entry's r, g, b.
    [[nodiscard]] QColor colourFor(std::size_t curveIndex) const;

    /// Translated short name of a curve, for the legend and the log.
    [[nodiscard]] QString shortNameFor(std::size_t curveIndex) const;

    /// Translated full name, shown as a tooltip.
    [[nodiscard]] QString longNameFor(std::size_t curveIndex) const;

    /// Letter used in the R-squared readout.
    [[nodiscard]] QChar letterFor(std::size_t curveIndex) const;
};

}  // namespace weight::settings
