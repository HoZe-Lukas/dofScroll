// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Key picker as a tree in columns (like the Finder column view): a button that shows the current choice and opens
// a popup. The first column lists the categories, choosing one shows its subcategories or keys to the right; a leaf
// is the choice. Keys: Up/Down move in a column, Right opens the selected branch, Left goes back, Enter chooses,
// Esc closes. The tree is data (ui/model/key_tree_model.hpp).
#pragma once
#include <QPushButton>
#include <QString>

#include "ui/model/key_tree_model.hpp"

class QColumnView;
class QFrame;
class QStandardItemModel;

class KeyTreePicker : public QPushButton {
    Q_OBJECT
public:
    explicit KeyTreePicker(const QString &value, QWidget *parent = nullptr, spn::ui::KeyTree tree = spn::ui::buildKeyTree());
    ~KeyTreePicker() override;

    // The stored value of the choice ("" = nothing chosen).
    QString value() const { return value_; }
    void setValue(const QString &value);

    void openPopup();
    void closePopup();
    bool popupOpen() const;
    // For the self-test: the column view of the open popup (nullptr when closed).
    QColumnView *popupView() const { return view_; }

signals:
    void valueChosen(const QString &value);  // the user chose a leaf

private:
    void buildModel();
    void refreshText();
    void choose(const QModelIndex &index);

    spn::ui::KeyTree tree_;
    QString value_;
    QFrame *popup_ = nullptr;
    QColumnView *view_ = nullptr;
    QStandardItemModel *model_ = nullptr;
};

// Drop-in for makeKeyCombo: the picker with the current key chosen.
KeyTreePicker *makeKeyTreePicker(const QString &current, QWidget *parent);
