// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The "Motions and buttons" tab of the joystick mode editor (issue #143): a schematic picture of the cap with its six
// motions and the numbered buttons of the device, and an inspector for the selected item. It edits the ModeForm
// of the editor (spn::ui::motion_map_model: split and unsplit, reverse direction, lift/press as a mix term);
// the form is written by the editor's autosave like every other tab, there is no Save button.
//
// The picture is a second view of the same data as the Settings and Buttons tabs: everything it shows is also in
// words there, and the picture itself is reachable by keyboard (arrow keys between items, Enter to go to the
// inspector). Every state has a word (a muted, dashed item says "Not used" or "Unassigned"), never colour alone.
// All lengths follow the font metrics, so the layout also holds at a large font size.
#pragma once
#include <QList>
#include <QRect>
#include <QString>
#include <QWidget>
#include <functional>
#include <string>

#include "config.hpp"
#include "ui/model/device_layout_model.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/strings.hpp"

class QComboBox;
class QLabel;
class QGridLayout;
class QVBoxLayout;

// The picture: items are the six motions ("m:v_push", ...) and the buttons ("b:3").
class MotionPicture : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString id;       // "m:<motion id>" or "b:<number>"
        QString title;    // "Vertical push" / "1"
        QString text;     // the action in words, short
        QString tooltip;  // full sentence
        bool muted = false;
        double ux = 0.5, uy = 0.5;  // unit position of a button (device_layout_model.hpp): left or right group, row order
        QRect rect;       // set by the layout
    };
    explicit MotionPicture(QWidget *parent = nullptr);
    void setItems(const QList<Item> &items, spn::ui::LayoutKind kind);
    const QList<Item> &items() const { return items_; }
    QString current() const { return current_; }
    void setCurrent(const QString &id);
    QRect itemRect(const QString &id) const;  // in widget coordinates (tests)
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

signals:
    void selected(const QString &id);   // clicked, or moved to with the arrow keys
    void activated(const QString &id);  // Enter or Space: go to the inspector

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void changeEvent(QEvent *) override;
    bool event(QEvent *) override;

private:
    struct Plan {
        QList<QRect> rects;
        QRect cap;
        int height = 0;
        int buttonsTop = 0;
    };
    Plan plan(int width) const;
    int minimumWidth() const;
    void layoutItems();
    int indexOf(const QString &id) const;
    int neighbour(int from, int dx, int dy) const;
    void updateAccessible();

    QList<Item> items_;
    spn::ui::LayoutKind kind_ = spn::ui::LayoutKind::None;
    QString current_;
    QRect capRect_;
    int buttonsTop_ = 0;
};

struct MotionEditorEnv {
    spn::ui::ModeForm *form = nullptr;
    spn::Settings base;                                  // the profile the mode runs on top of (channel mixes, signs)
    const spn::ui::StringProvider *strings = nullptr;
    std::string deviceName;                              // the attached device ("" = none)
    int reportedButtons = 0;                             // the count to use for a device without a known factory count
    std::function<void()> changed;                       // the form changed: the host saves and rebuilds
    std::function<void()> committed;                     // a value in place changed (key picker): the host saves now
    std::function<void()> typed;                         // typed text changed: the host saves after a pause
    std::function<void()> flush;                         // focus left a text field: write now
    std::function<void(const QString &tabId)> showTab;   // "Show in Settings tab" ("settings", "overrides")
    // One row of the shared button editor for button n (the host owns the code of the Buttons tab).
    std::function<void(QWidget *parent, QGridLayout *grid, int gridRow, int n)> addButtonRow;
};

class MotionEditor : public QWidget {
    Q_OBJECT
public:
    MotionEditor(const MotionEditorEnv &env, const QString &selected, QWidget *parent = nullptr);
    QString selectedId() const { return picture_->current(); }
    bool select(const QString &id);
    MotionPicture *picture() const { return picture_; }
    QWidget *inspector() const { return inspectorBox_; }
    QString inspectorTitle() const;  // the heading of the inspector (tests)

signals:
    void selectionChanged(const QString &id);

private:
    void rebuildPicture();
    void rebuildInspector();
    void buildMotionInspector(QVBoxLayout *col, spn::ui::Motion m);
    void buildLiftInspector(QVBoxLayout *col);
    void buildButtonInspector(QVBoxLayout *col, int n);
    void buildTuning(QVBoxLayout *col, spn::ui::Motion m);
    QString T(const char *s) const;

    MotionEditorEnv env_;
    MotionPicture *picture_ = nullptr;
    QWidget *inspectorBox_ = nullptr;
    QVBoxLayout *inspectorCol_ = nullptr;
    QLabel *sentence_ = nullptr, *inspectorTitle_ = nullptr;
    QComboBox *deviceCombo_ = nullptr;
    int deviceChoice_ = 0;
};
