// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "driver_cleanup_wizard.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizardPage>

#include <algorithm>
#include <utility>

#include "a11y_announce.hpp"
#include "plain_text.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui_i18n.hpp"

namespace {

namespace m = spn::ui;

QString nativeDir(const std::string &dir) { return QDir::toNativeSeparators(qs8(dir)); }

QString outputText(m::DriverOutput o, const std::string &id) {
    switch (o) {
    case m::DriverOutput::MouseWheel: return T("mouse wheel");
    case m::DriverOutput::Keystroke: return T("keys");
    case m::DriverOutput::MouseMove: return T("mouse movement");
    case m::DriverOutput::Joystick: return T("joystick");
    case m::DriverOutput::Passthrough: return T("passed to the program");
    case m::DriverOutput::Other: break;
    }
    return qs8(id);
}

// "mouse wheel: X, Ry; keys: Z": what the driver does with which axes (an axis once per group).
QString axesText(const m::DriverApp &a) {
    QStringList parts;
    for (const m::DriverAxisGroup &g : m::groupDriverAxes(a)) {
        QStringList axes;
        for (const std::string &x : g.axes) axes << qs8(x);
        parts << outputText(g.output, g.outputId) + QStringLiteral(": ") + axes.join(QStringLiteral(", "));
    }
    return parts.join(QStringLiteral("; "));
}

QString programText(const m::DriverApp &a) { return qs8(a.name); }

// The state column: what happens to the program, in words (a tick is not the only sign).
QString stateText(const m::DriverApp &a) {
    if (a.navigation) return a.selected ? T("3D/CAD program: ticked, its 3D navigation through the driver will stop")
                                        : T("3D/CAD program: keeps the driver's axes");
    return a.selected ? T("The driver's axes will be switched off") : T("Left as it is");
}

// The model's reasons are fixed English strings ("<file>: reason"); the wizard says them in the language of the interface.
QString failureText(const std::string &entry) {
    const size_t colon = entry.find(": ");
    if (colon == std::string::npos) return qs8(entry);
    const QString file = qs8(entry.substr(0, colon));
    const std::string why = entry.substr(colon + 2);
    QString text;
    if (why == "cannot be read") text = T("the file cannot be read");
    else if (why == "the backup cannot be written, file left unchanged") text = T("the backup cannot be written, so the file was left unchanged");
    else if (why.rfind("cannot be written (", 0) == 0) text = T("the file cannot be written (%1)").arg(qs8(why.substr(19, why.size() - 20)));
    else if (why == "the change did not hold, original restored") text = T("the change did not hold, the original was put back");
    else if (why == "the backup folder cannot be created") text = T("the backup folder cannot be created, nothing was changed");
    else if (why == "not a driver file name") text = T("not the name of a driver file");
    else if (why == "invalid backup name") text = T("the name of the backup is not valid");
    else if (why == "the backup cannot be read") text = T("the backup cannot be read");
    else text = qs8(why);
    return file + QStringLiteral(": ") + text;
}

QString skippedText(const m::DriverSkippedFile &s) {
    switch (s.reason) {
    case m::DriverSkipReason::TooLarge: return qs8(s.file) + QStringLiteral(": ") + T("larger than 1 MB, not a driver file");
    case m::DriverSkipReason::Utf16: return qs8(s.file) + QStringLiteral(": ") + T("saved as UTF-16, which dofscroll does not change");
    case m::DriverSkipReason::Unreadable: break;
    }
    QString t = qs8(s.file) + QStringLiteral(": ") + T("cannot be read");
    if (!s.detail.empty()) t += QStringLiteral(" (") + qs8(s.detail) + QLatin1Char(')');
    return t;
}

QLabel *paragraph(const QString &text, QWidget *parent) {
    auto *l = new QLabel(parent);
    l->setTextFormat(Qt::PlainText);
    l->setWordWrap(true);
    l->setText(text);
    return l;
}

QLabel *heading(const QString &text, QWidget *parent) {
    QLabel *l = paragraph(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

QString backupName(const std::string &backupDir) {
    const size_t slash = backupDir.find_last_of('/');
    return qs8(slash == std::string::npos ? backupDir : backupDir.substr(slash + 1));
}

// ---- page 1 ------------------------------------------------------------------------------------------------------

class IntroPageImpl : public QWizardPage {
public:
    explicit IntroPageImpl(DriverCleanupWizard *w) : wiz_(w) {
        setTitle(T("Remove the driver's axis settings"));
        setSubTitle(T("This assistant switches off the 3Dconnexion driver's axes for programs that receive every movement twice."));
        auto *col = new QVBoxLayout(this);
        col->addWidget(heading(T("What the driver does"), this));
        col->addWidget(paragraph(T("When the 3Dconnexion driver has an axis switched on for a program (for example the twist on the mouse wheel), "
                                   "that program gets the driver's input and dofscroll's scrolling, so every movement arrives twice."),
                                 this));
        col->addWidget(heading(T("What will be changed, and what not"), this));
        col->addWidget(paragraph(T("Only the axis assignments of the programs you choose are switched off, in the driver's file of each program "
                                   "(folder: %1). Everything else stays as it is: the button assignments, the driver's global settings "
                                   "(Global.xml) and every program you do not choose.")
                                     .arg(nativeDir(w->state().dir)),
                                 this));
        col->addWidget(heading(T("Every changed file is backed up first"), this));
        col->addWidget(paragraph(T("Before a file is changed it is copied to the folder dofscroll-backup inside the driver's folder. "
                                   "You can put a backup back at any time."),
                                 this));
        col->addWidget(heading(T("The driver may be running"), this));
        col->addWidget(paragraph(T("The 3Dconnexion driver may keep these files open or write its own copy back when it quits. Close the "
                                   "3Dconnexion settings window before you continue. Afterwards restart the driver, or sign out and in "
                                   "again, so that it reads the changed files."),
                                 this));
        // the text is a wrapping label next to the box (a check box does not wrap its text); a click on it toggles the box
        auto *consentRow = new QHBoxLayout;
        consent_ = new QCheckBox(this);
        consent_->setObjectName(QStringLiteral("consent"));
        consent_->setProperty("look", QStringLiteral("box"));
        consent_->setAccessibleName(T("I have closed the 3Dconnexion settings window and want to continue"));
        consent_->setAccessibleDescription(T("Next is available once this is ticked"));
        auto *consentText = paragraph(T("I have closed the 3Dconnexion settings window and want to continue"), this);
        consentText->setObjectName(QStringLiteral("consent-text"));
        consentText->setBuddy(consent_);
        consentText->installEventFilter(this);
        consentRow->addWidget(consent_, 0, Qt::AlignTop);
        consentRow->addWidget(consentText, 1);
        col->addLayout(consentRow);
        auto *row = new QHBoxLayout;
        restore_ = new QPushButton(T("Restore a backup..."), this);
        restore_->setObjectName(QStringLiteral("intro-restore"));
        restore_->setAutoDefault(false);
        restore_->setToolTip(T("Put the files of an earlier backup back, without cleaning up anything now"));
        restore_->setAccessibleDescription(T("Opens the list of the backups made by this assistant"));
        row->addWidget(restore_);
        row->addStretch(1);
        col->addLayout(row);
        status_ = paragraph(QString(), this);
        status_->setObjectName(QStringLiteral("intro-status"));
        status_->setAccessibleName(T("Result of the restore"));
        col->addWidget(status_);
        col->addStretch(1);
        connect(consent_, &QCheckBox::toggled, this, [this] { emit completeChanged(); });
        connect(restore_, &QPushButton::clicked, this, [this] { wiz_->restoreOlder(); });
    }
    void initializePage() override {
        const bool any = !m::listDriverBackups(wiz_->state().dir).empty();
        restore_->setEnabled(any);
        if (!any) restore_->setToolTip(T("There is no backup yet: a backup is made when a cleanup changes a file"));
    }
    bool isComplete() const override { return consent_->isChecked(); }
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::MouseButtonRelease && o->objectName() == QStringLiteral("consent-text")) consent_->toggle();
        return QWizardPage::eventFilter(o, e);
    }
    void setStatus(const QString &t) { status_->setText(t); }

private:
    DriverCleanupWizard *wiz_;
    QCheckBox *consent_;
    QPushButton *restore_;
    QLabel *status_;
};

// ---- page 2 ------------------------------------------------------------------------------------------------------

class ScanPageImpl : public QWizardPage {
public:
    explicit ScanPageImpl(DriverCleanupWizard *w) : wiz_(w) {
        setTitle(T("What was found"));
        setSubTitle(T("The assistant has read the driver's settings folder. Nothing has been changed."));
        auto *col = new QVBoxLayout(this);
        folder_ = paragraph(QString(), this);
        folder_->setObjectName(QStringLiteral("scan-folder"));
        counts_ = paragraph(QString(), this);
        counts_->setObjectName(QStringLiteral("scan-counts"));
        notes_ = paragraph(QString(), this);
        notes_->setObjectName(QStringLiteral("scan-notes"));
        reason_ = paragraph(QString(), this);
        reason_->setObjectName(QStringLiteral("scan-reason"));
        reason_->setForegroundRole(QPalette::PlaceholderText);
        col->addWidget(folder_);
        col->addWidget(counts_);
        col->addWidget(notes_);
        col->addWidget(reason_);
        auto *row = new QHBoxLayout;
        again_ = new QPushButton(T("Read again"), this);
        again_->setObjectName(QStringLiteral("rescan"));
        again_->setAutoDefault(false);
        again_->setToolTip(T("Reads the driver's folder again, for example after you have changed a program's settings in the driver"));
        row->addWidget(again_);
        row->addStretch(1);
        col->addLayout(row);
        col->addStretch(1);
        connect(again_, &QPushButton::clicked, this, [this] {
            scan();
            m::announce(this, reason_->text().isEmpty() ? counts_->text() : reason_->text());
        });
    }
    void initializePage() override {
        if (!wiz_->state().scanned) scan();
        else showReport();
    }
    bool isComplete() const override {
        const DriverWizardState &s = wiz_->state();
        return s.scan.folderFound && !s.apps.empty();
    }
    void scan() {
        DriverWizardState &s = wiz_->state();
        m::scanDriverFolder(s.dir, &s.apps, &s.scan);
        s.scanned = true;
        ++s.generation;
        showReport();
        emit completeChanged();
    }

private:
    void showReport() {
        const DriverWizardState &s = wiz_->state();
        const m::DriverScanReport &r = s.scan;
        if (!r.folderFound) {
            folder_->setText(T("The settings folder of the 3Dconnexion driver was not found: %1. Is the driver installed? It keeps one file per program "
                               "in the folder 3Dconnexion\\3DxWare\\Cfg of your user data (APPDATA).")
                                 .arg(nativeDir(s.dir)));
            counts_->setText(QString());
            notes_->setText(QString());
            reason_->setText(T("Next is not available: there is no folder to read."));
            return;
        }
        folder_->setText(T("Folder read: %1").arg(nativeDir(s.dir)));
        counts_->setText(T("Program files read: %1\nPrograms with driver axes switched on: %2\nPrograms without an active axis (nothing to do): %3\n"
                           "Files without axis settings: %4")
                             .arg(int(r.xmlFiles))
                             .arg(int(r.programs))
                             .arg(int(r.withoutActiveAxes))
                             .arg(int(r.withoutAxisBank)));
        QStringList notes;
        if (!r.skipped.empty()) {
            notes << T("Files that were skipped (%1):").arg(int(r.skipped.size()));
            const size_t shown = std::min<size_t>(r.skipped.size(), 10);
            for (size_t i = 0; i < shown; ++i) notes << QStringLiteral("  ") + skippedText(r.skipped[i]);
            if (r.skipped.size() > shown) notes << T("  ... and %1 more").arg(int(r.skipped.size() - shown));
        }
        if (r.hasGlobalFile)
            notes << T("Global.xml holds the driver's settings for every program that has no file of its own. It is never changed here.");
        if (r.truncated) notes << T("The folder holds more files than are read; the list is not complete.");
        notes_->setText(notes.join(QLatin1Char('\n')));
        notes_->setVisible(!notes.isEmpty());
        reason_->setText(s.apps.empty() ? T("Next is not available: no program has driver axes switched on, so there is nothing to clean up.")
                                        : QString());
        reason_->setVisible(!reason_->text().isEmpty());
    }
    DriverCleanupWizard *wiz_;
    QLabel *folder_, *counts_, *notes_, *reason_;
    QPushButton *again_;
};

// ---- page 3 ------------------------------------------------------------------------------------------------------

// The table never needs a horizontal scroll bar: the columns share the width, long texts wrap into taller rows.
class ProgramTable : public QTableWidget {
public:
    explicit ProgramTable(QWidget *parent) : QTableWidget(parent) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        horizontalHeader()->setStretchLastSection(false);
        horizontalHeader()->setMinimumSectionSize(40);
        horizontalHeader()->setSectionsClickable(false);
        verticalHeader()->setVisible(false);
        verticalHeader()->setMinimumSectionSize(m::tokens::kRowHeight + 4);
        setWordWrap(true);
        setTextElideMode(Qt::ElideNone);
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QTableWidget::resizeEvent(e);
        layoutColumns();
    }

public:
    void layoutColumns() {
        const int w = viewport()->width();
        if (w <= 0 || columnCount() < 5) return;
        const int tick = std::max(64, fontMetrics().horizontalAdvance(horizontalHeaderItem(0) ? horizontalHeaderItem(0)->text() : QString()) + 28);
        const int rest = std::max(0, w - tick);
        const int c1 = rest * 22 / 100, c2 = rest * 17 / 100, c3 = rest * 28 / 100;
        setColumnWidth(0, tick);
        setColumnWidth(1, c1);
        setColumnWidth(2, c2);
        setColumnWidth(3, c3);
        setColumnWidth(4, std::max(40, rest - c1 - c2 - c3));  // set here, so the rows are measured with the final widths
        resizeRowsToContents();
    }
};

class ChoosePageImpl : public QWizardPage {
public:
    explicit ChoosePageImpl(DriverCleanupWizard *w) : wiz_(w) {
        setTitle(T("Choose the programs"));
        setSubTitle(T("Tick the programs whose driver axes should be switched off. Programs that use the driver for 3D navigation (CAD, 3D) "
                      "are not ticked by default: leave them as they are."));
        auto *col = new QVBoxLayout(this);

        auto *grid = new QGridLayout;
        auto button = [&](const char *obj, const QString &text, const QString &tip) {
            auto *b = new QPushButton(text, this);
            b->setObjectName(QLatin1String(obj));
            b->setToolTip(tip);
            b->setAutoDefault(false);  // Next is the default button of the assistant
            return b;
        };
        auto *all = button("tick-all", T("Tick all"), T("Tick every program shown in the table"));
        auto *none = button("tick-none", T("Tick none"), T("Remove the tick of every program shown in the table"));
        auto *except = button("tick-except-3d", T("Tick all except 3D and CAD programs"),
                              T("Tick every program shown, but not the 3D and CAD programs that use the driver for 3D navigation"));
        auto *invert = button("tick-invert", T("Invert"), T("Tick what is not ticked and remove the tick of what is ticked, for the programs shown"));
        grid->addWidget(all, 0, 0);
        grid->addWidget(none, 0, 1);
        grid->addWidget(invert, 0, 2);
        grid->addWidget(except, 1, 0, 1, 3);
        grid->setColumnStretch(3, 1);
        col->addLayout(grid);

        auto *frow = new QHBoxLayout;
        filter_ = new QLineEdit(this);
        filter_->setObjectName(QStringLiteral("program-filter"));
        filter_->setPlaceholderText(T("Filter by program name"));
        filter_->setClearButtonEnabled(true);
        filter_->setAccessibleName(T("Filter the programs by name"));
        filter_->setAccessibleDescription(T("Only the programs whose name, executable or file contains this text are shown. The buttons above act on the programs shown."));
        frow->addWidget(filter_, 1);
        col->addLayout(frow);

        table_ = new ProgramTable(this);
        table_->setObjectName(QStringLiteral("programs-table"));
        table_->setColumnCount(5);
        table_->setHorizontalHeaderLabels({T("Tick"), T("Program"), T("Executable"), T("Driver axes"), T("State")});
        table_->horizontalHeaderItem(3)->setToolTip(T("What the driver does with which axes"));
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setMinimumHeight(180);
        table_->setAccessibleName(T("Programs with active driver axes"));
        table_->setAccessibleDescription(T("One row per program. Click the box of a row to tick it. Select rows with the arrow keys, Shift or Ctrl, "
                                           "press Space to tick or untick the selected rows, Ctrl+A selects all rows."));
        table_->installEventFilter(this);
        col->addWidget(table_, 1);

        counter_ = paragraph(QString(), this);
        counter_->setObjectName(QStringLiteral("tick-counter"));
        counter_->setAccessibleName(T("Number of programs ticked"));
        reason_ = paragraph(QString(), this);
        reason_->setObjectName(QStringLiteral("choose-reason"));
        reason_->setForegroundRole(QPalette::PlaceholderText);
        col->addWidget(counter_);
        col->addWidget(reason_);

        connect(all, &QPushButton::clicked, this, [this] { tick(m::DriverTick::All); });
        connect(none, &QPushButton::clicked, this, [this] { tick(m::DriverTick::None); });
        connect(except, &QPushButton::clicked, this, [this] { tick(m::DriverTick::ExceptNavigation); });
        connect(invert, &QPushButton::clicked, this, [this] { tick(m::DriverTick::Invert); });
        connect(filter_, &QLineEdit::textChanged, this, [this] {
            applyFilter();
            refreshText();
        });
    }

    void initializePage() override {
        DriverWizardState &s = wiz_->state();
        if (built_ != s.generation) {
            filter_->clear();
            rebuild();
            built_ = s.generation;
        }
        refreshText();
        table_->layoutColumns();
        emit completeChanged();
    }
    bool isComplete() const override { return m::driverTickedCount(wiz_->state().apps) > 0; }

    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == table_ && e->type() == QEvent::KeyPress) {
            auto *k = static_cast<QKeyEvent *>(e);
            if (k->key() == Qt::Key_Space && !(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
                if (!k->isAutoRepeat()) toggleSelected();
                return true;  // the table would toggle its own selection instead
            }
        }
        return QWizardPage::eventFilter(o, e);
    }

    // The rows shown (not hidden by the filter), as indices into the list of programs.
    std::vector<size_t> shownRows() const {
        std::vector<size_t> rows;
        for (int r = 0; r < table_->rowCount(); ++r)
            if (!table_->isRowHidden(r)) rows.push_back(size_t(r));
        return rows;
    }
    std::vector<size_t> selectedRows() const {
        std::vector<size_t> rows;
        if (!table_->selectionModel()) return rows;
        for (const QModelIndex &i : table_->selectionModel()->selectedRows())
            if (!table_->isRowHidden(i.row())) rows.push_back(size_t(i.row()));
        std::sort(rows.begin(), rows.end());
        return rows;
    }
    void tick(m::DriverTick how) {
        const std::vector<size_t> rows = shownRows();
        if (rows.empty()) return;  // an empty list means "every program" to the model: not when the filter shows none
        m::applyDriverTick(wiz_->state().apps, how, rows);
        sync();
        m::announce(this, counter_->text());
    }
    void toggleSelected() {
        const std::vector<size_t> rows = selectedRows();
        if (rows.empty()) return;
        m::toggleDriverTicks(wiz_->state().apps, rows);
        sync();
        m::announce(this, counter_->text());
    }

private:
    void rebuild() {
        const std::vector<m::DriverApp> &apps = wiz_->state().apps;
        table_->setRowCount(0);
        table_->setRowCount(int(apps.size()));
        for (size_t i = 0; i < apps.size(); ++i) {
            const m::DriverApp &a = apps[i];
            const int row = int(i);
            auto text = [&](int c, const QString &t) {
                auto *it = new QTableWidgetItem(t);
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                it->setToolTip(t);
                table_->setItem(row, c, it);
            };
            text(1, programText(a));
            text(2, qs8(a.exe));
            text(3, axesText(a));
            text(4, stateText(a));
            // a real check box in the first column: drawn by the style in every theme, whatever the row selection is
            auto *cell = new QWidget;
            auto *lay = new QHBoxLayout(cell);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setAlignment(Qt::AlignCenter);
            auto *cb = new QCheckBox(cell);
            cb->setObjectName(QStringLiteral("tick-box"));
            cb->setProperty("look", QStringLiteral("box"));
            cb->setFocusPolicy(Qt::NoFocus);  // the table is the one stop of the Tab key; Space ticks the selected rows
            cb->setAccessibleName(T("Switch off the driver axes of %1").arg(programText(a)));
            cb->setAccessibleDescription(T("%1. Ticked: the axes are switched off. Not ticked: the program is left as it is.").arg(axesText(a)));
            lay->addWidget(cb);
            table_->setCellWidget(row, 0, cell);
            connect(cb, &QCheckBox::toggled, this, [this, i](bool on) {
                wiz_->state().apps[i].selected = on;
                refreshRow(int(i));
                refreshText();
                emit completeChanged();
            });
        }
        sync();
        table_->resizeRowsToContents();
        if (table_->rowCount() > 0) table_->selectRow(0);
    }
    void refreshRow(int row) {
        const std::vector<m::DriverApp> &apps = wiz_->state().apps;
        if (row < 0 || row >= int(apps.size())) return;
        const m::DriverApp &a = apps[size_t(row)];
        if (QTableWidgetItem *it = table_->item(row, 4)) {
            it->setText(stateText(a));
            it->setToolTip(it->text());
        }
        if (QTableWidgetItem *it = table_->item(row, 1)) {
            QFont f = it->font();
            f.setBold(a.selected);
            it->setFont(f);
        }
    }
    // The table follows the model (after a button, Space or the filter).
    void sync() {
        const std::vector<m::DriverApp> &apps = wiz_->state().apps;
        for (int r = 0; r < table_->rowCount() && r < int(apps.size()); ++r) {
            if (QWidget *cell = table_->cellWidget(r, 0))
                if (auto *cb = cell->findChild<QCheckBox *>()) {
                    const QSignalBlocker block(cb);
                    cb->setChecked(apps[size_t(r)].selected);
                }
            refreshRow(r);
        }
        refreshText();
        emit completeChanged();
    }
    void applyFilter() {
        const std::vector<m::DriverApp> &apps = wiz_->state().apps;
        const std::vector<size_t> rows = m::driverFilterRows(apps, filter_->text().toStdString());
        std::vector<bool> shown(apps.size(), false);
        for (size_t i : rows) shown[i] = true;
        for (int r = 0; r < table_->rowCount(); ++r) table_->setRowHidden(r, !shown[size_t(r)]);
    }
    void refreshText() {
        const std::vector<m::DriverApp> &apps = wiz_->state().apps;
        const size_t ticked = m::driverTickedCount(apps);
        size_t hiddenTicked = 0;
        for (int r = 0; r < table_->rowCount() && r < int(apps.size()); ++r)
            if (table_->isRowHidden(r) && apps[size_t(r)].selected) ++hiddenTicked;
        QString c = T("%1 of %2 programs ticked").arg(int(ticked)).arg(int(apps.size()));
        if (hiddenTicked > 0) c += QStringLiteral(" ") + T("(%1 of them are hidden by the filter)").arg(int(hiddenTicked));
        counter_->setText(c);
        reason_->setText(ticked == 0 ? T("Next is not available: tick at least one program.") : QString());
        reason_->setVisible(ticked == 0);
    }
    DriverCleanupWizard *wiz_;
    ProgramTable *table_;
    QLineEdit *filter_;
    QLabel *counter_, *reason_;
    int built_ = -1;
};

// ---- page 4 ------------------------------------------------------------------------------------------------------

class PreviewPageImpl : public QWizardPage {
public:
    explicit PreviewPageImpl(DriverCleanupWizard *w) : wiz_(w) {
        setTitle(T("Preview"));
        setSubTitle(T("This is what will change. Nothing has been written yet."));
        setCommitPage(true);  // after the change there is no way back to the choice
        setButtonText(QWizard::CommitButton, T("Switch off the ticked programs"));
        auto *col = new QVBoxLayout(this);
        text_ = new QPlainTextEdit(this);
        text_->setObjectName(QStringLiteral("preview-text"));
        text_->setReadOnly(true);
        text_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        text_->setMinimumHeight(180);
        text_->setAccessibleName(T("What will change"));
        text_->setAccessibleDescription(T("For every ticked program the file and the axes that are switched off. Nothing has been written yet."));
        col->addWidget(text_, 1);
    }
    void initializePage() override {
        const DriverWizardState &s = wiz_->state();
        const m::DriverPreview pv = m::buildDriverPreview(s.apps);
        QStringList lines;
        lines << T("%1 files will be saved to a new folder inside %2 and then changed. %3 axis settings are switched off in total.")
                     .arg(int(pv.files))
                     .arg(nativeDir(s.dir + "/" + m::kDriverBackupDir))
                     .arg(int(pv.axes));
        lines << T("Not touched: the button assignments, Global.xml and the programs you did not tick.");
        if (pv.navigationTicked > 0)
            lines << mark(m::StatusKind::Warning,
                          T("%1 of the ticked programs are 3D or CAD programs. Switching their axes off stops 3D navigation with the driver in them.")
                              .arg(int(pv.navigationTicked)));
        lines << QString();
        for (const m::DriverPreviewItem &it : pv.items) {
            QString head = qs8(it.name);
            if (!it.exe.empty() && it.exe != it.name) head += QStringLiteral(" (") + qs8(it.exe) + QLatin1Char(')');
            lines << head;
            lines << QStringLiteral("  ") + T("File: %1").arg(qs8(it.file));
            QStringList parts;
            for (const m::DriverAxisGroup &g : it.groups) {
                QStringList axes;
                for (const std::string &x : g.axes) axes << qs8(x);
                parts << outputText(g.output, g.outputId) + QStringLiteral(": ") + axes.join(QStringLiteral(", "));
            }
            lines << QStringLiteral("  ") + T("Axes switched off (%1): %2").arg(int(it.axisCount)).arg(parts.join(QStringLiteral("; ")));
            if (it.navigation)
                lines << QStringLiteral("  ") + mark(m::StatusKind::Warning, T("3D or CAD program: 3D navigation with the driver stops working in it."));
            lines << QString();
        }
        text_->setPlainText(lines.join(QLatin1Char('\n')));
    }

private:
    DriverCleanupWizard *wiz_;
    QPlainTextEdit *text_;
};

// ---- page 5 ------------------------------------------------------------------------------------------------------

class ApplyPageImpl : public QWizardPage {
public:
    explicit ApplyPageImpl(DriverCleanupWizard *w) : wiz_(w) {
        setTitle(T("Switching off the axes"));
        setSubTitle(T("Every file is saved first, then changed."));
        setCommitPage(true);  // like the preview: once the files are changed there is no way back to this page
        setButtonText(QWizard::CommitButton, T("Next"));
        auto *col = new QVBoxLayout(this);
        status_ = paragraph(QString(), this);
        status_->setObjectName(QStringLiteral("apply-status"));
        bar_ = new QProgressBar(this);
        bar_->setObjectName(QStringLiteral("apply-progress"));
        bar_->setAccessibleName(T("Progress of the cleanup"));
        col->addWidget(status_);
        col->addWidget(bar_);
        col->addStretch(1);
    }
    void initializePage() override {
        if (wiz_->state().applied) return;  // the files were changed already: never twice
        done_ = false;
        const m::DriverPreview pv = m::buildDriverPreview(wiz_->state().apps);
        bar_->setRange(0, std::max(1, int(pv.files)));
        bar_->setValue(0);
        status_->setText(T("Saving the originals and changing %1 files ...").arg(int(pv.files)));
        QTimer::singleShot(0, this, [this] { run(); });
    }
    bool isComplete() const override { return done_ || wiz_->state().applied; }
    void run() {
        if (done_) return;
        DriverWizardState &s = wiz_->state();
        QAbstractButton *cancel = wiz_->button(QWizard::CancelButton);
        if (cancel) cancel->setEnabled(false);
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        m::DriverCleanupReport rep;
        m::applyDriverCleanup(s.dir, s.apps, stamp.toStdString(), &rep, [this](size_t done, size_t total, const std::string &file) {
            bar_->setMaximum(std::max(1, int(total)));
            bar_->setValue(int(done));
            if (!file.empty()) status_->setText(T("Saving and changing %1 (%2 of %3) ...").arg(qs8(file)).arg(int(done) + 1).arg(int(total)));
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        });
        s.report = rep;
        s.applied = true;
        s.backupStamp = rep.changed.empty() ? std::string() : backupName(rep.backupDir).toStdString();
        QString t = T("Done: %1 files changed.").arg(int(rep.changed.size()));
        if (!rep.backupDir.empty() && !rep.changed.empty()) t += QLatin1Char('\n') + T("The originals are saved in the folder %1.").arg(nativeDir(rep.backupDir));
        if (!rep.failed.empty()) {
            t = T("Not everything worked. Files changed: %1. Files that were not changed:").arg(int(rep.changed.size()));
            for (const std::string &f : rep.failed) t += QStringLiteral("\n  ") + failureText(f);
        }
        status_->setText(t);
        done_ = true;
        if (cancel) cancel->setEnabled(true);
        emit completeChanged();
        m::announce(this, rep.failed.empty() ? T("Done: %1 files changed.").arg(int(rep.changed.size())) : T("Not everything worked."));
    }

private:
    DriverCleanupWizard *wiz_;
    QLabel *status_;
    QProgressBar *bar_;
    bool done_ = false;
};

// ---- page 6 ------------------------------------------------------------------------------------------------------

class ResultPageImpl : public QWizardPage {
public:
    explicit ResultPageImpl(DriverCleanupWizard *w) : wiz_(w) {
        setTitle(T("Done"));
        setFinalPage(true);
        auto *col = new QVBoxLayout(this);
        summary_ = paragraph(QString(), this);
        summary_->setObjectName(QStringLiteral("result-summary"));
        col->addWidget(summary_);
        col->addWidget(heading(T("Restart the driver"), this));
        col->addWidget(paragraph(T("Restart the 3Dconnexion driver, or sign out and in again, so that it reads the changed files. Programs that are "
                                   "running may need a restart too. If a program still receives every movement twice, run this assistant "
                                   "again after the driver has been restarted."),
                                 this));
        auto *row = new QHBoxLayout;
        this_ = new QPushButton(T("Restore this backup"), this);
        this_->setObjectName(QStringLiteral("restore-this"));
        this_->setAutoDefault(false);
        this_->setToolTip(T("Puts the original files of this cleanup back"));
        older_ = new QPushButton(T("Restore an older backup..."), this);
        older_->setObjectName(QStringLiteral("restore-older"));
        older_->setAutoDefault(false);
        older_->setToolTip(T("Choose one of the backups made earlier and put its files back"));
        row->addWidget(this_);
        row->addWidget(older_);
        row->addStretch(1);
        col->addLayout(row);
        restoreStatus_ = paragraph(QString(), this);
        restoreStatus_->setObjectName(QStringLiteral("restore-status"));
        restoreStatus_->setAccessibleName(T("Result of the restore"));
        col->addWidget(restoreStatus_);
        col->addStretch(1);
        connect(this_, &QPushButton::clicked, this, [this] { wiz_->restoreBackup(wiz_->state().backupStamp, true); });
        connect(older_, &QPushButton::clicked, this, [this] { wiz_->restoreOlder(); });
    }
    void initializePage() override {
        const DriverWizardState &s = wiz_->state();
        const m::DriverCleanupReport &r = s.report;
        QString t;
        if (r.failed.empty()) t = T("Files changed: %1. Their driver axes are switched off.").arg(int(r.changed.size()));
        else {
            t = T("Files changed: %1. Not everything worked:").arg(int(r.changed.size()));
            for (const std::string &f : r.failed) t += QStringLiteral("\n  ") + failureText(f);
        }
        if (!s.backupStamp.empty())
            t += QLatin1Char('\n') + T("The originals are saved in the folder %1 (backup of %2).")
                                         .arg(nativeDir(r.backupDir))
                                         .arg(qs8(m::driverBackupWhen(s.backupStamp)));
        else
            t += QLatin1Char('\n') + T("No backup was made because no file was changed.");
        summary_->setText(t);
        this_->setEnabled(!s.backupStamp.empty());
        older_->setEnabled(!m::listDriverBackups(s.dir).empty());
        restoreStatus_->setText(QString());
    }
    void setStatus(const QString &t) { restoreStatus_->setText(t); }

private:
    DriverCleanupWizard *wiz_;
    QLabel *summary_, *restoreStatus_;
    QPushButton *this_, *older_;
};

}  // namespace

// ---- the list of backups --------------------------------------------------------------------------------------------

DriverBackupDialog::DriverBackupDialog(const std::string &dir, QWidget *parent) : QDialog(parent) {
    setWindowTitle(T("Restore a backup"));
    auto *col = new QVBoxLayout(this);
    col->addWidget(paragraph(T("Choose the backup whose files should be put back. The files of the driver's folder with the same names are "
                               "replaced by the saved originals."),
                             this));
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("backup-list"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAccessibleName(T("Backups"));
    list_->setAccessibleDescription(T("The backups, newest first, with the date and the number of files."));
    list_->setWordWrap(true);
    for (const m::DriverBackupInfo &b : m::listDriverBackupInfo(dir)) {
        stamps_.push_back(b.stamp);
        list_->addItem(T("%1, %2 files").arg(qs8(b.when)).arg(int(b.files)));
    }
    col->addWidget(list_, 1);
    auto *row = new QHBoxLayout;
    restore_ = new QPushButton(T("Restore this backup"), this);
    restore_->setObjectName(QStringLiteral("backup-restore"));
    restore_->setEnabled(false);
    auto *close = new QPushButton(T("Close"), this);
    row->addStretch(1);
    row->addWidget(restore_);
    row->addWidget(close);
    col->addLayout(row);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int r) { restore_->setEnabled(r >= 0); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { accept(); });
    connect(restore_, &QPushButton::clicked, this, &QDialog::accept);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    resize(520, 340);
    if (list_->count() > 0) list_->setCurrentRow(0);
}

int DriverBackupDialog::rowCount() const { return list_->count(); }
void DriverBackupDialog::selectRow(int row) { list_->setCurrentRow(row); }
std::string DriverBackupDialog::selectedStamp() const {
    const int r = list_->currentRow();
    return r >= 0 && r < int(stamps_.size()) ? stamps_[size_t(r)] : std::string();
}

// ---- the wizard ------------------------------------------------------------------------------------------------------

DriverCleanupWizard::DriverCleanupWizard(const std::string &dir, QWidget *parent) : QWizard(parent) {
    state_.dir = dir;
    setWindowTitle(T("3Dconnexion driver: remove axis settings"));
    setWizardStyle(QWizard::ModernStyle);  // the same look on every platform; Windows' Aero style ignores the application theme
    setOption(QWizard::NoCancelButtonOnLastPage, true);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setButtonText(QWizard::BackButton, T("Back"));
    setButtonText(QWizard::NextButton, T("Next"));
    setButtonText(QWizard::CancelButton, T("Cancel"));
    setButtonText(QWizard::FinishButton, T("Finish"));
    setPage(IntroPage, new IntroPageImpl(this));
    setPage(ScanPage, new ScanPageImpl(this));
    setPage(ChoosePage, new ChoosePageImpl(this));
    setPage(PreviewPage, new PreviewPageImpl(this));
    setPage(ApplyPage, new ApplyPageImpl(this));
    setPage(ResultPage, new ResultPageImpl(this));
    setStartId(IntroPage);
    resize(940, 660);
}

bool DriverCleanupWizard::restoreBackup(const std::string &stamp, bool confirm) {
    auto show = [this](const QString &t) {
        if (auto *r = dynamic_cast<ResultPageImpl *>(page(ResultPage))) r->setStatus(t);
        if (auto *i = dynamic_cast<IntroPageImpl *>(page(IntroPage))) i->setStatus(t);
        spn::ui::announce(this, t);
    };
    if (stamp.empty()) {
        show(T("There is no backup."));
        emit restored(false);
        return false;
    }
    if (confirm && spn::ui::qt::plainBox(QMessageBox::Question, this, windowTitle(),
                                         T("Put the files of the backup of %1 back?").arg(qs8(spn::ui::driverBackupWhen(stamp)))) != QMessageBox::Yes)
        return false;
    spn::ui::DriverCleanupReport rep;
    const bool ok = spn::ui::restoreDriverBackup(state_.dir, stamp, &rep);
    state_.scanned = false;  // the programs listed by the scan have changed
    if (ok) {
        show(T("Restored: %1 files from the backup of %2. Restart the 3Dconnexion driver or sign out and in again.")
                 .arg(int(rep.changed.size()))
                 .arg(qs8(spn::ui::driverBackupWhen(stamp))));
    } else {
        QString t = T("The backup could not be restored completely. Files restored: %1.").arg(int(rep.changed.size()));
        for (const std::string &f : rep.failed) t += QStringLiteral("\n  ") + failureText(f);
        show(t);
    }
    emit restored(ok);
    return ok;
}

void DriverCleanupWizard::restoreOlder() {
    DriverBackupDialog dlg(state_.dir, this);
    if (dlg.rowCount() == 0) {
        restoreBackup(std::string(), false);
        return;
    }
    if (dlg.exec() != QDialog::Accepted) return;
    const std::string stamp = dlg.selectedStamp();
    if (!stamp.empty()) restoreBackup(stamp, true);
}
