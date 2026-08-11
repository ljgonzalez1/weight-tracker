#include <QtTest>

#include "ui/FieldValidators.hpp"

using namespace weight::ui;

/// A validator filters keystrokes, so it has to accept every prefix of a legal
/// value. The tests below are mostly about what must *not* be rejected.
class TestValidators : public QObject {
    Q_OBJECT

private:
    static QValidator::State check(const QValidator& validator, const QString& text) {
        QString copy = text;
        int position = copy.size();
        return validator.validate(copy, position);
    }

private slots:
    void integerFieldAcceptsPrefixes() {
        const IntegerFieldValidator hours(23, 2);
        QCOMPARE(check(hours, QString()), QValidator::Acceptable);   // clearing is allowed
        QCOMPARE(check(hours, QStringLiteral("0")), QValidator::Acceptable);
        QCOMPARE(check(hours, QStringLiteral("2")), QValidator::Acceptable);
        QCOMPARE(check(hours, QStringLiteral("23")), QValidator::Acceptable);
    }

    void integerFieldRejectsOutOfRangeAndNonDigits() {
        const IntegerFieldValidator hours(23, 2);
        QCOMPARE(check(hours, QStringLiteral("24")), QValidator::Invalid);
        QCOMPARE(check(hours, QStringLiteral("123")), QValidator::Invalid);
        QCOMPARE(check(hours, QStringLiteral("1a")), QValidator::Invalid);
        QCOMPARE(check(hours, QStringLiteral("-1")), QValidator::Invalid);
    }

    void dayValidatorFollowsTheSelectedMonth() {
        int lastDay = 31;
        const DayOfMonthValidator day([&lastDay] { return lastDay; });

        QCOMPARE(check(day, QStringLiteral("31")), QValidator::Acceptable);
        lastDay = 28;
        QCOMPARE(check(day, QStringLiteral("31")), QValidator::Invalid);
        QCOMPARE(check(day, QStringLiteral("28")), QValidator::Acceptable);
    }

    void dayValidatorAcceptsALeadingZeroAsAPrefix() {
        const DayOfMonthValidator day([] { return 31; });
        QCOMPARE(check(day, QStringLiteral("0")), QValidator::Acceptable);   // prefix of "05"
        QCOMPARE(check(day, QStringLiteral("00")), QValidator::Invalid);     // never a day
        QCOMPARE(check(day, QStringLiteral("05")), QValidator::Acceptable);
    }

    void massValidatorAcceptsPartialInput() {
        const MassFieldValidator mass;
        // Typing "92.5" passes through "9", "92", "92." on the way.
        QCOMPARE(check(mass, QStringLiteral("9")), QValidator::Acceptable);
        QCOMPARE(check(mass, QStringLiteral("92")), QValidator::Acceptable);
        QCOMPARE(check(mass, QStringLiteral("92.")), QValidator::Intermediate);
        QCOMPARE(check(mass, QStringLiteral("92.5")), QValidator::Acceptable);
        QCOMPARE(check(mass, QStringLiteral("92,5")), QValidator::Acceptable);
    }

    void massValidatorRejectsASecondSeparator() {
        const MassFieldValidator mass;
        QCOMPARE(check(mass, QStringLiteral("92.5.1")), QValidator::Invalid);
        QCOMPARE(check(mass, QStringLiteral("92,5,1")), QValidator::Invalid);
        QCOMPARE(check(mass, QStringLiteral("92.5,1")), QValidator::Invalid);
        QCOMPARE(check(mass, QStringLiteral("9a")), QValidator::Invalid);
    }

    void completenessIsDistinctFromValidity() {
        QVERIFY(MassFieldValidator::isPartialDecimal(QStringLiteral("92.")));
        QVERIFY(!MassFieldValidator::isCompleteDecimal(QStringLiteral("92.")));
        QVERIFY(MassFieldValidator::isCompleteDecimal(QStringLiteral("92.5")));
        QVERIFY(!MassFieldValidator::isCompleteDecimal(QStringLiteral("1.2.3")));
    }
};

QTEST_MAIN(TestValidators)
#include "test_validators.moc"
