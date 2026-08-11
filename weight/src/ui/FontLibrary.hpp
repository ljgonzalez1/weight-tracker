#pragma once

#include <QFont>
#include <QFontMetrics>

#include "settings/Settings.hpp"

namespace weight::ui {

/// Builds the window's fonts once and converts character counts into pixels.
///
/// Field widths in the configuration are expressed in character cells. Turning
/// them into pixels with QFontMetrics rather than hard-coding a width keeps the
/// layout correct under any font, display scaling factor or accessibility
/// setting, which a fixed pixel width would not survive.
class FontLibrary {
public:
    explicit FontLibrary(const settings::UiTheme& theme);

    [[nodiscard]] const QFont& label() const noexcept { return label_; }
    [[nodiscard]] const QFont& entry() const noexcept { return entry_; }
    [[nodiscard]] const QFont& largeEntry() const noexcept { return largeEntry_; }
    [[nodiscard]] const QFont& button() const noexcept { return button_; }
    [[nodiscard]] const QFont& error() const noexcept { return error_; }
    [[nodiscard]] const QFont& secondary() const noexcept { return secondary_; }
    [[nodiscard]] const QFont& monospace() const noexcept { return monospace_; }

    /// Width in pixels of a field `characters` cells wide in `font`.
    ///
    /// The digit zero is used as the reference glyph because in proportional
    /// faces its advance is a good approximation of the average digit and
    /// lowercase width, which is what these fields hold.
    [[nodiscard]] int widthForCharacters(const QFont& font, int characters) const;

private:
    static QFont fromSpec(const settings::FontSpec& spec);

    QFont label_;
    QFont entry_;
    QFont largeEntry_;
    QFont button_;
    QFont error_;
    QFont secondary_;
    QFont monospace_;
    int padding_;
};

}  // namespace weight::ui
