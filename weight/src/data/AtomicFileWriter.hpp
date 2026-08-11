#pragma once

#include <QByteArray>
#include <QString>

#include "core/Result.hpp"

namespace weight::data {

/// Writes a file so that it is either fully replaced or left untouched.
///
/// The sequence is: write the new contents to a temporary file beside the
/// target, flush it to the storage device, verify what landed there, and only
/// then rename it over the original. Rename within one filesystem is atomic, so
/// a crash, a power loss or a full disk at any point leaves the previous
/// version intact. Writing in place would risk truncating a history that took
/// months to accumulate.
///
/// QSaveFile implements exactly this pattern, including the flush, so it is
/// used rather than reimplemented; this class adds the read-back verification
/// and turns the failure modes into the actionable messages the rest of the
/// program expects.
class AtomicFileWriter {
public:
    /// Replaces `path` with `contents`. Creates parent directories when they
    /// are missing.
    ///
    /// `verify` re-reads the file after committing and compares it byte for
    /// byte. It costs one extra read and catches the case where a device
    /// reports success while silently discarding data, which is worth paying
    /// for on a file that holds irreplaceable history.
    [[nodiscard]] static core::Status write(const QString& path, const QByteArray& contents,
                                            bool verify = true);

    /// Checks in advance whether the target can be created or replaced, so the
    /// caller can warn before doing work that cannot be saved.
    [[nodiscard]] static core::Status checkWritable(const QString& path);
};

}  // namespace weight::data
