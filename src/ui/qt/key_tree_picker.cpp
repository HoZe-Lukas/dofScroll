// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "key_tree_picker.hpp"

#include <QApplication>
#include <QColumnView>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QScreen>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "ui_i18n.hpp"

namespace {

constexpr int kValueRole = Qt::UserRole + 1;  // the stored value of a leaf (absent on a branch)

// The popup: Esc closes it (a Qt::Popup gets the key events of its column view as they bubble up).
class PopupFrame : public QFrame {
public:
    using QFrame::QFrame;

protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QFrame::keyPressEvent(e);
    }
};

// Column view with the keys of the Finder, independent of how QColumnView routes them to its columns: Up/Down move
// in the column of the selection, Left goes back to the parent, Right opens the selected branch, Enter chooses a
// leaf (a branch is opened), Esc is left to the popup. The columns take no focus and pass their keys here.
class TreeColumnView : public QColumnView {
public:
    using QColumnView::QColumnView;
    std::function<void(const QModelIndex &)> chosen;

    bool handleKey(QKeyEvent *e) {
        const QModelIndex cur = currentIndex();
        QModelIndex next;
        switch (e->key()) {
            case Qt::Key_Up:
            case Qt::Key_Down: {
                const int row = cur.row() + (e->key() == Qt::Key_Down ? 1 : -1);
                next = cur.isValid() ? cur.sibling(row, 0) : model()->index(0, 0);
                if (!next.isValid()) next = cur;  // at the first or last entry
                break;
            }
            case Qt::Key_Left: next = cur.parent().isValid() ? cur.parent() : cur; break;
            case Qt::Key_Right: next = cur.isValid() && model()->hasChildren(cur) ? model()->index(0, 0, cur) : cur; break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (!cur.isValid()) return true;
                if (model()->hasChildren(cur)) next = model()->index(0, 0, cur);
                else if (chosen) chosen(cur);
                if (!next.isValid()) return true;
                break;
            default: return false;
        }
        if (next.isValid() && next != cur) setCurrentIndex(next);
        return true;
    }

protected:
    QAbstractItemView *createColumn(const QModelIndex &root) override {
        QAbstractItemView *col = QColumnView::createColumn(root);
        col->setFocusPolicy(Qt::NoFocus);
        col->installEventFilter(this);
        return col;
    }
    bool eventFilter(QObject *o, QEvent *ev) override {
        if (ev->type() == QEvent::KeyPress && handleKey(static_cast<QKeyEvent *>(ev))) return true;
        return QColumnView::eventFilter(o, ev);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (handleKey(e)) e->accept();
        else QColumnView::keyPressEvent(e);
    }
};

void addNodes(QStandardItem *parent, const std::vector<spn::ui::KeyTreeNode> &nodes) {
    for (const spn::ui::KeyTreeNode &n : nodes) {
        auto *item = new QStandardItem(T(n.title.c_str()));
        item->setEditable(false);
        item->setAccessibleText(item->text());
        if (n.isLeaf()) item->setData(qs8(n.value), kValueRole);
        else addNodes(item, n.children);
        parent->appendRow(item);
    }
}

QStandardItem *itemAt(QStandardItemModel *m, const spn::ui::KeyTreePath &path) {
    QStandardItem *it = m->invisibleRootItem();
    for (size_t i : path) {
        if (!it || int(i) >= it->rowCount()) return nullptr;
        it = it->child(int(i));
    }
    return it;
}

}  // namespace

KeyTreePicker::KeyTreePicker(const QString &value, QWidget *parent, spn::ui::KeyTree tree)
    : QPushButton(parent), tree_(std::move(tree)), value_(value) {
    setProperty("look", QStringLiteral("combo"));
    setFocusPolicy(Qt::StrongFocus);
    setAutoDefault(false);
    setToolTip(T("Choose a key from the list of categories"));
    refreshText();
    connect(this, &QPushButton::clicked, this, [this] { popupOpen() ? closePopup() : openPopup(); });
}

KeyTreePicker::~KeyTreePicker() { delete popup_; }

void KeyTreePicker::setValue(const QString &value) {
    value_ = value;
    refreshText();
}

void KeyTreePicker::refreshText() {
    const spn::ui::KeyTreePath path = tree_.pathOf(value_.toStdString());
    if (path.empty()) {
        // an unknown value (a hand-written file) is shown as it is, nothing chosen as a prompt
        setText(value_.isEmpty() ? T("Choose a key...") : value_);
        setAccessibleDescription(value_.isEmpty() ? QString() : value_);
        return;
    }
    const auto titles = tree_.titlesOf(path);
    QStringList parts;
    for (const std::string &t : titles) parts << T(t.c_str());
    setText(parts.last());
    setAccessibleDescription(parts.join(QStringLiteral(" > ")));
    setToolTip(parts.join(QStringLiteral(" > ")));
}

bool KeyTreePicker::popupOpen() const { return popup_ && popup_->isVisible(); }

void KeyTreePicker::closePopup() {
    if (popup_) popup_->close();
}

void KeyTreePicker::buildModel() {
    delete popup_;
    popup_ = new PopupFrame(this, Qt::Popup);
    popup_->setFrameShape(QFrame::StyledPanel);
    popup_->setAccessibleName(T("Key list"));
    auto *lay = new QVBoxLayout(popup_);
    lay->setContentsMargins(2, 2, 2, 2);
    model_ = new QStandardItemModel(popup_);
    addNodes(model_->invisibleRootItem(), tree_.roots);
    auto *tv = new TreeColumnView(popup_);
    tv->chosen = [this](const QModelIndex &i) { choose(i); };
    view_ = tv;
    view_->setModel(model_);
    view_->setAccessibleName(T("Key list"));
    view_->setAccessibleDescription(T("Columns: choose a category, then a key. Right arrow opens a category, Enter chooses, Escape closes."));
    view_->setFrameShape(QFrame::NoFrame);
    view_->setResizeGripsVisible(false);
    // right of a leaf: the text that is stored in the file, for people who know the file
    auto *preview = new QLabel(view_);
    preview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    preview->setMargin(8);
    preview->setWordWrap(true);
    preview->setTextFormat(Qt::PlainText);
    preview->setAccessibleName(T("Stored as"));
    view_->setPreviewWidget(preview);
    connect(view_, &QColumnView::updatePreviewWidget, preview, [preview](const QModelIndex &i) {
        preview->setText(T("Stored as:") + QLatin1Char(' ') + i.data(kValueRole).toString());
    });
    popup_->setFocusProxy(view_);
    lay->addWidget(view_);
    // single click on a leaf chooses (as in a drop-down); Enter chooses the selected leaf
    connect(view_, &QColumnView::clicked, this, [this](const QModelIndex &i) { choose(i); });

    // column widths follow the font: the widest label of each level plus room for the arrow and the scroll bar
    const QFontMetrics fm(view_->font());
    QList<int> widths;
    int depthMax = 1;
    std::vector<int> w;
    auto measure = [&](auto &&self, const std::vector<spn::ui::KeyTreeNode> &nodes, size_t depth) -> void {
        if (w.size() <= depth) w.resize(depth + 1, 0);
        depthMax = std::max(depthMax, int(depth) + 1);
        for (const spn::ui::KeyTreeNode &n : nodes) {
            w[depth] = std::max(w[depth], fm.horizontalAdvance(T(n.title.c_str())));
            if (!n.isLeaf()) self(self, n.children, depth + 1);
        }
    };
    measure(measure, tree_.roots, 0);
    const int pad = fm.height() * 2 + 24;
    int total = 4;
    for (int x : w) {
        widths << x + pad;
        total += x + pad;
    }
    const int previewW = fm.horizontalAdvance(T("Stored as:") + QStringLiteral(" bracket_right")) + 24;
    widths << previewW;
    total += previewW;
    view_->setColumnWidths(widths);
    const int rows = 12;
    const int h = rows * (fm.height() + 8) + 8;
    popup_->setFixedSize(total + 8, h);
}

void KeyTreePicker::openPopup() {
    buildModel();
    // the current choice preselected, the columns scrolled to it
    const spn::ui::KeyTreePath path = tree_.pathOf(value_.toStdString());
    QStandardItem *cur = path.empty() ? nullptr : itemAt(model_, path);
    if (cur) view_->setCurrentIndex(cur->index());
    else if (model_->rowCount() > 0) view_->setCurrentIndex(model_->index(0, 0));
    QPoint pos = mapToGlobal(QPoint(0, height()));
    const QRect avail = (screen() ? screen()->availableGeometry() : QRect(0, 0, 1920, 1080));
    if (pos.x() + popup_->width() > avail.right()) pos.setX(std::max(avail.left(), avail.right() - popup_->width()));
    if (pos.y() + popup_->height() > avail.bottom()) pos.setY(std::max(avail.top(), mapToGlobal(QPoint(0, 0)).y() - popup_->height()));
    popup_->move(pos);
    popup_->show();
    view_->setFocus();
    if (cur) view_->scrollTo(cur->index());
}

void KeyTreePicker::choose(const QModelIndex &index) {
    const QVariant v = index.data(kValueRole);
    if (!v.isValid() || !popupOpen()) return;  // a branch only opens the next column; a click may also arrive as activated
    const QString chosen = v.toString();
    closePopup();
    setFocus();
    setValue(chosen);
    emit valueChosen(chosen);
}

KeyTreePicker *makeKeyTreePicker(const QString &current, QWidget *parent) { return new KeyTreePicker(current, parent); }
