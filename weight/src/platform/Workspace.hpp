#pragma once

#include <QString>
#include <QStringList>

#include "core/Result.hpp"

namespace weight::platform {

/// Where the program keeps the history, the images and config.txt.
///
/// The rule is **search before create**: if a suitable folder already exists,
/// it is adopted; only when none of the candidates exists is the first one
/// created. That way a person who already has `~/Documentos/weight` keeps using
/// it, and someone starting fresh gets the platform's own convention rather
/// than whatever happened to be listed first.
///
/// The candidate lists below are ordinary arrays and are meant to be reordered
/// freely; nothing else in the program depends on their order.
class Workspace {
public:
    /// Folder name used inside whichever parent directory wins. Comes from the
    /// settings so it is not spelled out in code.
    explicit Workspace(QString folderName);

    /// Ordered candidate paths for this platform, already expanded.
    ///
    /// Linux, highest priority first:
    ///   1. $XDG_DOCUMENTS_DIR/<folder>          (from the XDG user-dirs spec)
    ///   2. $HOME/Documents/<folder>
    ///   3. $HOME/documents/<folder>
    ///   4. $HOME/Documentos/<folder>
    ///   5. $HOME/documentos/<folder>
    ///   6. $HOME/.local/share/<folder>
    ///
    /// Windows:
    ///   1. the Documents known folder, resolved through the platform API, so a
    ///      redirected or roamed Documents folder is honoured
    ///   2. %USERPROFILE%\Documents\<folder>
    ///
    /// macOS:
    ///   1. ~/Documents/<folder>
    ///   2. ~/Documentos/<folder>
    ///   3. ~/Library/Application Support/<folder>
    [[nodiscard]] QStringList candidates() const;

    /// First candidate that already exists as a writable directory, or an
    /// empty string when none does.
    [[nodiscard]] QString findExisting() const;

    /// The folder to use: the first existing one, or the first candidate
    /// created on demand. Reports precisely why when nothing can be used.
    [[nodiscard]] core::Result<QString> resolve() const;

    /// Paths inside the workspace.
    [[nodiscard]] static QString historyPath(const QString& workspace, const QString& fileName);
    [[nodiscard]] static QString imagesPath(const QString& workspace, const QString& subdir);
    [[nodiscard]] static QString configPath(const QString& workspace, const QString& fileName);

    /// The platform's Documents directory, resolved natively.
    ///
    /// On Windows this goes through `SHGetKnownFolderPath(FOLDERID_Documents)`
    /// via Qt's `QStandardPaths::DocumentsLocation`, which wraps exactly that
    /// call. Reading `%USERPROFILE%\Documents` directly would miss a redirected
    /// or roamed Documents folder, which is common on managed machines.
    [[nodiscard]] static QString platformDocumentsDirectory();

    /// Value of XDG_DOCUMENTS_DIR, read from the environment and then from
    /// `~/.config/user-dirs.dirs`, which is where the XDG spec actually stores
    /// it; the variable is only exported by some desktop sessions.
    [[nodiscard]] static QString xdgDocumentsDirectory();

private:
    QString folderName_;
};

}  // namespace weight::platform
