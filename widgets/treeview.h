/*
 * Cantata
 *
 * Copyright (c) 2011-2016 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef TREEVIEW_H
#define TREEVIEW_H

#include <QTreeView>
#include <QPixmap>
#include <QImage>

class QIcon;

class TreeView : public QTreeView
{
    Q_OBJECT

public:
    static QImage setOpacity(const QImage &orig, double opacity=0.15);
    static QPixmap createBgndPixmap(const QIcon &icon);
    static void setForceSingleClick(bool v);
    static bool getForceSingleClick();
    static QModelIndexList sortIndexes(const QModelIndexList &list);
    static void drag(Qt::DropActions supportedActions, QAbstractItemView *view, const QModelIndexList &items);

    TreeView(QWidget *parent=0, bool menuAlwaysAllowed=false);
    virtual ~TreeView();

    void setPageDefaults();
    void setExpandOnClick();
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    bool haveSelectedItems() const;
    bool haveUnSelectedItems() const;
    void startDrag(Qt::DropActions supportedActions) { drag(supportedActions, this, selectedIndexes()); }
    void mouseReleaseEvent(QMouseEvent *event);
    QModelIndexList selectedIndexes() const { return selectedIndexes(true); }
    QModelIndexList selectedIndexes(bool sorted) const;
    void expandAll(const QModelIndex &idx=QModelIndex(), bool singleLevelOnly=false);
    void collapseToLevel(int level, const QModelIndex &idx=QModelIndex());
    virtual void expand(const QModelIndex &idx, bool singleOnly=false);
    virtual void collapse(const QModelIndex &idx, bool singleOnly=false);
    virtual void setModel(QAbstractItemModel *m);
    bool checkBoxClicked(const QModelIndex &idx) const;
    void setUseSimpleDelegate();
    void setBackgroundImage(const QIcon &icon);
    void paintEvent(QPaintEvent *e);
    void setForceSingleColumn(bool f) { forceSingleColumn=f; }
    void installFilter(QObject *f) { eventFilter=f; installEventFilter(f); }
    QObject * filter() const { return eventFilter; }

private Q_SLOTS:
    void correctSelection();
//    void itemWasActivated(const QModelIndex &index);
    void itemWasClicked(const QModelIndex &index);

Q_SIGNALS:
    void itemsSelected(bool);
    void itemActivated(const QModelIndex &index); // Only emitted if view is set to single-click

private:
    QObject *eventFilter;
    bool forceSingleColumn;
    bool alwaysAllowMenu;
    QPixmap bgnd;
};

#endif
