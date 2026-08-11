#pragma once

#include <functional>

#include <QValidator>

namespace weight::ui {

/// Accepts a non-negative integer of bounded length and value.
///
/// Validators filter keystrokes, so they must accept every prefix of a legal
/// value: refusing "1" because it is below the minimum would make "15"
/// impossible to type. Range checking against the minimum therefore happens on
/// confirmation, not here.
class IntegerFieldValidator final : public QValidator {
    Q_OBJECT

public:
    IntegerFieldValidator(int maximumValue, int maximumLength, QObject* parent = nullptr);

    /// Constructs a validator with no upper bound on the value, only on length.
    static IntegerFieldValidator* lengthOnly(int maximumLength, QObject* parent = nullptr);

    [[nodiscard]] State validate(QString& input, int& position) const override;

private:
    int maximumValue_;
    int maximumLength_;
    bool hasMaximum_;
};

/// Day of the month, bounded by whatever month and year are currently selected.
///
/// The bound is supplied by a callback rather than a fixed number so that
/// switching from March to February immediately rejects the 30th, without the
/// validator having to be rebuilt.
class DayOfMonthValidator final : public QValidator {
    Q_OBJECT

public:
    using MaximumProvider = std::function<int()>;

    explicit DayOfMonthValidator(MaximumProvider provider, QObject* parent = nullptr);

    [[nodiscard]] State validate(QString& input, int& position) const override;

private:
    MaximumProvider provider_;
};

/// A decimal mass, accepting either a point or a comma but only one of them.
///
/// Partially typed values such as "92," are reported as Intermediate: they
/// cannot be confirmed but can still become valid, so the keystroke is allowed
/// through. The plausible-range check belongs to confirmation, so that typing
/// the "9" of "92.5" is not rejected for being below the minimum.
class MassFieldValidator final : public QValidator {
    Q_OBJECT

public:
    explicit MassFieldValidator(QObject* parent = nullptr);

    [[nodiscard]] State validate(QString& input, int& position) const override;

    /// True when the text could still grow into a valid decimal.
    [[nodiscard]] static bool isPartialDecimal(const QString& text);

    /// True when the text is a complete, unambiguous decimal.
    [[nodiscard]] static bool isCompleteDecimal(const QString& text);
};

}  // namespace weight::ui
