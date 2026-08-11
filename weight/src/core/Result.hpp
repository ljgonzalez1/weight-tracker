#pragma once

#include <QString>

#include <optional>
#include <utility>
#include <variant>

/// Explicit error handling for operations that can fail for reasons the user
/// can act on: missing files, denied permissions, a full disk, malformed data.
/// Exceptions remain reserved for programming errors and are not used to
/// signal these ordinary, expected outcomes.
namespace weight::core {

/// Category of a failure, so callers can react without parsing text.
enum class ErrorCode {
    None,
    NotFound,
    PermissionDenied,
    ReadFailed,
    WriteFailed,
    OutOfSpace,
    InvalidFormat,
    InvalidArgument,
    Cancelled,
    Unknown,
};

/// A failure with a message meant to be shown to a person: what failed, on
/// which path, and when possible what to do about it.
struct Error {
    ErrorCode code = ErrorCode::Unknown;
    QString message;

    [[nodiscard]] QString toString() const { return message; }
};

/// Human readable name of an error category, used in logs.
[[nodiscard]] QString describe(ErrorCode code);

/// Builds an Error whose message follows the house style:
///     "<action> failed: <path>: <reason>"
[[nodiscard]] Error makeError(ErrorCode code, const QString& action, const QString& detail);

/// Either a value or an Error. Deliberately minimal: construct, test, unwrap.
template <typename T>
class Result {
public:
    Result(T value) : storage_(std::move(value)) {}      // NOLINT(google-explicit-constructor)
    Result(Error error) : storage_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }
    [[nodiscard]] T& value() & { return std::get<0>(storage_); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(storage_)); }

    [[nodiscard]] const Error& error() const& { return std::get<1>(storage_); }

    /// Returns the value if present, otherwise the supplied fallback.
    [[nodiscard]] T valueOr(T fallback) const {
        return hasValue() ? value() : std::move(fallback);
    }

private:
    std::variant<T, Error> storage_;
};

/// Result of an operation that produces no value.
class Status {
public:
    Status() = default;
    Status(Error error) : error_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] const Error& error() const { return *error_; }

    static Status success() { return {}; }

private:
    std::optional<Error> error_;
};

}  // namespace weight::core
