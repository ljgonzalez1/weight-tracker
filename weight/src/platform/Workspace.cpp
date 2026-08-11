#include "platform/Workspace.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdlib>

namespace weight::platform {
namespace {

QString environmentValue(const char* name) {
    const char* raw = std::getenv(name);
    return (raw != nullptr) ? QString::fromLocal8Bit(raw).trimmed() : QString();
}

/// True when the path names an existing, writable directory.
bool usable(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    return info.exists() && info.isDir() && info.isWritable();
}

}  // namespace

Workspace::Workspace(QString folderName) : folderName_(std::move(folderName)) {}

QString Workspace::platformDocumentsDirectory() {
    // QStandardPaths::DocumentsLocation is implemented on Windows with
    // SHGetKnownFolderPath(FOLDERID_Documents), on macOS with the Cocoa
    // equivalent, and on Linux by reading the XDG user-dirs configuration.
    // Using it keeps one code path and still honours a redirected folder.
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString Workspace::xdgDocumentsDirectory() {
    // The variable is exported by some sessions and not others.
    const QString exported = environmentValue("XDG_DOCUMENTS_DIR");
    if (!exported.isEmpty()) {
        return exported;
    }

    // Otherwise read the file the specification actually stores it in. The
    // format is: XDG_DOCUMENTS_DIR="$HOME/Documents"
    QString configHome = environmentValue("XDG_CONFIG_HOME");
    if (configHome.isEmpty()) {
        configHome = QDir(QDir::homePath()).filePath(QStringLiteral(".config"));
    }
    const QString userDirs = QDir(configHome).filePath(QStringLiteral("user-dirs.dirs"));

    QFile file(userDirs);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*XDG_DOCUMENTS_DIR\\s*=\\s*\"?([^\"\\n]*)\"?\\s*$"));

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QRegularExpressionMatch match = pattern.match(stream.readLine());
        if (!match.hasMatch()) {
            continue;
        }
        QString value = match.captured(1).trimmed();
        value.replace(QLatin1String("$HOME"), QDir::homePath());
        value.replace(QLatin1String("${HOME}"), QDir::homePath());
        return value;
    }
    return {};
}

QStringList Workspace::candidates() const {
    const QString home = QDir::homePath();
    QStringList parents;

#if defined(Q_OS_WIN)
    // 1. The real Documents folder, wherever the person has moved it to.
    //    QStandardPaths::DocumentsLocation calls SHGetKnownFolderPath with
    //    FOLDERID_Documents, so a redirected or OneDrive-backed Documents is
    //    honoured; hardcoding C:\Users\<user>\Documents is not.
    // 2. The literal path under USERPROFILE, for the rare configuration where
    //    the known folder cannot be resolved.
    // 3. %LOCALAPPDATA%, which always exists and is always writable, so the
    //    program can still start on a machine with no Documents folder at all.
    parents << platformDocumentsDirectory()
            << QDir(environmentValue("USERPROFILE").isEmpty() ? home
                                                              : environmentValue("USERPROFILE"))
                   .filePath(QStringLiteral("Documents"))
            << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#elif defined(Q_OS_MACOS)
    // DocumentsLocation first, because a macOS account can have iCloud Drive
    // redirect Documents; then the literal names, then the standard
    // per-application location.
    //
    // GenericDataLocation, not AppDataLocation: the latter already appends the
    // organisation and application names, so joining "weight" onto it would
    // produce ~/Library/Application Support/Weight/weight. The generic form is
    // ~/Library/Application Support, and the join gives exactly the
    // ~/Library/Application Support/weight the brief asks for.
    parents << platformDocumentsDirectory()
            << QDir(home).filePath(QStringLiteral("Documents"))
            << QDir(home).filePath(QStringLiteral("Documentos"))
            << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#else
    // Linux and the other Unixes. XDG first, because that is the only entry a
    // localised desktop actually updates; then the four spellings the brief
    // lists, in its order; then the XDG data directory as the fallback that
    // always exists.
    //
    // GenericDataLocation resolves $XDG_DATA_HOME when it is set and
    // ~/.local/share when it is not, which is one line instead of two and is
    // correct on a system that has moved it.
    parents << xdgDocumentsDirectory()
            << platformDocumentsDirectory()
            << QDir(home).filePath(QStringLiteral("Documents"))
            << QDir(home).filePath(QStringLiteral("documents"))
            << QDir(home).filePath(QStringLiteral("Documentos"))
            << QDir(home).filePath(QStringLiteral("documentos"))
            << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            << QDir(home).filePath(QStringLiteral(".local/share"));
#endif

    QStringList result;
    for (const QString& parent : parents) {
        if (parent.isEmpty()) {
            continue;
        }
        const QString full = QDir::cleanPath(QDir(parent).filePath(folderName_));
        if (!result.contains(full)) {
            result.append(full);
        }
    }
    return result;
}

QString Workspace::findExisting() const {
    for (const QString& candidate : candidates()) {
        if (usable(candidate)) {
            return candidate;
        }
    }
    return {};
}

core::Result<QString> Workspace::resolve() const {
    if (const QString existing = findExisting(); !existing.isEmpty()) {
        return existing;
    }

    // Nothing exists yet. Create the first candidate whose *parent* already
    // exists, so a fresh install lands in the real Documents folder rather
    // than inventing a "documentos" directory that the user never had.
    const QStringList options = candidates();
    for (const QString& candidate : options) {
        const QString parent = QFileInfo(candidate).absolutePath();
        if (!QFileInfo(parent).isDir()) {
            continue;
        }
        if (QDir().mkpath(candidate) && usable(candidate)) {
            return candidate;
        }
    }

    // No candidate parent exists either; fall back to creating the last one,
    // which on every platform sits under a directory the system guarantees.
    if (!options.isEmpty()) {
        const QString last = options.last();
        if (QDir().mkpath(last) && usable(last)) {
            return last;
        }
    }

    return core::makeError(
        core::ErrorCode::PermissionDenied, QStringLiteral("Locating the workspace"),
        QStringLiteral("none of these could be created: %1").arg(options.join(QLatin1String(", "))));
}

QString Workspace::historyPath(const QString& workspace, const QString& fileName) {
    return QDir(workspace).filePath(fileName);
}

QString Workspace::imagesPath(const QString& workspace, const QString& subdir) {
    return QDir(workspace).filePath(subdir);
}

QString Workspace::configPath(const QString& workspace, const QString& fileName) {
    return QDir(workspace).filePath(fileName);
}

}  // namespace weight::platform
