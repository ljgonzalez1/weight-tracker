#include "app/CommandLine.hpp"

#include <QCommandLineParser>
#include <QDate>
#include <QDir>
#include <QTextStream>

#include "core/Logging.hpp"
#include "core/ProjectVersion.hpp"

namespace weight::app {
namespace {

/// Reads a floating point option, reporting rather than ignoring a bad value.
bool applyDouble(const QCommandLineParser& parser, const QString& name, double& target,
                 QString& error) {
    if (!parser.isSet(name)) {
        return true;
    }
    bool ok = false;
    const double value = parser.value(name).toDouble(&ok);
    if (!ok) {
        error = QStringLiteral("--%1 expects a number, got \"%2\"").arg(name, parser.value(name));
        return false;
    }
    target = value;
    return true;
}

bool applyInt(const QCommandLineParser& parser, const QString& name, int& target,
              QString& error) {
    if (!parser.isSet(name)) {
        return true;
    }
    bool ok = false;
    const int value = parser.value(name).toInt(&ok);
    if (!ok) {
        error =
            QStringLiteral("--%1 expects an integer, got \"%2\"").arg(name, parser.value(name));
        return false;
    }
    target = value;
    return true;
}

}  // namespace

QString CommandLine::helpText(const settings::Settings& configuration) {
    return QStringLiteral(
               "%1 %2 \u2014 %3\n"
               "\n"
               "Usage:\n"
               "  %4 [options] [history.csv]\n"
               "\n"
               "The history file uses the format day;mass;timestamp with ';' as the\n"
               "delimiter. The day is a fractional offset from the origin date (%5);\n"
               "negative values denote earlier dates and are valid.\n"
               "\n"
               "With no arguments the history is read from the standard data directory\n"
               "for this platform, and a history left by an earlier release is adopted\n"
               "automatically on first run.\n")
        .arg(QString::fromLatin1(core::version::kDisplayName),
             QString::fromLatin1(core::version::kVersionString),
             QString::fromLatin1(core::version::kDescription),
             QString::fromLatin1(core::version::kExecutableName),
             configuration.plot.geometry.originDate.toString(Qt::ISODate));
}

CommandLineResult CommandLine::parse(const QStringList& arguments,
                                     settings::Settings& configuration) {
    CommandLineResult result;

    QCommandLineParser parser;
    parser.setApplicationDescription(helpText(configuration));
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);

    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();

    const QCommandLineOption workspaceOption(
        QStringLiteral("workspace"),
        QStringLiteral("Folder holding weight-data.csv, images/ and config.txt. Defaults to "
                       "the first of the platform's document directories that exists."),
        QStringLiteral("path"));
    const QCommandLineOption originOption(
        QStringLiteral("origin-date"),
        QStringLiteral("Calendar date mapped to day 0.0, in yyyy-MM-dd form."),
        QStringLiteral("date"));
    const QCommandLineOption smoothnessOption(
        QStringLiteral("smoothness"),
        QStringLiteral("Trend smoothness: above 1 is smoother, below 1 follows the samples "
                       "more closely."),
        QStringLiteral("value"));
    const QCommandLineOption selfTestOption(
        QStringLiteral("self-test"),
        QStringLiteral("Run the built-in checks against this binary and exit."));
    const QCommandLineOption quietOption(QStringLiteral("quiet"),
                                         QStringLiteral("Suppress everything except errors."));

    QList<QCommandLineOption> options{
        workspaceOption, originOption, smoothnessOption, selfTestOption, quietOption,
        {QStringLiteral("x-min"), QStringLiteral("Left bound of the day axis."),
         QStringLiteral("value")},
        {QStringLiteral("x-max"), QStringLiteral("Right bound of the day axis."),
         QStringLiteral("value")},
        {QStringLiteral("y-min"), QStringLiteral("Lower bound of the mass axis, kg."),
         QStringLiteral("value")},
        {QStringLiteral("y-max"), QStringLiteral("Upper bound of the mass axis, kg."),
         QStringLiteral("value")},
        {QStringLiteral("x-label-step-days"), QStringLiteral("Days between X labels."),
         QStringLiteral("value")},
        {QStringLiteral("y-label-step"), QStringLiteral("kg between major Y ticks."),
         QStringLiteral("value")},
        {QStringLiteral("y-grid-step"), QStringLiteral("kg between minor Y grid lines."),
         QStringLiteral("value")},
        {QStringLiteral("width-px"), QStringLiteral("Exported image width in pixels."),
         QStringLiteral("value")},
        {QStringLiteral("height-px"), QStringLiteral("Exported image height in pixels."),
         QStringLiteral("value")},
        {QStringLiteral("dpi"), QStringLiteral("Exported image resolution."),
         QStringLiteral("value")},
    };
    for (const QCommandLineOption& option : options) {
        parser.addOption(option);
    }
    parser.addPositionalArgument(QStringLiteral("workspace"),
                                 QStringLiteral("Folder to read and write."));

    if (!parser.parse(arguments)) {
        result.action = CommandLineResult::Action::Error;
        result.errorMessage = parser.errorText();
        return result;
    }
    if (parser.isSet(helpOption)) {
        QTextStream(stdout) << parser.helpText() << Qt::endl;
        result.action = CommandLineResult::Action::ShowHelp;
        return result;
    }
    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << QString::fromLatin1(core::version::kDisplayName) << QLatin1Char(' ')
                            << QString::fromLatin1(core::version::kVersionString) << Qt::endl;
        result.action = CommandLineResult::Action::ShowVersion;
        return result;
    }

    QString error;
    const auto applyAll = [&] {
        return applyDouble(parser, QStringLiteral("x-min"), configuration.plot.geometry.xMinimum,
                           error)
               && applyDouble(parser, QStringLiteral("x-max"),
                              configuration.plot.geometry.xMaximum, error)
               && applyDouble(parser, QStringLiteral("y-min"),
                              configuration.plot.geometry.yMinimum, error)
               && applyDouble(parser, QStringLiteral("y-max"),
                              configuration.plot.geometry.yMaximum, error)
               && applyDouble(parser, QStringLiteral("x-label-step-days"),
                              configuration.plot.geometry.xLabelStepDays, error)
               && applyDouble(parser, QStringLiteral("y-label-step"),
                              configuration.plot.geometry.yLabelStep, error)
               && applyDouble(parser, QStringLiteral("y-grid-step"),
                              configuration.plot.geometry.yGridStep, error)
               && applyDouble(parser, QStringLiteral("smoothness"),
                              configuration.trend.common.generalSmoothness, error)
               && applyInt(parser, QStringLiteral("width-px"),
                           configuration.plot.geometry.widthPixels, error)
               && applyInt(parser, QStringLiteral("height-px"),
                           configuration.plot.geometry.heightPixels, error)
               && applyInt(parser, QStringLiteral("dpi"), configuration.plot.geometry.dotsPerInch,
                           error);
    };
    if (!applyAll()) {
        result.action = CommandLineResult::Action::Error;
        result.errorMessage = error;
        return result;
    }
    result.overridesApplied = true;

    if (parser.isSet(originOption)) {
        const QDate origin = QDate::fromString(parser.value(originOption), Qt::ISODate);
        if (!origin.isValid()) {
            result.action = CommandLineResult::Action::Error;
            result.errorMessage =
                QStringLiteral("--origin-date expects yyyy-MM-dd, got \"%1\"")
                    .arg(parser.value(originOption));
            return result;
        }
        configuration.plot.geometry.originDate = origin;
    }


    if (parser.isSet(selfTestOption)) {
        result.action = CommandLineResult::Action::SelfTest;
        return result;
    }
    if (parser.isSet(workspaceOption)) {
        result.workspaceOverride = QDir::cleanPath(parser.value(workspaceOption));
    }
    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        result.workspaceOverride = QDir::cleanPath(positional.first());
    }
    if (positional.size() > 1) {
        result.action = CommandLineResult::Action::Error;
        result.errorMessage = QStringLiteral("At most one workspace folder may be given.");
        return result;
    }
    if (positional.size() > 1) {
        result.action = CommandLineResult::Action::Error;
        result.errorMessage = QStringLiteral("At most one history file may be given.");
        return result;
    }

    if (parser.isSet(quietOption)) {
        core::log::setQuiet(true);
    }
    return result;
}

}  // namespace weight::app
