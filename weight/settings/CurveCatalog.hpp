#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <QColor>
#include <QString>

#include "math/common/TrendEstimator.hpp"

namespace weight::settings {

/// A colour written the way the brief requires: explicit r, g, b components,
/// never a hex string buried in code.
struct Rgb {
    int red = 0;
    int green = 0;
    int blue = 0;

    [[nodiscard]] QColor toColor() const { return QColor(red, green, blue); }
};

/// Everything the program needs to know about one trend curve.
///
/// Adding a fourth curve is a three-step change and nothing else:
///
///   1. create `src/math/<your_curve>/YourCurve.{hpp,cpp}` deriving from
///      `math::TrendEstimator`;
///   2. add one entry to the array in `CurveCatalog.cpp`, giving it an id, a
///      colour and a factory;
///   3. add the folder to `WEIGHT_CURVE_DIRS` in `CMakeLists.txt`.
///
/// Nothing in the renderer, the UI, the settings store or the threading layer
/// mentions a specific curve, so none of them has to be touched. The plot
/// builder derives its band and line colours from `colour` through
/// `CurveStyle`, and the preview page builds its checkboxes by iterating this
/// catalogue.
struct CurveDescriptor {
    /// Stable identifier used in config.txt and on the command line. Must not
    /// change between releases once published, or saved settings stop applying.
    QString id;

    /// Single letter shown in the R-squared readout and the log.
    QChar letter;

    /// Keys into the localisation catalogue, so names are translated.
    QString shortNameKey;
    QString longNameKey;

    /// Line colour. Bands are derived from it, never specified separately.
    Rgb colour;

    /// Whether the curve and its rate of change start switched on.
    bool visibleByDefault = false;
    bool derivativeVisibleByDefault = false;

    /// Builds the estimator. A factory rather than a type keeps the catalogue
    /// free of the concrete headers.
    std::function<std::unique_ptr<math::TrendEstimator>()> factory;
};

/// The curves the program knows about, in drawing order.
[[nodiscard]] std::span<const CurveDescriptor> curveCatalogue();

/// Number of registered curves. Array sizes throughout the program derive from
/// this, so nothing has to be resized by hand when a curve is added.
[[nodiscard]] std::size_t curveCount();

/// Lookup by stable id; returns nullptr when unknown, which is what lets an
/// old config.txt naming a removed curve be ignored rather than crash.
[[nodiscard]] const CurveDescriptor* findCurve(const QString& id);

/// Index of a curve within the catalogue, or std::nullopt.
[[nodiscard]] std::optional<std::size_t> curveIndex(const QString& id);

}  // namespace weight::settings
