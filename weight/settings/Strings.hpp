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
    ///   1. `WEIGHT_LANG`   — an explicit override, mainly for testing
    ///   2. the POSIX environment — `LC_ALL`, `LC_MESSAGES`, `LANG`, with
    ///      `LANGUAGE` given the priority GNU software gives it (and ignored
    ///      when the resolved locale is `C`, which is the documented exception)
    ///   3. the operating system's own UI-language interface —
    ///      `GetUserPreferredUILanguages` on Windows,
    ///      `CFLocaleCopyPreferredLanguages` on macOS,
    ///      `setlocale(LC_MESSAGES, "")` on POSIX
    ///   4. `QLocale::system()` — Qt's own view, as a last resort
    ///
    /// Step 3 is a list, not a single value: both Windows and macOS let the
    /// user rank several languages, and the first *supported* one wins. A
    /// machine ranked `de, es, en` gets Spanish, which is the whole point of
    /// the ranking and is what every other application on it does.
    ///
    /// The mapping applied to each candidate is decided by the **primary
    /// language subtag**, never by a string prefix:
    ///
    /// | Tag                                     | Language |
    /// |-----------------------------------------|----------|
    /// | `es`, `es_CL`, `es-419`, `es_ES.UTF-8`, `es-Latn-MX@valencia`, `spa` | Spanish |
    /// | `en`, `en_US`, `en-GB`, `eng`           | English  |
    /// | anything else (`de_DE`, `pt_BR`, `ja`)  | English  |
    /// | `C`, `POSIX`, `C.UTF-8`, unset          | English  |
    ///
    /// Spanish is chosen only when the system both *supports* and *uses* it:
    /// an `es_*` value in one of those places is exactly that statement, and a
    /// bare `C` is exactly its absence.
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

    /// The primary language subtag, lowercased and stripped of everything
    /// else: `es_CL.UTF-8` and `es-Latn-419@valencia` both give `es`.
    ///
    /// Exposed because it is the part that decides the language, and because
    /// the naive alternative — asking whether the tag *starts with* "es" — is
    /// wrong in both directions. It says yes to Estonian written in the
    /// three-letter form (`est_EE`) and, on a Windows machine that reports
    /// `sr-Cyrl-RS`, it invites the same class of mistake for every other
    /// language. Splitting the tag and comparing whole subtags cannot.
    [[nodiscard]] static QString primaryLanguageSubtag(const QString& tag);

    /// True when the tag names a language this program actually ships text
    /// for. Distinct from `languageForTag`, which always returns something:
    /// the difference is what lets a ranked list like `de, es, en` skip past
    /// German instead of stopping at it and answering English.
    [[nodiscard]] static bool tagIsSupported(const QString& tag);

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
