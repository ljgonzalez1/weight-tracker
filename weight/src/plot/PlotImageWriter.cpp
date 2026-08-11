#include "plot/PlotImageWriter.hpp"

#include <QBuffer>

#include "core/Logging.hpp"
#include "data/AtomicFileWriter.hpp"
#include "settings/Strings.hpp"

namespace weight::plot {

QString PlotImageWriter::buildFileName(const settings::Settings& settings, double smoothness,
                                       const QDateTime& moment) {
    const settings::OutputNaming& naming = settings.output;
    const QDateTime effective = moment.isValid() ? moment : QDateTime::currentDateTime();
    return naming.filePrefix + effective.toString(naming.timestampFormat) + naming.smoothnessInfix
           + QString::number(smoothness, 'f', naming.smoothnessDecimals) + naming.fileExtension;
}

core::Status PlotImageWriter::writePng(const QImage& image, const QString& path) {
    if (image.isNull()) {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               QStringLiteral("Saving the chart"),
                               QStringLiteral("the rendered image is empty"));
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return core::makeError(core::ErrorCode::WriteFailed, QStringLiteral("Saving the chart"),
                               QStringLiteral("could not allocate an encoding buffer"));
    }
    if (!image.save(&buffer, "PNG")) {
        return core::makeError(core::ErrorCode::WriteFailed, QStringLiteral("Saving the chart"),
                               QStringLiteral("%1: PNG encoding failed").arg(path));
    }
    buffer.close();

    const core::Status status = data::AtomicFileWriter::write(path, encoded);
    if (status) {
        core::log::success(settings::Strings::get(QStringLiteral("log.saved.chart"), path));
    }
    return status;
}

}  // namespace weight::plot
