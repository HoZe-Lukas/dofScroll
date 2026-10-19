// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "theme.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#ifdef QT_SVG_LIB
#include <QSvgRenderer>
#endif

namespace {

QColor qc(spn::ui::Rgb c) { return QColor(c.r, c.g, c.b); }

// Style and palette the application started with, kept to switch back to "System".
struct Original {
    bool saved = false;
    QString style;
    QPalette palette;
};
Original &original() {
    static Original o;
    return o;
}

}  // namespace

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
    Original &o = original();
    if (!o.saved) {
        o.style = app->style()->objectName();
        o.palette = app->palette();
        o.saved = true;
    }
    spn::ui::ThemeColors colors;
    if (spn::ui::themeColors(theme, &colors)) {
        if (app->style()->objectName() != QLatin1String("fusion")) app->setStyle(QStringLiteral("Fusion"));
        app->setPalette(paletteFor(colors));
    } else {
        if (app->style()->objectName() != o.style) app->setStyle(o.style);
        app->setPalette(o.palette);
    }
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
