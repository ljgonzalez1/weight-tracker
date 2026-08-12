#include <QtTest>

#include "settings/Settings.hpp"
#include "settings/Strings.hpp"
#include "ui/FontLibrary.hpp"
#include "ui/InputPage.hpp"

using namespace weight;

/// "View chart only" plots the stored history without recording anything.
/// With an empty history it would produce a chart with no data in it, which
/// looks like a failure rather than like an answer, so the button is disabled
/// instead.
///
/// The rule is easy to state and easy to lose: any code that re-enables the
/// page by looping over its widgets switches the button back on. These tests
/// exist mostly to catch that regression.
class TestInputPage : public QObject {
    Q_OBJECT

private:
    settings::Settings configuration_ = settings::Settings::defaults();

private slots:
    void initTestCase() { settings::Strings::install(settings::Language::English); }

    void theButtonStartsDisabled() {
        // Before anybody has said whether a history exists, "no" is both the
        // safe answer and the true one on a first run.
        const ui::FontLibrary fonts(configuration_.ui);
        ui::InputPage page(configuration_, fonts);

        QVERIFY(!page.historyAvailable());
        QVERIFY(!page.viewChartButtonEnabled());
    }

    void anAvailableHistoryEnablesIt() {
        const ui::FontLibrary fonts(configuration_.ui);
        ui::InputPage page(configuration_, fonts);

        page.setHistoryAvailable(true);
        QVERIFY(page.viewChartButtonEnabled());

        page.setHistoryAvailable(false);
        QVERIFY(!page.viewChartButtonEnabled());
    }

    void enablingThePageDoesNotEnableTheButton() {
        // The regression this whole file is here for.
        const ui::FontLibrary fonts(configuration_.ui);
        ui::InputPage page(configuration_, fonts);

        page.setInputsEnabled(false);
        page.setInputsEnabled(true);
        QVERIFY2(!page.viewChartButtonEnabled(),
                 "re-enabling the page must not enable a button with nothing to show");

        // ...and with a history it must come back, or the button would be
        // permanently dead after the first busy period.
        page.setHistoryAvailable(true);
        page.setInputsEnabled(false);
        QVERIFY(!page.viewChartButtonEnabled());
        page.setInputsEnabled(true);
        QVERIFY(page.viewChartButtonEnabled());
    }

    void theTooltipExplainsWhichStateItIsIn() {
        const ui::FontLibrary fonts(configuration_.ui);
        ui::InputPage page(configuration_, fonts);

        const QString whenEmpty =
            settings::Strings::get(QStringLiteral("button.view.chart.disabled"));
        const QString whenAvailable =
            settings::Strings::get(QStringLiteral("button.view.chart.tip"));
        QVERIFY(whenEmpty != whenAvailable);

        // A disabled control with no explanation is the thing being avoided.
        QVERIFY(!whenEmpty.isEmpty());
    }
};

QTEST_MAIN(TestInputPage)
#include "test_input_page.moc"
