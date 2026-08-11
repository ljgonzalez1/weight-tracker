#pragma once

#include <QString>

/// Terminal diagnostics. The application writes a running commentary of what it
/// reads, repairs and saves; the categories below mirror the ones the previous
/// implementation used so existing habits and log filters keep working.
namespace weight::core::log {

enum class Level {
    Info,     ///< Normal progress.
    Success,  ///< An operation completed and produced a durable result.
    Warning,  ///< Something was skipped or ignored; the run continues.
    Error,    ///< An operation failed.
    Repair,   ///< Malformed input was recovered; says exactly what changed.
};

/// Enables or disables ANSI colour escapes. Detected once at start-up from
/// whether stderr is a terminal, and forced off when NO_COLOR is set.
void setColorEnabled(bool enabled);
[[nodiscard]] bool colorEnabled();

/// Silences every category except Error; used by the test suite.
void setQuiet(bool quiet);

void write(Level level, const QString& message);

void info(const QString& message);
void success(const QString& message);
void warning(const QString& message);
void error(const QString& message);
void repair(const QString& message);

}  // namespace weight::core::log
