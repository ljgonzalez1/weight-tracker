#pragma once

#include <QString>

namespace weight::data {

/// Normalisation of raw text taken from a CSV cell.
///
/// History files are edited by hand, pasted from spreadsheets and copied out of
/// PDFs, so they arrive carrying typographic characters that a strict numeric
/// parser cannot read even though the value itself is perfectly valid. These
/// helpers remove that noise without ever attempting to reinterpret a number.
class TextSanitizer {
public:
    /// Applies Unicode NFKC normalisation, maps the typographic look-alikes
    /// listed in the implementation onto their ASCII equivalents, strips zero
    /// width characters and surrounding quotes, and trims whitespace.
    ///
    /// NFKC is the compatibility form: it folds full-width digits and similar
    /// presentational variants onto the plain characters a parser expects.
    [[nodiscard]] static QString sanitize(const QString& text);

    /// Removes combining marks after canonical decomposition, so that header
    /// words can be compared regardless of accents ("día" matches "dia").
    [[nodiscard]] static QString stripAccents(const QString& text);

    /// Lowercased, accent-free form with any trailing "[unit]" or "(unit)"
    /// suffix removed. Used to recognise header cells.
    [[nodiscard]] static QString headerToken(const QString& text);
};

}  // namespace weight::data
