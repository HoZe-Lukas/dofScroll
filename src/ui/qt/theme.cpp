// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "theme.hpp"

#include <QAbstractSpinBox>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QGroupBox>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QProxyStyle>
#include <QSlider>
#include <QStyle>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QWheelEvent>
#ifdef QT_SVG_LIB
#include <QSvgRenderer>
#endif

#include "ui/model/design_tokens.hpp"

namespace {

namespace tk = spn::ui::tokens;

QColor qc(spn::ui::Rgb c) { return QColor(c.r, c.g, c.b); }

// Style, palette and font the application started with, kept to switch back to "System".
struct Original {
    bool saved = false;
    QString style;
    QPalette palette;
    QFont font;
    QString currentStyle;  // key of the style the SpacingStyle wraps now
};
Original &original() {
    static Original o;
    return o;
}

// Margins and gaps of every layout that does not set its own: one scale for all pages and dialogs
// (design_tokens.hpp). The layout asks the style with its parent widget, so group boxes get card
// padding and everything else the page margin; a layout that sets its own numbers keeps them.
class SpacingStyle : public QProxyStyle {
public:
    explicit SpacingStyle(const QString &key) : QProxyStyle(key) {}
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override {
        const bool card = qobject_cast<const QGroupBox *>(widget) != nullptr;
        switch (metric) {
        case PM_LayoutLeftMargin:
        case PM_LayoutRightMargin:
        case PM_LayoutBottomMargin:
            return card ? tk::kCardPadding : tk::kPageMargin;
        case PM_LayoutTopMargin:
            return card ? tk::kSpaceSm : tk::kPageMargin;
        case PM_LayoutHorizontalSpacing:
            return tk::kRowGap;
        case PM_LayoutVerticalSpacing:
            return card ? tk::kRowGap : tk::kSpaceLg;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

// A wheel never changes a combo box, spin box or slider: it goes to the surrounding widget
// (usually the scroll area of the page), so scrolling a page with the mouse over a control scrolls.
class WheelGuard : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::Wheel) return false;
        auto *w = qobject_cast<QWidget *>(watched);
        if (!w || !(qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QSlider *>(w)))
            return false;
        auto *we = static_cast<QWheelEvent *>(event);
        // Hand the wheel to the ancestors, nearest first, until one takes it (a scroll area does).
        for (QWidget *parent = w->parentWidget(); parent; parent = parent->parentWidget()) {
            const QPointF local(w->mapTo(parent, we->position().toPoint()));
            QWheelEvent forwarded(local, we->globalPosition(), we->pixelDelta(), we->angleDelta(), we->buttons(),
                                  we->modifiers(), we->phase(), we->inverted(), we->source());
            QApplication::sendEvent(parent, &forwarded);
            if (forwarded.isAccepted() || parent->isWindow()) break;
        }
        return true;  // the control itself never sees the wheel
    }
};

QString hex(spn::ui::Rgb c) {
    return QStringLiteral("#%1%2%3")
        .arg(c.r, 2, 16, QLatin1Char('0'))
        .arg(c.g, 2, 16, QLatin1Char('0'))
        .arg(c.b, 2, 16, QLatin1Char('0'));
}

// Small glyphs (arrows, check mark, radio dot) as PNG files: a style sheet can only point at image
// files. One private directory per run; a new generation per theme change, because Qt caches style
// sheet images by file name.
struct Glyphs {
    QTemporaryDir dir;
    int generation = 0;
};
Glyphs &glyphs() {
    static Glyphs g;
    return g;
}

enum class Glyph { Down, Up, Check, Dot, SwitchOff, SwitchOn };

QString glyphUrl(const QString &name, spn::ui::Rgb color, Glyph glyph) {
    Glyphs &g = glyphs();
    if (!g.dir.isValid()) return QStringLiteral("none");
    const int s = 48;
    const bool sw = glyph == Glyph::SwitchOff || glyph == Glyph::SwitchOn;
    QImage image(sw ? s * 2 : s, s, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(qc(color), s / 8.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        switch (glyph) {
        case Glyph::Down:
            p.drawPolyline(QPolygonF({QPointF(s * .2, s * .36), QPointF(s * .5, s * .66), QPointF(s * .8, s * .36)}));
            break;
        case Glyph::Up:
            p.drawPolyline(QPolygonF({QPointF(s * .2, s * .64), QPointF(s * .5, s * .34), QPointF(s * .8, s * .64)}));
            break;
        case Glyph::Check:
            p.drawPolyline(QPolygonF({QPointF(s * .24, s * .52), QPointF(s * .43, s * .70), QPointF(s * .76, s * .32)}));
            break;
        case Glyph::SwitchOff:
        case Glyph::SwitchOn:  // `color` is the track; the knob is white
            p.setPen(Qt::NoPen);
            p.setBrush(qc(color));
            p.drawRoundedRect(QRectF(2, s * .08, s * 2 - 4, s * .84), s * .42, s * .42);
            p.setBrush(Qt::white);
            p.drawEllipse(QRectF(glyph == Glyph::SwitchOn ? s * 2 - s * .72 - s * .12 : s * .12 + 2, s * .5 - s * .30, s * .6, s * .6));
            break;
        case Glyph::Dot:
            p.setPen(Qt::NoPen);
            p.setBrush(qc(color));
            p.drawEllipse(QRectF(s * .3, s * .3, s * .4, s * .4));
            break;
        }
    }
    const QString file = g.dir.path() + QStringLiteral("/%1-%2.png").arg(name).arg(g.generation);
    image.save(file);
    return QStringLiteral("url(\"%1\")").arg(file);
}

// The application style sheet: shapes (rounded, flat, thin borders), the accent and the states of
// every common widget, built from the colours of the theme and the numbers of design_tokens.hpp.
QString styleSheetFor(const spn::ui::ThemeColors &c) {
    using spn::ui::mixColors;
    glyphs().generation++;
    const QString down = glyphUrl(QStringLiteral("down"), c.windowText, Glyph::Down);
    const QString up = glyphUrl(QStringLiteral("up"), c.windowText, Glyph::Up);
    const QString dot = glyphUrl(QStringLiteral("dot"), c.highlightedText, Glyph::Dot);
    const QString check = glyphUrl(QStringLiteral("check"), c.highlightedText, Glyph::Check);
    const QString swOff = glyphUrl(QStringLiteral("swoff"), c.controlBorder, Glyph::SwitchOff);
    const QString swOn = glyphUrl(QStringLiteral("swon"), c.highlight, Glyph::SwitchOn);
    const QString swOffDis = glyphUrl(QStringLiteral("swoffd"), c.border, Glyph::SwitchOff);
    const QString swOnDis = glyphUrl(QStringLiteral("swond"), mixColors(c.window, c.highlight, 0.4), Glyph::SwitchOn);
    const QString soft = hex(c.accentSoft);
    const QString r = QString::number(tk::kRadius);
    const QString rc = QString::number(tk::kRadiusCard);
    const QString inner = QString::number(tk::kControlHeight - 12);  // content height; padding 5+5, border 1+1
    const QString scroll = hex(c.controlBorder);
    QString q;
    auto add = [&q](const QString &s) {
        q += s;
        q += QLatin1Char('\n');
    };
    add(QStringLiteral("QToolTip { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 6px 10px; }")
            .arg(hex(c.surface), hex(c.text), hex(c.border), r));
    // cards
    add(QStringLiteral("QGroupBox { background: %1; border: 1px solid %2; border-radius: %3px; margin-top: 0; padding-top: 40px; "
                       "font-weight: 600; }")
            .arg(hex(c.surface), hex(c.border), rc));
    add(QStringLiteral("QGroupBox::title { subcontrol-origin: padding; subcontrol-position: top left; left: %1px; top: 12px; }")
            .arg(tk::kCardPadding));
    add(QStringLiteral("QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QRadioButton, QGroupBox QPushButton, QGroupBox QComboBox, "
                       "QGroupBox QLineEdit, QGroupBox QAbstractSpinBox, QGroupBox QAbstractItemView, QGroupBox QToolButton "
                       "{ font-weight: normal; }"));
    // buttons
    add(QStringLiteral("QPushButton, QToolButton { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; "
                       "padding: 5px 18px; min-height: %5px; }")
            .arg(hex(c.button), hex(c.buttonText), hex(c.controlBorder), r, inner));
    add(QStringLiteral("QToolButton { padding: 5px 10px; }"));
    add(QStringLiteral("QPushButton:hover, QToolButton:hover { background: %1; }").arg(hex(c.hover)));
    add(QStringLiteral("QPushButton:pressed, QToolButton:pressed { background: %1; }").arg(hex(c.pressed)));
    // focus: a 2 px ring in the accent (padding one pixel less, so nothing moves); never removed
    add(QStringLiteral("QPushButton:focus, QToolButton:focus { border: 2px solid %1; padding: 4px 17px; }").arg(hex(c.focusRing)));
    add(QStringLiteral("QToolButton:focus { padding: 4px 9px; }"));
    add(QStringLiteral("QPushButton:disabled, QToolButton:disabled { color: %1; background: %2; border-color: %3; }")
            .arg(hex(c.disabledText), hex(c.window), hex(c.border)));
    add(QStringLiteral("QPushButton[primary=\"true\"], QPushButton:default { background: %1; color: %2; border: 1px solid %1; }")
            .arg(hex(c.highlight), hex(c.highlightedText)));
    add(QStringLiteral("QPushButton[primary=\"true\"]:hover, QPushButton:default:hover { background: %1; border-color: %1; }")
            .arg(hex(c.accentHover)));
    add(QStringLiteral("QPushButton[primary=\"true\"]:pressed, QPushButton:default:pressed { background: %1; border-color: %1; }")
            .arg(hex(c.accentPressed)));
    add(QStringLiteral("QPushButton[primary=\"true\"]:disabled, QPushButton:default:disabled { background: %1; color: %2; border-color: %3; }")
            .arg(hex(c.window), hex(c.disabledText), hex(c.border)));
    // inputs
    add(QStringLiteral("QLineEdit, QTextEdit, QPlainTextEdit, QAbstractSpinBox, QComboBox { background: %1; color: %2; "
                       "border: 1px solid %3; border-radius: %4px; padding: 5px 10px; min-height: %5px; "
                       "selection-background-color: %6; selection-color: %7; }")
            .arg(hex(c.base), hex(c.text), hex(c.controlBorder), r, inner, hex(c.highlight), hex(c.highlightedText)));
    add(QStringLiteral("QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QAbstractSpinBox:focus, QComboBox:focus, "
                       "QComboBox:on { border: 2px solid %1; padding: 4px 9px; }")
            .arg(hex(c.focusRing)));
    add(QStringLiteral("QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled { background: %1; color: %2; }")
            .arg(hex(c.window), hex(c.disabledText)));
    add(QStringLiteral("QComboBox { padding-right: 32px; } QComboBox:focus, QComboBox:on { padding-right: 31px; }"));
    add(QStringLiteral("QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 30px; "
                       "border: none; }"));
    add(QStringLiteral("QComboBox::down-arrow { image: %1; width: 12px; height: 12px; }").arg(down));
    add(QStringLiteral("QComboBox QAbstractItemView { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; "
                       "padding: 4px; selection-background-color: %5; selection-color: %2; outline: 0; }")
            .arg(hex(c.base), hex(c.text), hex(c.border), r, soft));
    add(QStringLiteral("QComboBox QAbstractItemView::item { min-height: %1px; padding: 2px 8px; border-radius: 6px; }")
            .arg(tk::kRowHeight - 4));
    add(QStringLiteral("QAbstractSpinBox { padding-right: 28px; min-width: 96px; max-width: 180px; } "
                       "QAbstractSpinBox:focus { padding-right: 27px; }"));
    add(QStringLiteral("QAbstractSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 24px; "
                       "border: none; background: transparent; }"));
    add(QStringLiteral("QAbstractSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 24px; "
                       "border: none; background: transparent; }"));
    add(QStringLiteral("QAbstractSpinBox::up-arrow { image: %1; width: 10px; height: 10px; }").arg(up));
    add(QStringLiteral("QAbstractSpinBox::down-arrow { image: %1; width: 10px; height: 10px; }").arg(down));
    // check boxes, radio buttons, sliders
    add(QStringLiteral("QCheckBox, QRadioButton { spacing: 12px; min-height: %1px; }").arg(tk::kControlHeight - 8));
    // check boxes are toggle switches (Fluent / macOS style); radio buttons stay round
    add(QStringLiteral("QCheckBox::indicator { width: 40px; height: 22px; border: none; background: transparent; image: %1; }").arg(swOff));
    add(QStringLiteral("QCheckBox::indicator:checked, QCheckBox::indicator:indeterminate { image: %1; }").arg(swOn));
    add(QStringLiteral("QCheckBox::indicator:disabled { image: %1; }").arg(swOffDis));
    add(QStringLiteral("QCheckBox::indicator:checked:disabled { image: %1; }").arg(swOnDis));
    // a check box that is one of several choices (modifier keys, axes) stays a box: setProperty("look", "box")
    add(QStringLiteral("QCheckBox[look=\"box\"]::indicator { width: 18px; height: 18px; border-radius: 5px; border: 1px solid %1; background: %2; image: none; }")
            .arg(hex(c.controlBorder), hex(c.base)));
    add(QStringLiteral("QCheckBox[look=\"box\"]::indicator:disabled { image: none; background: %1; }").arg(hex(c.window)));
    add(QStringLiteral("QCheckBox[look=\"box\"]::indicator:checked { background: %1; border-color: %1; image: %2; }").arg(hex(c.highlight), check));
    add(QStringLiteral("QCheckBox[look=\"box\"]::indicator:hover { border-color: %1; }").arg(hex(c.highlight)));
    add(QStringLiteral("QRadioButton::indicator { width: 18px; height: 18px; border-radius: 10px; border: 1px solid %1; background: %2; }")
            .arg(hex(c.controlBorder), hex(c.base)));
    add(QStringLiteral("QRadioButton::indicator:hover { border-color: %1; }").arg(hex(c.highlight)));
    add(QStringLiteral("QRadioButton::indicator:checked { background: %1; border-color: %1; image: %2; }")
            .arg(hex(c.highlight), dot));
    add(QStringLiteral("QRadioButton::indicator:disabled { background: %1; border-color: %2; }").arg(hex(c.window), hex(c.border)));
    add(QStringLiteral("QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: %1; }").arg(hex(c.controlBorder)));
    add(QStringLiteral("QSlider::sub-page:horizontal { border-radius: 3px; background: %1; }").arg(hex(c.highlight)));
    add(QStringLiteral("QSlider::handle:horizontal { width: 18px; height: 18px; margin: -7px 0; border-radius: 10px; "
                       "background: %1; border: 2px solid %2; }")
            .arg(hex(c.base), hex(c.highlight)));
    add(QStringLiteral("QSlider::handle:horizontal:focus { border: 3px solid %1; }").arg(hex(c.focusRing)));
    // check boxes, radio buttons and switches: the focus ring is a 2 px frame around box and text
    add(QStringLiteral("QCheckBox, QRadioButton { border: 2px solid transparent; border-radius: 6px; padding: 0 2px; }"));
    add(QStringLiteral("QCheckBox:focus, QRadioButton:focus { border: 2px solid %1; }").arg(hex(c.focusRing)));
    // scroll bars
    add(QStringLiteral("QScrollBar:vertical { width: 14px; background: transparent; margin: 2px; }"));
    add(QStringLiteral("QScrollBar:horizontal { height: 14px; background: transparent; margin: 2px; }"));
    add(QStringLiteral("QScrollBar::handle { background: %1; border-radius: 5px; min-height: 36px; min-width: 36px; }").arg(scroll));
    add(QStringLiteral("QScrollBar::handle:hover { background: %1; }").arg(hex(mixColors(c.window, c.windowText, 0.5))));
    add(QStringLiteral("QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }"));
    add(QStringLiteral("QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"));
    add(QStringLiteral("QScrollArea { border: none; background: transparent; }"));
    // page tabs: underline navigation
    add(QStringLiteral("QTabWidget::pane { border: none; border-top: 1px solid %1; }").arg(hex(c.border)));
    add(QStringLiteral("QTabWidget::tab-bar { left: %1px; }").arg(tk::kSpaceSm));
    add(QStringLiteral("QTabWidget QTabBar::tab { background: transparent; color: %1; border: none; "
                       "border-bottom: 3px solid transparent; padding: 10px 18px 8px 18px; margin-right: 4px; }")
            .arg(hex(c.placeholderText)));
    add(QStringLiteral("QTabWidget QTabBar::tab:hover { color: %1; }").arg(hex(c.windowText)));
    add(QStringLiteral("QTabWidget QTabBar::tab:focus { background: %1; color: %2; border-radius: 6px; }")
            .arg(soft, hex(c.selectedText)));
    add(QStringLiteral("QTabWidget QTabBar::tab:selected { color: %1; border-bottom: 3px solid %2; font-weight: 600; }")
            .arg(hex(c.windowText), hex(c.highlight)));
    // lists and trees
    add(QStringLiteral("QTreeView, QListView, QTableView, QListWidget { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: %4px; padding: 4px; outline: 0; }")
            .arg(hex(c.base), hex(c.text), hex(c.controlBorder), r));
    // keyboard focus on a list or tree: the 2 px ring (padding one pixel less)
    add(QStringLiteral("QTreeView:focus, QListView:focus, QTableView:focus, QListWidget:focus "
                       "{ border: 2px solid %1; padding: 3px; }")
            .arg(hex(c.focusRing)));
    add(QStringLiteral("QTreeView::item, QListView::item { min-height: %1px; padding: 2px 6px; border-radius: 6px; }")
            .arg(tk::kRowHeight - 4));
    add(QStringLiteral("QTreeView::item:hover, QListView::item:hover { background: %1; }").arg(hex(c.hover)));
    // selected rows (focused or not: no :active/:focus condition), also under the mouse: the soft accent with
    // the selected text colour (AAA); never HighlightedText, which is meant for the strong accent
    add(QStringLiteral("QTreeView::item:selected, QListView::item:selected, QTableView::item:selected "
                       "{ background: %1; color: %2; }")
            .arg(soft, hex(c.selectedText)));
    add(QStringLiteral("QTreeView::item:selected:hover, QListView::item:selected:hover, QTableView::item:selected:hover "
                       "{ background: %1; color: %2; }")
            .arg(soft, hex(c.selectedText)));
    add(QStringLiteral("QTreeView::branch { background: transparent; } QTreeView::branch:selected { background: %1; }").arg(soft));
    add(QStringLiteral("QHeaderView::section { background: %1; color: %2; border: none; border-bottom: 1px solid %3; "
                       "padding: 6px 8px; }")
            .arg(hex(c.window), hex(c.windowText), hex(c.border)));
    // menus, status bar, splitter
    add(QStringLiteral("QMenu { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 6px; }")
            .arg(hex(c.surface), hex(c.text), hex(c.border), r));
    add(QStringLiteral("QMenu::item { padding: 7px 26px 7px 12px; border-radius: 6px; }"));
    add(QStringLiteral("QMenu::item:selected { background: %1; color: %2; }").arg(soft, hex(c.selectedText)));
    add(QStringLiteral("QMenu::item:disabled { color: %1; }").arg(hex(c.disabledText)));
    add(QStringLiteral("QMenu::separator { height: 1px; background: %1; margin: 5px 8px; }").arg(hex(c.border)));
    add(QStringLiteral("QStatusBar { border-top: 1px solid %1; } QStatusBar::item { border: none; } "
                       "QStatusBar QLabel { padding: 4px 12px; }")
            .arg(hex(c.border)));
    add(QStringLiteral("QSplitter::handle { background: transparent; }"));
    add(QStringLiteral("QDialogButtonBox QPushButton { min-width: 88px; }"));
    add(QStringLiteral("QFrame#sidebar { background: %1; border: none; border-right: 1px solid %2; }")
            .arg(hex(c.surface), hex(c.border)));
    add(QStringLiteral("QFrame[card=\"true\"] { background: %1; border: 1px solid %2; border-radius: %3px; }")
            .arg(hex(c.window), hex(c.border), rc));
    add(QStringLiteral("QFrame[card=\"true\"] QLabel { border: none; background: transparent; }"));
    // problems next to a field: label.setProperty("error", true) (text, never colour alone) and
    // input.setProperty("invalid", true) (red border)
    add(QStringLiteral("QLabel[error=\"true\"] { color: %1; }").arg(hex(c.danger)));
    add(QStringLiteral("QLineEdit[invalid=\"true\"], QAbstractSpinBox[invalid=\"true\"], QComboBox[invalid=\"true\"] "
                       "{ border: 1px solid %1; }")
            .arg(hex(c.danger)));
    // headings: label.setProperty("heading", "page" or "section")
    const double base = QApplication::font().pointSizeF() > 0 ? QApplication::font().pointSizeF() : 10.0;
    add(QStringLiteral("QLabel[heading=\"page\"] { font-size: %1pt; font-weight: 600; }").arg(base * 1.4));
    add(QStringLiteral("QLabel[heading=\"section\"] { font-size: %1pt; font-weight: 600; }").arg(base * 1.15));
    return q;
}

// The colours of the sheet for "System": the platform palette with our accent and shapes.
spn::ui::ThemeColors systemColors(const QPalette &p) {
    auto rgb = [](const QColor &c) {
        return spn::ui::Rgb{static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue())};
    };
    const bool dark = p.color(QPalette::Window).lightness() < 128;
    spn::ui::ThemeColors c = dark ? spn::ui::darkColors() : spn::ui::lightColors();
    c.window = rgb(p.color(QPalette::Window));
    c.windowText = rgb(p.color(QPalette::WindowText));
    c.base = rgb(p.color(QPalette::Base));
    c.text = rgb(p.color(QPalette::Text));
    c.button = rgb(p.color(QPalette::Button));
    c.buttonText = rgb(p.color(QPalette::ButtonText));
    c.disabledText = rgb(p.color(QPalette::Disabled, QPalette::WindowText));
    c.placeholderText = rgb(p.color(QPalette::PlaceholderText));
    return spn::ui::finishColors(c);
}

spn::ui::ThemeColors &current() {
    static spn::ui::ThemeColors c = spn::ui::lightColors();
    return c;
}

// The system asks for high contrast: Qt 6.10+ reports it on every platform; on Windows the high-contrast
// flag is read directly (older Qt). Elsewhere nothing reliable exists, so nothing is guessed from the palette
// (a plain black-on-white theme must keep the normal look).
bool systemHighContrast(const QPalette &) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    if (QGuiApplication::styleHints()->contrastPreference() == Qt::ContrastPreference::HighContrast) return true;
#endif
#ifdef Q_OS_WIN
    HIGHCONTRASTW hc{sizeof(HIGHCONTRASTW), 0, nullptr};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) return true;
#endif
    return false;
}

void setBaseStyle(QApplication *app, const QString &key) {
    Original &o = original();
    if (o.currentStyle == key) return;
    o.currentStyle = key;
    app->setStyle(new SpacingStyle(key));
}

void initLook(QApplication *app) {
    Original &o = original();
    if (o.saved) return;
    o.style = app->style()->objectName();
    o.palette = app->palette();
    o.font = app->font();
    o.saved = true;
    QFont f = o.font;
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * tk::kFontBoostPercent / 100.0);
#ifdef Q_OS_LINUX
    // A clean sans first where it is installed (the platform fonts of Windows and macOS are kept).
    f.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans"), f.family()});
#endif
    app->setFont(f);
    app->installEventFilter(new WheelGuard(app));
    setBaseStyle(app, o.style);
}

}  // namespace

void installLook() {
    if (qApp) initLook(qApp);
}

QPalette paletteFor(const spn::ui::ThemeColors &c) {
    QPalette p;
    p.setColor(QPalette::Window, qc(c.window));
    p.setColor(QPalette::WindowText, qc(c.windowText));
    p.setColor(QPalette::Base, qc(c.base));
    p.setColor(QPalette::AlternateBase, qc(c.window));
    p.setColor(QPalette::Text, qc(c.text));
    p.setColor(QPalette::Button, qc(c.button));
    p.setColor(QPalette::ButtonText, qc(c.buttonText));
    p.setColor(QPalette::Highlight, qc(c.highlight));
    p.setColor(QPalette::HighlightedText, qc(c.highlightedText));
    p.setColor(QPalette::Link, qc(c.link));
    p.setColor(QPalette::LinkVisited, qc(c.link));
    p.setColor(QPalette::PlaceholderText, qc(c.placeholderText));
    p.setColor(QPalette::ToolTipBase, qc(c.base));
    p.setColor(QPalette::ToolTipText, qc(c.text));
    p.setColor(QPalette::BrightText, qc(c.highlightedText));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, qc(c.disabledText));
    return p;
}

void applyTheme(spn::ui::Theme theme) {
    QApplication *app = qApp;
    if (!app) return;
    initLook(app);
    Original &o = original();
    spn::ui::ThemeColors colors;
    if (spn::ui::themeColors(theme, &colors)) {
        setBaseStyle(app, QStringLiteral("fusion"));
        app->setPalette(paletteFor(colors));
    } else {
        setBaseStyle(app, o.style);
        app->setPalette(o.palette);
        colors = systemColors(o.palette);
        // A high-contrast request of the operating system wins: the platform palette without our accents
        // and shapes (no style sheet), as the user asked for it.
        if (systemHighContrast(o.palette)) {
            current() = colors;
            app->setStyleSheet(QString());
            return;
        }
    }
    current() = colors;
    app->setStyleSheet(styleSheetFor(colors));
}

QColor selectedTextColor(bool muted) {
    const spn::ui::Rgb c = muted ? current().selectedMutedText : current().selectedText;
    return QColor(c.r, c.g, c.b);
}

QIcon themedIcon(const QString &resource, const QColor &color, int size) {
#ifdef QT_SVG_LIB
    QSvgRenderer renderer(resource);
    if (!renderer.isValid() || size <= 0) return QIcon();
    const int scale = 2;  // one image at twice the size serves normal and high-DPI screens
    QImage image(size * scale, size * scale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        renderer.render(&painter);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
    }
    image.setDevicePixelRatio(scale);
    return QIcon(QPixmap::fromImage(image));
#else
    Q_UNUSED(resource);
    Q_UNUSED(color);
    Q_UNUSED(size);
    return QIcon();
#endif
}
