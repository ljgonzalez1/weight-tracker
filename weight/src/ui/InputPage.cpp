#include "ui/InputPage.hpp"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include "core/Logging.hpp"
#include "data/CalendarUtils.hpp"
#include "data/NumberParser.hpp"
#include "ui/FieldValidators.hpp"
#include "settings/Strings.hpp"
#include "ui/FontLibrary.hpp"

namespace weight::ui {

using settings::Strings;

InputPage::InputPage(const settings::Settings& configuration, const FontLibrary& fonts,
                     QWidget* parent)
    : QWidget(parent), configuration_(&configuration) {
    buildLayout(fonts);
    connectSignals();
    resetToNow();
    refreshWeekday();
}

void InputPage::buildLayout(const FontLibrary& fonts) {
    const settings::UiTheme& theme = configuration_->ui;
    const settings::InputLimits& limits = configuration_->limits;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme.mainPaddingHorizontal, theme.mainPaddingVertical,
                              theme.mainPaddingHorizontal, theme.mainPaddingVertical);
    outer->setSpacing(theme.paddingVertical);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(theme.paddingHorizontal);
    grid->setVerticalSpacing(theme.paddingVertical);
    outer->addLayout(grid);

    // Row 0: section captions, spanning the columns they describe.
    auto* timeCaption = new QLabel(Strings::get(QStringLiteral("page.input.time")), this);
    timeCaption->setFont(fonts.label());
    grid->addWidget(timeCaption, 0, 0, 1, 3);

    auto* dateCaption = new QLabel(Strings::get(QStringLiteral("page.input.date")), this);
    dateCaption->setFont(fonts.label());
    dateCaption->setAlignment(Qt::AlignCenter);
    grid->addWidget(dateCaption, 0, 3, 1, 5);

    auto* massCaption = new QLabel(Strings::get(QStringLiteral("page.input.mass")), this);
    massCaption->setFont(fonts.label());
    massCaption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(massCaption, 0, 8, 1, 2);

    // Row 1: hour : minute | weekday day of month of year | mass kg
    hourField_ = new QLineEdit(this);
    hourField_->setFont(fonts.largeEntry());
    hourField_->setAlignment(Qt::AlignCenter);
    hourField_->setMaxLength(2);
    hourField_->setValidator(new IntegerFieldValidator(limits.maximumHour, 2, hourField_));
    hourField_->setFixedWidth(
        fonts.widthForCharacters(fonts.largeEntry(), theme.timeFieldCharacters));
    grid->addWidget(hourField_, 1, 0);

    auto* colon = new QLabel(Strings::get(QStringLiteral("page.input.colon")), this);
    colon->setFont(fonts.largeEntry());
    colon->setAlignment(Qt::AlignCenter);
    grid->addWidget(colon, 1, 1);

    minuteField_ = new QLineEdit(this);
    minuteField_->setFont(fonts.largeEntry());
    minuteField_->setAlignment(Qt::AlignCenter);
    minuteField_->setMaxLength(2);
    minuteField_->setValidator(new IntegerFieldValidator(limits.maximumMinute, 2, minuteField_));
    minuteField_->setFixedWidth(
        fonts.widthForCharacters(fonts.largeEntry(), theme.timeFieldCharacters));
    grid->addWidget(minuteField_, 1, 2);

    weekdayLabel_ = new QLabel(QString(), this);
    weekdayLabel_->setFont(fonts.secondary());
    weekdayLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(weekdayLabel_, 1, 3);

    dayField_ = new QLineEdit(this);
    dayField_->setFont(fonts.largeEntry());
    dayField_->setAlignment(Qt::AlignCenter);
    dayField_->setMaxLength(2);
    dayField_->setValidator(
        new DayOfMonthValidator([this] { return daysInSelectedMonth(); }, dayField_));
    dayField_->setFixedWidth(
        fonts.widthForCharacters(fonts.largeEntry(), theme.dayFieldCharacters));
    grid->addWidget(dayField_, 1, 4);

    auto* firstOf = new QLabel(Strings::get(QStringLiteral("page.input.of")), this);
    firstOf->setFont(fonts.entry());
    firstOf->setAlignment(Qt::AlignCenter);
    grid->addWidget(firstOf, 1, 5);

    monthBox_ = new QComboBox(this);
    monthBox_->setFont(fonts.entry());
    QStringList monthNames;
    for (int month = 1; month <= 12; ++month) {
        monthNames << Strings::get(QStringLiteral("month.%1").arg(month));
    }
    monthBox_->addItems(monthNames);
    monthBox_->setEditable(false);
    monthBox_->setMinimumWidth(
        fonts.widthForCharacters(fonts.entry(), theme.monthComboCharacters));
    grid->addWidget(monthBox_, 1, 6);

    auto* secondOf = new QLabel(Strings::get(QStringLiteral("page.input.of")), this);
    secondOf->setFont(fonts.entry());
    secondOf->setAlignment(Qt::AlignCenter);
    grid->addWidget(secondOf, 1, 7);

    yearField_ = new QLineEdit(this);
    yearField_->setFont(fonts.largeEntry());
    yearField_->setAlignment(Qt::AlignCenter);
    yearField_->setMaxLength(4);
    yearField_->setValidator(IntegerFieldValidator::lengthOnly(4, yearField_));
    yearField_->setFixedWidth(
        fonts.widthForCharacters(fonts.largeEntry(), theme.yearFieldCharacters));
    grid->addWidget(yearField_, 1, 8);

    massField_ = new QLineEdit(this);
    massField_->setFont(fonts.largeEntry());
    massField_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    massField_->setValidator(new MassFieldValidator(massField_));
    massField_->setFixedWidth(
        fonts.widthForCharacters(fonts.largeEntry(), theme.massFieldCharacters));
    grid->addWidget(massField_, 1, 9);

    auto* unit = new QLabel(Strings::get(QStringLiteral("page.input.kilograms")), this);
    unit->setFont(fonts.entry());
    grid->addWidget(unit, 1, 10);

    // Row 2: inline error message.
    errorLabel_ = new QLabel(QString(), this);
    errorLabel_->setFont(fonts.error());
    errorLabel_->setStyleSheet(
        QStringLiteral("color: %1;").arg(configuration_->ui.errorTextColor.name()));
    errorLabel_->setAlignment(Qt::AlignCenter);
    outer->addWidget(errorLabel_);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    outer->addWidget(separator);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(theme.buttonSpacing);
    outer->addLayout(buttons);

    const auto addButton = [&](const QString& caption, int characters, bool primary) {
        auto* button = new QPushButton(caption, this);
        button->setFont(fonts.button());
        button->setMinimumWidth(fonts.widthForCharacters(fonts.button(), characters));
        button->setDefault(primary);
        button->setAutoDefault(primary);
        buttons->addWidget(button);
        return button;
    };

    cancelButton_ = addButton(Strings::get(QStringLiteral("button.cancel")), theme.smallButtonCharacters, false);
    resetButton_ = addButton(Strings::get(QStringLiteral("button.reset.datetime")), theme.smallButtonCharacters + 2, false);
    continueButton_ = addButton(Strings::get(QStringLiteral("button.continue")), theme.largeButtonCharacters, true);
    viewChartButton_ = addButton(Strings::get(QStringLiteral("button.view.chart")), theme.largeButtonCharacters, false);
}

void InputPage::connectSignals() {
    connect(dayField_, &QLineEdit::textChanged, this, &InputPage::refreshWeekday);
    connect(monthBox_, &QComboBox::currentIndexChanged, this, [this] { clampDayToMonth(); });
    connect(yearField_, &QLineEdit::textChanged, this, [this] { clampDayToMonth(); });

    connect(cancelButton_, &QPushButton::clicked, this, &InputPage::cancelRequested);
    connect(resetButton_, &QPushButton::clicked, this, &InputPage::resetToNow);
    connect(continueButton_, &QPushButton::clicked, this, &InputPage::continueRequested);
    connect(viewChartButton_, &QPushButton::clicked, this, &InputPage::viewChartOnlyRequested);

    // Enter confirms from anywhere on the page, matching the habit of typing a
    // mass and pressing return.
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* shortcut = new QShortcut(QKeySequence(key), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, &InputPage::continueRequested);
    }
}

void InputPage::resetToNow() {
    const QDateTime now = QDateTime::currentDateTime();
    const QSignalBlocker blockDay(dayField_);
    const QSignalBlocker blockMonth(monthBox_);
    const QSignalBlocker blockYear(yearField_);

    hourField_->setText(QStringLiteral("%1").arg(now.time().hour(), 2, 10, QLatin1Char('0')));
    minuteField_->setText(QStringLiteral("%1").arg(now.time().minute(), 2, 10, QLatin1Char('0')));
    dayField_->setText(QString::number(now.date().day()));
    monthBox_->setCurrentIndex(now.date().month() - 1);
    yearField_->setText(QString::number(now.date().year()));
    clearError();
    refreshWeekday();

    core::log::info(settings::Strings::get(
        QStringLiteral("log.datetime.reset"),
        now.toString(configuration_->csv.timestampFormat)));
}

int InputPage::selectedMonth() const {
    const int index = monthBox_->currentIndex();
    return (index >= 0 && index < 12) ? index + 1 : 0;
}

int InputPage::selectedYear() const {
    bool ok = false;
    const int year = yearField_->text().toInt(&ok);
    if (!ok || year < configuration_->limits.minimumYear
        || year > configuration_->limits.maximumYear) {
        return 0;
    }
    return year;
}

int InputPage::daysInSelectedMonth() const {
    const int month = selectedMonth();
    const int year = selectedYear();
    if (month == 0 || year == 0) {
        return configuration_->limits.maximumDayOfMonth;
    }
    return data::CalendarUtils::daysInMonth(month, year, configuration_->limits);
}

void InputPage::clampDayToMonth() {
    const int maximum = daysInSelectedMonth();
    bool ok = false;
    const int day = dayField_->text().toInt(&ok);
    if (ok && day > maximum) {
        dayField_->setText(QString::number(maximum));
        core::log::info(
            QStringLiteral("Day shortened to %1, the last day of the selected month.")
                .arg(maximum));
    }
    refreshWeekday();
}

void InputPage::refreshWeekday() {
    bool ok = false;
    const int day = dayField_->text().toInt(&ok);
    const int month = selectedMonth();
    const int year = selectedYear();

    if (ok && day > 0 && month > 0 && year > 0) {
        const QDate date(year, month, day);
        weekdayLabel_->setText(data::CalendarUtils::weekdayName(date, configuration_->strings));
        return;
    }
    weekdayLabel_->clear();
}

std::optional<int> InputPage::readIntegerField(const QLineEdit* field, int minimum, int maximum,
                                               const QString& errorMessage) {
    bool ok = false;
    const int value = field->text().toInt(&ok);
    if (ok && value >= minimum && value <= maximum) {
        return value;
    }
    showError(errorMessage);
    return std::nullopt;
}

std::optional<std::pair<QDateTime, double>> InputPage::readMeasurement() {
    const settings::InputLimits& limits = configuration_->limits;
    clearError();

    const auto hour = readIntegerField(hourField_, limits.minimumHour, limits.maximumHour,
                                       Strings::get(QStringLiteral("error.hour")));
    if (!hour) {
        return std::nullopt;
    }
    const auto minute = readIntegerField(minuteField_, limits.minimumMinute, limits.maximumMinute,
                                         Strings::get(QStringLiteral("error.minute")));
    if (!minute) {
        return std::nullopt;
    }
    const auto year = readIntegerField(yearField_, limits.minimumYear, limits.maximumYear,
                                       Strings::get(QStringLiteral("error.year")));
    if (!year) {
        return std::nullopt;
    }

    const int month = selectedMonth();
    if (month < 1 || month > 12) {
        showError(Strings::get(QStringLiteral("error.month")));
        return std::nullopt;
    }

    const int lastDay = data::CalendarUtils::daysInMonth(month, *year, limits);
    const auto day =
        readIntegerField(dayField_, limits.minimumDayOfMonth, lastDay, Strings::get(QStringLiteral("error.day")));
    if (!day) {
        return std::nullopt;
    }

    const QDateTime moment(QDate(*year, month, *day), QTime(*hour, *minute, 0));
    if (!moment.isValid()) {
        showError(Strings::get(QStringLiteral("error.timestamp")));
        return std::nullopt;
    }

    const QString rawMass = massField_->text().trimmed();
    if (rawMass.isEmpty()) {
        showError(Strings::get(QStringLiteral("error.mass.empty")));
        return std::nullopt;
    }
    const std::optional<double> mass =
        data::NumberParser::parseMass(rawMass, limits.minimumMass, limits.maximumMass);
    if (!mass) {
        showError(Strings::get(QStringLiteral("error.mass.invalid")));
        return std::nullopt;
    }

    return std::make_pair(moment, *mass);
}

void InputPage::clearError() { errorLabel_->clear(); }

void InputPage::showError(const QString& message) { errorLabel_->setText(message); }

QList<QWidget*> InputPage::interactiveWidgets() const {
    return {hourField_, minuteField_, dayField_,       monthBox_,
            yearField_, massField_,   resetButton_,    continueButton_,
            viewChartButton_};
}

void InputPage::focusPrimaryField() { massField_->setFocus(Qt::OtherFocusReason); }

}  // namespace weight::ui
