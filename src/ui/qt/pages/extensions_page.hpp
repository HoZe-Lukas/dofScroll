// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Extensions" page (issue #129): the joystick mode files and button mode files of the user in one list,
// each with a switch (on = "<id>.conf", off = "<id>.conf.disabled", see ui/model/extensions_model.hpp),
// where it is used, and Remove. A thin view on the model; nothing is executed, the daemon's loaders read
// the files with their own checks. A switch is written at once (no Save button).
#pragma once
#include <QString>
#include <QWidget>
#include <map>
#include <vector>

#include "ui/model/extensions_model.hpp"
#include "ui_context.hpp"

class QCheckBox;
class QLabel;
class QVBoxLayout;

class ExtensionsPage : public QWidget {
    Q_OBJECT
public:
    explicit ExtensionsPage(const UiContext &ctx, QWidget *parent = nullptr);
    void refresh();  // reads both folders again
    int count() const { return int(entries_.size()); }
    // kind: "joystick" or "button". For the self-test and the switches: the same code path as a click.
    bool exists(const QString &kind, const QString &id) const;
    bool isEnabled(const QString &kind, const QString &id) const;
    bool setEnabled(const QString &kind, const QString &id, bool on);  // false + message() on a problem
    bool removeEntry(const QString &kind, const QString &id, bool ask);
    QCheckBox *switchOf(const QString &kind, const QString &id) const;
    QString message() const;  // the last problem ("" = none)

protected:
    void showEvent(QShowEvent *e) override;

private:
    const spn::ui::ExtensionEntry *find(const QString &kind, const QString &id) const;
    void rebuild();
    void showMessage(const QString &text, bool error);
    static QString kindName(spn::ui::ExtensionKind k);
    std::vector<QString> usedBy(spn::ui::ExtensionKind kind, const std::string &id) const;

    UiContext ctx_;
    std::vector<spn::ui::ExtensionEntry> entries_;
    std::map<QString, QCheckBox *> switches_;  // "kind:id"
    QVBoxLayout *joystickList_ = nullptr, *buttonList_ = nullptr;
    QLabel *joystickNone_ = nullptr, *buttonNone_ = nullptr, *message_ = nullptr;
    QWidget *openJoystick_ = nullptr, *openButton_ = nullptr;  // "Open folder" of each section
    QString problem_;
};

QWidget *createExtensionsPage(const UiContext &ctx);
