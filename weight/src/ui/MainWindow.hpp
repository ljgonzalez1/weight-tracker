#pragma once

#include <memory>
#include <optional>

#include <QWidget>

#include "app/ApplicationController.hpp"
#include "app/RenderEngine.hpp"
#include "ui/FontLibrary.hpp"

class QTextEdit;
class QVBoxLayout;

namespace weight::ui {

class DialogFactory;
class InputPage;
class PreviewPage;

/// The application window: a data-entry page and a chart page.
///
/// The window coordinates; it does not compute. Every operation it performs is
/// a call into ApplicationController, and every layout decision belongs to one
/// of the two pages. What is left here is the part that genuinely concerns the
/// window itself: which page is showing, how large the window must be to fit
/// it, and whether closing right now would lose something.
class MainWindow final : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(app::ApplicationController& controller, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// True when a chart was written to disk during this run.
    [[nodiscard]] bool completedSuccessfully() const noexcept { return saved_; }

protected:
    /// Intercepts the window's close button so that leaving the chart page asks
    /// for confirmation, while leaving the entry page does not: there is
    /// nothing to lose before a chart has been generated.
    void closeEvent(QCloseEvent* event) override;

private:
    enum class Page { Input, Preview };

    void showInputPage();
    void showPreviewPage();

    /// Recomputes the fixed window size from the visible page.
    ///
    /// The window is not user-resizable, but it must resize itself when the
    /// page changes or a new chart arrives. The layout has to be invalidated
    /// and reactivated first, otherwise the size hint is measured before the
    /// new pixmap has propagated upwards and the window ends up a few pixels
    /// too small, clipping its own right and bottom edges.
    void refitToCurrentPage();

    void onContinue();
    void onViewChartOnly();

    /// Asks the controller whether the history holds anything and tells the
    /// input page, which is what enables or disables "View chart only".
    void refreshHistoryAvailability();
    void onPreviewOptionsChanged();
    void onSmoothnessCommitted();
    void onSave();
    void onResetPreview();
    void onCancelFromPreview();

    /// Queues a preview render. Returns immediately: the chart arrives later
    /// through onRenderFinished, so the window never blocks.
    void requestPreview();

    /// Receives a finished chart on the UI thread.
    void onRenderFinished(const app::RenderResult& result);

    /// Writes the current checkbox and slider state to config.txt.
    void persistSessionState();

    void enterPreviewFor(app::PendingSession session);
    void showFatalError(const QString& details);

    app::ApplicationController* controller_;
    FontLibrary fonts_;
    std::unique_ptr<DialogFactory> dialogs_;

    QVBoxLayout* rootLayout_ = nullptr;
    InputPage* inputPage_ = nullptr;
    PreviewPage* previewPage_ = nullptr;
    QTextEdit* diagnosticsBox_ = nullptr;

    std::optional<app::PendingSession> session_;
    Page currentPage_ = Page::Input;

    /// Set once the user has confirmed leaving, so the confirmation dialog is
    /// not shown twice for a single close.
    bool closeConfirmed_ = false;
    bool saved_ = false;
    std::unique_ptr<app::RenderEngine> renderEngine_;
    quint64 nextSequence_ = 1;
    quint64 latestSequence_ = 0;
    bool hasRenderedOnce_ = false;

    /// Set while the user is on the preview page, so a render result that
    /// arrives after they navigated away is discarded rather than shown.
    bool previewLive_ = false;
};

}  // namespace weight::ui
