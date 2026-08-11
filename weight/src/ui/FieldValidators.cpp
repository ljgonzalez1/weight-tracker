#include "ui/FieldValidators.hpp"

#include <algorithm>

namespace weight::ui {
namespace {

bool isAllDigits(const QString& text) {
    return !text.isEmpty()
           && std::all_of(text.begin(), text.end(), [](QChar c) {
                  return c >= QLatin1Char('0') && c <= QLatin1Char('9');
              });
}

}  // namespace

IntegerFieldValidator::IntegerFieldValidator(int maximumValue, int maximumLength, QObject* parent)
    : QValidator(parent),
      maximumValue_(maximumValue),
      maximumLength_(maximumLength),
      hasMaximum_(true) {}

IntegerFieldValidator* IntegerFieldValidator::lengthOnly(int maximumLength, QObject* parent) {
    auto* validator = new IntegerFieldValidator(0, maximumLength, parent);
    validator->hasMaximum_ = false;
    return validator;
}

QValidator::State IntegerFieldValidator::validate(QString& input, int& position) const {
    Q_UNUSED(position)
    if (input.isEmpty()) {
        return Acceptable;  // clearing a field must always be allowed
    }
    if (!isAllDigits(input) || input.size() > maximumLength_) {
        return Invalid;
    }
    if (hasMaximum_ && input.toInt() > maximumValue_) {
        return Invalid;
    }
    return Acceptable;
}

DayOfMonthValidator::DayOfMonthValidator(MaximumProvider provider, QObject* parent)
    : QValidator(parent), provider_(std::move(provider)) {}

QValidator::State DayOfMonthValidator::validate(QString& input, int& position) const {
    Q_UNUSED(position)
    if (input.isEmpty()) {
        return Acceptable;
    }
    if (!isAllDigits(input) || input.size() > 2) {
        return Invalid;
    }

    const int value = input.toInt();
    if (value == 0) {
        // A lone "0" is a legitimate prefix of "01"; "00" never becomes a day.
        return input.size() == 1 ? Acceptable : Invalid;
    }
    const int maximum = provider_ ? provider_() : 31;
    return value <= maximum ? Acceptable : Invalid;
}

MassFieldValidator::MassFieldValidator(QObject* parent) : QValidator(parent) {}

bool MassFieldValidator::isPartialDecimal(const QString& text) {
    if (text.isEmpty()) {
        return true;
    }
    bool separatorSeen = false;
    for (const QChar character : text) {
        if (character.isDigit()) {
            continue;
        }
        const bool isSeparator =
            character == QLatin1Char('.') || character == QLatin1Char(',');
        if (isSeparator && !separatorSeen) {
            separatorSeen = true;
            continue;
        }
        return false;
    }
    return true;
}

bool MassFieldValidator::isCompleteDecimal(const QString& text) {
    const qsizetype dots = text.count(QLatin1Char('.'));
    const qsizetype commas = text.count(QLatin1Char(','));
    const qsizetype separators = dots + commas;

    if (separators == 0) {
        return isAllDigits(text);
    }
    if (separators > 1) {
        return false;
    }
    const QChar separator = (dots == 1) ? QLatin1Char('.') : QLatin1Char(',');
    const qsizetype position = text.indexOf(separator);
    return isAllDigits(text.left(position)) && isAllDigits(text.mid(position + 1));
}

QValidator::State MassFieldValidator::validate(QString& input, int& position) const {
    Q_UNUSED(position)
    if (!isPartialDecimal(input)) {
        return Invalid;
    }
    if (isCompleteDecimal(input)) {
        return Acceptable;
    }
    // States such as "", ".", "92." are on the way to a valid value.
    return Intermediate;
}

}  // namespace weight::ui
