// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Channels" tab of the profile editor: vertical scroll, horizontal scroll, zoom (with zoom
// detection) and the step trigger of one profile, with a live curve preview per channel. The
// editor edits the profile it is given by setSection(): [global] shows the plain values, every
// other profile shows the effective value of each setting with its origin ("built-in default",
// "from the global settings", "from mode ...") and lets the user override it and reset it again
// (Reset = remove the profile's own value, the inherited one applies). Every change is stored in
// the SettingsModel and written by the autosave (UiContext::autosave); there is no Save button.
// Thin view over SettingsModel and channels_page_model.hpp.
#pragma once
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ui_context.hpp"

class QAbstractSpinBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QToolButton;

class CurveWidget;

class ChannelsEditor : public QWidget {
    Q_OBJECT
public:
    explicit ChannelsEditor(const UiContext &ctx, QWidget *parent = nullptr);
    void setSection(size_t section);  // which profile is edited (0 = [global]); refreshes every row
    size_t section() const { return section_; }
    // Shows only the rows that match the text (name, help, config key, group title); empty = all.
    void setFilter(const QString &text);
    int visibleRowCount() const;  // rows that match the filter (self-test)
    int rowCount() const { return int(rows_.size()); }
    // Removes every value of this tab from the profile (the inherited ones apply again). true if
    // anything changed; the autosave writes it.
    bool restoreDefaults();
    // Self-test seam: the editor of a config key ("SCROLL_V_SENS") and its Reset button.
    QWidget *editorFor(const std::string &key) const;
    QToolButton *resetFor(const std::string &key) const;
    QString originFor(const std::string &key) const;

private:
    struct Meta {
        std::string key;
        QLabel *origin = nullptr;
        QToolButton *reset = nullptr;
    };
    struct FilterRow {
        std::vector<QWidget *> widgets;
        std::vector<std::string> fields;  // label, help, config key, group title
        QGroupBox *group = nullptr;
    };

    // model access
    std::string displayText(const std::string &key) const;  // effective value of the shown profile
    double number(const std::string &key) const;
    bool apply(const std::string &key, const std::string &text, QWidget *editor, QLabel *help, const QString &helpText,
               bool typed);
    void showProblem(QWidget *editor, QLabel *help, const QString &helpText, const QString &error);
    void changed(const std::string &key, bool typed);
    void refreshAll();
    void refreshMeta();
    void refreshPreviews();
    void onResetKey(const std::string &key);
    void onResetGroup(const std::vector<std::string> &keys);
    void applyFilter();

    // widgets
    QAbstractSpinBox *intSpin(const std::string &key, int lo, int hi, const QString &help, QLabel *helpLabel);
    QAbstractSpinBox *dblSpin(const std::string &key, double lo, double hi, double step, int dec, const QString &help,
                              QLabel *helpLabel);
    template <class Make>
    void spinRow(QGridLayout *grid, int *row, const std::string &key, const QString &label, const QString &help,
                 Make make);
    void registerRow(const std::string &key, const QString &label, const QString &help, std::vector<QWidget *> widgets);
    void addMeta(QGridLayout *grid, int row, const std::string &key, FilterRow *fr);
    QGroupBox *buildGroup(const spn::ui::SettingsModel::Group &g);
    void addSign(QGridLayout *grid, int *row, const std::string &key);
    void addCurve(QGridLayout *grid, int *row, const std::string &key, const std::string &groupId);
    void addZoomDetection(QGridLayout *grid, int *row);

    UiContext ctx_;
    size_t section_ = 0;
    QLabel *noMatch_ = nullptr;
    std::vector<FilterRow> rows_;
    std::vector<QGroupBox *> groups_;
    QGroupBox *currentBox_ = nullptr;  // group being built
    std::string currentTitle_;
    std::string filter_;
    std::vector<std::function<void()>> refreshers_;
    std::vector<std::pair<std::string, CurveWidget *>> previews_;
    std::vector<Meta> metas_;
    std::vector<std::pair<std::string, QWidget *>> editors_;
};
