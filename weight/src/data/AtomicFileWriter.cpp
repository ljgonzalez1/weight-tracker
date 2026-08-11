#include "data/AtomicFileWriter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>

namespace weight::data {
namespace {

using core::ErrorCode;
using core::makeError;
using core::Status;

/// Maps a Qt file error onto the category the user interface reacts to.
ErrorCode classify(QFileDevice::FileError error) {
    switch (error) {
        case QFileDevice::PermissionsError:
            return ErrorCode::PermissionDenied;
        case QFileDevice::ResourceError:
            return ErrorCode::OutOfSpace;
        case QFileDevice::OpenError:
        case QFileDevice::WriteError:
        case QFileDevice::FatalError:
            return ErrorCode::WriteFailed;
        default:
            return ErrorCode::WriteFailed;
    }
}

}  // namespace

Status AtomicFileWriter::checkWritable(const QString& path) {
    const QFileInfo info(path);
    const QDir parent = info.absoluteDir();

    if (!parent.exists()) {
        if (!QDir().mkpath(parent.absolutePath())) {
            return makeError(ErrorCode::PermissionDenied, QStringLiteral("Preparing to write"),
                             QStringLiteral("%1: the directory does not exist and could not be "
                                            "created")
                                 .arg(parent.absolutePath()));
        }
    }

    if (info.exists()) {
        if (!info.isFile()) {
            return makeError(ErrorCode::InvalidArgument, QStringLiteral("Preparing to write"),
                             QStringLiteral("%1: exists but is not a regular file").arg(path));
        }
        if (!info.isWritable()) {
            return makeError(ErrorCode::PermissionDenied, QStringLiteral("Preparing to write"),
                             QStringLiteral("%1: the file is read-only").arg(path));
        }
    }

    const QFileInfo parentInfo(parent.absolutePath());
    if (!parentInfo.isWritable()) {
        return makeError(ErrorCode::PermissionDenied, QStringLiteral("Preparing to write"),
                         QStringLiteral("%1: the directory is not writable")
                             .arg(parent.absolutePath()));
    }
    return Status::success();
}

Status AtomicFileWriter::write(const QString& path, const QByteArray& contents, bool verify) {
    if (const Status writable = checkWritable(path); !writable) {
        return writable;
    }

    // Refuse up front when the device clearly cannot hold the payload; the
    // resulting message is far more useful than a truncated write later.
    const QStorageInfo storage(QFileInfo(path).absolutePath());
    if (storage.isValid() && storage.bytesAvailable() >= 0
        && storage.bytesAvailable() < contents.size()) {
        return makeError(ErrorCode::OutOfSpace, QStringLiteral("Writing"),
                         QStringLiteral("%1: %2 bytes needed but only %3 available on %4")
                             .arg(path)
                             .arg(contents.size())
                             .arg(storage.bytesAvailable())
                             .arg(QString::fromUtf8(storage.rootPath().toUtf8())));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return makeError(classify(file.error()), QStringLiteral("Writing"),
                         QStringLiteral("%1: %2").arg(path, file.errorString()));
    }
    if (file.write(contents) != contents.size()) {
        const QString reason = file.errorString();
        file.cancelWriting();
        return makeError(ErrorCode::WriteFailed, QStringLiteral("Writing"),
                         QStringLiteral("%1: %2").arg(path, reason));
    }
    if (!file.commit()) {
        return makeError(classify(file.error()), QStringLiteral("Writing"),
                         QStringLiteral("%1: %2").arg(path, file.errorString()));
    }

    if (verify) {
        QFile written(path);
        if (!written.open(QIODevice::ReadOnly)) {
            return makeError(ErrorCode::ReadFailed, QStringLiteral("Verifying the written file"),
                             QStringLiteral("%1: %2").arg(path, written.errorString()));
        }
        if (written.readAll() != contents) {
            return makeError(ErrorCode::WriteFailed,
                             QStringLiteral("Verifying the written file"),
                             QStringLiteral("%1: the file on disk does not match what was "
                                            "written; the storage device may be failing")
                                 .arg(path));
        }
    }
    return Status::success();
}

}  // namespace weight::data
