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
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QLayout>
#include <QLayoutItem>
#include <QList>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>
#include <string>

#include "config.hpp"
#include "live_feed/live_feed.hpp"
#include "ui/model/detect_model.hpp"
#include "ui/model/live_feed_model.hpp"
#include "ui/model/suspend_heartbeat.hpp"
#include "ui/model/tour_model.hpp"
#include "ui/model/device_layout_model.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/strings.hpp"

class QBoxLayout;
class QComboBox;
class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;
class QGridLayout;
class QHBoxLayout;
class QVBoxLayout;

// Removes everything from a layout, nested layouts and their widgets too, and deletes the widgets at once (a lookup
// right after a rebuild must not find the old controls).
inline void clearLayoutNow(QLayout *l) {
    while (QLayoutItem *item = l->takeAt(0)) {
        if (QLayout *sub = item->layout()) clearLayoutNow(sub);
        delete item->widget();
        delete item;
    }
}

struct KeyEngineEnv;  // key_engine_box.hpp

// The picture: items are the six motions ("m:v_push", ...) and the buttons ("b:3").
class MotionPicture : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString id;       // "m:<motion id>" or "b:<number>"
        QString title;    // "Vertical push" / "1"
        QString text;     // the action in words, short
        QString glyph;    // the symbol drawn before the action
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

    // Drag and drop of one motion onto another (swap; Ctrl, on macOS Cmd, copies). `dropOn` is what a drop does and
    // the test seam; the keyboard has "Same as" and "Swap with" in the inspector.
    bool canDrag(const QString &id) const { return id.startsWith(QLatin1String("m:")) && id != QLatin1String("m:lift"); }
    void dropOn(const QString &from, const QString &to, bool copy);
    QString dropTarget() const { return dropTarget_; }

signals:
    void dropped(const QString &from, const QString &to, bool copy);
    void selected(const QString &id);   // clicked, or moved to with the arrow keys
    void activated(const QString &id);  // Enter or Space: go to the inspector

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
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
    QPoint pressPos_;
    QString pressId_, dropTarget_;
    QString dropItemAt(const QPoint &pos, const QString &from) const;
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
    // "Take over from ...": the host asks for a source and copies the effective assignment into the form (borrow_model.hpp).
    std::function<void(spn::ui::Motion)> takeOverMotion;
    std::function<void(int n)> takeOverButton;
    std::function<void(const QString &tabId)> showTab;   // "Show in Settings tab" ("settings", "overrides")
    // One row of the shared button editor for button n (the host owns the code of the Buttons tab).
    std::function<void(QWidget *parent, QGridLayout *grid, int gridRow, int n)> addButtonRow;
    // Live parts (phase 3). Everything is optional: without them the tab works by clicking only.
    std::string runtimeDir;                              // the live feed and the output pause live there
    std::function<std::unique_ptr<spn::FeedConn>(std::string *err)> connector;  // null: the real transport
    std::function<bool()> daemonRunning;                 // for the words and the tour
    std::function<bool(const std::string &id, std::string *err)> tryMode;  // ask the daemon to switch (global)
    std::function<std::string()> activeMode;             // the joystick mode that is active now
    std::string uiProgressFile;                          // where "the tour was shown" is kept
    spn::ui::TourModel *tour = nullptr;                  // owned by the host: it survives the rebuild after an edit
    bool detect = false;                                 // Detect by moving was on before the rebuild
    std::function<void(bool)> detectChanged;
    std::string tryingFrom;                              // "" = not trying; else the joystick mode before the try
    std::function<void(const std::string &)> tryingChanged;
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
    // Detect by moving: the feed selects the motion you move and the button you press; the daemon's output is
    // paused only while it is on (the same heartbeat as the Axes page).
    void setDetect(bool on);
    bool detecting() const { return detectOn_; }
    QString detectText() const;       // the sentence next to the switch ("" = none)
    QString liveSentence() const;     // the line under the picture
    QProgressBar *liveBar() const { return liveBar_; }
    QString tryText() const;          // "Trying: ..." ("" = not trying)
    QStringList hintTexts() const;    // the hints that name the cause
    QString tourText() const;         // "" when no tour is shown
    // A sample as the feed would deliver it (also the test seam).
    void injectSample(const spn::RawAxes &axes, uint32_t buttons);

signals:
    void selectionChanged(const QString &id);

protected:
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void buildTop(QVBoxLayout *left, QHBoxLayout *tools);
    void refreshTop();
    void refreshHints();
    void refreshTour();
    void refreshSentence();
    void refreshDetect();
    void pollFeed();
    bool deviceAvailable() const;
    void tourEvent(spn::ui::TourEvent e);
    void tryThis(const std::string &modeId);
    QToolButton *helpButton(QWidget *parent, const QString &topic);
    void rebuildPicture();
    void rebuildInspector();
    void buildMotionInspector(QVBoxLayout *col, spn::ui::Motion m);
    void buildLiftInspector(QVBoxLayout *col);
    void buildButtonInspector(QVBoxLayout *col, int n);
    void buildTuning(QVBoxLayout *col, spn::ui::Motion m);
    KeyEngineEnv keyEngineEnv(spn::ui::ChannelForm *ref, spn::ui::Motion m);
    QString T(const char *s) const;

    MotionEditorEnv env_;
    QBoxLayout *root_ = nullptr;
    QWidget *leftBox_ = nullptr;
    MotionPicture *picture_ = nullptr;
    QWidget *inspectorBox_ = nullptr;
    QVBoxLayout *inspectorCol_ = nullptr;
    QLabel *sentence_ = nullptr, *inspectorTitle_ = nullptr;
    QComboBox *deviceCombo_ = nullptr;
    int deviceChoice_ = 0;
    // live parts
    QPushButton *detectBtn_ = nullptr, *tryBtn_ = nullptr, *backBtn_ = nullptr, *tourBtn_ = nullptr;
    QLabel *detectState_ = nullptr, *tryState_ = nullptr, *tourLabel_ = nullptr;
    QFrame *tourBox_ = nullptr;
    QPushButton *tourClick_ = nullptr, *tourNext_ = nullptr, *tourSkip_ = nullptr;
    QComboBox *templates_ = nullptr;
    QWidget *hintBox_ = nullptr;
    QProgressBar *liveBar_ = nullptr;
    std::unique_ptr<spn::LiveFeedClient> client_;
    std::unique_ptr<spn::ui::SuspendHeartbeat> suspend_;
    spn::LiveFeedModel feed_;
    spn::ui::DetectModel detectModel_;
    QTimer *timer_ = nullptr;
    QElapsedTimer clock_;
    bool detectOn_ = false, haveLive_ = false, fromDetect_ = false;
    spn::RawAxes live_{};
    std::string tryingFrom_;
    QStringList hints_;
};
