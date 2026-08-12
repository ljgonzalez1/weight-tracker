#include "ui/MainWindow.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QFontMetrics>
#include <QPixmap>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include "core/Logging.hpp"
#include "ui/DialogFactory.hpp"
#include "ui/InputPage.hpp"
#include "ui/PreviewPage.hpp"
#include "settings/Strings.hpp"

namespace weight::ui {

using settings::Strings;

MainWindow::MainWindow(app::ApplicationController& controller, QWidget* parent)
    : QWidget(parent),
      controller_(&controller),
      fonts_(controller.settings().ui) {
    const settings::Settings& appSettings = controller_->settings();
    dialogs_ = std::make_unique<DialogFactory>(appSettings, this);

    setWindowTitle(Strings::get(QStringLiteral("app.window.title")));
    // Fixed size, with a close button but no maximise: the layout is sized to
    // its content and has nothing useful to do with extra space.
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowTitleHint
                   | Qt::CustomizeWindowHint | Qt::WindowSystemMenuHint);

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(0, 0, 0, 0);
    rootLayout_->setSpacing(0);

    inputPage_ = new InputPage(appSettings, fonts_, this);
    previewPage_ = new PreviewPage(appSettings, fonts_, this);
    rootLayout_->addWidget(inputPage_);
    rootLayout_->addWidget(previewPage_);

    // The render engine must exist before any page can ask for a chart.
    //
    // This is where release 0.48.0 crashed: the member was declared and never
    // constructed, so the first click on "Continue" or "View chart only"
    // dereferenced a null unique_ptr inside requestPreview(). The engine is
    // deliberately *not* given `this` as its QObject parent — it is owned by
    // the unique_ptr, and handing the same object to two owners is how
    // double-frees are written by accident.
    renderEngine_ = std::make_unique<app::RenderEngine>(controller_->settings());
    connect(renderEngine_.get(), &app::RenderEngine::finished, this,
            &MainWindow::onRenderFinished);

    connect(inputPage_, &InputPage::continueRequested, this, &MainWindow::onContinue);
    connect(inputPage_, &InputPage::viewChartOnlyRequested, this, &MainWindow::onViewChartOnly);
    connect(inputPage_, &InputPage::cancelRequested, this, [this] {
        closeConfirmed_ = true;
        core::log::info(Strings::get(QStringLiteral("log.cancelled")));
        close();
    });

    connect(previewPage_, &PreviewPage::optionsChanged, this,
            &MainWindow::onPreviewOptionsChanged);
    connect(previewPage_, &PreviewPage::smoothnessCommitted, this,
            &MainWindow::onSmoothnessCommitted);
    connect(previewPage_, &PreviewPage::saveRequested, this, &MainWindow::onSave);
    connect(previewPage_, &PreviewPage::backRequested, this, &MainWindow::showInputPage);
    connect(previewPage_, &PreviewPage::resetRequested, this, &MainWindow::onResetPreview);
    connect(previewPage_, &PreviewPage::cancelRequested, this,
            &MainWindow::onCancelFromPreview);

    showInputPage();
    refitToCurrentPage();
    QTimer::singleShot(0, this, [this] { DialogFactory::centreOnPrimaryScreen(this); });
}

MainWindow::~MainWindow() {
    // Stop the worker before any member is torn down. A render in flight holds
    // a pointer to the settings owned by the controller and emits into this
    // object; joining the thread here means no result can arrive against a
    // half-destroyed window.
    previewLive_ = false;
    if (renderEngine_) {
        renderEngine_->shutdown();
    }
}

void MainWindow::showInputPage() {
    previewLive_ = false;
    previewPage_->hide();
    inputPage_->show();
    currentPage_ = Page::Input;
    // Asked again on every return to this page rather than once at startup:
    // the history changes underneath the window when a measurement is saved,
    // and it can change outside the window entirely, because the file is a
    // plain CSV the person is invited to edit.
    refreshHistoryAvailability();
    inputPage_->setInputsEnabled(true);
    refitToCurrentPage();
    inputPage_->focusPrimaryField();
}

void MainWindow::showPreviewPage() {
    inputPage_->hide();
    previewPage_->show();
    currentPage_ = Page::Preview;
    previewPage_->setControlsEnabled(true);
    refitToCurrentPage();
    previewPage_->focusPrimaryButton();
}

void MainWindow::refitToCurrentPage() {
    const int safety = controller_->settings().ui.windowSafetyPadding;

    // Release the previous lock so the layout is free to shrink as well as grow.
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    if (QLayout* layout = this->layout(); layout != nullptr) {
        layout->invalidate();
        layout->activate();
    }
    adjustSize();

    // Take the larger of what adjustSize produced and the freshly computed
    // hint, plus a small margin: rounding when the chart pixmap is scaled can
    // otherwise leave the last pixel column outside the window.
    const QSize hint = sizeHint();
    const QSize current = size();
    setFixedSize(std::max(current.width(), hint.width()) + safety,
                 std::max(current.height(), hint.height()) + safety);
    DialogFactory::centreOnPrimaryScreen(this);
}

void MainWindow::enterPreviewFor(app::PendingSession session) {
    session_ = std::move(session);
    hasRenderedOnce_ = false;
    // Start from what the person last had switched on, not from the shipped
    // defaults: config.txt is their state, and ignoring it on every launch
    // would make the persistence pointless.
    settings::RenderOptions restored = controller_->settings().render;
    restored.resizeToCatalogue();
    controller_->sessionState().applyTo(restored);
    previewPage_->applyState(restored, controller_->sessionState().smoothness);

    previewLive_ = true;
    showPreviewPage();
    requestPreview();
}

void MainWindow::onContinue() {
    inputPage_->clearError();
    inputPage_->setInputsEnabled(false);

    const auto measurement = inputPage_->readMeasurement();
    if (!measurement.has_value()) {
        inputPage_->setInputsEnabled(true);
        return;  // the page has already explained what is wrong
    }

    core::Result<app::PendingSession> session =
        controller_->buildSessionWithMeasurement(measurement->first, measurement->second);
    if (!session) {
        inputPage_->setInputsEnabled(true);
        inputPage_->showError(session.error().toString());
        core::log::error(session.error().toString());
        return;
    }
    enterPreviewFor(std::move(session.value()));
}

void MainWindow::refreshHistoryAvailability() {
    inputPage_->setHistoryAvailable(controller_->hasStoredMeasurements());
}

void MainWindow::onViewChartOnly() {
    inputPage_->clearError();
    // The button is disabled without stored measurements, so this is not the
    // path an ordinary click takes. It is still checked, because a disabled
    // button is a courtesy and not a guarantee: the signal can also arrive
    // from a keyboard shortcut, a style that ignores the disabled state, or a
    // history that was emptied by hand since the page was shown.
    if (!controller_->hasStoredMeasurements()) {
        inputPage_->setHistoryAvailable(false);
        inputPage_->showError(Strings::get(QStringLiteral("error.no.history")));
        return;
    }
    core::Result<app::PendingSession> session = controller_->buildSessionForViewing();
    if (!session) {
        inputPage_->showError(session.error().toString());
        core::log::error(session.error().toString());
        return;
    }
    enterPreviewFor(std::move(session.value()));
}

void MainWindow::requestPreview() {
    if (!session_.has_value()) {
        inputPage_->showError(Strings::get(QStringLiteral("error.no.preview")));
        return;
    }
    if (!renderEngine_) {
        // Cannot happen once the constructor has run, but a preview request is
        // reachable from six different signals and a null engine used to be a
        // segmentation fault rather than a message.
        showFatalError(Strings::get(QStringLiteral("error.engine.missing")));
        return;
    }

    const settings::PlotGeometry& geometry = controller_->settings().plot.geometry;
    const int width = std::max(1, geometry.previewWidthPixels);
    const double ratio = static_cast<double>(geometry.heightPixels)
                         / static_cast<double>(std::max(1, geometry.widthPixels));

    app::RenderRequest request;
    request.samples = session_->samples;
    request.options = previewPage_->renderOptions();
    request.smoothness = previewPage_->smoothness();
    request.widthPixels = width;
    request.heightPixels = std::max(1, static_cast<int>(std::lround(width * ratio)));
    request.dotsPerInch = std::max(1, geometry.previewDotsPerInch);
    request.sequence = nextSequence_++;

    latestSequence_ = request.sequence;
    previewPage_->setBusy(true);
    renderEngine_->submit(std::move(request));
}

void MainWindow::onRenderFinished(const app::RenderResult& result) {
    // Two reasons to drop a result: it belongs to a request the user has
    // already superseded, or they have left the preview page entirely.
    if (!previewLive_ || result.sequence != latestSequence_) {
        return;
    }
    previewPage_->setBusy(false);

    if (!result.succeeded) {
        showFatalError(result.failureReason);
        return;
    }

    previewPage_->showChart(QPixmap::fromImage(result.image));
    previewPage_->updateCoefficients(result.coefficients);
    hasRenderedOnce_ = true;
    refitToCurrentPage();
}

void MainWindow::persistSessionState() {
    controller_->persistSession(core::SessionState::capture(previewPage_->renderOptions(),
                                                             previewPage_->smoothness()));
}

void MainWindow::onPreviewOptionsChanged() {
    if (!session_.has_value()) {
        return;
    }
    // config.txt is rewritten the moment anything changes, on a worker thread,
    // so the click stays instant.
    persistSessionState();
    requestPreview();
}

void MainWindow::onSmoothnessCommitted() {
    if (!session_.has_value() || !previewPage_->anyTrendActive()) {
        return;
    }
    persistSessionState();
    requestPreview();
}

void MainWindow::onResetPreview() {
    previewPage_->restoreDefaults();
    hasRenderedOnce_ = false;
    persistSessionState();
    requestPreview();
}

void MainWindow::onSave() {
    if (!session_.has_value()) {
        inputPage_->showError(Strings::get(QStringLiteral("error.no.preview")));
        showInputPage();
        return;
    }

    previewPage_->setControlsEnabled(false);
    QApplication::processEvents();

    const settings::RenderOptions options = previewPage_->renderOptions();
    const double smoothness = previewPage_->smoothness();

    QString destination;
    if (session_->writeHistory) {
        const QDateTime moment =
            session_->measurementMoment.value_or(QDateTime::currentDateTime());
        destination = controller_->defaultImagePath(smoothness, moment);
    } else {
        // Nothing is being recorded, so the user picks where the image goes.
        // Nothing is being recorded, so the person picks where the image goes.
        // The workspace images folder is offered as the starting point.
        destination = dialogs_->askImageDestination(
            controller_->imagesDirectory(),
            controller_->suggestedImageName(smoothness, QDateTime::currentDateTime()));
        if (destination.isEmpty()) {
            core::log::info(Strings::get(QStringLiteral("log.image.cancelled")));
            previewPage_->setControlsEnabled(true);
            return;
        }
    }

    // Rendered synchronously and at full resolution: the user is already
    // waiting, and this result must not be superseded by a stray slider event.
    app::RenderRequest request;
    request.samples = session_->samples;
    request.options = options;
    request.smoothness = smoothness;
    request.widthPixels = controller_->settings().plot.geometry.widthPixels;
    request.heightPixels = controller_->settings().plot.geometry.heightPixels;
    request.dotsPerInch = controller_->settings().plot.geometry.dotsPerInch;

    const app::RenderResult chart =
        app::RenderEngine::renderNow(request, controller_->settings());
    if (!chart.succeeded) {
        previewPage_->setControlsEnabled(true);
        showFatalError(Strings::get(QStringLiteral("error.render.full")));
        return;
    }

    const core::Status status = controller_->commit(*session_, chart.image, destination);
    if (!status) {
        previewPage_->setControlsEnabled(true);
        core::log::error(status.error().toString());
        dialogs_->reportError(Strings::get(QStringLiteral("error.save.title")),
                              status.error().toString());
        return;
    }

    // Persist the state that produced the saved chart before leaving.
    persistSessionState();
    controller_->flushSession();
    saved_ = true;
    closeConfirmed_ = true;
    close();
}

void MainWindow::onCancelFromPreview() {
    // A chart exists in memory at this point, so closing silently would throw
    // away work the user can see. Ask first.
    if (!dialogs_->confirmDiscard()) {
        return;
    }
    closeConfirmed_ = true;
    core::log::info(Strings::get(QStringLiteral("log.cancelled")));
    close();
}

void MainWindow::showFatalError(const QString& details) {
    const settings::Settings& appSettings = controller_->settings();
    core::log::error(details);

    inputPage_->showError(Strings::get(QStringLiteral("error.fatal")));
    inputPage_->setInputsEnabled(false);
    previewPage_->setControlsEnabled(false);

    if (diagnosticsBox_ == nullptr) {
        diagnosticsBox_ = new QTextEdit(this);
        diagnosticsBox_->setReadOnly(true);
        diagnosticsBox_->setFont(fonts_.monospace());
        diagnosticsBox_->setStyleSheet(
            QStringLiteral("background-color: %1; color: %2;")
                .arg(appSettings.ui.fatalBackgroundColor.name(),
                     appSettings.ui.fatalTextColor.name()));
        const QFontMetrics metrics(fonts_.monospace());
        diagnosticsBox_->setFixedHeight(metrics.lineSpacing()
                                            * appSettings.ui.tracebackVisibleLines
                                        + 12);
        rootLayout_->addWidget(diagnosticsBox_);
    }
    diagnosticsBox_->setPlainText(details);
    refitToCurrentPage();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (closeConfirmed_ || currentPage_ != Page::Preview) {
        event->accept();
        return;
    }
    if (dialogs_->confirmDiscard()) {
        closeConfirmed_ = true;
        core::log::info(Strings::get(QStringLiteral("log.cancelled")));
        event->accept();
    } else {
        event->ignore();
    }
}

}  // namespace weight::ui
