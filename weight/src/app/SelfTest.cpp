#include "app/SelfTest.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

#include "core/Concurrency.hpp"
#include "core/SessionConfig.hpp"
#include "data/CsvSampleRepository.hpp"
#include "math/common/CurveFactory.hpp"
#include "math/common/TrendService.hpp"
#include "platform/SingleInstance.hpp"
#include "platform/Workspace.hpp"
#include "plot/PainterPlotRenderer.hpp"
#include "plot/PlotSceneBuilder.hpp"
#include "settings/CurveCatalog.hpp"
#include "settings/Strings.hpp"

namespace weight::app {
namespace {

struct Recorder {
    int passed = 0;
    int failed = 0;
    QTextStream out{stdout};

    void check(bool condition, const QString& name, const QString& detail = QString()) {
        if (condition) {
            ++passed;
            out << "  ok    " << name << Qt::endl;
        } else {
            ++failed;
            out << "  FAIL  " << name;
            if (!detail.isEmpty()) {
                out << "  (" << detail << ')';
            }
            out << Qt::endl;
        }
    }
    void section(const QString& title) { out << '\n' << title << Qt::endl; }
};

/// A short, deterministic history used by the numeric checks.
std::vector<math::SamplePoint> syntheticSamples() {
    std::vector<math::SamplePoint> samples;
    for (int day = 0; day < 90; ++day) {
        // A clean linear decline of 0.1 kg/day, so the expected weekly rate is
        // exactly -0.7 and any error is unambiguous.
        samples.push_back({static_cast<double>(day), 90.0 - 0.1 * day});
    }
    return samples;
}

}  // namespace

int runSelfTest(const settings::Settings& baseSettings) {
    Recorder recorder;
    recorder.out << "Weight self-test" << Qt::endl;
    recorder.out << "Platform: " << QSysInfo::prettyProductName() << "  ("
                 << QSysInfo::currentCpuArchitecture() << ", kernel "
                 << QSysInfo::kernelType() << ' ' << QSysInfo::kernelVersion() << ')'
                 << Qt::endl;
    recorder.out << "Qt: " << QT_VERSION_STR << Qt::endl;

    // Everything below runs in a throwaway directory. Nothing touches the real
    // Documents folder: these are tests, and they must not be able to damage a
    // person's records even if one of them is wrong.
    QTemporaryDir sandbox;
    recorder.section(QStringLiteral("Sandbox"));
    recorder.check(sandbox.isValid(), QStringLiteral("temporary workspace created"),
                   sandbox.path());
    if (!sandbox.isValid()) {
        return 1;
    }

    // -- embedded assets ---------------------------------------------------
    recorder.section(QStringLiteral("Embedded assets"));
    const QIcon icon(QStringLiteral(":/icons/program-icon-512.png"));
    recorder.check(!icon.availableSizes().isEmpty(),
                   QStringLiteral("application icon is inside the binary"),
                   QStringLiteral("the .qrc must be compiled into the executable, not a "
                                  "static library"));

    int facesLoaded = 0;
    QString family;
    for (const char* face : {":/fonts/OpenRunde-Regular.otf", ":/fonts/OpenRunde-Medium.otf",
                             ":/fonts/OpenRunde-Semibold.otf", ":/fonts/OpenRunde-Bold.otf"}) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(face));
        if (id >= 0) {
            ++facesLoaded;
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (family.isEmpty() && !families.isEmpty()) {
                family = families.first();
            }
        }
    }
    recorder.check(facesLoaded == 4, QStringLiteral("all four Open Runde faces load"),
                   QStringLiteral("%1 of 4").arg(facesLoaded));
    recorder.check(family.contains(QStringLiteral("Runde")),
                   QStringLiteral("the font family really is Open Runde"), family);
    recorder.check(QFontDatabase::families().contains(family),
                   QStringLiteral("the font is registered with the font database"));

    // -- localisation ------------------------------------------------------
    recorder.section(QStringLiteral("Localisation"));
    recorder.check(settings::Strings::languageForTag(QStringLiteral("C"))
                       == settings::Language::English,
                   QStringLiteral("a bare C locale selects English"));
    recorder.check(settings::Strings::languageForTag(QStringLiteral("es_CL.UTF-8"))
                       == settings::Language::Spanish,
                   QStringLiteral("es_* selects Spanish"));
    recorder.check(settings::Strings::languageForTag(QStringLiteral("de_DE"))
                       == settings::Language::English,
                   QStringLiteral("an unsupported locale falls back to English"));
    recorder.check(settings::Strings::keys(settings::Language::English).size()
                       == settings::Strings::keys(settings::Language::Spanish).size(),
                   QStringLiteral("both languages define the same number of strings"));

    // -- curve catalogue ---------------------------------------------------
    recorder.section(QStringLiteral("Curve catalogue"));
    recorder.check(settings::curveCount() > 0, QStringLiteral("at least one curve is registered"));
    for (std::size_t i = 0; i < settings::curveCount(); ++i) {
        const settings::CurveDescriptor& curve = settings::curveCatalogue()[i];
        recorder.check(math::createCurveEstimator(i) != nullptr,
                       QStringLiteral("curve '%1' can be constructed").arg(curve.id));
        recorder.check(curve.colour.toColor().isValid(),
                       QStringLiteral("curve '%1' declares a valid r,g,b").arg(curve.id));
    }

    // -- numerics ----------------------------------------------------------
    recorder.section(QStringLiteral("Numerics"));
    const std::vector<math::SamplePoint> samples = syntheticSamples();
    math::TrendRequest request(settings::curveCount());
    for (std::size_t i = 0; i < settings::curveCount(); ++i) {
        request.curveWanted[i] = true;
        request.derivativeWanted[i] = true;
    }
    core::Concurrency::configure(static_cast<int>(settings::curveCount()),
                                 baseSettings.concurrency.auxiliaryThreads,
                                 baseSettings.concurrency.minimumThreads);

    const math::TrendResults parallel =
        math::computeTrends(samples, request, baseSettings.trend);
    const math::TrendResults serial =
        math::computeTrendsSerial(samples, request, baseSettings.trend);

    for (std::size_t i = 0; i < parallel.size(); ++i) {
        const QString id = settings::curveCatalogue()[i].id;
        const bool fitted = parallel.curve(i).has_value();
        recorder.check(fitted, QStringLiteral("curve '%1' produced a fit").arg(id));
        if (!fitted) {
            continue;
        }
        recorder.check(parallel.curve(i)->y == serial.curve(i)->y,
                       QStringLiteral("curve '%1': concurrent and serial fits are identical")
                           .arg(id));

        // A constant slope of -0.1 kg/day must read as -0.7 kg/week. This is
        // the whole point of the secondary axis, so it is checked directly.
        const auto& rate = parallel.derivative(i);
        bool rateCorrect = rate.has_value();
        if (rateCorrect) {
            const std::size_t from = rate->y.size() / 4;
            const std::size_t to = rate->y.size() * 3 / 4;
            for (std::size_t k = from; k < to && rateCorrect; ++k) {
                rateCorrect = std::abs(rate->y[k] + 0.7) < 0.05;
            }
        }
        recorder.check(rateCorrect,
                       QStringLiteral("curve '%1': rate of change reads -0.7 kg/week").arg(id));
    }

    // -- rendering ---------------------------------------------------------
    recorder.section(QStringLiteral("Rendering"));
    settings::Settings renderSettings = baseSettings;
    renderSettings.plot.geometry.yMinimum = 78.0;
    renderSettings.plot.geometry.yMaximum = 92.0;
    if (!family.isEmpty()) {
        renderSettings.plot.typography.fontFamily = family;
    }
    settings::RenderOptions options = renderSettings.render;
    options.resizeToCatalogue();
    for (std::size_t i = 0; i < options.showTrendCurve.size(); ++i) {
        options.showTrendCurve[i] = true;
        options.showDerivative[i] = true;
    }

    plot::PlotInput input;
    input.samples = samples;
    input.trends = &parallel;
    input.settings = &renderSettings;
    input.options = &options;
    input.widthPixels = 1200;
    input.heightPixels = 500;
    input.dotsPerInch = 100;

    const plot::PlotScene scene = plot::PlotSceneBuilder::build(input);
    recorder.check(scene.axes.width() > 0 && scene.axes.height() > 0,
                   QStringLiteral("the axes rectangle is non-degenerate"));
    recorder.check(!scene.labels.empty(),
                   QStringLiteral("axis labels were laid out"));

    const QImage image =
        plot::PainterPlotRenderer::render(scene, renderSettings.plot.typography.fontFamily, 100);
    recorder.check(!image.isNull() && image.width() == 1200,
                   QStringLiteral("a chart image was rendered"));

    // A blank canvas would also be "not null"; require that something was
    // actually drawn on it.
    bool hasInk = false;
    if (!image.isNull()) {
        const QRgb background = renderSettings.plot.palette.background.rgb();
        for (int y = 0; y < image.height() && !hasInk; y += 3) {
            for (int x = 0; x < image.width(); x += 3) {
                if ((image.pixel(x, y) & 0x00FFFFFF) != (background & 0x00FFFFFF)) {
                    hasInk = true;
                    break;
                }
            }
        }
    }
    recorder.check(hasInk, QStringLiteral("the chart is not a blank canvas"));

    const QString chartPath = QDir(sandbox.path()).filePath(QStringLiteral("selftest.png"));
    recorder.check(image.save(chartPath, "PNG") && QFileInfo(chartPath).size() > 1000,
                   QStringLiteral("the chart writes to a PNG file"));

    // -- storage -----------------------------------------------------------
    recorder.section(QStringLiteral("Storage"));
    settings::Settings storageSettings = baseSettings;
    const QString historyPath =
        QDir(sandbox.path()).filePath(storageSettings.csv.defaultFileName);
    data::CsvSampleRepository repository(historyPath, storageSettings);
    recorder.check(bool(repository.ensureExists()),
                   QStringLiteral("a history file can be created"));

    std::vector<data::CsvRecord> records;
    records.push_back(data::CsvRecord::makeValid(0.0, 92.4, QStringLiteral("2026-08-10 08:00")));
    records.push_back(data::CsvRecord::makeValid(1.0, 92.1, QString()));
    recorder.check(bool(repository.save(records)), QStringLiteral("the history writes"));

    const core::Result<data::LoadedHistory> reloaded = repository.load();
    recorder.check(bool(reloaded) && reloaded.value().report.validRecords == 2,
                   QStringLiteral("the history reads back with both records"));

    const QString configPath = QDir(sandbox.path()).filePath(QStringLiteral("config.txt"));
    core::SessionConfigStore store(configPath, storageSettings);
    core::SessionState state = core::SessionState::fromDefaults(storageSettings);
    state.smoothness = 1.11;
    recorder.check(bool(store.saveNow(state)), QStringLiteral("config.txt writes"));
    const core::Result<core::SessionState> back = store.load();
    recorder.check(bool(back) && std::abs(back.value().smoothness - 1.11) < 1e-9,
                   QStringLiteral("config.txt round-trips the slider position"));

    // -- workspace and single instance -------------------------------------
    recorder.section(QStringLiteral("Platform"));
    const platform::Workspace workspace(storageSettings.storage.folderName);
    recorder.check(!workspace.candidates().isEmpty(),
                   QStringLiteral("workspace candidates are enumerated"),
                   workspace.candidates().first());
    recorder.check(!platform::Workspace::platformDocumentsDirectory().isEmpty(),
                   QStringLiteral("the platform Documents directory resolves"));

    platform::SingleInstance guard(QStringLiteral("weight-selftest-%1")
                                       .arg(QCoreApplication::applicationPid()));
    const bool acquired = guard.tryAcquire();
    recorder.check(acquired, QStringLiteral("the single-instance guard can be acquired"));

    recorder.section(QStringLiteral("Summary"));
    recorder.out << "  " << recorder.passed << " passed, " << recorder.failed << " failed"
                 << Qt::endl;
    recorder.out << "  the sandbox at " << sandbox.path() << " is removed on exit" << Qt::endl;
    return recorder.failed == 0 ? 0 : 1;
}

}  // namespace weight::app
