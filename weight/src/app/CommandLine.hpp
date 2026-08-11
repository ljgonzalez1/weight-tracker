#pragma once

#include <optional>

#include <QString>
#include <QStringList>

#include "settings/Settings.hpp"

namespace weight::app {

/// What the command line asked the program to do.
struct CommandLineResult {
    enum class Action {
        Run,          ///< Open the window.
        ShowHelp,     ///< Help was printed; exit successfully.
        ShowVersion,  ///< Version was printed; exit successfully.
        SelfTest,     ///< Run the built-in checks and exit.
        Error,        ///< Bad usage; exit with a non-zero status.
    };

    Action action = Action::Run;
    /// Overrides where the workspace folder lives. Empty means "search the
    /// platform's document directories in the documented order".
    QString workspaceOverride;
    QString errorMessage;
    bool overridesApplied = false;
};

/// Parses the command line and applies the overrides it carries.
///
/// Options are resolved in a fixed order of increasing precedence: compiled
/// defaults, then the user's settings file, then the command line. That way a
/// one-off experiment on the command line never has to be undone afterwards,
/// and a permanent preference never has to be retyped.
class CommandLine {
public:
    [[nodiscard]] static CommandLineResult parse(const QStringList& arguments,
                                                 settings::Settings& configuration);

    /// Help text, also used by --help.
    [[nodiscard]] static QString helpText(const settings::Settings& configuration);
};

}  // namespace weight::app
