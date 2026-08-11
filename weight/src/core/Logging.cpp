#include "core/Logging.hpp"

#include <QByteArray>
#include <QTextStream>

#include <cstdio>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace weight::core::log {
namespace {

struct Style {
    const char* tag;
    const char* color;
};

constexpr const char* kReset = "\033[0m";

Style styleFor(Level level) {
    switch (level) {
        case Level::Info:    return {"INFO", "\033[36m"};   // cyan
        case Level::Success: return {"OK",   "\033[32m"};   // green
        case Level::Warning: return {"WARN", "\033[33m"};   // yellow
        case Level::Error:   return {"ERR",  "\033[31m"};   // red
        case Level::Repair:  return {"FIX",  "\033[35m"};   // magenta
    }
    return {"INFO", "\033[36m"};
}

bool detectColorSupport() {
    if (!qEnvironmentVariableIsEmpty("NO_COLOR")) {
        return false;
    }
#ifdef _WIN32
    return false;
#else
    return ::isatty(fileno(stderr)) != 0;
#endif
}

bool g_colorEnabled = detectColorSupport();
bool g_quiet = false;

}  // namespace

void setColorEnabled(bool enabled) { g_colorEnabled = enabled; }
bool colorEnabled() { return g_colorEnabled; }
void setQuiet(bool quiet) { g_quiet = quiet; }

void write(Level level, const QString& message) {
    if (g_quiet && level != Level::Error) {
        return;
    }
    const Style style = styleFor(level);
    QTextStream stream(stderr);
    if (g_colorEnabled) {
        stream << QString::fromLatin1(style.color) << QLatin1Char('[')
               << QString::fromLatin1(style.tag) << QLatin1Char(']')
               << QString::fromLatin1(kReset);
    } else {
        stream << QLatin1Char('[') << QString::fromLatin1(style.tag) << QLatin1Char(']');
    }
    stream << QLatin1Char(' ') << message << Qt::endl;
}

void info(const QString& message) { write(Level::Info, message); }
void success(const QString& message) { write(Level::Success, message); }
void warning(const QString& message) { write(Level::Warning, message); }
void error(const QString& message) { write(Level::Error, message); }
void repair(const QString& message) { write(Level::Repair, message); }

}  // namespace weight::core::log
