#!/usr/bin/python3
# -*- coding: utf-8 -*-

"""
grafico_masa_tiempo_suavizado_v20.py
════════════════════════════════════
Registra masa corporal mediante una ventana gráfica (PyQt6), la almacena
en un archivo CSV con formato ``día;masa;fecha`` y genera un gráfico PNG
de masa vs tiempo superpuesto a un modelo matemático de curva.

Esta es la versión PyQt6 del programa original basado en Tkinter
(``grafico_masa_tiempo_suavizado_v17.py``).  Toda la lógica matemática,
el modelo de la curva, el parseo/escritura del CSV y los parámetros de
configuración se mantienen *idénticos* al original; sólo cambió la capa
de interfaz gráfica, pasando de Tkinter a PyQt6.

El "día" es fraccionario respecto a una fecha de origen configurable:
  - 0.0   → medianoche del día de origen
  - 1.5   → mediodía del día siguiente
  - 2.25  → 06:00 del segundo día

Dependencias de sistema
───────────────────────
  Debian / Ubuntu:
      sudo apt install python3-pyqt6 python3-matplotlib python3-numpy

  Arch Linux / AUR:
      sudo pacman -S python-pyqt6 python-matplotlib python-numpy

  Entorno virtual (venv):
      pip install PyQt6 matplotlib numpy

Uso
───
  python3 grafico_masa_tiempo_suavizado_v20.py
  python3 grafico_masa_tiempo_suavizado_v20.py mi_archivo.csv
  python3 grafico_masa_tiempo_suavizado_v20.py --help

Notas específicas del port PyQt6
────────────────────────────────
- La ventana es no redimensionable por el usuario, pero el programa sí
  ajusta su tamaño al conmutar entre la página de entrada y la página
  de vista previa (igual que hacía la versión Tkinter original).
- La ventana aparece centrada en la *pantalla principal* (primary
  screen), independientemente del sistema operativo y del servidor
  gráfico subyacente (X11 / Wayland / Windows / macOS).  El cálculo del
  centro se hace contra la geometría disponible del monitor primario
  reportado por Qt, no contra la unión de todas las pantallas.
- La clase ``MassInputApp`` hereda de ``QWidget`` en lugar de usar un
  ``tk.Tk`` externo; todo el ciclo de vida (construcción, modalidad,
  cierre) se gestiona desde dentro.
- Se añade una ventana modal de confirmación cuando el usuario intenta
  cerrar (botón "Cancelar" o la "X" de la barra de título) estando en
  la página de vista previa.  Esta ventana bloquea toda interacción
  hasta resolverse, estilo "¿Desea guardar los cambios antes de
  cerrar?" clásico.
"""

# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 0 — Importaciones                                              ║
# ╚════════════════════════════════════════════════════════════════════════════╝

import argparse
import base64
import calendar
import csv
import math
import os
import sys
import tempfile
import traceback
from dataclasses import dataclass, field, replace
from datetime import date, datetime, timedelta
from pathlib import Path

# Matplotlib se usa exclusivamente para generar PNGs en disco; la GUI es de
# PyQt6.  Por eso fijamos el backend Agg antes de importar pyplot: así
# evitamos que matplotlib cargue un backend interactivo que pudiera entrar en
# conflicto con la propia aplicación Qt.
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import FuncFormatter, MultipleLocator

# ── PyQt6 ──
from PyQt6.QtCore import Qt, QSize, QTimer, QStandardPaths
from PyQt6.QtGui import (
    QCloseEvent,
    QFont,
    QFontMetrics,
    QGuiApplication,
    QIcon,
    QIntValidator,
    QKeySequence,
    QPixmap,
    QShortcut,
    QValidator,
)
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSlider,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 1 — PARÁMETROS CONFIGURABLES                                   ║
# ║                                                                          ║
# ║  Todos los valores "mágicos" del programa están aquí.  Para cambiar el   ║
# ║  comportamiento basta editar esta sección; el resto del código los lee    ║
# ║  a través de los dataclasses que los agrupan.                            ║
# ╚════════════════════════════════════════════════════════════════════════════╝

# ┌────────────────────────────────────────────────────────────────────────────┐
# │  1-A  Rangos de validación para la entrada del usuario                    │
# │                                                                           │
# │  Estos límites se aplican tanto en la GUI (filtro de teclado) como al     │
# │  parsear el CSV.  Un valor fuera de rango se considera inválido.          │
# └────────────────────────────────────────────────────────────────────────────┘

MIN_HOUR:   int   = 0       # Hora mínima  (formato 24 h)
MAX_HOUR:   int   = 23      # Hora máxima

MIN_MINUTE: int   = 0       # Minuto mínimo
MAX_MINUTE: int   = 59      # Minuto máximo

MIN_DAY:    int   = 1       # Día mínimo del mes
MAX_DAY:    int   = 31      # Día máximo (se reduce según mes/año)

MIN_YEAR:   int   = 1900    # Año mínimo aceptado
MAX_YEAR:   int   = 2100    # Año máximo aceptado

MIN_MASS:   float = 0.0     # Masa mínima en kg
MAX_MASS:   float = 500.0   # Masa máxima en kg

DEFAULT_SMOOTHNESS: float = 1.35
DEFAULT_CURVE_START_T: float = 0.0
DEFAULT_CURVE_STOP_T:  float = 280.0

# ┌────────────────────────────────────────────────────────────────────────────┐
# │  1-B  Configuración del CSV                                               │
# │                                                                           │
# │  Formato: ``día;masa;fecha``                                              │
# │  - Columna 1 (día):   float con punto decimal, hasta N decimales          │
# │  - Columna 2 (masa):  float con punto decimal                             │
# │  - Columna 3 (fecha): string ``AAAA-MM-DD HH:MM`` (informativa)          │
# └────────────────────────────────────────────────────────────────────────────┘

DEFAULT_CSV_FILENAME:  str = "valores.csv"     # Nombre del CSV cuando no se da argumento
CSV_HEADER:            str = "día;masa;fecha"  # Encabezado al crear CSV nuevo
CSV_SEPARATOR:         str = ";"               # Delimitador de columnas
CSV_ENCODING:          str = "utf-8"           # Codificación de escritura
CSV_ENCODING_READ:     str = "utf-8-sig"       # Codificación de lectura (tolera BOM)
DAY_FRACTION_DECIMALS: int = 5                 # Decimales para el día fraccionario
MASS_FORMAT_DECIMALS:  int = 5                 # Decimales para la masa al escribir
SECONDS_PER_DAY:       float = 86_400.0        # Segundos en un día solar medio
TIMESTAMP_FORMAT:      str = "%Y-%m-%d %H:%M"  # Formato de la tercera columna

# ┌────────────────────────────────────────────────────────────────────────────┐
# │  1-C  Cadenas de localización (español)                                   │
# │                                                                           │
# │  Se usan en la GUI y en las etiquetas del eje X del gráfico.              │
# └────────────────────────────────────────────────────────────────────────────┘

MONTHS_ES: list[str] = [
    "enero", "febrero", "marzo", "abril", "mayo", "junio",
    "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre",
]

MONTHS_ES_ABBR: list[str] = [
    "Ene", "Feb", "Mar", "Abr", "May", "Jun",
    "Jul", "Ago", "Sep", "Oct", "Nov", "Dic",
]

WEEKDAYS_ES: list[str] = [
    "Lunes", "Martes", "Miércoles", "Jueves",
    "Viernes", "Sábado", "Domingo",
]


# ┌────────────────────────────────────────────────────────────────────────────┐
# │  1-C-bis  Textos del diálogo de confirmación al cerrar                    │
# │                                                                           │
# │  Usados cuando el usuario intenta cerrar la ventana estando en la página  │
# │  de vista previa.  El diálogo bloquea toda interacción hasta resolverse.  │
# └────────────────────────────────────────────────────────────────────────────┘

CONFIRM_DISCARD_TITLE:         str = "¿Descartar el gráfico generado?"
CONFIRM_DISCARD_TEXT:          str = "Si cierras ahora, se perderán los datos generados y no se guardará la imagen."
CONFIRM_DISCARD_QUESTION:      str = "¿Realmente quieres salir sin guardar?"
CONFIRM_DISCARD_BTN_YES:       str = "Sí, cerrar"
CONFIRM_DISCARD_BTN_NO:        str = "Volver"

OUTPUT_FILENAME_TIMESTAMP_FORMAT: str = "%Y-%m-%d_%H.%M.%S"
APP_ICON_PNG_BASE64: str = (
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAADXRFWHRsb2dpY2FsWAAzMjUzr8FvhQAAAAx0RVh0bG9naWNhbFkANDM15Sq3yAAAAChpVFh0c3ViR2VvbWV0cnlMaXN0AAAAVVRGLTgAc3ViR2VvbWV0cnlMaXN0ANzG6VIAAAQ9SURBVHhe7ZvNbxNHGMYfj0MTu1IpUmQSFJSolH8gJNwIPZcLkM9rbMdJeml7aHvhI/TafNByg9hJS0A0iAvFqKp6KB+iKikJFT2Vq0UsFVuiNCvHOFkOMOP1m7Hj2exaa69/UiTvM2tFz0/zOs448TQHWnS4GEYDt1EXQAO3URdAA7fRQAMjhzo78cn4mLgOjUQMq7VBSQGt+1px7NjHNK4adu9+Dy9e/EfjAmp2BCIjYdy78xsOHvyQLhVQkwIiI2F8fW4CgUAAN64vlpRQcwJ4ec77e/ag58iR/A0ExwlgjOGrL79AS0sLXdoWWj776hVmZ6P49/nz/E0ERwlgjGF68ht8/tmnuHF9UUlCsfKapuVvkuAYAbz80NAgAODAgQ/KlmC2POAQAbQ8pxwJo5ER0+UBBwiQldf1/G/opSSMjUZwbuKsuM5ms0rlAUUBDQ0Nln69s2sXvj0/U1A+lUrhuwsX8PDhkshkEsbHRjFx9oy4zmazmI3GlMoD27wTpORyORqZhjGGqalJ9Pf1iiyVSuHywhXkcjncu38fAHD4cDeAvITe/gGcPHEcZ06fEs8zWx5QFGAVfNsPDg6IzFgeeDMGMgm//vIzmpubxfN2Uh5QHAErkM08Lc/hEozjYGV5oMICVMpzZBIAa8oDFRRgpjyHSrCqPFCh1wDGGGamJkvO/HZwCbmNHB4//suS8kAFBDDGMDM9hcGBfpGplufouo4HD36n8Y6wVQBjDOdnpjHQ3ycys+XtwrbXgGooDyjugOilizQqyt69AXR1dYlrJ5YHFAWYPR9Mp9OOLA/YOAKcdDqNHy4vOLI8oLgD7ty9S6OS6DqwsrKCjY0NuuQYlAQsLf1Jo6rH9hFwOnUBNHAbdQE0cBt1ATRwG64XoPRGaCf4fD54vV4AgMfjEWf/uq5D13VkMhlsbm4an1IRKiKgvb294PhbhqZpiMbmsL6+TpdsxfYRKKc8APj9foSCw2hsbKRLtmKrgHLLc/x+P8KhYEUl2DYCHR0d6Os9Ka7X1tYQm5uXbvG2tjYMvT0w9fl8CIeCmI3GpPdajS07QKU8ACQSCVz7cVFccwlNTU2Gu+zBcgGy8nPz3xctz5FJCAWHbZegNALd3fkzPhnv+v0F54C8fCaTMdxVHC7BOA6h4DD+IJ8KyfB4PDQSeL1e3Lz5E40BKAo42tNDo6JomqZUniOT8NHR8r+vjP9fvqSRwPIRAN6Uj83NK5fn0HGwE6UdUO6Z4JMnf5suz0kkEli4chX797fRJWX+efqURgIlAZU+E0wmk0gmkzRWppQAW0agmnC9AA/9p6nopYumPwFyKvH47aJ/6u/6HeB6AVtG4FBnJ1r3tYrH42OjYi0cyT+uJlafreLR8jKNAUh+DD5aXgbk9+LWrTiNqh7Xj8CWHWBk9dkq4vHbNK4ptrwGuA3Xj0BdAA3cxmu3uxic+DqsiAAAAABJRU5ErkJggg=="
)


# ┌────────────────────────────────────────────────────────────────────────────┐
# │  1-D  Apariencia de la ventana PyQt6                                      │
# │                                                                           │
# │  Tipografía, colores, textos de botones y etiquetas.  Cambiar aquí para   │
# │  personalizar la GUI sin tocar la lógica.                                 │
# └────────────────────────────────────────────────────────────────────────────┘

@dataclass(frozen=True)
class GuiStyle:
    """Agrupa toda la configuración visual de la ventana PyQt6.

    Los valores de tipografía son tuplas ``(familia, tamaño)`` o
    ``(familia, tamaño, "bold")``.  Se traducen a ``QFont`` dentro de la
    clase principal.  Los anchos de widget se expresan en "columnas de
    carácter" aproximadas y se calculan con ``QFontMetrics``.
    """

    # ── Título de la ventana ──
    window_title: str = "Registrar masa"

    # ── Tipografías  (familia, tamaño [, estilo]) ──
    font_label:     tuple = ("Sans", 13)
    font_entry:     tuple = ("Sans", 14)
    font_entry_lg:  tuple = ("Sans", 16)
    font_button:    tuple = ("Sans", 12)
    font_error:     tuple = ("Sans", 12, "bold")
    font_weekday:   tuple = ("Sans", 12)
    font_traceback: tuple = ("Monospace", 9)

    # ── Colores (se aplican puntualmente, NO se define una hoja global) ──
    color_error_text:  str = "#b00020"  # Rojo oscuro para mensajes de error
    color_fatal_bg:    str = "#fff0f0"  # Fondo del área de stack trace
    color_fatal_fg:    str = "#800000"  # Texto del stack trace

    # ── Textos de botones ──
    text_cancel:           str = "Cancelar"
    text_reset_date:       str = "Resetear fecha"
    text_reset_datetime:   str = "Resetear fecha y hora"
    text_continue:         str = "Continuar"
    text_view_graph_only:  str = "Sólo ver gráfico"
    text_save:             str = "Guardar y generar"
    text_save_image:       str = "Guardar imagen"
    text_back:             str = "Volver"

    # ── Textos de vista previa ──
    text_preview_title: str = "Vista previa del gráfico"
    text_preview_hint:  str = "Cancelar cierra sin guardar. Guardar imagen escribe el CSV y guarda el PNG final."
    text_preview_smoothness: str = "Suavidad de la curva"
    text_preview_options:    str = "Opciones de visualización"
    text_preview_reset:      str = "Reset"
    text_preview_show_other_thing: str = "Línea entre muestras 🟧"
    text_preview_show_adjustment:  str = "Curva de ajuste 🟥"
    text_preview_show_derivative:  str = "Diferencial tendencia [kg/sem] 🟥⋯"
    text_preview_show_points:      str = "Muestras 🟧"

    # ── Textos de etiquetas ──
    label_time:    str = "Hora del día"
    label_date:    str = "Fecha"
    label_mass:    str = "Masa"
    label_kg:      str = "kg"
    label_colon:   str = ":"
    label_of:      str = "de"

    # ── Mensajes de error que muestra la GUI ──
    error_invalid_hour:      str = "Hora inválida"
    error_invalid_minute:    str = "Minuto inválido"
    error_invalid_year:      str = "Año inválido"
    error_invalid_month:     str = "Mes inválido"
    error_invalid_day:       str = "Día inválido"
    error_invalid_timestamp: str = "Timestamp inválido"
    error_empty_mass:        str = "Masa vacía"
    error_invalid_mass:      str = "Masa inválida"
    error_fatal:             str = "Error fatal — ver detalles abajo"

    # ── Dimensiones de widgets ──
    entry_width_time:  int = 3    # Ancho del campo hora / minuto (en "chars")
    entry_width_day:   int = 3    # Ancho del campo día
    entry_width_year:  int = 5    # Ancho del campo año
    entry_width_mass:  int = 10   # Ancho del campo masa
    combo_width_month: int = 12   # Ancho del combobox de mes
    button_width_sm:   int = 14   # Ancho de botones pequeños
    button_width_lg:   int = 16   # Ancho del botón grande
    traceback_height:  int = 12   # Líneas visibles del stack trace

    # ── Padding general (px) ──
    pad_x: int = 8
    pad_y: int = 6
    pad_main_x: int = 16
    pad_main_y: int = 12
    pad_button: int = 12


# ┌────────────────────────────────────────────────────────────────────────────┐
# │  1-E  Apariencia del gráfico (matplotlib)                                 │
# │                                                                           │
# │  Dimensiones, colores, grosores, franjas de tolerancia, márgenes.         │
# └────────────────────────────────────────────────────────────────────────────┘

@dataclass
class PlotConfig:
    """Parámetros que controlan la curva modelo, los ejes y el aspecto visual."""

    # ── Fecha de origen: el día 0.0 del eje X ──
    start_date: date = field(default_factory=lambda: date(2026, 3, 31))

    # ── Dominio del modelo ──
    stretch_factor: float = 7.0              # Factor de ensanchamiento horizontal de Q(x)→P(t)
    curve_start_t:  float = DEFAULT_CURVE_START_T  # Día inicial del tramo parametrizado
    curve_stop_t:   float = DEFAULT_CURVE_STOP_T   # Día donde P(t) se congela en su último valor

    # ── Límites de los ejes ──
    x_min: float = 0.0
    x_max: float = 297.0
    y_min: float = 60.0    # kg
    y_max: float = 100.0   # kg

    # ── Pasos de las etiquetas y grilla ──
    x_label_step_days: float = 7.0    # Separación en días del eje X
    y_label_step:      float = 2.0    # Separación en kg del eje Y (mayor)
    y_grid_step:       float = 0.5    # Separación en kg de la grilla fina

    # ── Tamaño de la imagen de salida ──
    width_px:  int = 5326   # Ancho aproximado en píxeles
    height_px: int = 2048   # Alto aproximado en píxeles
    dpi:       int = 200    # Resolución

    # ── Vista previa dentro de PyQt6 ──
    preview_width_px: int = 1400  # Ancho horizontal pedido para la página 2
    preview_dpi:      int = 100   # DPI de la imagen temporal de vista previa

    # ── Slider de suavidad en la página de vista previa ──
    smooth_slider_min:          float = 0.10
    smooth_slider_max:          float = 4.00
    smooth_slider_resolution:   float = 0.01
    smooth_slider_tickinterval: float = 0.1

    # ── Línea central de referencia ──
    central_line_y:         float = 70.56     # Posición Y de la línea guía
    central_line_alpha:     float = 0.32      # Transparencia
    central_line_width:     float = 1.4       # Grosor
    central_line_style:     str   = "--"      # Estilo ("--" = guiones)

    # ── Colores ──
    color_curve:        str = "#1f77b4"   # Azul — curva principal y franjas
    color_guide_line:   str = "#5f8f86"   # Verde grisáceo — línea de referencia
    color_points:       str = "#f28e2b"   # Naranja — puntos de datos reales
    color_smooth_curve: str = "tab:red"   # Rojo suave de matplotlib para el ajuste

    # ── Curva principal ──
    curve_linewidth: float = 2.5

    # ── Curva de ajuste suave (spline cúbica natural + mollifier gaussiano adaptativo) ──
    #    La curva roja se recalcula completa en cada ejecución a partir del
    #    conjunto de puntos válido del CSV.  La estrategia combina dos ideas:
    #
    #      1. una spline cúbica natural sobre los x únicos agregados;
    #      2. una convolución gaussiana adaptativa sobre una malla densa.
    #
    #    El primer paso entrega una curva base suave y globalmente coherente.
    #    El segundo paso actúa como un "mollifier" gaussiano: redondea la
    #    curva resultante con un perfil de ancho h(t) continuo sobre toda la
    #    malla, en vez de definido por intervalos duros.  En la práctica esto
    #    reduce puntas visuales y aproxima mejor la idea pedida de una curva
    #    "casi infinitamente diferenciable".
    smooth_curve_alpha:           float = 0.375  # 75% de la opacidad roja anterior
    smooth_curve_linewidth_ratio: float = 0.50   # 50% del grosor de la curva azul
    smooth_band_half_width:       float = 0.50   # Franja principal ±0.5 kg alrededor de la curva roja
    smooth_outer_band_half_width: float = 1.00   # Franja secundaria ±1.0 kg alrededor de la curva roja
    smooth_band_alpha:            float = 0.1125 # 75% de la opacidad roja anterior
    smooth_outer_band_alpha:      float = 0.075  # 2/3 de la opacidad de la franja roja principal
    smooth_min_points:            int   = 3      # Mínimo de puntos para intentar el ajuste

    # ── Diferencial de la curva roja (kg por semana) ──
    derivative_sample_step_days: float = 0.5
    derivative_y_min:            float = -4.0
    derivative_y_max:            float = 4.0
    derivative_y_tick_step:      float = 1.0
    derivative_linewidth_ratio:  float = 0.50
    derivative_line_alpha:       float = 0.85
    derivative_line_style:       str   = ':'
    derivative_ylabel:           str   = 'Tendencia [kg/sem]'
    derivative_zero_line_alpha:  float = 0.45
    derivative_zero_line_width:  float = 1.1
    derivative_zero_line_style:  str   = '--'

    # Resolución interna del trazado rojo.  Se limita para mantener el costo
    # del suavizado adaptativo bajo control aun cuando el eje X sea largo.
    smooth_dense_grid_min_points: int = 1400
    smooth_dense_grid_max_points: int = 3200
    smooth_dense_grid_per_day:    int = 12

    # Parámetro maestro de suavidad del ajuste rojo.
    #
    # Interpretación intencional:
    #   > 1.0  → más suave / más redondeada / menos pegada a los puntos
    #   = 1.0  → comportamiento base
    #   < 1.0  → más fiel a los puntos / mejor R² típico / menos suavizada
    #
    # Aun así, internamente se aplica un pequeño piso de suavidad visual para
    # evitar que aparezcan puntas visibles por bajar demasiado el parámetro.
    smooth_general_smoothness: float = DEFAULT_SMOOTHNESS
    smooth_general_smoothness_visual_floor: float = 0.45

    # Parámetros de la spline base (antes del mollifier gaussiano)
    smooth_duplicate_count_exponent: float = 0.35  # Influencia de la multiplicidad al estimar la densidad

    # Parámetros del perfil continuo de ancho gaussiano h(t)
    smooth_bandwidth_min_days:            float = 2.5
    smooth_bandwidth_max_days:            float = 32.0
    smooth_bandwidth_spacing_factor:      float = 2.40
    smooth_bandwidth_span_fraction:       float = 0.12
    smooth_bandwidth_density_exponent:    float = 0.60
    smooth_bandwidth_pilot_spacing_factor: float = 1.60
    smooth_bandwidth_pilot_span_fraction:  float = 0.05
    smooth_bandwidth_profile_sigma_fraction: float = 0.22
    smooth_bandwidth_profile_min_sigma:     float = 1.2
    smooth_bandwidth_profile_max_sigma:     float = 32.0
    smooth_bandwidth_profile_radius_sigmas: float = 3.0

    # Parámetros del mollifier gaussiano adaptativo aplicado a la spline base
    smooth_mollifier_radius_sigmas: float = 4.0
    smooth_mollifier_min_weight_sum: float = 1e-12

    # ── Puntos de datos ──
    point_size: float = 9.6     # Tamaño del marcador (scatter `s`) = 80% del radio anterior
    point_alpha: float = 0.85       # Opacidad de los puntos de datos reales
    point_line_width: float = 0.60  # Grosor de la línea naranja que puede unir las muestras
    point_line_alpha: float = 0.85  # Opacidad de la línea naranja fina

    # ── Anotación de R² ──
    r2_box_alpha: float = 0.78
    r2_box_facecolor: str = '#fff6f6'
    r2_box_edgecolor: str = '#d0a0a0'
    r2_text_color: str = '#7a2e2e'

    # ── Franjas de tolerancia alrededor de la curva ──
    #    Cada tupla es (±delta_kg, transparencia_alpha).
    #    Se dibujan de mayor a menor para que las internas queden encima.
    tolerance_bands: list = field(default_factory=lambda: [
        (6.0, 0.10),   # ±6 kg — franja más externa
        (4.0, 0.10),   # ±4 kg
        (2.0, 0.15),   # ±2 kg
        (1.0, 0.25),   # ±1 kg — franja más interna (más opaca)
    ])

    # ── Grilla ──
    grid_major_linewidth: float = 0.5    # Líneas verticales (eje X, mayor)
    grid_minor_linewidth: float = 0.35   # Líneas horizontales (eje Y, menor)

    # ── Márgenes del subplot (fracción de la figura) ──
    margin_left:   float = 0.055
    margin_right:  float = 0.995
    margin_top:    float = 0.93
    margin_bottom: float = 0.20

    # ── Etiquetas del gráfico ──
    plot_title:  str = "P(t)"
    plot_xlabel: str = "Día"
    plot_ylabel: str = "Masa [kg]"

    # ── Resolución del muestreo ──
    samples_per_unit_x: int = 100  # Densidad para la curva base Q(x)
    render_density:     int = 20   # Puntos por día al renderizar P(t)

    # ── Prefijo del archivo PNG de salida ──
    output_prefix: str = "grafico_masa_"


# ┌────────────────────────────────────────────────────────────────────────────┐
# │  1-F  MODELO MATEMÁTICO DE MASA — "LA FUNCIÓN"                           │
# │                                                                           │
# │  Esta zona contiene EXCLUSIVAMENTE los coeficientes del modelo.  La       │
# │  función de evaluación está en la Sección 6.  Para cambiar el modelo,     │
# │  editar los coeficientes aquí y la función evaluate_base_curve() allá.    │
# │                                                                           │
# │  Modelo actual                                                            │
# │  ──────────────                                                           │
# │  Sea g(x) = (x − a)(x − b) · exp(c₀ + c₁·x + c₂·x²)                   │
# │                                                                           │
# │  Se definen las integrales acumuladas:                                    │
# │    I₁(x) = ∫₀ˣ g(u) du                                                  │
# │    Iug(x) = ∫₀ˣ u·g(u) du                                               │
# │                                                                           │
# │  Entonces la curva base es:                                               │
# │    Q(x) = q₀ + q₁·x − q₂·(x·I₁(x) − Iug(x))                          │
# │                                                                           │
# │  Y la curva final P(t) se obtiene estirando el eje horizontal:           │
# │    P(t) = Q(t / stretch_factor)                para t ≤ curve_stop_t     │
# │    P(t) = Q(curve_stop_t / stretch_factor)     para t > curve_stop_t     │
# └────────────────────────────────────────────────────────────────────────────┘

@dataclass(frozen=True)
class MassModelCoefficients:
    """Coeficientes del modelo de masa corporal.

    Attributes:
        root_a, root_b:  Raíces del factor polinomial (x−a)(x−b).
        exp_c0, exp_c1, exp_c2:  Coeficientes del exponente c₀ + c₁·x + c₂·x².
        q0:  Valor basal de Q (masa inicial en x=0).
        q1:  Pendiente lineal.
        q2:  Peso de la corrección integral.
    """
    root_a: float = 2.87886533
    root_b: float = 37.42113010
    exp_c0: float = -2.56592494
    exp_c1: float = -0.576356728
    exp_c2: float = 0.0128775073
    q0:     float = 92.98
    q1:     float = 4.2980386358
    q2:     float = 1.06


# Instancia global por defecto — se usa en todo el programa.
MASS_MODEL = MassModelCoefficients()


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 2 — Log con colores ANSI (salida por terminal)                  ║
# ║                                                                          ║
# ║  Imprime mensajes etiquetados en stderr para no contaminar stdout.       ║
# ║  Cada nivel tiene un color distinto para facilitar la lectura rápida.    ║
# ╚════════════════════════════════════════════════════════════════════════════╝

class _Ansi:
    """Secuencias de escape ANSI para color y estilo en terminal."""
    RESET   = "\033[0m"
    BOLD    = "\033[1m"
    RED     = "\033[31m"
    GREEN   = "\033[32m"
    YELLOW  = "\033[33m"
    CYAN    = "\033[36m"
    MAGENTA = "\033[35m"


def _log(tag: str, color: str, message: str) -> None:
    """Imprime un mensaje con etiqueta coloreada en stderr."""
    print(f"{color}{_Ansi.BOLD}[{tag}]{_Ansi.RESET} {message}", file=sys.stderr)


def log_info(msg: str) -> None:
    """Mensaje informativo (cyan)."""
    _log("INFO", _Ansi.CYAN, msg)

def log_ok(msg: str) -> None:
    """Operación exitosa (verde)."""
    _log(" OK ", _Ansi.GREEN, msg)

def log_warn(msg: str) -> None:
    """Advertencia no bloqueante (amarillo)."""
    _log("WARN", _Ansi.YELLOW, msg)

def log_error(msg: str) -> None:
    """Error que impide continuar (rojo)."""
    _log("ERROR", _Ansi.RED, msg)

def log_fix(msg: str) -> None:
    """Corrección automática aplicada (magenta)."""
    _log(" FIX", _Ansi.MAGENTA, msg)


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 3 — Parseo y normalización de números                           ║
# ║                                                                          ║
# ║  Funciones puras que convierten cadenas con formatos ambiguos (comas,    ║
# ║  puntos, miles) en floats estándar de Python.                            ║
# ╚════════════════════════════════════════════════════════════════════════════╝

def normalize_decimal_string(text: str) -> str:
    """Normaliza una cadena numérica a formato Python (punto decimal).

    Reglas de desambiguación cuando hay punto Y coma:
      - El separador que aparece *último* se interpreta como decimal.
      - El otro se descarta (asumido separador de miles).

    Ejemplos:
      "69,9"     →  "69.9"    (coma es decimal)
      "1.234,5"  →  "1234.5"  (punto = miles, coma = decimal)
      "1,234.5"  →  "1234.5"  (coma = miles, punto = decimal)
      "100"      →  "100"     (sin cambios)
    """
    s = text.strip()
    has_dot   = "." in s
    has_comma = "," in s

    if has_dot and has_comma:
        # Ambos presentes: el que aparece más a la derecha es el decimal
        if s.rfind(",") > s.rfind("."):
            s = s.replace(".", "").replace(",", ".")  # "1.234,5" → "1234.5"
        else:
            s = s.replace(",", "")                    # "1,234.5" → "1234.5"
    elif has_comma:
        s = s.replace(",", ".")                       # "69,9"   → "69.9"

    return s


def parse_strict_number(text: str) -> float:
    """Convierte texto a float, rechazando espacios internos.

    Un espacio dentro del número (ej. "9 8") lo invalida, ya que es ambiguo.
    Los espacios en los extremos sí se toleran y se eliminan.
    """
    stripped = text.strip()
    if " " in stripped:
        raise ValueError(f"espacio dentro del número: {text!r}")

    value = float(normalize_decimal_string(stripped))

    if not math.isfinite(value):
        raise ValueError(f"valor no finito: {text!r}")

    return value


def parse_day_value(text: str) -> float:
    """Parsea la primera columna del CSV (día fraccionario)."""
    return parse_strict_number(text)


def parse_mass_value(text: str) -> float:
    """Parsea la segunda columna del CSV (masa en kg)."""
    value = parse_strict_number(text)
    if not (MIN_MASS <= value <= MAX_MASS):
        raise ValueError(
            f"masa fuera de rango [{MIN_MASS}, {MAX_MASS}]: {value}"
        )
    return value


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 4 — Gestión del CSV (lectura, validación, escritura)            ║
# ║                                                                          ║
# ║  El CSV se lee completo en memoria, se valida fila por fila, se ordena   ║
# ║  cronológicamente y se reescribe.  Las filas inválidas se preservan al   ║
# ║  final del archivo.                                                      ║
# ╚════════════════════════════════════════════════════════════════════════════╝

@dataclass
class CsvRow:
    """Una fila del CSV, válida o no."""
    raw:         str
    day:         float | None
    mass:        float | None
    extra:       str
    valid:       bool = True
    line_number: int  = 0


def _looks_like_header(cells: list[str]) -> bool:
    """Determina si una fila del CSV es un encabezado (no datos)."""
    if len(cells) < 2:
        return True
    try:
        parse_day_value(cells[0])
        parse_mass_value(cells[1])
        return False
    except (ValueError, TypeError):
        return True


def _parse_csv_row(cells: list[str], line_number: int) -> CsvRow:
    """Parsea una lista de celdas en un CsvRow."""
    raw   = CSV_SEPARATOR.join(cells)
    extra = CSV_SEPARATOR.join(cells[2:]) if len(cells) > 2 else ""
    invalid_row = CsvRow(
        raw=raw, day=None, mass=None, extra=extra,
        valid=False, line_number=line_number,
    )

    if len(cells) < 2:
        log_warn(f"Línea {line_number}: faltan columnas → inválida. "
                 f"Contenido={raw!r}")
        return invalid_row

    try:
        day_val = parse_day_value(cells[0])
    except (ValueError, TypeError) as exc:
        log_warn(f"Línea {line_number}: día inválido ({exc}). "
                 f"Contenido={raw!r}")
        return invalid_row

    try:
        mass_val = parse_mass_value(cells[1])
    except (ValueError, TypeError) as exc:
        log_warn(f"Línea {line_number}: masa inválida ({exc}). "
                 f"Contenido={raw!r}")
        return invalid_row

    return CsvRow(
        raw=raw, day=day_val, mass=mass_val, extra=extra,
        valid=True, line_number=line_number,
    )


def load_csv(csv_path: Path) -> list[CsvRow]:
    """Lee un archivo CSV y devuelve la lista de filas parseadas."""
    if not csv_path.exists():
        log_info(f"El CSV {csv_path} no existe; se creará al guardar.")
        return []

    with csv_path.open("r", encoding=CSV_ENCODING_READ) as f:
        raw_lines = f.readlines()

    non_empty = [line.rstrip("\n\r") for line in raw_lines if line.strip()]
    if not non_empty:
        log_warn("El CSV está vacío.")
        return []

    all_rows = list(csv.reader(non_empty, delimiter=CSV_SEPARATOR))

    start = 0
    if all_rows and _looks_like_header(all_rows[0]):
        log_info("Cabecera detectada en el CSV; se omitirá.")
        start = 1

    rows: list[CsvRow] = []
    for i, cells in enumerate(all_rows[start:], start=start + 1):
        cleaned_cells = [c.strip() for c in cells]
        rows.append(_parse_csv_row(cleaned_cells, line_number=i))

    valid_count   = sum(r.valid for r in rows)
    invalid_count = len(rows) - valid_count
    log_ok(f"CSV leído: {valid_count} válido(s), {invalid_count} inválido(s).")
    return rows


def sort_and_fix_rows(rows: list[CsvRow]) -> list[CsvRow]:
    """Ordena filas válidas por día ascendente; inválidas al final."""
    valid:   list[CsvRow] = []
    invalid: list[CsvRow] = []

    for row in rows:
        if row.valid and row.day is not None:
            rounded = round(row.day, DAY_FRACTION_DECIMALS)
            if rounded != row.day:
                log_fix(f"Día {row.day} redondeado a {rounded} "
                        f"({DAY_FRACTION_DECIMALS} decimales).")
            row.day = rounded
            valid.append(row)
        else:
            invalid.append(row)

    valid.sort(key=lambda r: r.day)  # type: ignore[arg-type]

    if invalid:
        log_fix(f"{len(invalid)} fila(s) inválida(s) → fin del CSV.")

    return valid + invalid


def _format_number(value: float, max_decimals: int) -> str:
    """Formatea un float con hasta *max_decimals* posiciones, sin trailing zeros."""
    s = f"{value:.{max_decimals}f}"
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return s


def write_csv(csv_path: Path, rows: list[CsvRow]) -> None:
    """Escribe el CSV completo con header y filas ordenadas."""
    lines: list[str] = [CSV_HEADER + "\n"]

    for row in rows:
        if row.valid and row.day is not None and row.mass is not None:
            day_str  = _format_number(row.day,  DAY_FRACTION_DECIMALS)
            mass_str = _format_number(row.mass, MASS_FORMAT_DECIMALS)
            parts = [day_str, mass_str]
            if row.extra:
                parts.append(row.extra)
            lines.append(CSV_SEPARATOR.join(parts) + "\n")
        else:
            lines.append(row.raw + "\n")

    with csv_path.open("w", encoding=CSV_ENCODING, newline="") as f:
        f.writelines(lines)

    log_ok(f"CSV guardado: {csv_path}  ({len(rows)} fila(s) de datos).")


def ensure_csv_exists(csv_path: Path) -> None:
    """Crea el archivo CSV con encabezado si no existe."""
    if csv_path.exists():
        return
    log_info(f"Creando CSV nuevo: {csv_path}")
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", encoding=CSV_ENCODING) as f:
        f.write(CSV_HEADER + "\n")
    log_ok(f"CSV creado con header: {CSV_HEADER}")


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 5 — Utilidades de fecha y tiempo                                ║
# ╚════════════════════════════════════════════════════════════════════════════╝

def fractional_day(dt: datetime, start: date) -> float:
    """Calcula el día fraccionario de *dt* respecto a *start* a las 00:00."""
    origin = datetime(start.year, start.month, start.day)
    delta_seconds = (dt - origin).total_seconds()
    return round(delta_seconds / SECONDS_PER_DAY, DAY_FRACTION_DECIMALS)


def max_day_in_month(month: int, year: int) -> int:
    """Devuelve cuántos días tiene un mes en un año dado."""
    if 1 <= month <= 12 and MIN_YEAR <= year <= MAX_YEAR:
        return calendar.monthrange(year, month)[1]
    return MAX_DAY


def build_date_formatter(start_date: date):
    """Devuelve un formateador para matplotlib que traduce días a ``Mes-DD``."""
    def formatter(day_value: float, _pos=None) -> str:
        d = start_date + timedelta(days=int(round(day_value)))
        return f"{MONTHS_ES_ABBR[d.month - 1]}-{d.day:02d}"
    return formatter


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 6 — MODELO MATEMÁTICO DE MASA  (la función)                     ║
# ║                                                                          ║
# ║  Esta sección contiene la evaluación numérica del modelo cuyos           ║
# ║  coeficientes están en MassModelCoefficients (Sección 1-F).              ║
# ║                                                                          ║
# ║  Para cambiar la función:                                                ║
# ║    1. Ajustar los coeficientes en MassModelCoefficients.                 ║
# ║    2. Modificar evaluate_base_curve() si la forma funcional cambia.      ║
# ║    3. evaluate_piecewise_curve() gestiona el tramo constante y no        ║
# ║       necesita cambios salvo que se quiera otro comportamiento fuera     ║
# ║       del dominio.                                                       ║
# ╚════════════════════════════════════════════════════════════════════════════╝

def _cumulative_trapezoid(y: np.ndarray, x: np.ndarray) -> np.ndarray:
    """Integral acumulada por la regla del trapecio.

    Devuelve un arreglo del mismo largo que *x*, con valor inicial 0.
    """
    dx  = np.diff(x)
    avg = 0.5 * (y[1:] + y[:-1])
    out = np.empty_like(x, dtype=float)
    out[0]  = 0.0
    out[1:] = np.cumsum(dx * avg)
    return out


def evaluate_base_curve(
    config: PlotConfig,
    model: MassModelCoefficients = MASS_MODEL,
) -> tuple[np.ndarray, np.ndarray]:
    """Evalúa Q(x) — la curva base antes del estiramiento horizontal.

    Construye un muestreo denso de x en [0, curve_stop_t/stretch_factor] y
    calcula:

      g(x)   = (x − a)(x − b) · exp(c₀ + c₁·x + c₂·x²)
      I₁(x)  = ∫₀ˣ g(u) du
      Iug(x) = ∫₀ˣ u·g(u) du
      Q(x)   = q₀ + q₁·x − q₂·(x·I₁(x) − Iug(x))
    """
    x_end    = config.curve_stop_t / config.stretch_factor
    n_points = max(2, int(math.ceil(x_end * config.samples_per_unit_x)) + 1)
    x = np.linspace(0.0, x_end, n_points)

    # g(x) = (x − a)(x − b) · exp(c₀ + c₁·x + c₂·x²)
    g = (
        (x - model.root_a)
        * (x - model.root_b)
        * np.exp(model.exp_c0 + model.exp_c1 * x + model.exp_c2 * x * x)
    )

    # Integrales acumuladas
    integral_g  = _cumulative_trapezoid(g, x)      # I₁(x)
    integral_xg = _cumulative_trapezoid(x * g, x)  # Iug(x)

    # Q(x) = q₀ + q₁·x − q₂·(x·I₁(x) − Iug(x))
    q = (
        model.q0
        + model.q1 * x
        - model.q2 * (x * integral_g - integral_xg)
    )

    return x, q


def evaluate_piecewise_curve(
    t: np.ndarray,
    x_base: np.ndarray,
    q_base: np.ndarray,
    config: PlotConfig,
) -> np.ndarray:
    """Evalúa P(t) — curva por tramos en el dominio temporal.

    - Para curve_start_t ≤ t ≤ curve_stop_t:  P(t) = Q(t / stretch_factor)
    - Para curve_stop_t < t ≤ x_max:  P(t) = P(curve_stop_t)  (constante)
    - Fuera de ese rango: NaN.
    """
    y = np.full_like(t, np.nan, dtype=float)

    # Valor constante en el tramo plano
    x_stop = config.curve_stop_t / config.stretch_factor
    p_stop = float(np.interp(x_stop, x_base, q_base))

    # Tramo curvo: interpolar Q(t / stretch_factor)
    mask_curve = (t >= config.curve_start_t) & (t <= config.curve_stop_t)
    if np.any(mask_curve):
        y[mask_curve] = np.interp(
            t[mask_curve] / config.stretch_factor, x_base, q_base
        )

    # Tramo plano: valor congelado
    mask_flat = (t > config.curve_stop_t) & (t <= config.x_max)
    if np.any(mask_flat):
        y[mask_flat] = p_stop

    return y


def _sorted_point_arrays(points: list[tuple[float, float]]) -> tuple[np.ndarray, np.ndarray]:
    """Ordena los puntos cronológicamente y devuelve arreglos NumPy."""
    ordered = sorted(points, key=lambda pair: pair[0])
    x = np.asarray([pair[0] for pair in ordered], dtype=float)
    y = np.asarray([pair[1] for pair in ordered], dtype=float)
    return x, y


def _aggregate_points_by_x(
    x_data: np.ndarray,
    y_data: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Agrupa observaciones que comparten exactamente el mismo valor de ``x``.

    Para cada ``x`` único se calcula la media de sus ``y`` asociados y la
    cantidad de observaciones originales que cayeron ahí.
    """
    unique_x, inverse, counts = np.unique(
        x_data,
        return_inverse=True,
        return_counts=True,
    )

    weighted_sum = np.zeros_like(unique_x, dtype=float)
    np.add.at(weighted_sum, inverse, y_data)
    y_mean = weighted_sum / counts.astype(float)

    return unique_x.astype(float), y_mean.astype(float), counts.astype(float)


def _build_natural_cubic_spline_second_derivatives(
    x_values: np.ndarray,
    y_values: np.ndarray,
) -> np.ndarray:
    """Calcula las segundas derivadas de una spline cúbica natural 1D."""
    n = int(x_values.size)
    if n <= 2:
        return np.zeros(n, dtype=float)

    h = np.diff(x_values)
    if np.any(h <= 0.0):
        raise ValueError('Los nodos x de la spline deben ser estrictamente crecientes.')

    a = h[:-1].copy()
    b = 2.0 * (h[:-1] + h[1:])
    c = h[1:].copy()
    d = 6.0 * (
        (y_values[2:] - y_values[1:-1]) / h[1:]
        - (y_values[1:-1] - y_values[:-2]) / h[:-1]
    )

    # Algoritmo de Thomas para la matriz tridiagonal de los nodos internos
    c_prime = np.zeros_like(c)
    d_prime = np.zeros_like(d)
    c_prime[0] = c[0] / b[0]
    d_prime[0] = d[0] / b[0]
    for i in range(1, d.size):
        denom = b[i] - a[i - 1] * c_prime[i - 1]
        if i < c.size:
            c_prime[i] = c[i] / denom
        d_prime[i] = (d[i] - a[i - 1] * d_prime[i - 1]) / denom

    internal = np.zeros(d.size, dtype=float)
    internal[-1] = d_prime[-1]
    for i in range(d.size - 2, -1, -1):
        internal[i] = d_prime[i] - c_prime[i] * internal[i + 1]

    second = np.zeros(n, dtype=float)
    second[1:-1] = internal
    return second


def _evaluate_natural_cubic_spline(
    x_nodes: np.ndarray,
    y_nodes: np.ndarray,
    second: np.ndarray,
    x_eval: np.ndarray,
) -> np.ndarray:
    """Evalúa una spline cúbica natural en una malla arbitraria."""
    if x_nodes.size == 1:
        return np.full_like(x_eval, y_nodes[0], dtype=float)
    if x_nodes.size == 2:
        return np.interp(x_eval, x_nodes, y_nodes).astype(float)

    indices = np.searchsorted(x_nodes, x_eval, side='right') - 1
    indices = np.clip(indices, 0, x_nodes.size - 2)

    x_left = x_nodes[indices]
    x_right = x_nodes[indices + 1]
    y_left = y_nodes[indices]
    y_right = y_nodes[indices + 1]
    m_left = second[indices]
    m_right = second[indices + 1]
    h = x_right - x_left
    a = (x_right - x_eval) / h
    b = (x_eval - x_left) / h

    return (
        a * y_left
        + b * y_right
        + ((a**3 - a) * m_left + (b**3 - b) * m_right) * (h**2) / 6.0
    )


def _gaussian_kernel_1d(sigma_samples: float, radius_sigmas: float) -> np.ndarray:
    """Construye un kernel gaussiano discreto 1D normalizado."""
    safe_sigma = max(float(sigma_samples), 1e-9)
    safe_radius_sigmas = max(float(radius_sigmas), 1.0)
    radius = max(1, int(math.ceil(safe_sigma * safe_radius_sigmas)))
    grid = np.arange(-radius, radius + 1, dtype=float)
    kernel = np.exp(-0.5 * (grid / safe_sigma) ** 2)
    kernel_sum = float(np.sum(kernel))
    if kernel_sum <= 0.0:
        kernel[radius] = 1.0
        kernel_sum = 1.0
    return kernel / kernel_sum


def _apply_gaussian_post_smoothing(
    values: np.ndarray,
    sigma_samples: float,
    radius_sigmas: float,
) -> np.ndarray:
    """Suaviza una serie densa mediante convolución gaussiana 1D."""
    if values.size <= 2:
        return values.copy()

    kernel = _gaussian_kernel_1d(sigma_samples, radius_sigmas)
    radius = kernel.size // 2
    padded = np.pad(values, pad_width=radius, mode='edge')
    return np.convolve(padded, kernel, mode='valid')


def _select_dense_grid_size(
    x_start: float,
    x_end: float,
    config: PlotConfig,
) -> int:
    """Devuelve el tamaño de la malla densa usada por la curva roja."""
    span = max(0.0, float(x_end - x_start))
    proposed = int(math.ceil(span * float(config.smooth_dense_grid_per_day))) + 1
    proposed = max(int(config.smooth_dense_grid_min_points), proposed)
    proposed = min(int(config.smooth_dense_grid_max_points), proposed)
    return max(3, proposed)


def _effective_general_smoothness(config: PlotConfig) -> float:
    """Devuelve la suavidad efectiva del ajuste rojo.

    El usuario controla la curva mediante ``smooth_general_smoothness``.
    Internamente se aplica un piso visual suave para evitar esquinas o puntas
    demasiado marcadas cuando el valor es muy bajo.
    """
    requested = max(float(config.smooth_general_smoothness), 1e-6)
    visual_floor = max(float(config.smooth_general_smoothness_visual_floor), 1e-6)
    return max(requested, visual_floor)


def _compute_pilot_density(
    x_grid: np.ndarray,
    x_unique: np.ndarray,
    counts: np.ndarray,
    config: PlotConfig,
) -> tuple[np.ndarray, float]:
    """Estima una densidad piloto gaussiana continua sobre la malla densa."""
    span = float(x_unique[-1] - x_unique[0]) if x_unique.size >= 2 else 0.0
    if x_unique.size >= 2:
        spacings = np.diff(x_unique)
        median_spacing = float(np.median(spacings))
    else:
        median_spacing = max(span, 1.0)

    pilot_sigma = max(
        float(config.smooth_bandwidth_min_days),
        median_spacing * float(config.smooth_bandwidth_pilot_spacing_factor),
        span * float(config.smooth_bandwidth_pilot_span_fraction),
        1e-9,
    )

    normalized = (x_grid[:, None] - x_unique[None, :]) / pilot_sigma
    multiplicity = counts ** float(config.smooth_duplicate_count_exponent)
    density = np.exp(-0.5 * normalized * normalized) @ multiplicity
    density = np.maximum(density, 1e-12)
    return density.astype(float), float(pilot_sigma)


def _build_continuous_bandwidth_profile(
    x_grid: np.ndarray,
    x_unique: np.ndarray,
    counts: np.ndarray,
    config: PlotConfig,
) -> np.ndarray:
    """Construye un perfil continuo ``h(t)`` para el mollifier gaussiano."""
    density, _pilot_sigma = _compute_pilot_density(x_grid, x_unique, counts, config)
    median_density = float(np.median(density))
    span = float(x_unique[-1] - x_unique[0]) if x_unique.size >= 2 else 0.0
    if x_unique.size >= 2:
        spacings = np.diff(x_unique)
        median_spacing = float(np.median(spacings))
    else:
        median_spacing = max(span, 1.0)

    base_bandwidth = max(
        float(config.smooth_bandwidth_min_days),
        median_spacing * float(config.smooth_bandwidth_spacing_factor),
        span * float(config.smooth_bandwidth_span_fraction),
        1e-9,
    )
    effective_smoothness = _effective_general_smoothness(config)
    base_bandwidth *= effective_smoothness

    density_ratio = median_density / density
    raw_bandwidth = base_bandwidth * (density_ratio ** float(config.smooth_bandwidth_density_exponent))
    raw_bandwidth = np.clip(
        raw_bandwidth,
        float(config.smooth_bandwidth_min_days),
        float(config.smooth_bandwidth_max_days),
    )

    if x_grid.size >= 3:
        dx_grid = float(np.mean(np.diff(x_grid)))
        if dx_grid > 0.0:
            sigma_samples = (
                float(np.mean(raw_bandwidth))
                * float(config.smooth_bandwidth_profile_sigma_fraction)
                * math.sqrt(effective_smoothness)
                / dx_grid
            )
            sigma_samples = min(
                max(float(config.smooth_bandwidth_profile_min_sigma), sigma_samples),
                float(config.smooth_bandwidth_profile_max_sigma),
            )
            raw_bandwidth = _apply_gaussian_post_smoothing(
                raw_bandwidth,
                sigma_samples=sigma_samples,
                radius_sigmas=float(config.smooth_bandwidth_profile_radius_sigmas),
            )

    return np.clip(
        raw_bandwidth,
        float(config.smooth_bandwidth_min_days),
        float(config.smooth_bandwidth_max_days),
    ).astype(float)


def _apply_adaptive_gaussian_mollifier(
    x_grid: np.ndarray,
    y_base: np.ndarray,
    bandwidth_profile: np.ndarray,
    config: PlotConfig,
) -> np.ndarray:
    """Aplica una convolución gaussiana adaptativa sobre una malla uniforme."""
    n = int(x_grid.size)
    if n <= 2:
        return y_base.copy()

    result = np.empty_like(y_base, dtype=float)
    dx_grid = float(np.mean(np.diff(x_grid)))
    if dx_grid <= 0.0:
        return y_base.copy()

    radius_sigmas = max(float(config.smooth_mollifier_radius_sigmas), 1.0)
    min_weight_sum = float(config.smooth_mollifier_min_weight_sum)

    for index in range(n):
        x_center = float(x_grid[index])
        h = max(float(bandwidth_profile[index]), 1e-9)
        radius = radius_sigmas * h
        left = int(np.searchsorted(x_grid, x_center - radius, side='left'))
        right = int(np.searchsorted(x_grid, x_center + radius, side='right'))
        local_x = x_grid[left:right]
        local_y = y_base[left:right]
        normalized = (local_x - x_center) / h
        weights = np.exp(-0.5 * normalized * normalized)
        weight_sum = float(np.sum(weights))
        if weight_sum <= min_weight_sum:
            result[index] = y_base[index]
        else:
            result[index] = float(np.dot(weights, local_y) / weight_sum)

    return result


def build_smooth_adjustment_curve(
    points: list[tuple[float, float]],
    config: PlotConfig,
) -> tuple[np.ndarray, np.ndarray, float, float, float] | None:
    """Construye la curva roja con spline natural + mollifier gaussiano adaptativo.

    Procedimiento:
      1. ordenar los puntos válidos y agrupar los x duplicados por promedio;
      2. construir una spline cúbica natural sobre los x únicos;
      3. evaluarla en una malla densa y uniforme;
      4. calcular un perfil continuo de anchos gaussianos h(t);
      5. convolucionar la spline base con una gaussiana adaptativa.
    """
    if len(points) < config.smooth_min_points:
        return None

    x_data, y_data = _sorted_point_arrays(points)
    if x_data.size < config.smooth_min_points:
        return None

    x_unique, y_mean, counts = _aggregate_points_by_x(x_data, y_data)
    if x_unique.size < 2:
        return None

    x_start = float(x_unique[0])
    x_end = float(x_unique[-1])
    if not math.isfinite(x_start) or not math.isfinite(x_end) or x_end <= x_start:
        return None

    dense_size = _select_dense_grid_size(x_start, x_end, config)
    x_grid = np.linspace(x_start, x_end, dense_size)

    second = _build_natural_cubic_spline_second_derivatives(x_unique, y_mean)
    y_base = _evaluate_natural_cubic_spline(x_unique, y_mean, second, x_grid)

    bandwidth_profile = _build_continuous_bandwidth_profile(
        x_grid=x_grid,
        x_unique=x_unique,
        counts=counts,
        config=config,
    )
    y_smooth = _apply_adaptive_gaussian_mollifier(
        x_grid=x_grid,
        y_base=y_base,
        bandwidth_profile=bandwidth_profile,
        config=config,
    )

    if x_grid.size >= 3:
        dx_grid = float(np.mean(np.diff(x_grid)))
        if dx_grid > 0.0:
            effective_smoothness = _effective_general_smoothness(config)
            sigma_samples = (
                float(np.mean(bandwidth_profile))
                * 0.10
                * (effective_smoothness ** 0.85)
                / dx_grid
            )
            sigma_samples = min(max(0.75, sigma_samples), 18.0)
            y_smooth = _apply_gaussian_post_smoothing(
                y_smooth,
                sigma_samples=sigma_samples,
                radius_sigmas=3.0,
            )

    y_fitted_at_data = np.interp(x_data, x_grid, y_smooth)
    ss_res = float(np.sum((y_data - y_fitted_at_data) ** 2))
    ss_tot = float(np.sum((y_data - np.mean(y_data)) ** 2))
    r2 = 1.0 if ss_tot <= 0.0 else 1.0 - (ss_res / ss_tot)

    return (
        x_grid.astype(float),
        y_smooth.astype(float),
        float(r2),
        float(np.min(bandwidth_profile)),
        float(np.max(bandwidth_profile)),
    )



def build_adjustment_derivative_curve(
    x_smooth: np.ndarray,
    y_smooth: np.ndarray,
    config: PlotConfig,
) -> tuple[np.ndarray, np.ndarray] | None:
    """Calcula la diferencial de la curva roja en kg/sem sobre una malla regular.

    Se remuestrea la curva de ajuste cada ``derivative_sample_step_days`` días
    y luego se deriva numéricamente con ``np.gradient``.  El resultado se
    expresa en kg/semana multiplicando por 7.
    """
    if x_smooth.size < 2 or y_smooth.size < 2:
        return None

    x_start = float(x_smooth[0])
    x_end = float(x_smooth[-1])
    if not math.isfinite(x_start) or not math.isfinite(x_end) or x_end <= x_start:
        return None

    step = max(float(config.derivative_sample_step_days), 1e-9)
    x_derivative = np.arange(x_start, x_end + step * 0.5, step, dtype=float)
    if x_derivative.size < 2:
        x_derivative = np.array([x_start, x_end], dtype=float)
    elif x_derivative[-1] < x_end:
        x_derivative = np.append(x_derivative, x_end)

    y_resampled = np.interp(x_derivative, x_smooth, y_smooth).astype(float)
    dy_per_day = np.gradient(y_resampled, x_derivative)
    dy_per_week = dy_per_day * 7.0
    return x_derivative.astype(float), dy_per_week.astype(float)


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 7 — Generación del gráfico (matplotlib)                         ║
# ╚════════════════════════════════════════════════════════════════════════════╝

@dataclass(frozen=True)
class PlotRenderOptions:
    """Opciones de visibilidad para la página de vista previa y el PNG final."""

    show_adjustment: bool = True
    show_adjustment_derivative: bool = False
    show_points: bool = True
    show_point_lines: bool = False


def format_smoothness_token(value: float, decimals: int = 2) -> str:
    """Formatea la suavidad para incluirla de forma estable en el nombre del PNG."""
    return f'{float(value):.{decimals}f}'


def load_app_icon() -> QIcon:
    """Construye el ícono de la aplicación a partir del PNG embebido en base64."""
    icon_bytes = base64.b64decode(APP_ICON_PNG_BASE64)
    pixmap = QPixmap()
    if not pixmap.loadFromData(icon_bytes, 'PNG'):
        return QIcon()
    return QIcon(pixmap)


def build_output_image_filename(
    config: PlotConfig,
    timestamp_dt: datetime | None = None,
) -> str:
    """Construye el nombre del PNG usando el timestamp indicado y la suavidad."""
    effective_timestamp = timestamp_dt or datetime.now()
    timestamp_str = effective_timestamp.strftime(OUTPUT_FILENAME_TIMESTAMP_FORMAT)
    smooth_token = format_smoothness_token(config.smooth_general_smoothness)
    return f"{config.output_prefix}{timestamp_str}_smoothness_{smooth_token}.png"


def build_output_image_path(
    output_dir: Path,
    config: PlotConfig,
    timestamp_dt: datetime | None = None,
) -> Path:
    """Construye la ruta final del PNG usando el timestamp indicado."""
    return output_dir / build_output_image_filename(config, timestamp_dt=timestamp_dt)


def render_plot(
    points: list[tuple[float, float]],
    config: PlotConfig,
    output_dir: Path | None = None,
    output_path: Path | None = None,
    render_options: PlotRenderOptions | None = None,
    output_timestamp_dt: datetime | None = None,
) -> Path:
    """Genera la imagen PNG del gráfico masa-vs-tiempo."""
    if render_options is None:
        render_options = PlotRenderOptions()

    ordered_points = sorted(points, key=lambda pair: pair[0])

    smooth_curve = None
    needs_smooth_curve = bool(
        render_options.show_adjustment or render_options.show_adjustment_derivative
    )
    if needs_smooth_curve:
        smooth_curve = build_smooth_adjustment_curve(ordered_points, config)
        if smooth_curve is not None:
            _, _, smooth_r2, smooth_bandwidth_min, smooth_bandwidth_max = smooth_curve
            log_info(
                'Curva roja: spline+mollifier gaussiano adaptativo '
                f'(suavidad={config.smooth_general_smoothness:.3f}, '
                f'h_min={smooth_bandwidth_min:.3f} días, '
                f'h_max={smooth_bandwidth_max:.3f} días).'
            )
        elif ordered_points:
            log_info('Curva roja omitida: puntos insuficientes para calcular el ajuste suave.')

    # ── Crear la figura ──
    fig_w = config.width_px / config.dpi
    fig_h = config.height_px / config.dpi
    fig, ax = plt.subplots(figsize=(fig_w, fig_h), dpi=config.dpi)

    # ── Línea guía horizontal ──
    ax.hlines(
        y=config.central_line_y,
        xmin=config.x_min, xmax=config.x_max,
        colors=config.color_guide_line,
        linestyles=config.central_line_style,
        linewidth=config.central_line_width,
        alpha=config.central_line_alpha,
        zorder=0,
    )

    curve_zorder = 1.5

    visible = [
        (d, m) for d, m in ordered_points
        if config.x_min <= d <= config.x_max and config.y_min <= m <= config.y_max
    ]
    hidden_count = len(ordered_points) - len(visible)
    if hidden_count > 0:
        log_info(f'{hidden_count} punto(s) fuera de los márgenes del gráfico.')

    # ── Línea naranja fina uniendo puntos ──
    if render_options.show_point_lines and len(visible) >= 2:
        ax.plot(
            [p[0] for p in visible],
            [p[1] for p in visible],
            color=config.color_points,
            linewidth=config.point_line_width,
            alpha=config.point_line_alpha,
            zorder=curve_zorder + 0.90,
        )

    # ── Franjas y curva roja ──
    if render_options.show_adjustment and smooth_curve is not None:
        x_smooth, y_smooth, smooth_r2, _hmin, _hmax = smooth_curve
        smooth_band_zorder = curve_zorder + 0.25
        smooth_line_zorder = curve_zorder + 0.50

        ax.fill_between(
            x_smooth,
            y_smooth - config.smooth_outer_band_half_width,
            y_smooth + config.smooth_outer_band_half_width,
            color=config.color_smooth_curve,
            alpha=config.smooth_outer_band_alpha,
            linewidth=0.0,
            edgecolor='none',
            zorder=smooth_band_zorder,
        )
        ax.fill_between(
            x_smooth,
            y_smooth - config.smooth_band_half_width,
            y_smooth + config.smooth_band_half_width,
            color=config.color_smooth_curve,
            alpha=config.smooth_band_alpha,
            linewidth=0.0,
            edgecolor='none',
            zorder=smooth_band_zorder + 0.05,
        )
        ax.plot(
            x_smooth,
            y_smooth,
            color=config.color_smooth_curve,
            alpha=config.smooth_curve_alpha,
            linewidth=(config.curve_linewidth * config.smooth_curve_linewidth_ratio),
            zorder=smooth_line_zorder,
        )

    # ── Diferencial de la curva roja (eje derecho, kg/sem) ──
    if render_options.show_adjustment_derivative and smooth_curve is not None:
        x_smooth, y_smooth, _smooth_r2, _hmin, _hmax = smooth_curve
        derivative_curve = build_adjustment_derivative_curve(x_smooth, y_smooth, config)
        if derivative_curve is not None:
            x_derivative, y_derivative = derivative_curve
            ax_right = ax.twinx()
            ax_right.set_xlim(config.x_min, config.x_max)
            ax_right.set_ylim(config.derivative_y_min, config.derivative_y_max)
            ax_right.set_ylabel(config.derivative_ylabel)
            ax_right.yaxis.set_major_locator(MultipleLocator(config.derivative_y_tick_step))
            ax_right.patch.set_alpha(0.0)
            ax_right.hlines(
                y=0.0,
                xmin=config.curve_start_t,
                xmax=config.curve_stop_t,
                colors=config.color_smooth_curve,
                linestyles=config.derivative_zero_line_style,
                linewidth=config.derivative_zero_line_width,
                alpha=config.derivative_zero_line_alpha,
                zorder=smooth_line_zorder + 0.05 if 'smooth_line_zorder' in locals() else curve_zorder + 0.55,
            )
            ax_right.plot(
                x_derivative,
                y_derivative,
                color=config.color_smooth_curve,
                alpha=config.derivative_line_alpha,
                linewidth=(config.curve_linewidth * config.derivative_linewidth_ratio),
                linestyle=config.derivative_line_style,
                zorder=smooth_line_zorder + 0.10 if 'smooth_line_zorder' in locals() else curve_zorder + 0.60,
            )

    # ── Puntos ──
    if render_options.show_points and visible:
        ax.scatter(
            [p[0] for p in visible],
            [p[1] for p in visible],
            s=config.point_size,
            color=config.color_points,
            alpha=config.point_alpha,
            edgecolors='none',
            marker='o',
            clip_on=True,
            zorder=curve_zorder + 1,
        )

    # ── Ejes y etiquetas ──
    ax.set_title(config.plot_title)
    ax.set_xlabel(config.plot_xlabel)
    ax.set_ylabel(config.plot_ylabel)
    ax.set_xlim(config.x_min, config.x_max)
    ax.set_ylim(config.y_min, config.y_max)

    ax.xaxis.set_major_locator(MultipleLocator(config.x_label_step_days))
    ax.xaxis.set_major_formatter(FuncFormatter(build_date_formatter(config.start_date)))
    ax.yaxis.set_major_locator(MultipleLocator(config.y_label_step))
    ax.yaxis.set_minor_locator(MultipleLocator(config.y_grid_step))

    ax.grid(True, which='major', axis='x', linewidth=config.grid_major_linewidth)
    ax.grid(True, which='minor', axis='y', linewidth=config.grid_minor_linewidth)

    for label in ax.get_xticklabels():
        label.set_rotation(90)

    right_margin = config.margin_right
    if render_options.show_adjustment_derivative:
        right_margin = min(right_margin, 0.955)

    fig.subplots_adjust(
        left=config.margin_left, right=right_margin,
        top=config.margin_top, bottom=config.margin_bottom,
    )

    # ── Guardar ──
    if output_path is None:
        if output_dir is None:
            raise ValueError('Se requiere output_dir u output_path para guardar el PNG.')
        output_path = build_output_image_path(
            output_dir,
            config,
            timestamp_dt=output_timestamp_dt,
        )
    else:
        output_path = Path(output_path)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=config.dpi)
    plt.close(fig)

    return output_path


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 8 — Interfaz gráfica (PyQt6)                                    ║
# ║                                                                          ║
# ║  Una sola clase construye la ventana, valida la entrada, guarda el dato  ║
# ║  en el CSV y dispara la generación de la imagen.  La ventana tiene dos   ║
# ║  "páginas": entrada de datos y vista previa del gráfico.                 ║
# ║                                                                          ║
# ║  Decisiones de diseño:                                                   ║
# ║    - La ventana NO usa stylesheets; cada widget se deja con el look      ║
# ║      nativo del estilo por defecto de PyQt6 en el sistema.               ║
# ║    - La ventana es no redimensionable por el usuario.                    ║
# ║    - La ventana se centra en la pantalla primaria reportada por Qt       ║
# ║      (independiente de Xorg/Wayland/Windows/macOS).                      ║
# ║    - El cierre desde la página de vista previa dispara un diálogo modal  ║
# ║      "¿Descartar el gráfico generado?" que bloquea la interacción.       ║
# ╚════════════════════════════════════════════════════════════════════════════╝

# ────────────────────────────────────────────────────────────────────────────
#  8-A  Validadores personalizados
#
#  Replican el comportamiento de ``validate='key'`` de Tkinter: una pulsación
#  que llevaría el campo a un estado inválido es rechazada de inmediato, sin
#  posibilidad de "arreglarla" a posteriori.  Qt usa un enum de tres valores
#  (Invalid / Intermediate / Acceptable).  Devolvemos Invalid para rechazar
#  y Acceptable para permitir.
# ────────────────────────────────────────────────────────────────────────────

class _IntFieldValidator(QValidator):
    """Validador de enteros no negativos con longitud y máximo opcionales.

    Empareja la semántica del ``_make_int_entry`` Tkinter del original.
    """

    def __init__(self, max_val: int | None, max_len: int, parent=None):
        super().__init__(parent)
        self._max_val = max_val
        self._max_len = int(max_len)

    def validate(self, text: str, pos: int):  # type: ignore[override]
        if text == '':
            return (QValidator.State.Acceptable, text, pos)
        if not text.isdigit() or len(text) > self._max_len:
            return (QValidator.State.Invalid, text, pos)
        if self._max_val is not None and int(text) > self._max_val:
            return (QValidator.State.Invalid, text, pos)
        return (QValidator.State.Acceptable, text, pos)


class _DayFieldValidator(QValidator):
    """Validador del día del mes — stateful por mes/año actual.

    Recibe un ``callable`` que devuelve el día máximo permitido para el mes
    y año actualmente seleccionados.  Así puede variar dinámicamente sin
    recrear el validador.
    """

    def __init__(self, get_max_day, parent=None):
        super().__init__(parent)
        self._get_max_day = get_max_day

    def validate(self, text: str, pos: int):  # type: ignore[override]
        if text == '':
            return (QValidator.State.Acceptable, text, pos)
        if not text.isdigit() or len(text) > 2:
            return (QValidator.State.Invalid, text, pos)
        value = int(text)
        if value == 0:
            # "0" solo: aceptable como prefijo transitorio ("01"... pero
            # nunca como valor final).  Se replica el comportamiento exacto
            # del validador Tkinter.
            return (
                QValidator.State.Acceptable if len(text) == 1
                else QValidator.State.Invalid,
                text, pos
            )
        if value <= int(self._get_max_day()):
            return (QValidator.State.Acceptable, text, pos)
        return (QValidator.State.Invalid, text, pos)


class _MassFieldValidator(QValidator):
    """Validador del campo de masa — acepta coma o punto decimal.

    Replica la lógica del ``_make_mass_entry`` Tkinter original.
    """

    def validate(self, text: str, pos: int):  # type: ignore[override]
        if text == '':
            return (QValidator.State.Acceptable, text, pos)
        test = text.replace(',', '.')
        if test.count('.') > 1:
            return (QValidator.State.Invalid, text, pos)
        if any(ch not in '0123456789.' for ch in test):
            return (QValidator.State.Invalid, text, pos)
        if test == '.':
            return (QValidator.State.Acceptable, text, pos)
        if test.endswith('.'):
            try:
                if float(test + '0') <= MAX_MASS:
                    return (QValidator.State.Acceptable, text, pos)
                return (QValidator.State.Invalid, text, pos)
            except ValueError:
                return (QValidator.State.Invalid, text, pos)
        try:
            val = float(test)
            if MIN_MASS <= val <= MAX_MASS:
                return (QValidator.State.Acceptable, text, pos)
            return (QValidator.State.Invalid, text, pos)
        except ValueError:
            return (QValidator.State.Invalid, text, pos)


# ────────────────────────────────────────────────────────────────────────────
#  8-B  Ventana principal
# ────────────────────────────────────────────────────────────────────────────

class MassInputApp(QWidget):
    """Ventana PyQt6 con dos páginas: entrada de datos y vista previa."""

    # ----- ciclo de vida -----------------------------------------------------

    def __init__(
        self,
        csv_path: Path,
        config: PlotConfig,
        output_dir: Path,
        style: GuiStyle = GuiStyle(),
    ):
        super().__init__()
        self.csv_path = csv_path
        self.config = config
        self.output_dir = output_dir
        self.style = style
        self.result_ok = False
        self._app_icon = load_app_icon()
        self._pending_output_timestamp_dt: datetime | None = None

        # Flag que evita doble diálogo de confirmación cuando cerramos la
        # ventana explícitamente (después del "Guardar imagen" exitoso, o
        # después de que el usuario ya confirmó en el diálogo modal).
        self._confirmed_close = False

        # "input" o "preview".  Controla qué página se ve y, por lo tanto,
        # si el cierre dispara o no el diálogo modal de confirmación.
        self._current_page: str = 'input'

        # Estado del flujo entre páginas (equivalente a las StringVar/BoolVar
        # del original Tkinter, pero gestionado como atributos simples).
        self._preview_pixmap: QPixmap | None = None
        self._preview_path: Path | None = None
        self._pending_rows: list[CsvRow] | None = None
        self._pending_points: list[tuple[float, float]] | None = None
        self._preview_last_smoothness: float | None = None
        self._pending_csv_write_enabled: bool = True

        # Widgets que se deshabilitan masivamente cuando la aplicación está
        # ocupada o se muestra un error fatal.
        self._interactive_widgets: list[QWidget] = []

        # ── Configuración visual global de la ventana ──
        self.setWindowTitle(style.window_title)
        self.setWindowIcon(self._app_icon)
        # No queremos que el usuario pueda maximizar o cambiar el tamaño.
        self.setWindowFlags(
            Qt.WindowType.Window
            | Qt.WindowType.WindowCloseButtonHint
            | Qt.WindowType.WindowTitleHint
            | Qt.WindowType.CustomizeWindowHint
            | Qt.WindowType.WindowSystemMenuHint
        )

        # ── Construir la UI ──
        self._font_label     = self._make_font(style.font_label)
        self._font_entry     = self._make_font(style.font_entry)
        self._font_entry_lg  = self._make_font(style.font_entry_lg)
        self._font_button    = self._make_font(style.font_button)
        self._font_error     = self._make_font(style.font_error)
        self._font_weekday   = self._make_font(style.font_weekday)
        self._font_traceback = self._make_font(style.font_traceback)

        # Layout raíz: contiene una página a la vez mediante show()/hide().
        self._root_layout = QVBoxLayout(self)
        self._root_layout.setContentsMargins(0, 0, 0, 0)
        self._root_layout.setSpacing(0)

        self._page_input = QWidget(self)
        self._page_preview = QWidget(self)
        self._build_input_page(self._page_input)
        self._build_preview_page(self._page_preview)

        self._root_layout.addWidget(self._page_input)
        self._root_layout.addWidget(self._page_preview)

        # Rellenar la hora actual antes de conectar los signals para evitar
        # ruido en el log.
        self._fill_with_current_datetime()

        # Conectar los cambios reactivos del día/mes/año para refrescar el
        # texto del día de la semana (equivalente a trace_add('write', ...)).
        self._entry_day.textChanged.connect(self._refresh_weekday)
        self._combo_month.currentIndexChanged.connect(self._on_month_or_year_changed)
        self._entry_year.textChanged.connect(self._on_month_or_year_changed)

        self._refresh_weekday()
        self._update_preview_smoothness_label()
        self._show_input_page()

        # La geometría inicial depende de que las páginas estén medidas por
        # Qt.  Hacemos eso con setFixedSize a partir del sizeHint actual.
        self._relock_window_to_current_page()
        # Centrar SOLO al inicio.  Los cambios de página posteriores mantienen
        # la posición que el usuario haya elegido (Qt no la mueve por sí mismo).
        QTimer.singleShot(0, self._center_on_primary_screen)

    # ----- construcción de fuentes y métricas -------------------------------

    def _make_font(self, spec: tuple) -> QFont:
        """Crea un ``QFont`` a partir de una tupla de estilo de GuiStyle.

        Acepta:
          - ``(familia, tamaño)``
          - ``(familia, tamaño, "bold")``
        """
        family = str(spec[0])
        size = int(spec[1])
        font = QFont(family, size)
        if len(spec) >= 3 and str(spec[2]).lower() == 'bold':
            font.setBold(True)
        return font

    def _char_width(self, font: QFont, n_chars: int, padding: int = 18) -> int:
        """Emula el ancho en "chars" de Tkinter con ``QFontMetrics``.

        Se usa el ancho de ``'0'`` como referencia porque, en fuentes
        proporcionales, el dígito cero suele aproximarse razonablemente al
        ancho promedio de los dígitos y letras minúsculas.
        """
        fm = QFontMetrics(font)
        return int(fm.horizontalAdvance('0') * n_chars + padding)

    def _preview_content_width_that_fits_screen(self, requested_width: int) -> int:
        """Limita el ancho de la vista previa para que la ventana quepa completa.

        ``preview_width_px`` puede ser mayor que el ancho disponible de algunas
        pantallas.  En ese caso Qt intenta construir una ventana más ancha que
        la pantalla y los bordes del gráfico terminan recortados.  Esta función
        calcula un ancho seguro considerando márgenes, borde del frame y una
        reserva para el resto de controles de la página.
        """
        requested = max(1, int(requested_width))
        screen = QGuiApplication.primaryScreen()
        if screen is None:
            return requested

        available = screen.availableGeometry()

        # Reserva horizontal: márgenes externos de la página, margen interno
        # del frame de la imagen y holgura para bordes/decoraciones del gestor
        # de ventanas.  Esto evita que la ventana quede más ancha que pantalla.
        horizontal_reserved = (
            2 * int(self.style.pad_main_x)
            + 2 * 16
            + 180
        )
        width_limit = max(320, int(available.width()) - horizontal_reserved)

        # Reserva vertical aproximada para título, texto, grupo de opciones,
        # slider, botones y márgenes.  Si la pantalla es baja, también reduce
        # el ancho para mantener la proporción del PNG y evitar recortes abajo.
        aspect = float(self.config.height_px) / max(float(self.config.width_px), 1.0)
        vertical_reserved = 390
        height_limit = max(180, int(available.height()) - vertical_reserved)
        width_from_height = int(height_limit / max(aspect, 1e-9))
        width_limit = min(width_limit, max(320, width_from_height))

        return max(1, min(requested, width_limit))

    # ----- centrado y bloqueo de tamaño -------------------------------------

    def _center_widget_on_primary_screen(self, widget: QWidget) -> None:
        """Centra ``widget`` en la pantalla principal usando su frameGeometry."""
        screen = QGuiApplication.primaryScreen()
        if screen is None:
            return
        available = screen.availableGeometry()
        frame = widget.frameGeometry()
        frame.moveCenter(available.center())
        widget.move(frame.topLeft())

    def _center_on_primary_screen(self) -> None:
        """Centra la ventana en la pantalla primaria reportada por Qt.

        Qt resuelve este concepto de forma uniforme en Linux (X11 o Wayland),
        Windows y macOS: ``QGuiApplication.primaryScreen()`` devuelve la
        pantalla que el sistema operativo marca como "principal" y
        ``availableGeometry()`` devuelve el área visible excluyendo paneles
        y docks.  Así evitamos centrar sobre la unión de todos los monitores.
        """
        self._center_widget_on_primary_screen(self)

    def _center_widget_on_parent(self, widget: QWidget, parent: QWidget) -> None:
        """Centra ``widget`` respecto del frame de ``parent``."""
        parent_frame = parent.frameGeometry()
        frame = widget.frameGeometry()
        frame.moveCenter(parent_frame.center())
        widget.move(frame.topLeft())

    def _relock_window_to_current_page(self) -> None:
        """Recalcula el tamaño para ajustarse al contenido visible actual.

        Qt no reemite sizeHint cuando ocultamos/mostramos sub-widgets;
        ``adjustSize()`` lo fuerza.  Luego congelamos el tamaño con
        ``setFixedSize()`` para que el usuario no pueda redimensionar.
        """
        # Liberamos el bloqueo anterior para poder recalcular el tamaño.
        self.setMinimumSize(0, 0)
        self.setMaximumSize(16777215, 16777215)
        self.adjustSize()
        # Congelamos el nuevo tamaño.
        self.setFixedSize(self.size())
        self._center_on_primary_screen()
        QTimer.singleShot(0, self._center_on_primary_screen)

    # ----- página 1: entrada de datos ---------------------------------------

    def _build_input_page(self, parent: QWidget) -> None:
        """Construye la página 1 (entrada manual de hora/fecha/masa).

        Se usa un QGridLayout para replicar el layout en columnas del
        original Tkinter.  El orden de columnas es, de izquierda a derecha:
            Hora : Minuto |  Día  de  Mes  de  Año  |  Masa  kg
        """
        s = self.style

        outer = QVBoxLayout(parent)
        outer.setContentsMargins(s.pad_main_x, s.pad_main_y, s.pad_main_x, s.pad_main_y)
        outer.setSpacing(s.pad_y)

        # ── Grid principal de campos ──
        grid = QGridLayout()
        grid.setHorizontalSpacing(s.pad_x)
        grid.setVerticalSpacing(s.pad_y)
        outer.addLayout(grid)

        # Fila 0 — títulos de sección
        lbl_time = QLabel(s.label_time, parent); lbl_time.setFont(self._font_label)
        lbl_date = QLabel(s.label_date, parent); lbl_date.setFont(self._font_label); lbl_date.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lbl_mass = QLabel(s.label_mass, parent); lbl_mass.setFont(self._font_label); lbl_mass.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        grid.addWidget(lbl_time, 0, 0, 1, 3)
        grid.addWidget(lbl_date, 0, 3, 1, 5)
        grid.addWidget(lbl_mass, 0, 8, 1, 2)

        # Fila 1 — campos editables
        # Hora
        self._entry_hour = QLineEdit(parent)
        self._entry_hour.setFont(self._font_entry_lg)
        self._entry_hour.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._entry_hour.setMaxLength(2)
        self._entry_hour.setValidator(_IntFieldValidator(MAX_HOUR, 2, self._entry_hour))
        self._entry_hour.setFixedWidth(self._char_width(self._font_entry_lg, s.entry_width_time))
        grid.addWidget(self._entry_hour, 1, 0)

        lbl_colon = QLabel(s.label_colon, parent)
        lbl_colon.setFont(self._font_entry_lg)
        lbl_colon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        grid.addWidget(lbl_colon, 1, 1)

        # Minuto
        self._entry_minute = QLineEdit(parent)
        self._entry_minute.setFont(self._font_entry_lg)
        self._entry_minute.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._entry_minute.setMaxLength(2)
        self._entry_minute.setValidator(_IntFieldValidator(MAX_MINUTE, 2, self._entry_minute))
        self._entry_minute.setFixedWidth(self._char_width(self._font_entry_lg, s.entry_width_time))
        grid.addWidget(self._entry_minute, 1, 2)

        # Día de la semana (texto derivado — sólo lectura)
        self._lbl_weekday = QLabel('', parent)
        self._lbl_weekday.setFont(self._font_weekday)
        self._lbl_weekday.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        grid.addWidget(self._lbl_weekday, 1, 3)

        # Día del mes
        self._entry_day = QLineEdit(parent)
        self._entry_day.setFont(self._font_entry_lg)
        self._entry_day.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._entry_day.setMaxLength(2)
        self._entry_day.setValidator(_DayFieldValidator(self._current_max_day, self._entry_day))
        self._entry_day.setFixedWidth(self._char_width(self._font_entry_lg, s.entry_width_day))
        grid.addWidget(self._entry_day, 1, 4)

        lbl_of_1 = QLabel(s.label_of, parent)
        lbl_of_1.setFont(self._font_entry)
        lbl_of_1.setAlignment(Qt.AlignmentFlag.AlignCenter)
        grid.addWidget(lbl_of_1, 1, 5)

        # Mes
        self._combo_month = QComboBox(parent)
        self._combo_month.setFont(self._font_entry)
        self._combo_month.addItems(MONTHS_ES)
        self._combo_month.setEditable(False)
        self._combo_month.setMinimumWidth(self._char_width(self._font_entry, s.combo_width_month))
        grid.addWidget(self._combo_month, 1, 6)

        lbl_of_2 = QLabel(s.label_of, parent)
        lbl_of_2.setFont(self._font_entry)
        lbl_of_2.setAlignment(Qt.AlignmentFlag.AlignCenter)
        grid.addWidget(lbl_of_2, 1, 7)

        # Año
        self._entry_year = QLineEdit(parent)
        self._entry_year.setFont(self._font_entry_lg)
        self._entry_year.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._entry_year.setMaxLength(4)
        self._entry_year.setValidator(_IntFieldValidator(None, 4, self._entry_year))
        self._entry_year.setFixedWidth(self._char_width(self._font_entry_lg, s.entry_width_year))
        grid.addWidget(self._entry_year, 1, 8)

        # Masa
        self._entry_mass = QLineEdit(parent)
        self._entry_mass.setFont(self._font_entry_lg)
        self._entry_mass.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self._entry_mass.setValidator(_MassFieldValidator(self._entry_mass))
        self._entry_mass.setFixedWidth(self._char_width(self._font_entry_lg, s.entry_width_mass))
        grid.addWidget(self._entry_mass, 1, 9)

        lbl_kg = QLabel(s.label_kg, parent)
        lbl_kg.setFont(self._font_entry)
        grid.addWidget(lbl_kg, 1, 10)

        # Fila 2 — etiqueta de error reactiva
        self._lbl_error = QLabel('', parent)
        self._lbl_error.setFont(self._font_error)
        # El color rojo del mensaje de error se aplica a la etiqueta sin
        # definir un stylesheet global del programa.
        self._lbl_error.setStyleSheet(f'color: {s.color_error_text};')
        self._lbl_error.setAlignment(Qt.AlignmentFlag.AlignCenter)
        outer.addWidget(self._lbl_error)

        # Fila 3 — separador horizontal fino
        sep = QFrame(parent)
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setFrameShadow(QFrame.Shadow.Sunken)
        outer.addWidget(sep)

        # Fila 4 — botones
        btn_row = QHBoxLayout()
        btn_row.setSpacing(s.pad_button)
        outer.addLayout(btn_row)

        self._btn_cancel_input = QPushButton(s.text_cancel, parent)
        self._btn_cancel_input.setFont(self._font_button)
        self._btn_cancel_input.setMinimumWidth(self._char_width(self._font_button, s.button_width_sm))
        self._btn_cancel_input.clicked.connect(self._on_cancel_from_input)
        btn_row.addWidget(self._btn_cancel_input)

        self._btn_reset_datetime = QPushButton(s.text_reset_datetime, parent)
        self._btn_reset_datetime.setFont(self._font_button)
        self._btn_reset_datetime.setMinimumWidth(self._char_width(self._font_button, s.button_width_sm + 2))
        self._btn_reset_datetime.clicked.connect(self._fill_with_current_datetime)
        btn_row.addWidget(self._btn_reset_datetime)

        self._btn_continue = QPushButton(s.text_continue, parent)
        self._btn_continue.setFont(self._font_button)
        self._btn_continue.setMinimumWidth(self._char_width(self._font_button, s.button_width_lg))
        self._btn_continue.setDefault(True)
        self._btn_continue.setAutoDefault(True)
        self._btn_continue.clicked.connect(self._on_continue)
        btn_row.addWidget(self._btn_continue)

        self._btn_view_graph_only = QPushButton(s.text_view_graph_only, parent)
        self._btn_view_graph_only.setFont(self._font_button)
        self._btn_view_graph_only.setMinimumWidth(self._char_width(self._font_button, s.button_width_lg))
        self._btn_view_graph_only.clicked.connect(self._on_view_graph_only)
        btn_row.addWidget(self._btn_view_graph_only)

        self._shortcut_continue_return = QShortcut(QKeySequence(Qt.Key.Key_Return), parent)
        self._shortcut_continue_return.setContext(Qt.ShortcutContext.WidgetWithChildrenShortcut)
        self._shortcut_continue_return.activated.connect(self._on_continue)
        self._shortcut_continue_enter = QShortcut(QKeySequence(Qt.Key.Key_Enter), parent)
        self._shortcut_continue_enter.setContext(Qt.ShortcutContext.WidgetWithChildrenShortcut)
        self._shortcut_continue_enter.activated.connect(self._on_continue)

        self._interactive_widgets.extend([
            self._entry_hour, self._entry_minute, self._entry_day,
            self._combo_month, self._entry_year, self._entry_mass,
            self._btn_reset_datetime, self._btn_continue, self._btn_view_graph_only,
        ])

    # ----- página 2: vista previa -------------------------------------------

    def _build_preview_page(self, parent: QWidget) -> None:
        """Construye la página 2 (imagen del gráfico, opciones y slider)."""
        s = self.style

        outer = QVBoxLayout(parent)
        outer.setContentsMargins(s.pad_main_x, s.pad_main_y, s.pad_main_x, s.pad_main_y)
        outer.setSpacing(s.pad_y)

        # Título y subtítulo explicativo
        lbl_title = QLabel(s.text_preview_title, parent)
        lbl_title.setFont(self._font_label)
        lbl_title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        outer.addWidget(lbl_title)

        lbl_hint = QLabel(s.text_preview_hint, parent)
        lbl_hint.setFont(self._font_weekday)
        lbl_hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        outer.addWidget(lbl_hint)

        # Frame con borde fino que contiene la imagen (equivalente del
        # ``tk.Frame(..., bd=1, relief='solid')`` del original).
        preview_frame = QFrame(parent)
        preview_frame.setFrameShape(QFrame.Shape.Box)
        preview_frame.setFrameShadow(QFrame.Shadow.Plain)
        preview_inner = QVBoxLayout(preview_frame)
        preview_inner.setContentsMargins(16, 16, 16, 16)
        self._preview_image_label = QLabel(preview_frame)
        self._preview_image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        preview_inner.addWidget(self._preview_image_label)
        outer.addWidget(preview_frame)

        # Grupo de opciones con borde y título (equivalente de tk.LabelFrame)
        options_group = QGroupBox(s.text_preview_options, parent)
        options_group.setFont(self._font_label)
        options_grid = QGridLayout(options_group)
        options_grid.setContentsMargins(8, 6, 8, 6)
        options_grid.setHorizontalSpacing(12)
        options_grid.setVerticalSpacing(2)

        self._chk_point_lines = QCheckBox(s.text_preview_show_other_thing, options_group)
        self._chk_adjustment  = QCheckBox(s.text_preview_show_adjustment, options_group)
        self._chk_derivative  = QCheckBox(s.text_preview_show_derivative, options_group)
        self._chk_points      = QCheckBox(s.text_preview_show_points, options_group)

        # Estado inicial (equivalente a las BooleanVar originales)
        self._chk_point_lines.setChecked(False)
        self._chk_adjustment.setChecked(True)
        self._chk_derivative.setChecked(False)
        self._chk_points.setChecked(True)

        # Signals — replican _on_preview_toggle_changed / _on_toggle_adjustment
        self._chk_point_lines.toggled.connect(lambda _c: self._on_preview_toggle_changed())
        self._chk_points.toggled.connect(lambda _c: self._on_preview_toggle_changed())
        self._chk_derivative.toggled.connect(lambda _c: self._on_toggle_adjustment())
        self._chk_adjustment.toggled.connect(lambda _c: self._on_toggle_adjustment())

        options_grid.addWidget(self._chk_point_lines, 0, 0)
        options_grid.addWidget(self._chk_adjustment,  0, 1)
        options_grid.addWidget(self._chk_points,      1, 0)
        options_grid.addWidget(self._chk_derivative,  1, 1)
        outer.addWidget(options_group)

        # Slider de suavidad + etiqueta de valor
        smoothness_container = QVBoxLayout()
        smoothness_container.setSpacing(2)
        outer.addLayout(smoothness_container)

        smoothness_header = QHBoxLayout()
        smoothness_container.addLayout(smoothness_header)

        lbl_smoothness = QLabel(s.text_preview_smoothness, parent)
        lbl_smoothness.setFont(self._font_label)
        smoothness_header.addWidget(lbl_smoothness)
        smoothness_header.addStretch(1)
        self._lbl_smoothness_value = QLabel('', parent)
        self._lbl_smoothness_value.setFont(self._font_weekday)
        smoothness_header.addWidget(self._lbl_smoothness_value)

        # QSlider trabaja con enteros.  Convertimos desde/hacia float usando
        # la resolución configurada en PlotConfig.  Esto permite mantener la
        # misma granularidad que el tk.Scale del original.
        self._slider_min = float(self.config.smooth_slider_min)
        self._slider_max = float(self.config.smooth_slider_max)
        self._slider_step = max(float(self.config.smooth_slider_resolution), 1e-9)
        slider_int_max = int(round((self._slider_max - self._slider_min) / self._slider_step))

        self._smoothness_scale = QSlider(Qt.Orientation.Horizontal, parent)
        self._smoothness_scale.setMinimum(0)
        self._smoothness_scale.setMaximum(slider_int_max)
        self._smoothness_scale.setSingleStep(1)
        self._smoothness_scale.setPageStep(max(1, int(round(
            float(self.config.smooth_slider_tickinterval) / self._slider_step
        ))))
        self._smoothness_scale.setTickPosition(QSlider.TickPosition.TicksBelow)
        self._smoothness_scale.setTickInterval(max(1, int(round(
            float(self.config.smooth_slider_tickinterval) / self._slider_step
        ))))
        preview_control_width = self._preview_content_width_that_fits_screen(
            int(self.config.preview_width_px)
        )
        self._smoothness_scale.setMinimumWidth(max(200, preview_control_width - 120))

        # Valor inicial del slider — corresponde a la suavidad de base.
        self._smoothness_float = float(self.config.smooth_general_smoothness)
        self._smoothness_scale.setValue(self._float_to_slider(self._smoothness_float))

        # Conexiones de signals — mantienen el comportamiento del original:
        #   - ``valueChanged``  → actualización en vivo de la etiqueta.
        #   - ``sliderReleased`` y tecla soltada → re-renderizar la vista.
        self._smoothness_scale.valueChanged.connect(self._on_preview_smoothness_change)
        self._smoothness_scale.sliderReleased.connect(self._on_preview_smoothness_release)
        # Para cubrir tanto el arrastre del ratón como los cambios por teclado
        # (flechas), observamos también los cambios totales vía un timer de
        # "debounce" simple que se relanza en el próximo ciclo del event loop.
        self._smoothness_pending_rerender = False
        self._smoothness_scale.actionTriggered.connect(self._on_smoothness_action_triggered)

        smoothness_container.addWidget(self._smoothness_scale)

        # Botones inferiores
        btn_row = QHBoxLayout()
        btn_row.setSpacing(s.pad_button)
        outer.addLayout(btn_row)
        btn_row.addStretch(1)

        self._btn_cancel_preview = QPushButton(s.text_cancel, parent)
        self._btn_cancel_preview.setFont(self._font_button)
        self._btn_cancel_preview.setMinimumWidth(self._char_width(self._font_button, s.button_width_sm))
        self._btn_cancel_preview.clicked.connect(self._on_cancel_from_preview)
        btn_row.addWidget(self._btn_cancel_preview)

        self._btn_reset_preview = QPushButton(s.text_preview_reset, parent)
        self._btn_reset_preview.setFont(self._font_button)
        self._btn_reset_preview.setMinimumWidth(self._char_width(self._font_button, s.button_width_sm))
        self._btn_reset_preview.clicked.connect(self._on_reset_preview)
        btn_row.addWidget(self._btn_reset_preview)

        self._btn_back = QPushButton(s.text_back, parent)
        self._btn_back.setFont(self._font_button)
        self._btn_back.setMinimumWidth(self._char_width(self._font_button, s.button_width_sm))
        self._btn_back.clicked.connect(self._on_back)
        btn_row.addWidget(self._btn_back)

        self._btn_save_image = QPushButton(s.text_save_image, parent)
        self._btn_save_image.setFont(self._font_button)
        self._btn_save_image.setMinimumWidth(self._char_width(self._font_button, s.button_width_lg))
        self._btn_save_image.setDefault(True)
        self._btn_save_image.setAutoDefault(True)
        self._btn_save_image.clicked.connect(self._on_save_image)
        btn_row.addWidget(self._btn_save_image)

        btn_row.addStretch(1)

        self._shortcut_save_return = QShortcut(QKeySequence(Qt.Key.Key_Return), parent)
        self._shortcut_save_return.setContext(Qt.ShortcutContext.WidgetWithChildrenShortcut)
        self._shortcut_save_return.activated.connect(self._on_save_image)
        self._shortcut_save_enter = QShortcut(QKeySequence(Qt.Key.Key_Enter), parent)
        self._shortcut_save_enter.setContext(Qt.ShortcutContext.WidgetWithChildrenShortcut)
        self._shortcut_save_enter.activated.connect(self._on_save_image)

        self._interactive_widgets.extend([
            self._btn_save_image, self._btn_back, self._btn_reset_preview, self._smoothness_scale,
            self._chk_point_lines, self._chk_adjustment, self._chk_derivative, self._chk_points,
        ])

        self._update_adjustment_controls_state()

    # ----- conmutación de páginas -------------------------------------------

    def _show_input_page(self) -> None:
        """Muestra la página de entrada."""
        self._page_preview.hide()
        self._page_input.show()
        self._current_page = 'input'
        self._relock_window_to_current_page()
        self._entry_mass.setFocus(Qt.FocusReason.OtherFocusReason)
        QTimer.singleShot(0, lambda: self._entry_mass.setFocus(Qt.FocusReason.OtherFocusReason))

    def _show_preview_page(self) -> None:
        """Muestra la página de vista previa."""
        self._page_input.hide()
        self._page_preview.show()
        self._current_page = 'preview'
        self._set_preview_controls_enabled(enabled=True)
        self._relock_window_to_current_page()
        self._btn_save_image.setFocus(Qt.FocusReason.OtherFocusReason)
        QTimer.singleShot(0, lambda: self._btn_save_image.setFocus(Qt.FocusReason.OtherFocusReason))
        QTimer.singleShot(0, self._center_on_primary_screen)

    # ----- helpers del slider de suavidad -----------------------------------

    def _float_to_slider(self, value: float) -> int:
        """Convierte un valor continuo en un índice entero del slider."""
        clipped = min(max(float(value), self._slider_min), self._slider_max)
        return int(round((clipped - self._slider_min) / self._slider_step))

    def _slider_to_float(self, value: int) -> float:
        """Convierte un índice del slider en el valor continuo correspondiente."""
        raw = self._slider_min + int(value) * self._slider_step
        clipped = min(max(raw, self._slider_min), self._slider_max)
        return round(clipped, 6)

    # ----- relleno automático de campos -------------------------------------

    def _fill_with_current_datetime(self) -> None:
        """Rellena los campos con la fecha y hora actual (botón "Resetear")."""
        now = datetime.now()
        self._entry_hour.setText(f'{now.hour:02d}')
        self._entry_minute.setText(f'{now.minute:02d}')
        self._entry_day.setText(str(now.day))
        self._combo_month.setCurrentIndex(now.month - 1)
        self._entry_year.setText(str(now.year))
        self._lbl_error.setText('')
        log_info(f'Fecha/hora reseteada a {now.strftime(TIMESTAMP_FORMAT)}.')

    def _current_month_index(self) -> int:
        """Devuelve el mes actual como 1..12, o 0 si no hay selección válida."""
        index = int(self._combo_month.currentIndex())
        return index + 1 if 0 <= index < 12 else 0

    def _current_year_value(self) -> int:
        """Devuelve el año actual o 0 si es inválido / fuera de rango."""
        try:
            y = int(self._entry_year.text())
            return y if MIN_YEAR <= y <= MAX_YEAR else 0
        except (ValueError, TypeError):
            return 0

    def _current_max_day(self) -> int:
        """Devuelve cuántos días tiene el mes/año seleccionado actualmente."""
        m = self._current_month_index()
        y = self._current_year_value()
        return max_day_in_month(m, y) if m and y else MAX_DAY

    def _on_month_or_year_changed(self, *_args) -> None:
        """Trunca el día si el nuevo mes/año es más corto que el día actual."""
        mx = self._current_max_day()
        try:
            d = int(self._entry_day.text())
            if d > mx:
                self._entry_day.setText(str(mx))
                log_info(f'Día ajustado a {mx} (máximo del mes seleccionado).')
        except (ValueError, TypeError):
            pass
        self._refresh_weekday()

    def _refresh_weekday(self, *_args) -> None:
        """Actualiza la etiqueta textual del día de la semana."""
        try:
            d = int(self._entry_day.text())
            m = self._current_month_index()
            y = self._current_year_value()
            if d and m and y:
                self._lbl_weekday.setText(WEEKDAYS_ES[date(y, m, d).weekday()])
                return
        except (ValueError, TypeError, OverflowError):
            pass
        self._lbl_weekday.setText('')

    # ----- validación y parseo de la entrada completa -----------------------

    def _parse_all_inputs(self) -> tuple[datetime, float] | None:
        """Valida los seis campos y devuelve ``(datetime, masa)`` o None."""
        s = self.style
        self._lbl_error.setText('')

        hour = self._parse_int_field(self._entry_hour, MIN_HOUR, MAX_HOUR, s.error_invalid_hour)
        if hour is None:
            return None
        minute = self._parse_int_field(self._entry_minute, MIN_MINUTE, MAX_MINUTE, s.error_invalid_minute)
        if minute is None:
            return None
        year = self._parse_int_field(self._entry_year, MIN_YEAR, MAX_YEAR, s.error_invalid_year)
        if year is None:
            return None
        month = self._current_month_index()
        if not (1 <= month <= 12):
            self._lbl_error.setText(s.error_invalid_month)
            return None
        day_max = max_day_in_month(month, year)
        day = self._parse_int_field(self._entry_day, MIN_DAY, day_max, s.error_invalid_day)
        if day is None:
            return None
        try:
            dt = datetime(year, month, day, hour, minute, 0)
        except (ValueError, OverflowError):
            self._lbl_error.setText(s.error_invalid_timestamp)
            return None
        raw_mass = self._entry_mass.text().strip()
        if not raw_mass:
            self._lbl_error.setText(s.error_empty_mass)
            return None
        try:
            mass_val = float(raw_mass.replace(',', '.'))
            if not (MIN_MASS <= mass_val <= MAX_MASS):
                raise ValueError
            if not math.isfinite(mass_val):
                raise ValueError
        except (ValueError, TypeError):
            self._lbl_error.setText(s.error_invalid_mass)
            return None
        return dt, mass_val

    def _parse_int_field(self, widget: QLineEdit, lo: int, hi: int, error_msg: str) -> int | None:
        """Parsea un campo de entero con rango [lo, hi]."""
        try:
            value = int(widget.text())
            if lo <= value <= hi:
                return value
        except (ValueError, TypeError):
            pass
        self._lbl_error.setText(error_msg)
        return None

    # ----- preparación de filas CSV y vista previa --------------------------

    def _build_pending_rows(self, dt: datetime, mass: float) -> tuple[list[CsvRow], list[tuple[float, float]]]:
        """Construye las filas que se escribirían si el usuario confirma."""
        day_frac = fractional_day(dt, self.config.start_date)
        ts_str = dt.strftime(TIMESTAMP_FORMAT)
        log_info(f'Nuevo dato (pendiente) → día={day_frac}, masa={mass}, timestamp={ts_str}')
        rows = load_csv(self.csv_path)
        rows.append(CsvRow(raw='', day=day_frac, mass=mass, extra=ts_str, valid=True))
        rows = sort_and_fix_rows(rows)
        points = [(r.day, r.mass) for r in rows if r.valid and r.day is not None and r.mass is not None]
        return rows, points

    def _build_current_rows_and_points(self) -> tuple[list[CsvRow], list[tuple[float, float]]]:
        """Carga el CSV actual para vista previa sin añadir una masa nueva."""
        rows = sort_and_fix_rows(load_csv(self.csv_path))
        points = [(r.day, r.mass) for r in rows if r.valid and r.day is not None and r.mass is not None]
        return rows, points

    def _prepare_preview(self, *, add_new_point: bool) -> None:
        """Prepara el estado pendiente de la página de vista previa.

        Cuando ``add_new_point`` es True se valida la página 1 y se incorpora
        la masa ingresada solo en memoria.  Cuando es False se carga el CSV
        actual sin añadir un dato nuevo.
        """
        if add_new_point:
            parsed = self._parse_all_inputs()
            if parsed is None:
                raise ValueError('Entrada inválida para generar la vista previa.')
            dt, mass = parsed
            self._pending_rows, self._pending_points = self._build_pending_rows(dt, mass)
            self._pending_csv_write_enabled = True
            self._pending_output_timestamp_dt = dt
        else:
            self._pending_rows, self._pending_points = self._build_current_rows_and_points()
            self._pending_csv_write_enabled = False
            self._pending_output_timestamp_dt = None
            log_info('Vista previa solicitada sin añadir una masa nueva.')

        self._preview_last_smoothness = None
        self._smoothness_float = self._quantize_preview_smoothness(self.config.smooth_general_smoothness)
        self._smoothness_scale.blockSignals(True)
        self._smoothness_scale.setValue(self._float_to_slider(self._smoothness_float))
        self._smoothness_scale.blockSignals(False)
        self._update_preview_smoothness_label()
        self._apply_preview_default_options(rerender=False)
        self._render_preview_to_temp_file()
        self._show_preview_page()

    def _quantize_preview_smoothness(self, value: float) -> float:
        """Redondea ``value`` al múltiplo válido más cercano del paso del slider."""
        step = self._slider_step
        lo = self._slider_min
        hi = self._slider_max
        clipped = min(max(float(value), lo), hi)
        quantized = lo + round((clipped - lo) / step) * step
        quantized = min(max(quantized, lo), hi)
        return round(quantized, 6)

    def _current_preview_smoothness(self) -> float:
        """Devuelve la suavidad actual leída del slider, ya cuantizada."""
        raw = self._slider_to_float(int(self._smoothness_scale.value()))
        return self._quantize_preview_smoothness(raw)

    def _update_preview_smoothness_label(self) -> None:
        """Sincroniza la etiqueta '0.00..4.00' al lado del slider."""
        self._lbl_smoothness_value.setText(f'{self._current_preview_smoothness():.2f}')

    def _build_render_options(self) -> PlotRenderOptions:
        """Lee el estado actual de los checkboxes de la página 2."""
        return PlotRenderOptions(
            show_adjustment=bool(self._chk_adjustment.isChecked()),
            show_adjustment_derivative=bool(self._chk_derivative.isChecked()),
            show_points=bool(self._chk_points.isChecked()),
            show_point_lines=bool(self._chk_point_lines.isChecked()),
        )

    def _preview_option_checkboxes(self) -> tuple[QCheckBox, ...]:
        """Devuelve los checkboxes de opciones del gráfico."""
        return (
            self._chk_point_lines,
            self._chk_adjustment,
            self._chk_derivative,
            self._chk_points,
        )

    def _apply_preview_default_options(self, *, rerender: bool) -> None:
        """Restablece las opciones iniciales del gráfico de vista previa."""
        checkboxes = self._preview_option_checkboxes()
        for checkbox in checkboxes:
            checkbox.blockSignals(True)
        try:
            self._chk_point_lines.setChecked(False)
            self._chk_adjustment.setChecked(True)
            self._chk_derivative.setChecked(False)
            self._chk_points.setChecked(True)
        finally:
            for checkbox in checkboxes:
                checkbox.blockSignals(False)
        if hasattr(self, '_smoothness_scale'):
            self._update_adjustment_controls_state()
        if rerender and self._pending_points is not None:
            self._render_preview_to_temp_file()

    def _set_preview_controls_enabled(self, *, enabled: bool) -> None:
        """Habilita los controles propios de la página de vista previa."""
        self._set_widgets_state(
            [
                *self._preview_option_checkboxes(),
                self._btn_reset_preview,
                self._btn_save_image,
                self._btn_back,
                self._btn_cancel_preview,
            ],
            enabled=enabled,
        )
        if enabled:
            self._update_adjustment_controls_state()

    def _update_adjustment_controls_state(self) -> None:
        """Habilita o deshabilita el slider según las curvas dependientes del ajuste."""
        enabled = bool(self._chk_adjustment.isChecked() or self._chk_derivative.isChecked())
        self._smoothness_scale.setEnabled(enabled)

    # ----- signals del slider de suavidad -----------------------------------

    def _on_preview_smoothness_change(self, _value: int) -> None:
        """Actualiza sólo la etiqueta mientras el usuario arrastra el slider."""
        self._update_preview_smoothness_label()

    def _on_smoothness_action_triggered(self, _action: int) -> None:
        """Al terminar una acción discreta del slider (flecha), re-renderiza.

        Qt emite ``sliderReleased`` sólo al soltar el ratón; los pulsos de
        teclado generan ``actionTriggered``.  Usamos un pequeño timer para
        colapsar varias pulsaciones rápidas en un único re-render.
        """
        if not self._smoothness_pending_rerender:
            self._smoothness_pending_rerender = True
            QTimer.singleShot(0, self._flush_smoothness_rerender)

    def _flush_smoothness_rerender(self) -> None:
        """Entrada única para re-renderizar tras un cambio del slider."""
        self._smoothness_pending_rerender = False
        self._on_preview_smoothness_release()

    def _on_preview_toggle_changed(self) -> None:
        """Re-renderiza la vista previa cuando cambia un toggle de visibilidad."""
        if self._pending_points is None:
            return
        self._render_preview_to_temp_file()

    def _on_toggle_adjustment(self) -> None:
        """Habilita o deshabilita el slider y re-renderiza."""
        self._update_adjustment_controls_state()
        self._on_preview_toggle_changed()

    def _build_effective_plot_config(self, *, preview: bool) -> PlotConfig:
        """Devuelve la config con suavidad actual y tamaño apropiado.

        En vista previa se usan márgenes internos más conservadores que en el
        PNG final.  Así las etiquetas rotadas del eje X, el borde derecho del
        eje y el título quedan dentro del pixmap aun cuando la ventana deba
        reducir el gráfico para caber en pantalla.
        """
        effective = replace(self.config, smooth_general_smoothness=self._current_preview_smoothness())
        if not preview:
            return effective
        preview_width = self._preview_content_width_that_fits_screen(
            int(effective.preview_width_px)
        )
        preview_height = max(1, int(round(preview_width * effective.height_px / effective.width_px)))
        preview_dpi = max(1, int(effective.preview_dpi))
        return replace(
            effective,
            width_px=preview_width,
            height_px=preview_height,
            dpi=preview_dpi,
            margin_left=max(float(effective.margin_left), 0.070),
            margin_right=min(float(effective.margin_right), 0.955),
            margin_top=min(float(effective.margin_top), 0.900),
            margin_bottom=max(float(effective.margin_bottom), 0.285),
        )

    # ----- manejo del PNG temporal de vista previa --------------------------

    def _cleanup_preview_file(self) -> None:
        """Elimina el PNG temporal anterior, si existe."""
        if self._preview_path is None:
            return
        try:
            if self._preview_path.exists():
                self._preview_path.unlink()
        except OSError:
            pass
        self._preview_path = None

    def _load_preview_into_qt(self, preview_path: Path) -> None:
        """Carga el PNG temporal como un ``QPixmap`` dentro del label.

        Aunque el PNG de vista previa ya se genera con un ancho limitado, se
        vuelve a imponer un límite al cargarlo en Qt.  Esto actúa como segunda
        barrera contra recortes causados por decoraciones del gestor de
        ventanas, escalado fraccional o diferencias entre sizeHint y tamaño
        visible real.
        """
        pixmap = QPixmap(str(preview_path))
        self._preview_pixmap = pixmap

        screen = QGuiApplication.primaryScreen()
        display_pixmap = pixmap
        if screen is not None and not pixmap.isNull():
            available = screen.availableGeometry()
            max_width = max(320, int(available.width()) - 260)
            max_height = max(180, int(available.height()) - 430)
            if pixmap.width() > max_width or pixmap.height() > max_height:
                display_pixmap = pixmap.scaled(
                    max_width,
                    max_height,
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation,
                )

        self._preview_image_label.setPixmap(display_pixmap)
        # Fuerza al label a adoptar el tamaño mostrado, no necesariamente el
        # tamaño original del PNG.  Así el frame que lo contiene no empuja la
        # ventana más allá del área visible de la pantalla.
        self._preview_image_label.setFixedSize(display_pixmap.size())

    def _render_preview_to_temp_file(self) -> None:
        """Genera el PNG de vista previa y lo muestra dentro de la página 2."""
        if self._pending_points is None:
            raise RuntimeError('No hay puntos pendientes para generar la vista previa.')
        preview_config = self._build_effective_plot_config(preview=True)
        render_options = self._build_render_options()
        self._cleanup_preview_file()
        fd, preview_file = tempfile.mkstemp(prefix='grafico_masa_preview_', suffix='.png')
        os.close(fd)
        Path(preview_file).unlink(missing_ok=True)
        self._preview_path = Path(preview_file)
        render_plot(self._pending_points, preview_config,
                    output_path=self._preview_path, render_options=render_options)
        self._load_preview_into_qt(self._preview_path)
        self._preview_last_smoothness = preview_config.smooth_general_smoothness
        self._update_preview_smoothness_label()
        # Permitir que el tamaño de la ventana se adapte al nuevo pixmap.
        self._relock_window_to_current_page()

    def _on_preview_smoothness_release(self, _event=None) -> None:
        """Re-renderiza cuando el usuario libera el slider (ratón o teclado)."""
        if self._pending_points is None:
            return
        if not (self._chk_adjustment.isChecked() or self._chk_derivative.isChecked()):
            return
        new_smoothness = self._current_preview_smoothness()
        if self._preview_last_smoothness is not None and abs(new_smoothness - self._preview_last_smoothness) < 1e-9:
            return
        # Deshabilitar temporalmente los controles sensibles para evitar
        # reentradas mientras matplotlib trabaja.
        self._smoothness_scale.setEnabled(False)
        self._btn_save_image.setEnabled(False)
        QApplication.processEvents()
        try:
            log_info(f'Actualizando vista previa con suavidad={new_smoothness:.2f}...')
            self._render_preview_to_temp_file()
        except Exception:
            tb = traceback.format_exc()
            log_error(f'Error no recuperable:\n{tb}')
            self._show_fatal_error(tb)
            return
        finally:
            try:
                self._update_adjustment_controls_state()
                self._btn_save_image.setEnabled(True)
            except RuntimeError:
                pass

    # ----- acciones de los botones ------------------------------------------

    def _on_cancel_from_input(self) -> None:
        """Cancelar desde la página 1: cierra sin preguntar (no hay datos)."""
        self._confirmed_close = True
        self._cleanup_preview_file()
        log_info('Cancelado por el usuario.')
        self.close()

    def _on_cancel_from_preview(self) -> None:
        """Cancelar desde la página 2: abre diálogo modal de confirmación.

        Esto corresponde a la funcionalidad adicional pedida: una vez que el
        usuario está viendo el gráfico (con datos ya generados en memoria),
        el botón "Cancelar" no debe cerrar silenciosamente — debe preguntar
        si realmente quiere perder los datos y el PNG sin guardarlo.
        """
        if self._ask_discard_confirmation():
            self._confirmed_close = True
            self._cleanup_preview_file()
            log_info('Cancelado por el usuario.')
            self.close()

    def _on_back(self) -> None:
        """Volver a la página 1 manteniendo los datos de entrada."""
        self._show_input_page()
        self._set_widgets_state(self._interactive_widgets, enabled=True)

    def _on_reset_preview(self) -> None:
        """Restablece la suavidad y las opciones iniciales del gráfico."""
        self._smoothness_float = self._quantize_preview_smoothness(self.config.smooth_general_smoothness)
        self._smoothness_scale.blockSignals(True)
        self._smoothness_scale.setValue(self._float_to_slider(self._smoothness_float))
        self._smoothness_scale.blockSignals(False)
        self._update_preview_smoothness_label()
        self._apply_preview_default_options(rerender=True)

    def _on_continue(self) -> None:
        """Validar la página 1 y abrir la vista previa con la nueva masa."""
        self._lbl_error.setText('')
        self._set_widgets_state(self._interactive_widgets, enabled=False)
        QApplication.processEvents()
        try:
            self._prepare_preview(add_new_point=True)
        except ValueError:
            self._set_widgets_state(self._interactive_widgets, enabled=True)
            return
        except Exception:
            tb = traceback.format_exc()
            log_error(f'Error no recuperable:\n{tb}')
            self._show_fatal_error(tb)
            return
        finally:
            if self._current_page == 'preview':
                self._set_preview_controls_enabled(enabled=True)

    def _on_view_graph_only(self) -> None:
        """Abre la vista previa del gráfico sin añadir una masa nueva."""
        self._lbl_error.setText('')
        self._set_widgets_state(self._interactive_widgets, enabled=False)
        QApplication.processEvents()
        try:
            self._prepare_preview(add_new_point=False)
        except Exception:
            tb = traceback.format_exc()
            log_error(f'Error no recuperable:\n{tb}')
            self._show_fatal_error(tb)
            return
        finally:
            if self._current_page == 'preview':
                self._set_preview_controls_enabled(enabled=True)

    def _resolve_graph_only_save_dir(self) -> Path:
        """Devuelve la carpeta sugerida para guardar en modo "Sólo ver gráfico".

        Prioriza la carpeta de Descargas reportada por el sistema.  Si no está
        disponible, intenta con ~/Downloads y ~/Descargas.  Como último recurso,
        usa el directorio home del usuario.
        """
        home = Path.home()
        candidates: list[Path] = []

        downloads_path = QStandardPaths.writableLocation(
            QStandardPaths.StandardLocation.DownloadLocation
        )
        if downloads_path:
            candidates.append(Path(downloads_path).expanduser())

        candidates.extend([
            home / 'Downloads',
            home / 'Descargas',
        ])

        seen: set[Path] = set()
        for candidate in candidates:
            if candidate in seen:
                continue
            seen.add(candidate)
            if candidate.exists() and candidate.is_dir():
                return candidate

        return home

    def _ask_output_path_for_graph_only_mode(self, config: PlotConfig) -> Path | None:
        """Pide la ruta de salida cuando se entró por "Sólo ver gráfico"."""
        default_dir = self._resolve_graph_only_save_dir()
        default_path = default_dir / build_output_image_filename(
            config,
            timestamp_dt=datetime.now(),
        )

        dialog = QFileDialog(self)
        dialog.setWindowTitle('Guardar imagen del gráfico')
        dialog.setWindowIcon(self._app_icon)
        dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptSave)
        dialog.setFileMode(QFileDialog.FileMode.AnyFile)
        dialog.setNameFilter('Imagen PNG (*.png)')
        dialog.setDirectory(str(default_dir))
        dialog.selectFile(default_path.name)
        dialog.setDefaultSuffix('png')
        dialog.setOption(QFileDialog.Option.DontUseNativeDialog, True)
        QTimer.singleShot(0, lambda: self._center_widget_on_primary_screen(dialog))

        if not dialog.exec():
            return None

        selected_files = dialog.selectedFiles()
        if not selected_files:
            return None

        selected_path = Path(selected_files[0]).expanduser()
        if selected_path.suffix.lower() != '.png':
            selected_path = selected_path.with_suffix('.png')
        return selected_path

    def _on_save_image(self) -> None:
        """Escribe el CSV (si corresponde) y guarda la imagen definitiva."""
        if self._pending_rows is None or self._pending_points is None:
            self._lbl_error.setText('No hay vista previa preparada.')
            self._show_input_page()
            self._set_widgets_state(self._interactive_widgets, enabled=True)
            return
        self._btn_save_image.setEnabled(False)
        QApplication.processEvents()
        try:
            log_info('Confirmado por el usuario: guardando imagen final...')
            effective_config = self._build_effective_plot_config(preview=False)

            if self._pending_csv_write_enabled:
                write_csv(self.csv_path, self._pending_rows)
                self.output_dir.mkdir(parents=True, exist_ok=True)
                out_path = render_plot(
                    self._pending_points,
                    effective_config,
                    output_dir=self.output_dir,
                    render_options=self._build_render_options(),
                    output_timestamp_dt=self._pending_output_timestamp_dt,
                )
            else:
                log_info('Modo sólo ver gráfico: no se modifica el CSV.')
                output_path = self._ask_output_path_for_graph_only_mode(effective_config)
                if output_path is None:
                    log_info('Guardado cancelado por el usuario.')
                    return
                out_path = render_plot(
                    self._pending_points,
                    effective_config,
                    output_path=output_path,
                    render_options=self._build_render_options(),
                )

            log_ok(f'Imagen guardada en: {out_path}')
            self.result_ok = True
            self._confirmed_close = True
            self._cleanup_preview_file()
            self.close()
        except Exception:
            tb = traceback.format_exc()
            log_error(f'Error no recuperable:\n{tb}')
            self._show_fatal_error(tb)
        finally:
            try:
                if self._current_page == 'preview' and not self._confirmed_close:
                    self._btn_save_image.setEnabled(True)
            except RuntimeError:
                pass

    # ----- helpers genéricos ------------------------------------------------

    def _set_widgets_state(self, widgets: list[QWidget], *, enabled: bool) -> None:
        """Habilita/deshabilita una lista de widgets tolerando errores."""
        for widget in widgets:
            try:
                widget.setEnabled(enabled)
            except RuntimeError:
                pass

    def _show_fatal_error(self, tb_text: str) -> None:
        """Muestra un stack trace dentro de la ventana y desactiva los controles.

        Esta es la versión PyQt6 del panel inline que el original Tkinter
        mostraba con ``tk.Text``.  Mantiene el mismo comportamiento: los
        controles quedan inutilizables y el usuario ve el error sin salir.
        """
        s = self.style
        self._lbl_error.setText(s.error_fatal)
        self._set_widgets_state(self._interactive_widgets, enabled=False)

        box = QTextEdit(self)
        box.setReadOnly(True)
        box.setPlainText(tb_text)
        box.setFont(self._font_traceback)
        box.setStyleSheet(
            f'background-color: {s.color_fatal_bg}; color: {s.color_fatal_fg};'
        )
        # Alto aproximado en líneas (traceback_height * altura de línea de la
        # fuente monoespaciada).
        fm = QFontMetrics(self._font_traceback)
        box.setFixedHeight(fm.lineSpacing() * s.traceback_height + 12)

        self._root_layout.addWidget(box)
        self._relock_window_to_current_page()

    # ----- diálogo modal "¿Descartar cambios?" ------------------------------

    def _ask_discard_confirmation(self) -> bool:
        """Muestra un diálogo modal bloqueante preguntando si se descartan los datos.

        Devuelve True si el usuario confirma el descarte (quiere cerrar) o
        False si elige volver a la vista previa.  El diálogo bloquea toda
        interacción con la ventana principal y se centra sobre la ventana del
        gráfico, no sobre la pantalla completa.
        """
        box = QMessageBox(self)
        box.setWindowTitle(CONFIRM_DISCARD_TITLE)
        box.setWindowIcon(self._app_icon)
        box.setText(CONFIRM_DISCARD_TEXT)
        box.setInformativeText(CONFIRM_DISCARD_QUESTION)
        box.setIcon(QMessageBox.Icon.Warning)
        btn_yes = box.addButton(CONFIRM_DISCARD_BTN_YES, QMessageBox.ButtonRole.DestructiveRole)
        btn_no = box.addButton(CONFIRM_DISCARD_BTN_NO, QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(btn_no)
        box.setEscapeButton(btn_no)
        box.setWindowModality(Qt.WindowModality.WindowModal)
        QTimer.singleShot(0, lambda: self._center_widget_on_parent(box, self))
        box.exec()
        return box.clickedButton() is btn_yes

    # ----- interceptar el cierre (botón X de la barra de título) ------------

    def closeEvent(self, event: QCloseEvent) -> None:  # type: ignore[override]
        """Intercepta el cierre de la ventana para exigir confirmación.

        - Si el usuario ya confirmó (flag ``_confirmed_close``), cierra.
        - Si está en la página de vista previa, abre el diálogo modal y
          cancela el cierre si el usuario elige "Volver".
        - Si está en la página de entrada, cierra sin preguntar.
        """
        if self._confirmed_close:
            self._cleanup_preview_file()
            event.accept()
            return

        if self._current_page == 'preview':
            if self._ask_discard_confirmation():
                self._confirmed_close = True
                self._cleanup_preview_file()
                log_info('Cancelado por el usuario.')
                event.accept()
            else:
                event.ignore()
            return

        # Página de entrada: cierre directo.
        self._cleanup_preview_file()
        log_info('Cancelado por el usuario.')
        event.accept()


# ╔════════════════════════════════════════════════════════════════════════════╗
# ║  SECCIÓN 9 — Resolución de rutas por defecto y CLI                       ║
# ╚════════════════════════════════════════════════════════════════════════════╝

def _existing_csvs_in_directory(directory: Path) -> list[Path]:
    """Devuelve los CSV existentes de un directorio, ordenados por nombre."""
    if not directory.exists() or not directory.is_dir():
        return []
    return sorted([path for path in directory.glob('*.csv') if path.is_file()])


def resolve_default_csv_path() -> Path:
    """Busca el CSV por defecto en los directorios solicitados, en orden."""
    home = Path.home()
    local_share_dir = home / '.local' / 'share' / 'weight-plot'
    config_dir = home / '.config' / 'weight-plot'
    script_dir = Path(__file__).resolve().parent

    ordered_candidates: list[Path] = []

    ordered_candidates.append(local_share_dir / 'valores.csv')
    ordered_candidates.extend(_existing_csvs_in_directory(local_share_dir))
    ordered_candidates.append(config_dir / 'valores.csv')
    ordered_candidates.extend(_existing_csvs_in_directory(config_dir))
    ordered_candidates.append(home / 'Documentos' / 'valores.csv')
    ordered_candidates.append(home / 'Documents' / 'valores.csv')
    ordered_candidates.append(home / 'documentos' / 'valores.csv')
    ordered_candidates.append(home / 'documents' / 'valores.csv')
    ordered_candidates.append(script_dir / 'valores.csv')
    ordered_candidates.extend(_existing_csvs_in_directory(script_dir))

    seen: set[Path] = set()
    for candidate in ordered_candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        if candidate.exists() and candidate.is_file():
            log_info(f'CSV encontrado: {candidate}')
            return candidate

    local_share_dir.mkdir(parents=True, exist_ok=True)
    created = local_share_dir / DEFAULT_CSV_FILENAME
    log_info(f'No se encontró CSV existente; se usará {created}')
    return created


def resolve_default_output_dir() -> Path:
    """Devuelve el directorio por defecto para las imágenes finales."""
    output_dir = Path.home() / '.local' / 'share' / 'weight-plot' / 'images'
    output_dir.mkdir(parents=True, exist_ok=True)
    return output_dir


def build_argument_parser() -> argparse.ArgumentParser:
    """Construye el parser de argumentos de línea de comandos."""
    p = argparse.ArgumentParser(
        prog="grafico_masa_tiempo",
        description=(
            "Registra masa corporal vía GUI, la guarda en un CSV y genera un\n"
            "gráfico de masa vs tiempo.\n\n"
            "Formato CSV: día;masa;fecha  (separador ';')\n"
            "El día es fraccionario respecto a la fecha de inicio (por defecto\n"
            "2026-03-31).  La fecha se guarda como AAAA-MM-DD HH:MM."
        ),
        formatter_class=argparse.RawTextHelpFormatter,
    )

    # Argumento posicional
    p.add_argument(
        "csv_path", nargs="?", default=None,
        help='Archivo CSV de datos. Si se omite, se busca automáticamente en ubicaciones estándar.',
    )

    # Opciones del gráfico
    p.add_argument('--output-dir', default=None,
                   help='Directorio de salida para el PNG final. Si se omite, usa ~/.local/share/weight-plot/images/.')
    p.add_argument("--start-date", default="2026-03-31",
                   help="Fecha ISO del día 0 (AAAA-MM-DD).")
    p.add_argument("--x-min",             type=float, default=0.0)
    p.add_argument("--x-max",             type=float, default=287.0)
    p.add_argument("--curve-start-t",     type=float, default=DEFAULT_CURVE_START_T)
    p.add_argument("--y-min",             type=float, default=60.0)
    p.add_argument("--y-max",             type=float, default=100.0)
    p.add_argument("--curve-stop-t",      type=float, default=DEFAULT_CURVE_STOP_T)
    p.add_argument("--stretch-factor",    type=float, default=7.0)
    p.add_argument("--x-label-step-days", type=float, default=7.0)
    p.add_argument("--y-label-step",      type=float, default=2.0)
    p.add_argument("--y-grid-step",       type=float, default=0.5)
    p.add_argument("--width-px",          type=int,   default=5326)
    p.add_argument("--height-px",         type=int,   default=2048)
    p.add_argument("--dpi",               type=int,   default=200)
    p.add_argument(
        "--smoothness",
        type=float,
        default=DEFAULT_SMOOTHNESS,
        help=(
            "Suavidad global de la curva roja: >1 más suave, "
            "<1 más pegada a los puntos."
        ),
    )

    return p


def main() -> int:
    """Punto de entrada principal.

    Parsea argumentos, crea la ``QApplication``, abre la GUI y orquesta el
    flujo completo.
    """
    parser = build_argument_parser()
    args   = parser.parse_args()

    # Parsear la fecha de inicio
    try:
        start_date = datetime.strptime(args.start_date, "%Y-%m-%d").date()
    except ValueError:
        log_error("La fecha inicial debe estar en formato AAAA-MM-DD.")
        return 2

    # Construir la configuración del gráfico desde los argumentos
    config = PlotConfig(
        start_date=start_date,
        stretch_factor=args.stretch_factor,
        curve_start_t=args.curve_start_t,
        curve_stop_t=args.curve_stop_t,
        x_min=args.x_min, x_max=args.x_max,
        y_min=args.y_min, y_max=args.y_max,
        x_label_step_days=args.x_label_step_days,
        y_label_step=args.y_label_step,
        y_grid_step=args.y_grid_step,
        width_px=args.width_px, height_px=args.height_px,
        dpi=args.dpi,
        smooth_general_smoothness=args.smoothness,
    )

    if args.csv_path:
        csv_path = Path(args.csv_path).expanduser()
    else:
        csv_path = resolve_default_csv_path()

    if args.output_dir:
        output_dir = Path(args.output_dir).expanduser()
    else:
        output_dir = resolve_default_output_dir()

    # Crear el CSV si no existe
    ensure_csv_exists(csv_path)

    # Abrir la ventana
    log_info("Abriendo ventana de ingreso de datos...")
    app = QApplication.instance() or QApplication(sys.argv)
    app.setWindowIcon(load_app_icon())
    window = MassInputApp(csv_path, config, output_dir)
    window.show()
    exit_code = app.exec()

    if window.result_ok:
        log_ok("Proceso completado con éxito.")
    else:
        log_info("Se cerró sin guardar.")

    # El exit_code de Qt no nos importa: devolvemos 0 como hacía el original.
    _ = exit_code
    return 0


# ── Ejecución directa ──
if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        log_error("Interrumpido por el usuario.")
        raise SystemExit(130)
