#include "data/TextSanitizer.hpp"

#include <QChar>
#include <QHash>
#include <QRegularExpression>

namespace weight::data {
namespace {

/// Typographic characters that routinely appear inside numbers copied from
/// word processors, spreadsheets and PDFs, mapped onto the ASCII character the
/// author meant. Every entry is unambiguous: none of them changes the value of
/// a number, only its spelling.
const QHash<char32_t, QChar>& translationTable() {
    static const QHash<char32_t, QChar> table = {
        {0x00A0, QLatin1Char(' ')},   // no-break space
        {0x202F, QLatin1Char(' ')},   // narrow no-break space
        {0x2007, QLatin1Char(' ')},   // figure space
        {0x2009, QLatin1Char(' ')},   // thin space
        {0x2212, QLatin1Char('-')},   // minus sign
        {0x2010, QLatin1Char('-')},   // hyphen
        {0x2011, QLatin1Char('-')},   // non-breaking hyphen
        {0x2012, QLatin1Char('-')},   // figure dash
        {0x2013, QLatin1Char('-')},   // en dash
        {0x2014, QLatin1Char('-')},   // em dash
        {0x2044, QLatin1Char('/')},   // fraction slash
        {0x00B7, QLatin1Char('.')},   // middle dot used as a decimal mark
        {0x066B, QLatin1Char(',')},   // Arabic decimal separator
        {0x066C, QLatin1Char(' ')},   // Arabic thousands separator
        {0xFF0E, QLatin1Char('.')},   // fullwidth full stop
        {0xFF0C, QLatin1Char(',')},   // fullwidth comma
        {0xFF1B, QLatin1Char(';')},   // fullwidth semicolon
        {0x201C, QLatin1Char('"')},   // curly quotes
        {0x201D, QLatin1Char('"')},
        {0x2018, QLatin1Char('\'')},
        {0x2019, QLatin1Char('\'')},
    };
    return table;
}

}  // namespace

QString TextSanitizer::sanitize(const QString& text) {
    QString cleaned = text.normalized(QString::NormalizationForm_KC);

    const auto& table = translationTable();
    QString translated;
    translated.reserve(cleaned.size());
    for (const QChar character : cleaned) {
        // Zero-width space and byte order mark carry no meaning in a number.
        if (character.unicode() == 0x200B || character.unicode() == 0xFEFF) {
            continue;
        }
        const auto found = table.constFind(character.unicode());
        translated.append(found != table.constEnd() ? *found : character);
    }
    cleaned = translated.trimmed();

    // Quotes the CSV reader did not consume, for instance single quotes used as
    // a text marker by a spreadsheet export.
    while (cleaned.size() >= 2 && cleaned.front() == cleaned.back()
           && (cleaned.front() == QLatin1Char('"') || cleaned.front() == QLatin1Char('\''))) {
        cleaned = cleaned.mid(1, cleaned.size() - 2).trimmed();
    }
    return cleaned;
}

QString TextSanitizer::stripAccents(const QString& text) {
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(decomposed.size());
    for (const QChar character : decomposed) {
        if (character.category() != QChar::Mark_NonSpacing) {
            result.append(character);
        }
    }
    return result;
}

QString TextSanitizer::headerToken(const QString& text) {
    static const QRegularExpression bracketSuffix(QStringLiteral("\\s*\\[[^\\]]*\\]\\s*$"));
    static const QRegularExpression parenSuffix(QStringLiteral("\\s*\\([^)]*\\)\\s*$"));

    QString token = stripAccents(text).trimmed().toLower();
    while (!token.isEmpty()
           && (token.front() == QLatin1Char('"') || token.front() == QLatin1Char('\''))) {
        token.remove(0, 1);
    }
    while (!token.isEmpty()
           && (token.back() == QLatin1Char('"') || token.back() == QLatin1Char('\''))) {
        token.chop(1);
    }
    token.remove(bracketSuffix);
    token.remove(parenSuffix);
    return token.trimmed();
}

}  // namespace weight::data
