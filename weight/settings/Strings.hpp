#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace weight::settings {

/// Languages the program ships text for.
enum class Language {
    English,  ///< en_*, and the fallback for everything unrecognised
    Spanish,  ///< es_*
};

/// Every user-visible string, keyed by a stable identifier.
///
/// Strings live here rather than beside the widget that shows them for two
/// reasons: a translator can see the whole vocabulary in one place, and no
/// English literal can leak into a code path by accident, because the widgets
/// have no literals to leak.
///
/// Keys are dotted and stable. A missing key returns the key itself rather
/// than an empty string, which makes an omission obvious on screen instead of
/// silently blanking a label.
class Strings {
public:
    /// Detects the language from the environment and installs it.
    ///
    /// The chain, highest priority first:
    ///
    ///   1. `WEIGHT_LANG`      — an explicit override, mainly for testing
    ///   2. `LC_ALL`           — POSIX: overrides every other category
    ///   3. `LC_MESSAGES`      — POSIX: the category that governs UI text
    ///   4. `LANG`             — POSIX: the general fallback
    ///   5. `QLocale::system()` — Qt's own view, which on Windows and macOS
    ///                            reads the platform's UI language rather than
    ///                            the POSIX variables
    ///
    /// A value of `C` or `POSIX` at any step means "no locale configured", and
    /// selects English, as the brief requires. Anything beginning `es` selects
    /// Spanish; anything beginning `en` selects English; anything else falls
    /// back to English.
    ///
    /// Spanish is chosen only when the system both *supports* and *uses* it:
    /// an `es_*` value in one of these variables is exactly that statement, and
    /// a bare `C` is exactly its absence.
    static void detectAndInstall();

    /// Installs a language explicitly, bypassing detection.
    static void install(Language language);

    [[nodiscard]] static Language current();

    /// The tag that was detected, for the log and for `--version`.
    [[nodiscard]] static QString detectedTag();

    /// Which of the five steps supplied the answer, for diagnostics.
    [[nodiscard]] static QString detectionSource();

    /// Translated text for a key.
    [[nodiscard]] static QString get(const QString& key);

    /// Translated text with `%1`-style arguments already substituted.
    ///
    /// The list form is the general one; the fixed-arity overloads exist only
    /// so that the common cases read naturally at the call site. Substituting
    /// through a list rather than through chained .arg() calls also means a
    /// translation may reorder its placeholders — "%2 de %1" — which Spanish
    /// occasionally needs and chained .arg() cannot express.
    [[nodiscard]] static QString get(const QString& key, const QStringList& arguments);
    [[nodiscard]] static QString get(const QString& key, const QString& a1);
    [[nodiscard]] static QString get(const QString& key, const QString& a1, const QString& a2);
    [[nodiscard]] static QString get(const QString& key, const QString& a1, const QString& a2,
                                     const QString& a3);
    [[nodiscard]] static QString get(const QString& key, const QString& a1, const QString& a2,
                                     const QString& a3, const QString& a4);

    /// Maps a locale tag onto a supported language. Exposed for testing, since
    /// the mapping is the part most likely to be got wrong.
    [[nodiscard]] static Language languageForTag(const QString& tag);

    /// True when the tag means "no locale configured".
    [[nodiscard]] static bool tagIsNeutral(const QString& tag);

    /// Every key the catalogue defines, sorted. Used by the test that proves
    /// the two languages cover exactly the same vocabulary.
    [[nodiscard]] static QStringList keys(Language language);
};

/// Shorthand used throughout the UI. Deliberately short, because it appears
/// wherever a literal would otherwise have been written.
[[nodiscard]] inline QString tr_(const QString& key) { return Strings::get(key); }
[[nodiscard]] inline QString tr_(const QString& key, const QString& a1) {
    return Strings::get(key, a1);
}
[[nodiscard]] inline QString tr_(const QString& key, const QString& a1, const QString& a2) {
    return Strings::get(key, a1, a2);
}

}  // namespace weight::settings
