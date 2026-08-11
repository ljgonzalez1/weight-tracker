#include "ui/FontLibrary.hpp"

namespace weight::ui {

QFont FontLibrary::fromSpec(const settings::FontSpec& spec) {
    QFont font(spec.family, spec.pointSize);
    font.setBold(spec.bold);
    return font;
}

FontLibrary::FontLibrary(const settings::UiTheme& theme)
    : label_(fromSpec(theme.labelFont)),
      entry_(fromSpec(theme.entryFont)),
      largeEntry_(fromSpec(theme.largeEntryFont)),
      button_(fromSpec(theme.buttonFont)),
      error_(fromSpec(theme.errorFont)),
      secondary_(fromSpec(theme.secondaryFont)),
      monospace_(fromSpec(theme.monospaceFont)),
      padding_(theme.fieldWidthPaddingPixels) {}

int FontLibrary::widthForCharacters(const QFont& font, int characters) const {
    const QFontMetrics metrics(font);
    return metrics.horizontalAdvance(QStringLiteral("0")) * characters + padding_;
}

}  // namespace weight::ui
