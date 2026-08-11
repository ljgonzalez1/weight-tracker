// ===========================================================================
//  THE CONFIGURATION FILE OF WEIGHT
//  ---------------------------------------------------------------------
//  Every tunable value of the application is defined here, once. No other
//  translation unit contains a literal that changes behaviour: the code reads
//  these fields through the Settings struct declared in
//  include/weight/core/Settings.hpp.
//
//  To change how the program behaves, edit this file and rebuild, or override
//  the same keys at run time in the INI file managed by SettingsStore
//  (see docs/configuration.md for the key names and their meaning).
//
//  Sections
//    1. Input validation limits
//    2. CSV format
//    3. Storage layout
//    4. Output image naming
//    5. Plot geometry
//    6. Plot palette
//    7. Plot typography and line styles
//    8. Secondary (rate of change) axis
//    9. Default chart layer visibility
//   10. Trend estimator numerics
//   11. Trend method names
//   12. User interface theme
//   13. Smoothness slider
//   14. User interface strings
//   15. Consistency rules
// ===========================================================================

#include "settings/Settings.hpp"

#include "settings/CurveCatalog.hpp"
#include "settings/Strings.hpp"

#include <QStringLiteral>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace weight::settings {
namespace {

// ---------------------------------------------------------------------------
// 1. Input validation limits
//
// Applied by the GUI keystroke filters and by the CSV parser alike, so a file
// edited by hand is held to exactly the same standard as typed input.
// ---------------------------------------------------------------------------
InputLimits makeLimits() {
    InputLimits limits;
    limits.minimumHour = 0;          // 24-hour clock
    limits.maximumHour = 23;
    limits.minimumMinute = 0;
    limits.maximumMinute = 59;
    limits.minimumDayOfMonth = 1;
    limits.maximumDayOfMonth = 31;   // narrowed per month and year at run time
    limits.minimumYear = 1900;
    limits.maximumYear = 2100;
    limits.minimumMass = 30.0;       // kg; below this a reading is a typo
    limits.maximumMass = 300.0;      // kg
    return limits;
}

// ---------------------------------------------------------------------------
// 2. CSV format
//
// Canonical record: day;mass;timestamp
//   day       fractional offset from the plot origin; negative values are
//             legitimate and denote dates before the origin
//   mass      kilograms
//   timestamp informational, "yyyy-MM-dd HH:mm"
//
// The header is written in English. Headers produced by earlier Spanish
// versions are still recognised on read, so existing files keep working and
// are quietly upgraded the next time they are saved.
// ---------------------------------------------------------------------------
CsvFormat makeCsvFormat() {
    CsvFormat csv;
    csv.defaultFileName = QStringLiteral("weight-data.csv");
    csv.header = QStringLiteral("day;mass;timestamp");
    csv.legacyHeaders = QStringList{
        QStringLiteral("d\u00eda;masa;fecha"),  // v45/v46 Python releases
        QStringLiteral("dia;masa;fecha"),
    };
    csv.separator = QStringLiteral(";");

    // Delimiters accepted on read, in preference order. Ties are resolved in
    // favour of the first entry, which is the canonical one.
    csv.candidateSeparators = QStringList{
        QStringLiteral(";"),
        QStringLiteral(","),
        QStringLiteral("\t"),
        QStringLiteral("|"),
    };

    csv.dayFractionDecimals = 5;   // ~0.9 s of resolution on the day axis
    csv.massDecimals = 5;
    csv.secondsPerDay = 86400.0;   // mean solar day
    csv.timestampFormat = QStringLiteral("yyyy-MM-dd HH:mm");
    csv.separatorVoteSampleLines = 200;

    // Words that mark a row as a header rather than data. Compared after
    // removing accents, lowercasing and stripping any trailing "[unit]" or
    // "(unit)" suffix, so "Masa (kg)" and "MASS [KG]" both match.
    csv.headerTokens = QStringList{
        QStringLiteral("dia"),   QStringLiteral("dias"),
        QStringLiteral("day"),   QStringLiteral("days"),
        QStringLiteral("t"),     QStringLiteral("tiempo"),
        QStringLiteral("time"),
        QStringLiteral("masa"),  QStringLiteral("mass"),
        QStringLiteral("peso"),  QStringLiteral("weight"),
        QStringLiteral("kg"),    QStringLiteral("kilos"),
        QStringLiteral("kilogramos"), QStringLiteral("kilograms"),
        QStringLiteral("fecha"), QStringLiteral("date"),
        QStringLiteral("datetime"), QStringLiteral("timestamp"),
        QStringLiteral("hora"),  QStringLiteral("momento"),
        QStringLiteral("n"),     QStringLiteral("num"),
        QStringLiteral("numero"), QStringLiteral("number"),
        QStringLiteral("indice"), QStringLiteral("index"),
        QStringLiteral("id"),    QStringLiteral("#"),
    };
    return csv;
}

// ---------------------------------------------------------------------------
// 3. Storage layout
//
// Resolved through QStandardPaths, which yields
//   Linux  ~/.local/share/Weight/
//   macOS  ~/Library/Application Support/Weight/
// Nothing is written next to the executable or inside the install prefix.
// ---------------------------------------------------------------------------
StorageLayout makeStorage() {
    StorageLayout storage;
    storage.applicationName = QStringLiteral("Weight");
    storage.organisationDomain = QStringLiteral("weight.invalid");
    storage.imagesSubdirectory = QStringLiteral("images");
    storage.settingsFileName = QStringLiteral("config.txt");

    // The folder created inside whichever Documents directory wins. Everything
    // the program owns lives under it, so removing the binary never touches a
    // person's records.
    storage.folderName = QStringLiteral("weight");

    return storage;
}

// ---------------------------------------------------------------------------
// 4. Output image naming
//
// Produces e.g. mass_chart_2026-08-08_14.03.27_smoothness_0.50.png
// The smoothness is part of the name so that images generated with different
// settings never overwrite each other.
// ---------------------------------------------------------------------------
OutputNaming makeOutputNaming() {
    OutputNaming naming;
    naming.filePrefix = QStringLiteral("weight_");
    naming.timestampFormat = QStringLiteral("yyyy-MM-dd_HH.mm.ss");
    naming.smoothnessInfix = QStringLiteral("_smoothness_");
    naming.smoothnessDecimals = 2;
    naming.fileExtension = QStringLiteral(".png");
    return naming;
}

// ---------------------------------------------------------------------------
// 5. Plot geometry
// ---------------------------------------------------------------------------
PlotGeometry makeGeometry() {
    PlotGeometry geometry;
    geometry.originDate = QDate(2026, 3, 31);

    // The axis window is computed from the data, not fixed here: it starts at
    // the oldest measurement (or today when there is none) and reaches
    // whichever is further, a year from the first or three months from the
    // last. See src/plot/AxisWindow.cpp.
    geometry.horizonFromFirstDays = 365.0;
    geometry.horizonFromLastDays = 91.0;
    geometry.labelStepCandidatesDays = {7.0, 14.0, 28.0, 56.0, 91.0, 182.0, 364.0};
    geometry.maximumXLabels = 55;

    geometry.xMinimum = 0.0;
    geometry.xMaximum = 297.0;
    geometry.yMinimum = 60.0;   // kg
    geometry.yMaximum = 100.0;  // kg

    geometry.xLabelStepDays = 7.0;  // one label per week
    geometry.yLabelStep = 2.0;      // kg between major ticks
    geometry.yGridStep = 0.5;       // kg between minor grid lines

    geometry.widthPixels = 5326;
    geometry.heightPixels = 2048;
    geometry.dotsPerInch = 200;

    geometry.previewWidthPixels = 1400;
    geometry.previewDotsPerInch = 100;

    // Axes rectangle as fractions of the figure.
    geometry.marginLeft = 0.055;
    geometry.marginRight = 0.995;
    geometry.marginTop = 0.93;
    geometry.marginBottom = 0.20;
    geometry.marginRightWithSecondaryAxis = 0.955;
    return geometry;
}

// ---------------------------------------------------------------------------
// 6. Plot palette
//
// Orange marks the measured samples; the three trend curves are red, black and
// blue, matching the coloured squares shown in the legend checkboxes.
// ---------------------------------------------------------------------------
PlotPalette makePalette() {
    PlotPalette palette;
    palette.background = QColor(QStringLiteral("#ffffff"));
    palette.axisLine = QColor(QStringLiteral("#000000"));
    palette.axisText = QColor(QStringLiteral("#000000"));
    palette.titleText = QColor(QStringLiteral("#000000"));
    palette.majorGrid = QColor(QStringLiteral("#b0b0b0"));
    palette.minorGrid = QColor(QStringLiteral("#b0b0b0"));
    palette.samplePoints = QColor(QStringLiteral("#f28e2b"));  // orange

    // Curve colours come from settings/CurveCatalog.cpp, where each entry
    // states its own r, g, b. Duplicating them here would create two lists
    // that could disagree the moment a curve is added.

    palette.derivativeZeroLine = QColor(QStringLiteral("#666666"));
    return palette;
}

// ---------------------------------------------------------------------------
// 7. Plot typography and line styles
//
// Sizes are in typographic points (1/72 inch) and are converted to pixels with
// the DPI of the target image, so the preview and the exported PNG are
// visually identical apart from their resolution.
// ---------------------------------------------------------------------------
PlotTypography makeTypography() {
    PlotTypography typography;
    typography.fontFamily = QStringLiteral("DejaVu Sans");
    typography.titlePointSize = 12.0;
    typography.axisLabelPointSize = 10.0;
    typography.tickLabelPointSize = 10.0;
    typography.xTickRotationDegrees = 90.0;  // dates read vertically
    return typography;
}

PlotLineStyle makeLineStyle() {
    PlotLineStyle lines;

    // Each trend curve is drawn as a central line plus two translucent bands
    // that make a tolerance corridor visible at a glance.
    lines.trendCurveWidth = 1.25;
    lines.trendCurveOpacity = 0.85;
    lines.trendInnerBandHalfWidth = 0.50;  // kg
    lines.trendOuterBandHalfWidth = 1.00;  // kg
    lines.trendInnerBandOpacity = 0.1125;
    lines.trendOuterBandOpacity = 0.075;

    // Marker area in points squared, matching scatter-plot semantics: the
    // drawn diameter is sqrt(area).
    lines.sampleMarkerArea = 9.6;
    lines.sampleMarkerOpacity = 0.85;
    lines.sampleConnectorWidth = 0.60;
    lines.sampleConnectorOpacity = 0.85;

    lines.majorGridWidth = 0.5;   // vertical lines, one per X label
    lines.minorGridWidth = 0.35;  // horizontal lines on the mass axis
    lines.axisLineWidth = 0.8;
    lines.tickLength = 3.5;
    lines.tickWidth = 0.8;
    lines.axisLabelPadding = 4.0;
    lines.tickLabelPadding = 3.5;
    lines.titlePadding = 6.0;
    return lines;
}

// ---------------------------------------------------------------------------
// 8. Secondary (rate of change) axis
//
// Drawn on the right, shared by every active derivative. Dash patterns are
// expressed in multiples of the pen width, the same convention Qt uses for
// custom dash patterns.
// ---------------------------------------------------------------------------
DerivativeAxisStyle makeDerivativeAxis() {
    DerivativeAxisStyle axis;
    axis.yMinimum = -4.0;  // kg/week
    axis.yMaximum = 4.0;
    axis.yTickStep = 1.0;
    axis.lineWidthRatio = 1.0;  // same weight as the curve it derives from
    axis.lineOpacity = 0.85;
    axis.lineDashPattern = QVector<double>{1.0, 1.65};       // dotted
    axis.zeroLineDashPattern = QVector<double>{3.7, 1.6};    // dashed
    axis.zeroLineOpacity = 0.45;
    axis.zeroLineWidth = 1.1;
    axis.axisLabel = QStringLiteral("Rate of change [kg/week]");
    return axis;
}

// ---------------------------------------------------------------------------
// 9. Default chart layer visibility
//
// Samples, curve (B) and its rate of change. Everything else starts hidden so
// the first chart is readable rather than crowded.
// ---------------------------------------------------------------------------
RenderOptions makeRenderOptions() {
    RenderOptions options;
    options.showSampleConnector = false;
    options.showSamples = true;
    // Sized and filled from the catalogue, so a new curve arrives with its own
    // stated default rather than needing this list widened.
    options.resizeToCatalogue();
    return options;
}

// ---------------------------------------------------------------------------
// 10. Trend estimator numerics
//
// The individual fields are documented next to their declarations in
// include/weight/math/TrendParameters.hpp; the mathematics is explained in
// docs/mathematics.md and in the estimator sources.
// ---------------------------------------------------------------------------
math::TrendParameters makeTrendParameters() {
    math::TrendParameters trend;
    trend.common.generalSmoothness = 0.50;  // default slider position
    return trend;                           // remaining fields use their declared defaults
}

// ---------------------------------------------------------------------------
// 11. Trend method names
//
// Short names appear in the legend, long names as tooltips and in the log.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 12. User interface theme
//
// Field widths are given in character cells and resolved with QFontMetrics, so
// they remain correct under any font, DPI or accessibility scaling factor.
// ---------------------------------------------------------------------------
UiTheme makeUiTheme() {
    UiTheme theme;
    theme.labelFont = FontSpec{QStringLiteral("Sans"), 13, false};
    theme.entryFont = FontSpec{QStringLiteral("Sans"), 14, false};
    theme.largeEntryFont = FontSpec{QStringLiteral("Sans"), 16, false};
    theme.buttonFont = FontSpec{QStringLiteral("Sans"), 12, false};
    theme.errorFont = FontSpec{QStringLiteral("Sans"), 12, true};
    theme.secondaryFont = FontSpec{QStringLiteral("Sans"), 12, false};
    theme.monospaceFont = FontSpec{QStringLiteral("Monospace"), 9, false};

    theme.errorTextColor = QColor(QStringLiteral("#b00020"));
    theme.fatalBackgroundColor = QColor(QStringLiteral("#fff0f0"));
    theme.fatalTextColor = QColor(QStringLiteral("#800000"));

    theme.timeFieldCharacters = 3;
    theme.dayFieldCharacters = 3;
    theme.yearFieldCharacters = 5;
    theme.massFieldCharacters = 10;
    theme.monthComboCharacters = 12;
    theme.smallButtonCharacters = 14;
    theme.largeButtonCharacters = 16;
    theme.fieldWidthPaddingPixels = 18;
    theme.tracebackVisibleLines = 12;

    theme.paddingHorizontal = 8;
    theme.paddingVertical = 6;
    theme.mainPaddingHorizontal = 16;
    theme.mainPaddingVertical = 12;
    theme.buttonSpacing = 12;
    theme.previewFrameMargin = 6;
    theme.windowSafetyPadding = 2;
    theme.minimumSliderWidth = 200;
    theme.sliderWidthMargin = 60;
    return theme;
}

// ---------------------------------------------------------------------------
// 13. Smoothness slider
// ---------------------------------------------------------------------------
SmoothnessSlider makeSlider() {
    SmoothnessSlider slider;
    slider.minimum = 0.05;
    slider.maximum = 2.00;
    slider.resolution = 0.01;
    slider.tickInterval = 0.05;
    slider.displayedDecimals = 2;
    return slider;
}

// ---------------------------------------------------------------------------
// 14. User interface strings
// ---------------------------------------------------------------------------
UiStrings makeStrings() {
    UiStrings text;
    text.windowTitle = QStringLiteral("Record body mass");

    text.buttonCancel = QStringLiteral("Cancel");
    text.buttonResetDateTime = QStringLiteral("Reset date and time");
    text.buttonContinue = QStringLiteral("Continue");
    text.buttonViewChartOnly = QStringLiteral("View chart only");
    text.buttonSaveImage = QStringLiteral("Save image");
    text.buttonBack = QStringLiteral("Back");
    text.buttonReset = QStringLiteral("Reset");

    text.labelTimeOfDay = QStringLiteral("Time of day");
    text.labelDate = QStringLiteral("Date");
    text.labelMass = QStringLiteral("Mass");
    text.labelKilograms = QStringLiteral("kg");
    text.labelColon = QStringLiteral(":");
    text.labelOf = QStringLiteral("of");

    text.previewTitle = QStringLiteral("Chart preview");
    text.previewHint = QStringLiteral(
        "Cancel closes without saving. Save image writes the CSV and stores the final PNG.");
    text.previewOptionsGroup = QStringLiteral("Display options");
    text.previewShowSampleConnector = QStringLiteral("Sample connector \U0001F7E7");
    text.previewShowSamples = QStringLiteral("Samples [kg] \U0001F7E0");

    // %1 is the method letter, %2 its short name.
    text.previewCurveTemplate = QStringLiteral("Fitted curve %1 (%2) [kg]");
    text.previewDerivativeTemplate = QStringLiteral("Rate of change (%1) [kg/week]");
    text.previewCurveTooltipTemplate = QStringLiteral("%1");
    text.previewDerivativeTooltipTemplate = QStringLiteral("d/dt of curve %1 \u2014 %2");

    text.smoothnessLabelTemplate = QStringLiteral("Curve smoothness: %1");
    text.coefficientUnavailable = QStringLiteral("n/a");
    text.coefficientTooltip = QStringLiteral(
        "Coefficient of determination R\u00b2 of every active trend curve, "
        "evaluated against the recorded samples.");

    text.errorInvalidHour = QStringLiteral("Invalid hour");
    text.errorInvalidMinute = QStringLiteral("Invalid minute");
    text.errorInvalidYear = QStringLiteral("Invalid year");
    text.errorInvalidMonth = QStringLiteral("Invalid month");
    text.errorInvalidDay = QStringLiteral("Invalid day");
    text.errorInvalidTimestamp = QStringLiteral("Invalid timestamp");
    text.errorEmptyMass = QStringLiteral("Mass is empty");
    text.errorInvalidMass = QStringLiteral("Invalid mass");
    text.errorFatal = QStringLiteral("Fatal error \u2014 see the details below");
    text.errorNoPreview = QStringLiteral("No preview has been prepared.");

    text.discardTitle = QStringLiteral("Discard the generated chart?");
    text.discardText = QStringLiteral(
        "Closing now discards the generated data and the image will not be saved.");
    text.discardQuestion = QStringLiteral("Do you really want to exit without saving?");
    text.discardConfirm = QStringLiteral("Yes, close");
    text.discardCancel = QStringLiteral("Go back");

    text.saveDialogTitle = QStringLiteral("Save chart image");
    text.saveDialogFilter = QStringLiteral("PNG image (*.png)");

    text.monthNames = QStringList{
        QStringLiteral("January"), QStringLiteral("February"), QStringLiteral("March"),
        QStringLiteral("April"),   QStringLiteral("May"),      QStringLiteral("June"),
        QStringLiteral("July"),    QStringLiteral("August"),   QStringLiteral("September"),
        QStringLiteral("October"), QStringLiteral("November"), QStringLiteral("December"),
    };
    text.monthAbbreviations = QStringList{
        QStringLiteral("Jan"), QStringLiteral("Feb"), QStringLiteral("Mar"),
        QStringLiteral("Apr"), QStringLiteral("May"), QStringLiteral("Jun"),
        QStringLiteral("Jul"), QStringLiteral("Aug"), QStringLiteral("Sep"),
        QStringLiteral("Oct"), QStringLiteral("Nov"), QStringLiteral("Dec"),
    };
    text.weekdayNames = QStringList{
        QStringLiteral("Monday"), QStringLiteral("Tuesday"), QStringLiteral("Wednesday"),
        QStringLiteral("Thursday"), QStringLiteral("Friday"), QStringLiteral("Saturday"),
        QStringLiteral("Sunday"),
    };
    return text;
}

/// Clamps `value` into [low, high] and records a message when it had to move.
template <typename T>
void clampField(T& value, T low, T high, const QString& name, QStringList& corrections) {
    const T clamped = std::clamp(value, low, high);
    if (clamped != value) {
        corrections.append(QStringLiteral("%1 adjusted from %2 to %3")
                               .arg(name)
                               .arg(static_cast<double>(value))
                               .arg(static_cast<double>(clamped)));
        value = clamped;
    }
}

}  // namespace

Settings Settings::defaults() {
    Settings config;
    config.limits = makeLimits();
    config.csv = makeCsvFormat();
    config.storage = makeStorage();
    config.output = makeOutputNaming();
    config.plot.geometry = makeGeometry();
    config.plot.palette = makePalette();
    config.plot.typography = makeTypography();
    config.plot.lines = makeLineStyle();
    config.plot.derivativeAxis = makeDerivativeAxis();
    config.plot.title = QStringLiteral("Body mass");
    config.plot.xAxisLabel = QStringLiteral("Day");
    config.plot.yAxisLabel = QStringLiteral("Mass [kg]");
    config.render = makeRenderOptions();
    config.trend = makeTrendParameters();
    config.ui = makeUiTheme();
    config.slider = makeSlider();
    config.strings = makeStrings();
    return config;
}

// ---------------------------------------------------------------------------
// 15. Consistency rules
//
// Enforced after any external override so that a hand-edited or damaged
// settings file cannot drive the renderer into an impossible state. Each
// correction is reported instead of being applied silently.
// ---------------------------------------------------------------------------
QStringList Settings::sanitise() {
    QStringList corrections;

    if (limits.minimumMass >= limits.maximumMass) {
        corrections.append(QStringLiteral("Mass range was empty; restored to the defaults"));
        const auto restored = makeLimits();
        limits.minimumMass = restored.minimumMass;
        limits.maximumMass = restored.maximumMass;
    }
    if (limits.minimumYear > limits.maximumYear) {
        std::swap(limits.minimumYear, limits.maximumYear);
        corrections.append(QStringLiteral("Year range was inverted; the bounds were swapped"));
    }

    if (plot.geometry.xMaximum <= plot.geometry.xMinimum) {
        corrections.append(QStringLiteral("X axis range was empty; restored to the defaults"));
        const auto restored = makeGeometry();
        plot.geometry.xMinimum = restored.xMinimum;
        plot.geometry.xMaximum = restored.xMaximum;
    }
    if (plot.geometry.yMaximum <= plot.geometry.yMinimum) {
        corrections.append(QStringLiteral("Y axis range was empty; restored to the defaults"));
        const auto restored = makeGeometry();
        plot.geometry.yMinimum = restored.yMinimum;
        plot.geometry.yMaximum = restored.yMaximum;
    }
    if (!plot.geometry.originDate.isValid()) {
        plot.geometry.originDate = makeGeometry().originDate;
        corrections.append(QStringLiteral("Origin date was invalid; restored to the default"));
    }

    clampField(plot.geometry.widthPixels, 64, 32768,
               QStringLiteral("Image width"), corrections);
    clampField(plot.geometry.heightPixels, 64, 32768,
               QStringLiteral("Image height"), corrections);
    clampField(plot.geometry.dotsPerInch, 24, 1200,
               QStringLiteral("DPI"), corrections);
    clampField(plot.geometry.previewWidthPixels, 320, 8192,
               QStringLiteral("Preview width"), corrections);
    clampField(plot.geometry.previewDotsPerInch, 24, 600,
               QStringLiteral("Preview DPI"), corrections);

    // Margins must describe a non-degenerate rectangle inside the figure.
    clampField(plot.geometry.marginLeft, 0.0, 0.9,
               QStringLiteral("Left margin"), corrections);
    clampField(plot.geometry.marginBottom, 0.0, 0.9,
               QStringLiteral("Bottom margin"), corrections);
    clampField(plot.geometry.marginRight, plot.geometry.marginLeft + 0.05, 1.0,
               QStringLiteral("Right margin"), corrections);
    clampField(plot.geometry.marginTop, plot.geometry.marginBottom + 0.05, 1.0,
               QStringLiteral("Top margin"), corrections);
    clampField(plot.geometry.marginRightWithSecondaryAxis,
               plot.geometry.marginLeft + 0.05, plot.geometry.marginRight,
               QStringLiteral("Right margin with secondary axis"), corrections);

    if (plot.geometry.xLabelStepDays <= 0.0) {
        plot.geometry.xLabelStepDays = makeGeometry().xLabelStepDays;
        corrections.append(QStringLiteral("X label step must be positive; restored"));
    }
    if (plot.geometry.yLabelStep <= 0.0) {
        plot.geometry.yLabelStep = makeGeometry().yLabelStep;
        corrections.append(QStringLiteral("Y label step must be positive; restored"));
    }
    if (plot.geometry.yGridStep <= 0.0) {
        plot.geometry.yGridStep = makeGeometry().yGridStep;
        corrections.append(QStringLiteral("Y grid step must be positive; restored"));
    }

    if (slider.maximum <= slider.minimum) {
        const auto restored = makeSlider();
        slider.minimum = restored.minimum;
        slider.maximum = restored.maximum;
        corrections.append(QStringLiteral("Smoothness slider range was empty; restored"));
    }
    if (slider.resolution <= 0.0) {
        slider.resolution = makeSlider().resolution;
        corrections.append(QStringLiteral("Smoothness resolution must be positive; restored"));
    }
    clampField(trend.common.generalSmoothness, slider.minimum, slider.maximum,
               QStringLiteral("Smoothness"), corrections);

    if (trend.common.denseGridMaxPoints < trend.common.denseGridMinPoints) {
        std::swap(trend.common.denseGridMinPoints, trend.common.denseGridMaxPoints);
        corrections.append(QStringLiteral("Dense grid bounds were inverted; swapped"));
    }
    if (trend.common.minimumPoints < 2) {
        trend.common.minimumPoints = 2;
        corrections.append(QStringLiteral("At least two points are required for a fit"));
    }

    if (trend.splineA.bandwidthMaxDays < trend.splineA.bandwidthMinDays) {
        std::swap(trend.splineA.bandwidthMinDays, trend.splineA.bandwidthMaxDays);
        corrections.append(QStringLiteral("Curve A bandwidth bounds were inverted; swapped"));
    }
    if (trend.rbfB.maximumCentres < 2) {
        trend.rbfB.maximumCentres = 2;
        corrections.append(QStringLiteral("Curve B needs at least two basis centres"));
    }
    clampField(trend.loessC.spanFractionMin, 0.0, 1.0,
               QStringLiteral("Curve C minimum span fraction"), corrections);
    clampField(trend.loessC.spanFractionMax, trend.loessC.spanFractionMin, 1.0,
               QStringLiteral("Curve C maximum span fraction"), corrections);

    if (trend.derivative.sampleStepDays <= 0.0) {
        trend.derivative.sampleStepDays = math::DerivativeParameters{}.sampleStepDays;
        corrections.append(QStringLiteral("Derivative step must be positive; restored"));
    }
    if (plot.derivativeAxis.yMaximum <= plot.derivativeAxis.yMinimum) {
        const auto restored = makeDerivativeAxis();
        plot.derivativeAxis.yMinimum = restored.yMinimum;
        plot.derivativeAxis.yMaximum = restored.yMaximum;
        corrections.append(QStringLiteral("Rate of change axis range was empty; restored"));
    }

    if (csv.separator.isEmpty()) {
        csv.separator = makeCsvFormat().separator;
        corrections.append(QStringLiteral("CSV separator was empty; restored"));
    }
    clampField(csv.dayFractionDecimals, 0, 15,
               QStringLiteral("Day decimals"), corrections);
    clampField(csv.massDecimals, 0, 15,
               QStringLiteral("Mass decimals"), corrections);
    if (csv.secondsPerDay <= 0.0) {
        csv.secondsPerDay = makeCsvFormat().secondsPerDay;
        corrections.append(QStringLiteral("Seconds per day must be positive; restored"));
    }

    if (strings.monthNames.size() != 12 || strings.monthAbbreviations.size() != 12
        || strings.weekdayNames.size() != 7) {
        const auto restored = makeStrings();
        strings.monthNames = restored.monthNames;
        strings.monthAbbreviations = restored.monthAbbreviations;
        strings.weekdayNames = restored.weekdayNames;
        corrections.append(QStringLiteral("Calendar name lists were incomplete; restored"));
    }

    return corrections;
}

void RenderOptions::resizeToCatalogue() {
    const std::span<const CurveDescriptor> catalogue = curveCatalogue();
    showTrendCurve.assign(catalogue.size(), false);
    showDerivative.assign(catalogue.size(), false);
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        showTrendCurve[i] = catalogue[i].visibleByDefault;
        showDerivative[i] = catalogue[i].derivativeVisibleByDefault;
    }
}

QColor Settings::colourFor(std::size_t curveIndex) const {
    const std::span<const CurveDescriptor> catalogue = curveCatalogue();
    // Never return an invalid colour: an out-of-range index means a stale
    // config, and a chart drawn in a default colour beats one that crashes.
    return curveIndex < catalogue.size() ? catalogue[curveIndex].colour.toColor()
                                         : plot.palette.axisLine;
}

QString Settings::shortNameFor(std::size_t curveIndex) const {
    const std::span<const CurveDescriptor> catalogue = curveCatalogue();
    return curveIndex < catalogue.size() ? Strings::get(catalogue[curveIndex].shortNameKey)
                                         : QString();
}

QString Settings::longNameFor(std::size_t curveIndex) const {
    const std::span<const CurveDescriptor> catalogue = curveCatalogue();
    return curveIndex < catalogue.size() ? Strings::get(catalogue[curveIndex].longNameKey)
                                         : QString();
}

QChar Settings::letterFor(std::size_t curveIndex) const {
    const std::span<const CurveDescriptor> catalogue = curveCatalogue();
    return curveIndex < catalogue.size() ? catalogue[curveIndex].letter : QChar();
}

}  // namespace weight::settings
