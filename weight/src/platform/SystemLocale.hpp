#pragma once

#include <vector>

#include <QString>

namespace weight::platform {

/// One answer to "which language does this machine want?", together with where
/// it came from.
///
/// The source travels with the tag so the log can say *why* the program is
/// speaking Spanish. "Idioma: Español (desde LC_MESSAGES)" is a bug report;
/// "Idioma: Español" is a mystery.
struct LocaleCandidate {
    QString tag;     ///< A locale or language tag: es_CL.UTF-8, es-419, en-GB.
    QString source;  ///< The variable or API that supplied it.

    [[nodiscard]] bool isEmpty() const noexcept { return tag.isEmpty(); }
};

/// The POSIX environment chain, in the order POSIX and GNU gettext resolve it.
///
///   1. `LC_ALL`      overrides every category
///   2. `LC_MESSAGES` the category that governs user-visible text
///   3. `LANG`        the general fallback
///
/// and then, on top of those, `LANGUAGE` — which GNU software gives priority
/// over all three, *except* when the resolved locale is `C` or `POSIX`, in
/// which case it is ignored entirely. That exception is not a curiosity: it is
/// what makes `LC_ALL=C` a reliable way to get untranslated output, and this
/// implementation honours it.
///
/// Empty on a machine where none of them is set, which is the normal case on
/// Windows and on macOS outside a terminal.
[[nodiscard]] std::vector<LocaleCandidate> environmentLocales();

/// The languages the operating system itself reports, most preferred first,
/// each read through that system's own interface rather than through a guess:
///
///   Windows  `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, ...)`, the
///            ordered list behind Settings > Language, falling back to
///            `GetUserDefaultLocaleName`.
///   macOS    `CFLocaleCopyPreferredLanguages()`, the ordered list behind
///            System Settings > Language & Region, falling back to the
///            identifier of `CFLocaleCopyCurrent()`.
///   POSIX    the C library's own resolution, `setlocale(LC_MESSAGES, "")`,
///            asked and then put back exactly as it was found.
///
/// `QLocale::system().uiLanguages()` is appended everywhere as a last resort,
/// so a platform none of the branches above knows about still gets an answer.
[[nodiscard]] std::vector<LocaleCandidate> systemLocales();

}  // namespace weight::platform
