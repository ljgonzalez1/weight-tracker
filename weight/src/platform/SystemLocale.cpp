#include "platform/SystemLocale.hpp"

#include <QLocale>

#include <clocale>
#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// Why this file exists at all
//
// Qt already has QLocale::system(), and on a well-behaved desktop it gives the
// right answer. It is used here as the last resort rather than the first,
// because it answers a slightly different question from the one being asked:
// QLocale::system() reports the *formatting* locale (how to print a date, a
// number, a currency), whereas what selects the language of the interface is
// the *UI language* list, and the two genuinely differ on both Windows and
// macOS. Someone in Chile who reads English is a real configuration: Windows
// formats in es-CL and displays its interface in en-US. Reading the UI
// language list is what makes this program agree with every other program on
// that machine.
//
// Each branch below therefore calls the interface the platform actually
// publishes for this, and only falls back to Qt when that call fails.
// ---------------------------------------------------------------------------

#if defined(Q_OS_WIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <cwchar>
#elif defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
#  include <CoreFoundation/CoreFoundation.h>
#endif

namespace weight::platform {
namespace {

QString environmentValue(const char* name) {
    const char* raw = std::getenv(name);
    return (raw != nullptr) ? QString::fromLocal8Bit(raw).trimmed() : QString();
}

/// True for the tags that mean "no locale is configured on this machine".
/// Duplicated deliberately from settings::Strings: this layer must be able to
/// apply the LANGUAGE rule without depending on the string catalogue.
bool neutral(const QString& tag) {
    const QString upper = tag.trimmed().toUpper();
    return upper.isEmpty() || upper == QLatin1String("C") || upper == QLatin1String("POSIX")
           || upper.startsWith(QLatin1String("C.")) || upper.startsWith(QLatin1String("POSIX."));
}

void appendIfNew(std::vector<LocaleCandidate>& list, const QString& tag, const QString& source) {
    if (tag.trimmed().isEmpty()) {
        return;
    }
    for (const LocaleCandidate& existing : list) {
        if (existing.tag.compare(tag, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    list.push_back(LocaleCandidate{tag.trimmed(), source});
}

#if defined(Q_OS_WIN)

/// The ordered list behind Settings > Time & Language > Language.
///
/// MUI_LANGUAGE_NAME asks for BCP-47 names ("es-CL", "en-GB") rather than the
/// numeric LANGIDs, because a name carries the script and region that a LANGID
/// has to be decoded to recover. The buffer is filled with a double-null
/// terminated sequence of strings, which is why it is walked rather than
/// indexed.
std::vector<LocaleCandidate> windowsUiLanguages() {
    std::vector<LocaleCandidate> found;

    ULONG count = 0;
    ULONG characters = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &characters) != 0
        && characters > 0) {
        std::wstring buffer(static_cast<std::size_t>(characters), L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &characters)
            != 0) {
            const wchar_t* cursor = buffer.c_str();
            while (*cursor != L'\0') {
                appendIfNew(found, QString::fromWCharArray(cursor),
                            QStringLiteral("GetUserPreferredUILanguages"));
                cursor += wcslen(cursor) + 1;
            }
        }
    }

    // The user's regional format, which is what a machine with a single
    // language pack reports. Kept as a fallback rather than a peer: it is the
    // formatting locale, not the interface language.
    if (found.empty()) {
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
            appendIfNew(found, QString::fromWCharArray(name),
                        QStringLiteral("GetUserDefaultLocaleName"));
        }
    }
    return found;
}

#elif defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)

QString toQString(CFStringRef text) {
    if (text == nullptr) {
        return {};
    }
    // The fast path returns an interior pointer when the string happens to be
    // stored as the requested encoding; when it does not, a copy is made into
    // a buffer sized from the string's own length. Both are documented uses.
    if (const char* direct = CFStringGetCStringPtr(text, kCFStringEncodingUTF8)) {
        return QString::fromUtf8(direct);
    }
    const CFIndex length = CFStringGetLength(text);
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string buffer(static_cast<std::size_t>(capacity), '\0');
    if (CFStringGetCString(text, buffer.data(), capacity, kCFStringEncodingUTF8)) {
        return QString::fromUtf8(buffer.c_str());
    }
    return {};
}

/// The ordered list behind System Settings > General > Language & Region.
///
/// CFLocaleCopyPreferredLanguages returns canonicalised identifiers ("es-CL",
/// "en-GB", "zh-Hans-CN") in the order the user dragged them, which is exactly
/// the preference order every macOS application resolves against.
std::vector<LocaleCandidate> macUiLanguages() {
    std::vector<LocaleCandidate> found;

    if (CFArrayRef languages = CFLocaleCopyPreferredLanguages()) {
        const CFIndex total = CFArrayGetCount(languages);
        for (CFIndex i = 0; i < total; ++i) {
            const auto entry = static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages, i));
            appendIfNew(found, toQString(entry),
                        QStringLiteral("CFLocaleCopyPreferredLanguages"));
        }
        CFRelease(languages);
    }

    if (found.empty()) {
        if (CFLocaleRef current = CFLocaleCopyCurrent()) {
            appendIfNew(found, toQString(CFLocaleGetIdentifier(current)),
                        QStringLiteral("CFLocaleCopyCurrent"));
            CFRelease(current);
        }
    }
    return found;
}

#endif

#if defined(LC_MESSAGES)

/// The C library's own resolution of the message locale.
///
/// setlocale(LC_MESSAGES, "") is the POSIX way to ask "what did the
/// environment actually resolve to?", and it is asked here rather than
/// assumed, because the answer accounts for locale aliases and for a locale
/// that is configured but not installed. Only the LC_MESSAGES category is
/// touched, never LC_NUMERIC — a program that quietly switches decimal
/// separators under itself is a program that writes "1,5" into a file another
/// tool will read as fifteen — and the previous value is put back before the
/// function returns, so the process is left exactly as it was found.
QString posixMessagesLocale() {
    const char* previous = std::setlocale(LC_MESSAGES, nullptr);
    const std::string saved = (previous != nullptr) ? previous : "C";

    const char* resolved = std::setlocale(LC_MESSAGES, "");
    const QString answer = (resolved != nullptr) ? QString::fromLatin1(resolved) : QString();

    std::setlocale(LC_MESSAGES, saved.c_str());
    return answer;
}

#endif

}  // namespace

std::vector<LocaleCandidate> environmentLocales() {
    std::vector<LocaleCandidate> chain;

    // The POSIX ladder. The first one that is set wins outright; this is the
    // order the standard specifies and every C library implements.
    QString base;
    QString baseSource;
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const QString value = environmentValue(variable);
        if (!value.isEmpty()) {
            base = value;
            baseSource = QString::fromLatin1(variable);
            break;
        }
    }

    // LANGUAGE is GNU's ordered preference list and outranks the ladder — but
    // only when the ladder resolved to a real locale. `LC_ALL=C` must mean
    // "untranslated output" even for someone whose LANGUAGE says es:fr:de,
    // because that is the contract every script relies on when it wants
    // parseable output.
    if (!base.isEmpty() && !neutral(base)) {
        const QString language = environmentValue("LANGUAGE");
        if (!language.isEmpty()) {
            // A colon-separated list, most preferred first.
            const QStringList preferences =
                language.split(QLatin1Char(':'), Qt::SkipEmptyParts);
            for (const QString& preference : preferences) {
                appendIfNew(chain, preference, QStringLiteral("LANGUAGE"));
            }
        }
    }

    appendIfNew(chain, base, baseSource);
    return chain;
}

std::vector<LocaleCandidate> systemLocales() {
    std::vector<LocaleCandidate> found;

#if defined(Q_OS_WIN)
    for (const LocaleCandidate& candidate : windowsUiLanguages()) {
        appendIfNew(found, candidate.tag, candidate.source);
    }
#elif defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
    for (const LocaleCandidate& candidate : macUiLanguages()) {
        appendIfNew(found, candidate.tag, candidate.source);
    }
#elif defined(LC_MESSAGES)
    appendIfNew(found, posixMessagesLocale(), QStringLiteral("setlocale(LC_MESSAGES)"));
#endif

    // Qt's own view, last. On a platform handled above this normally repeats
    // an answer already in the list, and appendIfNew drops the duplicate; on
    // one that is not, it is the whole answer.
    const QStringList qtLanguages = QLocale::system().uiLanguages();
    for (const QString& language : qtLanguages) {
        appendIfNew(found, language, QStringLiteral("QLocale::uiLanguages"));
    }
    appendIfNew(found, QLocale::system().name(), QStringLiteral("QLocale::system"));

    return found;
}

}  // namespace weight::platform
