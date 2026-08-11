#pragma once

#include <functional>
#include <vector>

#include <QString>

namespace weight::data {

/// Repairs malformed numeric cells, one unambiguous transformation at a time.
///
/// Guiding rule: repair only what cannot change the value. "92,4 kg" is
/// unambiguously 92.4 and is repaired. Forms carrying two different separators,
/// such as "1.222,333", are also unambiguous: only one reading of them is a
/// valid number at all, so they are repaired too.
///
/// The genuinely ambiguous case is a single separator followed by exactly three
/// digits: "1.222" could be one thousand two hundred and twenty-two or one
/// point two two two, and no rule can tell. That case is settled before the
/// chain ever runs, because the parser makes a strict pass first and reads it
/// as a decimal; the chain only ever sees text the strict pass rejected. The
/// interpretation is therefore fixed and predictable rather than guessed.
///
/// Text that no rule can rescue, such as "9 8" or "abc", is left alone and the
/// row is preserved verbatim at the end of the file. Losing a measurement is
/// bad; silently altering one is worse.
///
/// The repairs are organised as a Chain of Responsibility: each rule inspects
/// the text, either rewrites it or passes it through unchanged, and the next
/// rule sees the result. That makes the order explicit, lets a rule be added or
/// removed in isolation, and lets every rule be tested on its own.
class NumericTextRepairChain {
public:
    /// One repair step: takes the current text and returns the rewritten form.
    struct Rule {
        QString name;
        std::function<QString(const QString&, bool allowSign)> apply;
    };

    /// Chain used for the day and mass columns, in the order the rules run.
    [[nodiscard]] static const NumericTextRepairChain& standard();

    /// Runs every rule in order. `allowSign` mirrors the column semantics: the
    /// day column keeps its sign, the mass column has none.
    [[nodiscard]] QString repair(const QString& text, bool allowSign) const;

    /// The rules, in order. Exposed so tests can exercise them individually.
    [[nodiscard]] const std::vector<Rule>& rules() const noexcept { return rules_; }

    void addRule(Rule rule) { rules_.push_back(std::move(rule)); }

private:
    std::vector<Rule> rules_;
};

}  // namespace weight::data
