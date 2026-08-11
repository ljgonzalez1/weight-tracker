#include "core/Result.hpp"

namespace weight::core {

QString describe(ErrorCode code) {
    switch (code) {
        case ErrorCode::None:             return QStringLiteral("no error");
        case ErrorCode::NotFound:         return QStringLiteral("not found");
        case ErrorCode::PermissionDenied: return QStringLiteral("permission denied");
        case ErrorCode::ReadFailed:       return QStringLiteral("read failed");
        case ErrorCode::WriteFailed:      return QStringLiteral("write failed");
        case ErrorCode::OutOfSpace:       return QStringLiteral("no space left on device");
        case ErrorCode::InvalidFormat:    return QStringLiteral("invalid format");
        case ErrorCode::InvalidArgument:  return QStringLiteral("invalid argument");
        case ErrorCode::Cancelled:        return QStringLiteral("cancelled");
        case ErrorCode::Unknown:          break;
    }
    return QStringLiteral("unknown error");
}

Error makeError(ErrorCode code, const QString& action, const QString& detail) {
    Error error;
    error.code = code;
    error.message = detail.isEmpty()
                        ? QStringLiteral("%1 failed: %2").arg(action, describe(code))
                        : QStringLiteral("%1 failed: %2").arg(action, detail);
    return error;
}

}  // namespace weight::core
