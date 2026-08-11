#include "settings/Strings.hpp"

#include <QLocale>

#include <cstdlib>

namespace weight::settings {
namespace {

Language g_language = Language::English;
QString g_detectedTag = QStringLiteral("C");
QString g_detectionSource = QStringLiteral("default");

QString environmentValue(const char* name) {
    const char* raw = std::getenv(name);
    return (raw != nullptr) ? QString::fromLocal8Bit(raw).trimmed() : QString();
}

// ===========================================================================
// ENGLISH — the reference catalogue and the fallback for every locale that is
// not Spanish. Keys added here must also be added to the Spanish table; a test
// asserts the two sets are identical so a half-translated release cannot ship.
// ===========================================================================
const QHash<QString, QString>& englishCatalogue() {
    static const QHash<QString, QString> table = {
        // -- window and pages ------------------------------------------------
        {QStringLiteral("app.window.title"), QStringLiteral("Weight")},
        {QStringLiteral("page.input.time"), QStringLiteral("Time")},
        {QStringLiteral("page.input.date"), QStringLiteral("Date")},
        {QStringLiteral("page.input.mass"), QStringLiteral("Mass")},
        {QStringLiteral("page.input.of"), QStringLiteral("of")},
        {QStringLiteral("page.input.colon"), QStringLiteral(":")},
        {QStringLiteral("page.input.kilograms"), QStringLiteral("kg")},
        {QStringLiteral("page.preview.title"), QStringLiteral("Chart preview")},
        {QStringLiteral("page.preview.hint"),
         QStringLiteral("Nothing is saved until you choose Save image.")},
        {QStringLiteral("page.preview.options"), QStringLiteral("Show")},
        {QStringLiteral("page.preview.connector"), QStringLiteral("Line between samples")},
        {QStringLiteral("page.preview.samples"), QStringLiteral("Measurements")},
        {QStringLiteral("page.preview.curve"), QStringLiteral("Curve %1 — %2")},
        {QStringLiteral("page.preview.derivative"), QStringLiteral("Rate of change %1")},
        {QStringLiteral("page.preview.curve.tip"), QStringLiteral("%1")},
        {QStringLiteral("page.preview.derivative.tip"),
         QStringLiteral("Weekly rate of change of curve %1 (%2)")},

        // -- buttons ---------------------------------------------------------
        {QStringLiteral("button.cancel"), QStringLiteral("Cancel")},
        {QStringLiteral("button.reset"), QStringLiteral("Reset")},
        {QStringLiteral("button.reset.datetime"), QStringLiteral("Reset date and time")},
        {QStringLiteral("button.continue"), QStringLiteral("Continue")},
        {QStringLiteral("button.view.chart"), QStringLiteral("View chart only")},
        {QStringLiteral("button.back"), QStringLiteral("Back")},
        {QStringLiteral("button.save"), QStringLiteral("Save image")},

        // -- slider and score ------------------------------------------------
        {QStringLiteral("slider.smoothness"), QStringLiteral("Smoothness: %1")},
        {QStringLiteral("score.unavailable"), QStringLiteral("n/a")},
        {QStringLiteral("score.tooltip"),
         QStringLiteral("How much of the variation in your measurements each curve accounts "
                        "for. 1.0 would pass through every point.")},

        // -- chart -----------------------------------------------------------
        {QStringLiteral("chart.title"), QStringLiteral("Body mass")},
        {QStringLiteral("chart.axis.x"), QStringLiteral("Day")},
        {QStringLiteral("chart.axis.y"), QStringLiteral("Mass [kg]")},
        {QStringLiteral("chart.axis.derivative"), QStringLiteral("Rate of change [kg/week]")},

        // -- curve names -----------------------------------------------------
        {QStringLiteral("curve.a.short"), QStringLiteral("Adaptive Gaussian spline")},
        {QStringLiteral("curve.a.long"),
         QStringLiteral("Natural cubic spline mollified by a Gaussian kernel of adaptive "
                        "bandwidth")},
        {QStringLiteral("curve.b.short"), QStringLiteral("Multiquadric RBF regression")},
        {QStringLiteral("curve.b.long"),
         QStringLiteral("Ridge regression on a multiquadric radial basis")},
        {QStringLiteral("curve.c.short"), QStringLiteral("Local linear LOESS")},
        {QStringLiteral("curve.c.long"),
         QStringLiteral("Local linear regression with Gaussian weights and a k-nearest "
                        "neighbour bandwidth")},

        // -- validation errors -----------------------------------------------
        {QStringLiteral("error.hour"), QStringLiteral("The hour must be between 0 and 23.")},
        {QStringLiteral("error.minute"), QStringLiteral("The minute must be between 0 and 59.")},
        {QStringLiteral("error.day"), QStringLiteral("That day does not exist in this month.")},
        {QStringLiteral("error.month"), QStringLiteral("Choose a month.")},
        {QStringLiteral("error.year"), QStringLiteral("Enter a four-digit year.")},
        {QStringLiteral("error.mass.empty"), QStringLiteral("Enter a mass.")},
        {QStringLiteral("error.mass.invalid"),
         QStringLiteral("The mass must be a number within a plausible range.")},
        {QStringLiteral("error.timestamp"), QStringLiteral("That date and time is not valid.")},
        {QStringLiteral("error.no.preview"),
         QStringLiteral("There is no chart to save. Go back and enter a measurement.")},
        {QStringLiteral("error.fatal"),
         QStringLiteral("Something went wrong. The details are below.")},

        // -- dialogs ---------------------------------------------------------
        {QStringLiteral("dialog.discard.title"), QStringLiteral("Discard the chart?")},
        {QStringLiteral("dialog.discard.text"),
         QStringLiteral("The chart has been generated but not saved.")},
        {QStringLiteral("dialog.discard.question"),
         QStringLiteral("Closing now discards it and records nothing.")},
        {QStringLiteral("dialog.discard.confirm"), QStringLiteral("Discard")},
        {QStringLiteral("dialog.discard.cancel"), QStringLiteral("Go back")},
        {QStringLiteral("dialog.save.title"), QStringLiteral("Save the chart")},
        {QStringLiteral("dialog.save.filter"), QStringLiteral("PNG image (*.png)")},
        {QStringLiteral("dialog.error.title"), QStringLiteral("Could not save")},

        // -- single instance -------------------------------------------------
        {QStringLiteral("instance.title"), QStringLiteral("Weight is already running")},
        {QStringLiteral("instance.text"),
         QStringLiteral("Only one copy of Weight can run at a time.")},
        {QStringLiteral("instance.pid"), QStringLiteral("(already running as process %1)")},
        {QStringLiteral("instance.pid.unknown"), QStringLiteral("(process id unavailable)")},

        // -- calendar --------------------------------------------------------
        {QStringLiteral("month.1"), QStringLiteral("January")},
        {QStringLiteral("month.2"), QStringLiteral("February")},
        {QStringLiteral("month.3"), QStringLiteral("March")},
        {QStringLiteral("month.4"), QStringLiteral("April")},
        {QStringLiteral("month.5"), QStringLiteral("May")},
        {QStringLiteral("month.6"), QStringLiteral("June")},
        {QStringLiteral("month.7"), QStringLiteral("July")},
        {QStringLiteral("month.8"), QStringLiteral("August")},
        {QStringLiteral("month.9"), QStringLiteral("September")},
        {QStringLiteral("month.10"), QStringLiteral("October")},
        {QStringLiteral("month.11"), QStringLiteral("November")},
        {QStringLiteral("month.12"), QStringLiteral("December")},
        {QStringLiteral("month.abbr.1"), QStringLiteral("Jan")},
        {QStringLiteral("month.abbr.2"), QStringLiteral("Feb")},
        {QStringLiteral("month.abbr.3"), QStringLiteral("Mar")},
        {QStringLiteral("month.abbr.4"), QStringLiteral("Apr")},
        {QStringLiteral("month.abbr.5"), QStringLiteral("May")},
        {QStringLiteral("month.abbr.6"), QStringLiteral("Jun")},
        {QStringLiteral("month.abbr.7"), QStringLiteral("Jul")},
        {QStringLiteral("month.abbr.8"), QStringLiteral("Aug")},
        {QStringLiteral("month.abbr.9"), QStringLiteral("Sep")},
        {QStringLiteral("month.abbr.10"), QStringLiteral("Oct")},
        {QStringLiteral("month.abbr.11"), QStringLiteral("Nov")},
        {QStringLiteral("month.abbr.12"), QStringLiteral("Dec")},
        {QStringLiteral("weekday.1"), QStringLiteral("Monday")},
        {QStringLiteral("weekday.2"), QStringLiteral("Tuesday")},
        {QStringLiteral("weekday.3"), QStringLiteral("Wednesday")},
        {QStringLiteral("weekday.4"), QStringLiteral("Thursday")},
        {QStringLiteral("weekday.5"), QStringLiteral("Friday")},
        {QStringLiteral("weekday.6"), QStringLiteral("Saturday")},
        {QStringLiteral("weekday.7"), QStringLiteral("Sunday")},

        // -- log -------------------------------------------------------------
        {QStringLiteral("log.starting"), QStringLiteral("%1 %2 starting.")},
        {QStringLiteral("log.workspace"), QStringLiteral("Workspace: %1")},
        {QStringLiteral("log.language"), QStringLiteral("Language: %1 (from %2)")},
        {QStringLiteral("log.saved.chart"), QStringLiteral("Chart saved: %1")},
        {QStringLiteral("log.saved.history"), QStringLiteral("History saved: %1")},
        {QStringLiteral("log.cancelled"), QStringLiteral("Cancelled by the user.")},

        // Messages that used to be written in English regardless of the
        // detected language, which produced logs half in one language and half
        // in the other. Every line the program prints now goes through here.
        {QStringLiteral("log.threads"),
         QStringLiteral("Thread pool: %1 worker(s) over %2 hardware thread(s).")},
        {QStringLiteral("log.datetime.reset"), QStringLiteral("Date and time reset to %1.")},
        {QStringLiteral("log.history.read"),
         QStringLiteral("History read: %1 valid, %2 unreadable, %3 header(s) and %4 "
                        "duplicate(s) skipped.")},
        {QStringLiteral("log.history.written"),
         QStringLiteral("History saved: %1 (%2 record(s)).")},
        {QStringLiteral("log.history.created"), QStringLiteral("Creating a new history file: %1")},
        {QStringLiteral("log.image.cancelled"), QStringLiteral("Saving cancelled by the user.")},
        {QStringLiteral("error.save.title"), QStringLiteral("Could not save")},
        {QStringLiteral("error.render.full"),
         QStringLiteral("The full-resolution chart could not be rendered. Try reducing "
                        "plot/widthPixels and plot/heightPixels in the settings file.")},
        {QStringLiteral("error.engine.missing"),
         QStringLiteral("The render engine is not available.")},
    };
    return table;
}

// ===========================================================================
// SPANISH — es_*
// ===========================================================================
const QHash<QString, QString>& spanishCatalogue() {
    static const QHash<QString, QString> table = {
        {QStringLiteral("app.window.title"), QStringLiteral("Weight")},
        {QStringLiteral("page.input.time"), QStringLiteral("Hora")},
        {QStringLiteral("page.input.date"), QStringLiteral("Fecha")},
        {QStringLiteral("page.input.mass"), QStringLiteral("Masa")},
        {QStringLiteral("page.input.of"), QStringLiteral("de")},
        {QStringLiteral("page.input.colon"), QStringLiteral(":")},
        {QStringLiteral("page.input.kilograms"), QStringLiteral("kg")},
        {QStringLiteral("page.preview.title"), QStringLiteral("Vista previa del gráfico")},
        {QStringLiteral("page.preview.hint"),
         QStringLiteral("No se guarda nada hasta que elijas Guardar imagen.")},
        {QStringLiteral("page.preview.options"), QStringLiteral("Mostrar")},
        {QStringLiteral("page.preview.connector"), QStringLiteral("Línea entre mediciones")},
        {QStringLiteral("page.preview.samples"), QStringLiteral("Mediciones")},
        {QStringLiteral("page.preview.curve"), QStringLiteral("Curva %1 — %2")},
        {QStringLiteral("page.preview.derivative"), QStringLiteral("Tasa de cambio %1")},
        {QStringLiteral("page.preview.curve.tip"), QStringLiteral("%1")},
        {QStringLiteral("page.preview.derivative.tip"),
         QStringLiteral("Tasa de cambio semanal de la curva %1 (%2)")},

        {QStringLiteral("button.cancel"), QStringLiteral("Cancelar")},
        {QStringLiteral("button.reset"), QStringLiteral("Restablecer")},
        {QStringLiteral("button.reset.datetime"), QStringLiteral("Restablecer fecha y hora")},
        {QStringLiteral("button.continue"), QStringLiteral("Continuar")},
        {QStringLiteral("button.view.chart"), QStringLiteral("Solo ver el gráfico")},
        {QStringLiteral("button.back"), QStringLiteral("Volver")},
        {QStringLiteral("button.save"), QStringLiteral("Guardar imagen")},

        {QStringLiteral("slider.smoothness"), QStringLiteral("Suavidad: %1")},
        {QStringLiteral("score.unavailable"), QStringLiteral("n/d")},
        {QStringLiteral("score.tooltip"),
         QStringLiteral("Qué parte de la variación de tus mediciones explica cada curva. "
                        "1,0 pasaría por todos los puntos.")},

        {QStringLiteral("chart.title"), QStringLiteral("Masa corporal")},
        {QStringLiteral("chart.axis.x"), QStringLiteral("Día")},
        {QStringLiteral("chart.axis.y"), QStringLiteral("Masa [kg]")},
        {QStringLiteral("chart.axis.derivative"), QStringLiteral("Tasa de cambio [kg/semana]")},

        {QStringLiteral("curve.a.short"), QStringLiteral("Spline gaussiana adaptativa")},
        {QStringLiteral("curve.a.long"),
         QStringLiteral("Spline cúbica natural suavizada con un núcleo gaussiano de ancho "
                        "de banda adaptativo")},
        {QStringLiteral("curve.b.short"), QStringLiteral("Regresión RBF multicuádrica")},
        {QStringLiteral("curve.b.long"),
         QStringLiteral("Regresión ridge sobre una base radial multicuádrica")},
        {QStringLiteral("curve.c.short"), QStringLiteral("LOESS lineal local")},
        {QStringLiteral("curve.c.long"),
         QStringLiteral("Regresión lineal local con pesos gaussianos y ancho de banda por "
                        "k vecinos más cercanos")},

        {QStringLiteral("error.hour"), QStringLiteral("La hora debe estar entre 0 y 23.")},
        {QStringLiteral("error.minute"), QStringLiteral("Los minutos deben estar entre 0 y 59.")},
        {QStringLiteral("error.day"), QStringLiteral("Ese día no existe en este mes.")},
        {QStringLiteral("error.month"), QStringLiteral("Elige un mes.")},
        {QStringLiteral("error.year"), QStringLiteral("Escribe un año de cuatro cifras.")},
        {QStringLiteral("error.mass.empty"), QStringLiteral("Escribe una masa.")},
        {QStringLiteral("error.mass.invalid"),
         QStringLiteral("La masa debe ser un número dentro de un rango plausible.")},
        {QStringLiteral("error.timestamp"), QStringLiteral("Esa fecha y hora no es válida.")},
        {QStringLiteral("error.no.preview"),
         QStringLiteral("No hay gráfico que guardar. Vuelve e introduce una medición.")},
        {QStringLiteral("error.fatal"),
         QStringLiteral("Algo ha fallado. Los detalles están abajo.")},

        {QStringLiteral("dialog.discard.title"), QStringLiteral("¿Descartar el gráfico?")},
        {QStringLiteral("dialog.discard.text"),
         QStringLiteral("El gráfico se ha generado pero no se ha guardado.")},
        {QStringLiteral("dialog.discard.question"),
         QStringLiteral("Si cierras ahora se descarta y no se registra nada.")},
        {QStringLiteral("dialog.discard.confirm"), QStringLiteral("Descartar")},
        {QStringLiteral("dialog.discard.cancel"), QStringLiteral("Volver")},
        {QStringLiteral("dialog.save.title"), QStringLiteral("Guardar el gráfico")},
        {QStringLiteral("dialog.save.filter"), QStringLiteral("Imagen PNG (*.png)")},
        {QStringLiteral("dialog.error.title"), QStringLiteral("No se pudo guardar")},

        {QStringLiteral("instance.title"), QStringLiteral("Weight ya se está ejecutando")},
        {QStringLiteral("instance.text"),
         QStringLiteral("Solo puede ejecutarse una copia de Weight a la vez.")},
        {QStringLiteral("instance.pid"), QStringLiteral("(ya en ejecución como proceso %1)")},
        {QStringLiteral("instance.pid.unknown"),
         QStringLiteral("(identificador de proceso no disponible)")},

        {QStringLiteral("month.1"), QStringLiteral("enero")},
        {QStringLiteral("month.2"), QStringLiteral("febrero")},
        {QStringLiteral("month.3"), QStringLiteral("marzo")},
        {QStringLiteral("month.4"), QStringLiteral("abril")},
        {QStringLiteral("month.5"), QStringLiteral("mayo")},
        {QStringLiteral("month.6"), QStringLiteral("junio")},
        {QStringLiteral("month.7"), QStringLiteral("julio")},
        {QStringLiteral("month.8"), QStringLiteral("agosto")},
        {QStringLiteral("month.9"), QStringLiteral("septiembre")},
        {QStringLiteral("month.10"), QStringLiteral("octubre")},
        {QStringLiteral("month.11"), QStringLiteral("noviembre")},
        {QStringLiteral("month.12"), QStringLiteral("diciembre")},
        {QStringLiteral("month.abbr.1"), QStringLiteral("ene")},
        {QStringLiteral("month.abbr.2"), QStringLiteral("feb")},
        {QStringLiteral("month.abbr.3"), QStringLiteral("mar")},
        {QStringLiteral("month.abbr.4"), QStringLiteral("abr")},
        {QStringLiteral("month.abbr.5"), QStringLiteral("may")},
        {QStringLiteral("month.abbr.6"), QStringLiteral("jun")},
        {QStringLiteral("month.abbr.7"), QStringLiteral("jul")},
        {QStringLiteral("month.abbr.8"), QStringLiteral("ago")},
        {QStringLiteral("month.abbr.9"), QStringLiteral("sep")},
        {QStringLiteral("month.abbr.10"), QStringLiteral("oct")},
        {QStringLiteral("month.abbr.11"), QStringLiteral("nov")},
        {QStringLiteral("month.abbr.12"), QStringLiteral("dic")},
        {QStringLiteral("weekday.1"), QStringLiteral("lunes")},
        {QStringLiteral("weekday.2"), QStringLiteral("martes")},
        {QStringLiteral("weekday.3"), QStringLiteral("miércoles")},
        {QStringLiteral("weekday.4"), QStringLiteral("jueves")},
        {QStringLiteral("weekday.5"), QStringLiteral("viernes")},
        {QStringLiteral("weekday.6"), QStringLiteral("sábado")},
        {QStringLiteral("weekday.7"), QStringLiteral("domingo")},

        {QStringLiteral("log.starting"), QStringLiteral("%1 %2 iniciando.")},
        {QStringLiteral("log.workspace"), QStringLiteral("Carpeta de trabajo: %1")},
        {QStringLiteral("log.language"), QStringLiteral("Idioma: %1 (desde %2)")},
        {QStringLiteral("log.saved.chart"), QStringLiteral("Gráfico guardado: %1")},
        {QStringLiteral("log.saved.history"), QStringLiteral("Historial guardado: %1")},
        {QStringLiteral("log.cancelled"), QStringLiteral("Cancelado por el usuario.")},
        {QStringLiteral("log.threads"),
         QStringLiteral("Grupo de hilos: %1 trabajador(es) sobre %2 hilo(s) de hardware.")},
        {QStringLiteral("log.datetime.reset"),
         QStringLiteral("Fecha y hora restablecidas a %1.")},
        {QStringLiteral("log.history.read"),
         QStringLiteral("Historial leído: %1 válidos, %2 ilegibles, %3 cabecera(s) y %4 "
                        "duplicado(s) omitidos.")},
        {QStringLiteral("log.history.written"),
         QStringLiteral("Historial guardado: %1 (%2 registro(s)).")},
        {QStringLiteral("log.history.created"),
         QStringLiteral("Creando un historial nuevo: %1")},
        {QStringLiteral("log.image.cancelled"),
         QStringLiteral("Guardado cancelado por el usuario.")},
        {QStringLiteral("error.save.title"), QStringLiteral("No se pudo guardar")},
        {QStringLiteral("error.render.full"),
         QStringLiteral("No se pudo generar el gráfico a resolución completa. Prueba a reducir "
                        "plot/widthPixels y plot/heightPixels en el archivo de configuración.")},
        {QStringLiteral("error.engine.missing"),
         QStringLiteral("El motor de gráficos no está disponible.")},
    };
    return table;
}

const QHash<QString, QString>& catalogueFor(Language language) {
    return language == Language::Spanish ? spanishCatalogue() : englishCatalogue();
}

}  // namespace

bool Strings::tagIsNeutral(const QString& tag) {
    const QString normalised = tag.trimmed().toUpper();
    // A bare "C" or "POSIX", with or without a codeset suffix, is the explicit
    // statement that no locale is configured.
    return normalised.isEmpty() || normalised == QLatin1String("C")
           || normalised == QLatin1String("POSIX")
           || normalised.startsWith(QLatin1String("C."))
           || normalised.startsWith(QLatin1String("POSIX."));
}

Language Strings::languageForTag(const QString& tag) {
    if (tagIsNeutral(tag)) {
        return Language::English;
    }
    const QString lowered = tag.trimmed().toLower();
    if (lowered.startsWith(QLatin1String("es"))) {
        return Language::Spanish;
    }
    // Everything else, including en_*, resolves to English.
    return Language::English;
}

void Strings::detectAndInstall() {
    struct Candidate {
        const char* variable;
        const char* label;
    };
    // Order matters and follows POSIX: LC_ALL beats LC_MESSAGES beats LANG.
    static constexpr Candidate kCandidates[] = {
        {"WEIGHT_LANG", "WEIGHT_LANG"},
        {"LC_ALL", "LC_ALL"},
        {"LC_MESSAGES", "LC_MESSAGES"},
        {"LANG", "LANG"},
    };

    for (const Candidate& candidate : kCandidates) {
        const QString value = environmentValue(candidate.variable);
        if (value.isEmpty()) {
            continue;
        }
        // A neutral value is a real answer, not a missing one: it means the
        // machine has no locale configured, so English is correct and the
        // search stops here rather than falling through to Qt.
        g_detectedTag = value;
        g_detectionSource = QString::fromLatin1(candidate.label);
        g_language = languageForTag(value);
        return;
    }

    // Nothing in the environment. On Windows and macOS the POSIX variables are
    // usually absent and Qt reads the platform's UI language instead.
    const QString systemTag = QLocale::system().name();
    g_detectedTag = systemTag;
    g_detectionSource = QStringLiteral("QLocale::system");
    g_language = languageForTag(systemTag);
}

void Strings::install(Language language) {
    g_language = language;
    g_detectedTag = (language == Language::Spanish) ? QStringLiteral("es")
                                                    : QStringLiteral("en");
    g_detectionSource = QStringLiteral("explicit");
}

Language Strings::current() { return g_language; }
QString Strings::detectedTag() { return g_detectedTag; }
QString Strings::detectionSource() { return g_detectionSource; }

QString Strings::get(const QString& key) {
    const QHash<QString, QString>& table = catalogueFor(g_language);
    const auto found = table.constFind(key);
    if (found != table.constEnd()) {
        return *found;
    }
    // Fall back to English before giving up, so a key missing only from the
    // Spanish table still shows readable text rather than an identifier.
    const QHash<QString, QString>& fallback = englishCatalogue();
    const auto englishEntry = fallback.constFind(key);
    return englishEntry != fallback.constEnd() ? *englishEntry : key;
}

QString Strings::get(const QString& key, const QString& a1) { return get(key).arg(a1); }

QString Strings::get(const QString& key, const QString& a1, const QString& a2) {
    return get(key, QStringList{a1, a2});
}

QString Strings::get(const QString& key, const QString& a1, const QString& a2,
                     const QString& a3) {
    return get(key, QStringList{a1, a2, a3});
}

QString Strings::get(const QString& key, const QString& a1, const QString& a2, const QString& a3,
                     const QString& a4) {
    return get(key, QStringList{a1, a2, a3, a4});
}

QString Strings::get(const QString& key, const QStringList& arguments) {
    // Substituted highest index first. Replacing %1 before %10 would corrupt
    // the latter, and doing it in one pass over the list avoids the chained
    // .arg() trap where a value that itself contains "%1" gets substituted
    // again by the next call.
    QString text = get(key);
    for (int index = arguments.size(); index >= 1; --index) {
        text.replace(QStringLiteral("%%1").arg(index), arguments.at(index - 1));
    }
    return text;
}

QStringList Strings::keys(Language language) {
    QStringList list = catalogueFor(language).keys();
    list.sort();
    return list;
}

}  // namespace weight::settings
