#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QIcon>
#include <QTextStream>

#include <memory>

#include "app/ApplicationController.hpp"
#include "app/CommandLine.hpp"
#include "app/SelfTest.hpp"
#include "core/Concurrency.hpp"
#include "core/Logging.hpp"
#include "core/ProjectVersion.hpp"
#include "platform/SingleInstance.hpp"
#include "platform/Workspace.hpp"
#include "settings/CurveCatalog.hpp"
#include "settings/Settings.hpp"
#include "settings/Strings.hpp"
#include "ui/DialogFactory.hpp"
#include "ui/MainWindow.hpp"

using namespace weight;

namespace {

enum ExitStatus : int {
    kSuccess = 0,
    kUsageError = 2,
    kStorageError = 3,
    kCancelled = 4,
    kAlreadyRunning = 5,
};

/// Loads the bundled Open Runde faces and makes the first one the application
/// font.
///
/// The font is compiled into the executable, so it applies whether or not the
/// machine has it installed, and a static build carries it too. Loading is
/// best-effort: if a face fails, Qt substitutes and the program still runs, it
/// just looks like the system default.
///
/// Returns the resolved family name, or an empty string when nothing loaded.
QString installBundledFont() {
    static const char* kFaces[] = {
        ":/fonts/OpenRunde-Regular.otf",
        ":/fonts/OpenRunde-Medium.otf",
        ":/fonts/OpenRunde-Semibold.otf",
        ":/fonts/OpenRunde-Bold.otf",
    };

    QString family;
    int loaded = 0;
    for (const char* face : kFaces) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(face));
        if (id < 0) {
            core::log::warning(
                QStringLiteral("Could not load the bundled font %1.").arg(QLatin1String(face)));
            continue;
        }
        ++loaded;
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (family.isEmpty() && !families.isEmpty()) {
            family = families.first();
        }
    }

    if (family.isEmpty()) {
        core::log::warning(
            QStringLiteral("No bundled font could be loaded; falling back to the system font."));
        return {};
    }
    core::log::info(QStringLiteral("Font: %1 (%2 face(s) embedded).").arg(family).arg(loaded));
    return family;
}

/// Sets the window icon.
///
/// Two things are needed and only one of them is obvious.
///
///   1. The icon has to actually be in the binary. Compiling the .qrc into a
///      static library does not achieve that: the generated object contains
///      only a static initialiser that nothing references, so the linker drops
///      it and the resource silently does not exist at run time. The .qrc is
///      therefore compiled into the *executable* target, and Q_INIT_RESOURCE
///      below forces the initialiser to be kept even so.
///
///   2. On Wayland the shell does not use the window icon at all. It matches
///      the surface to a desktop entry by name and takes the icon from there,
///      which is why setWindowIcon alone leaves the generic placeholder. The
///      desktop file name must be set, and a matching .desktop file installed,
///      for the taskbar icon to appear.
void installApplicationIcon() {
    const QIcon icon(QStringLiteral(":/icons/program-icon-512.png"));
    if (icon.availableSizes().isEmpty()) {
        core::log::warning(
            QStringLiteral("The application icon resource is missing; the desktop default will "
                           "be shown."));
    } else {
        QApplication::setWindowIcon(icon);
    }

    // Wayland resolves the taskbar icon through this name, not through the
    // window icon above.
    QGuiApplication::setDesktopFileName(QString::fromLatin1(core::version::kExecutableName));
}

}  // namespace

/// Composition root: the only place that knows about all the parts at once.
int main(int argc, char* argv[]) {
    // Forces the resource initialiser to be linked in and run. This must sit
    // in a function with external linkage: Q_INIT_RESOURCE expands to an
    // `extern` declaration, and inside an anonymous namespace that declaration
    // would acquire internal linkage and never resolve to the generated
    // symbol. The argument is the .qrc base name.
    Q_INIT_RESOURCE(weight);

    QApplication application(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(core::version::kDisplayName));
    QApplication::setApplicationVersion(QString::fromLatin1(core::version::kVersionString));
    QApplication::setOrganizationDomain(QString::fromLatin1(core::version::kOrganisationDomain));

    // 1. Language first: every message from here on is translated.
    settings::Strings::detectAndInstall();

    settings::Settings appSettings = settings::Settings::defaults();

    const app::CommandLineResult parsed =
        app::CommandLine::parse(application.arguments(), appSettings);
    switch (parsed.action) {
        case app::CommandLineResult::Action::ShowHelp:
        case app::CommandLineResult::Action::ShowVersion:
            return kSuccess;
        case app::CommandLineResult::Action::SelfTest:
            return app::runSelfTest(appSettings);
        case app::CommandLineResult::Action::Error:
            QTextStream(stderr) << parsed.errorMessage << Qt::endl;
            return kUsageError;
        case app::CommandLineResult::Action::Run:
            break;
    }

    core::log::info(settings::Strings::get(QStringLiteral("log.starting"),
                                           QString::fromLatin1(core::version::kDisplayName),
                                           QString::fromLatin1(core::version::kVersionString)));
    core::log::info(settings::Strings::get(QStringLiteral("log.language"),
                                           settings::Strings::detectedTag(),
                                           settings::Strings::detectionSource()));

    // 2. Refuse to start twice. Done before any file is touched, so a second
    //    copy cannot race the first one over the history.
    platform::SingleInstance instance(
        QStringLiteral("%1-single-instance").arg(QLatin1String(core::version::kExecutableName)));
    if (!instance.tryAcquire()) {
        ui::DialogFactory::reportAlreadyRunning(instance.runningProcessId());
        return kAlreadyRunning;
    }

    // 3. Appearance: the bundled font overrides whatever the system offers, and
    //    the icon has to be forced into the binary.
    if (const QString family = installBundledFont(); !family.isEmpty()) {
        appSettings.plot.typography.fontFamily = family;
        appSettings.ui.labelFont.family = family;
        appSettings.ui.entryFont.family = family;
        appSettings.ui.largeEntryFont.family = family;
        appSettings.ui.buttonFont.family = family;
        appSettings.ui.errorFont.family = family;
        appSettings.ui.secondaryFont.family = family;
        QApplication::setFont(QFont(family));
    }
    installApplicationIcon();

    // 4. Thread policy, sized from the number of curves rather than from cores.
    core::Concurrency::configure(static_cast<int>(settings::curveCount()),
                                 appSettings.concurrency.auxiliaryThreads,
                                 appSettings.concurrency.minimumThreads);
    core::log::info(core::Concurrency::describe());

    for (const QString& note : appSettings.sanitise()) {
        core::log::warning(note);
    }

    // 5. Where the data lives: found if it exists, created if it does not.
    QString workspace = parsed.workspaceOverride;
    if (workspace.isEmpty()) {
        const platform::Workspace locator(appSettings.storage.folderName);
        const core::Result<QString> resolved = locator.resolve();
        if (!resolved) {
            core::log::error(resolved.error().toString());
            return kStorageError;
        }
        workspace = resolved.value();
    } else if (!QDir().mkpath(workspace)) {
        core::log::error(QStringLiteral("%1 could not be created.").arg(workspace));
        return kStorageError;
    }
    core::log::info(settings::Strings::get(QStringLiteral("log.workspace"), workspace));

    // 6. Wire it together and open the window.
    app::ApplicationController controller(std::move(appSettings), workspace);
    if (const core::Status status = controller.prepareStorage(); !status) {
        core::log::error(status.error().toString());
        return kStorageError;
    }

    ui::MainWindow window(controller);
    window.show();

    const int code = application.exec();
    controller.flushSession();
    if (code != 0) {
        return code;
    }
    return window.completedSuccessfully() ? kSuccess : kCancelled;
}
