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

#include "itemview.h"
#include "groupedview.h"
#include "tableview.h"
#include "messageoverlay.h"
#include "models/roles.h"
#include "gui/covers.h"
#include "models/proxymodel.h"
#include "actionitemdelegate.h"
#include "basicitemdelegate.h"
#include "models/actionmodel.h"
#include "support/localize.h"
#include "support/icon.h"
#include "config.h"
#include "support/gtkstyle.h"
#include "support/proxystyle.h"
#include "support/spinner.h"
#include "support/action.h"
#include "support/actioncollection.h"
#include "support/configuration.h"
#include "support/flattoolbutton.h"
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QPainter>
#include <QAction>
#include <QTimer>
#include <QKeyEvent>
#include <QProxyStyle>

static int detailedViewDecorationSize=22;
static int simpleViewDecorationSize=16;
static int listCoverSize=22;
static int gridCoverSize=22;

static inline int adjust(int v)
{
    if (v>48) {
        static const int constStep=4;
        return (((int)(v/constStep))*constStep)+((v%constStep) ? constStep : 0);
    } else {
        return Icon::stdSize(v);
    }
}

void ItemView::setup()
{
    int height=QApplication::fontMetrics().height();

    if (height>22) {
        detailedViewDecorationSize=Icon::stdSize(height*1.4);
        simpleViewDecorationSize=Icon::stdSize(height);
    } else {
        detailedViewDecorationSize=22;
        simpleViewDecorationSize=16;
    }
    listCoverSize=qMax(32, adjust(2*height));
    gridCoverSize=qMax(128, adjust(8*height));
}

KeyEventHandler::KeyEventHandler(QAbstractItemView *v, QAction *a)
    : QObject(v)
    , view(v)
    , deleteAct(a)
    , interceptBackspace(qobject_cast<ListView *>(view))
{
}

bool KeyEventHandler::eventFilter(QObject *obj, QEvent *event)
{
    if (view->hasFocus()) {
        if (QEvent::KeyRelease==event->type()) {
            QKeyEvent *keyEvent=static_cast<QKeyEvent *>(event);
            if (deleteAct && Qt::Key_Delete==keyEvent->key() && Qt::NoModifier==keyEvent->modifiers()) {
                deleteAct->trigger();
                return true;
            }
        } else if (QEvent::KeyPress==event->type()) {
            QKeyEvent *keyEvent=static_cast<QKeyEvent *>(event);
            if (interceptBackspace && Qt::Key_Backspace==keyEvent->key() && Qt::NoModifier==keyEvent->modifiers()) {
                emit backspacePressed();
            }
        }
    }
    return QObject::eventFilter(obj, event);
}

ViewEventHandler::ViewEventHandler(ActionItemDelegate *d, QAbstractItemView *v)
    : KeyEventHandler(v, 0)
    , delegate(d)
{
}

// HACK time. For some reason, IconView is not always re-drawn when mouse leaves the view.
// We sometimes get an item that is left in the mouse-over state. So, work-around this by
// keeping track of when mouse is over listview.
bool ViewEventHandler::eventFilter(QObject *obj, QEvent *event)
{
    if (delegate) {
        if (QEvent::Enter==event->type()) {
            delegate->setUnderMouse(true);
            view->viewport()->update();
        } else if (QEvent::Leave==event->type()) {
            delegate->setUnderMouse(false);
            view->viewport()->update();
        }
    }

    return KeyEventHandler::eventFilter(obj, event);
}

static const int constDevImageSize=32;

static inline double subTextAlpha(bool selected)
{
    return selected ? 0.7 : 0.5;
}

class ListDelegate : public ActionItemDelegate
{
public:
    ListDelegate(ListView *v, QAbstractItemView *p)
        : ActionItemDelegate(p)
        , view(v)
    {
    }

    virtual ~ListDelegate()
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        Q_UNUSED(option)
        if (view && QListView::IconMode==view->viewMode()) {
            return QSize(gridCoverSize+8, gridCoverSize+(QApplication::fontMetrics().height()*2.5));
        } else {
            int imageSize = index.data(Cantata::Role_ListImage).toBool() ? listCoverSize : 0;
            // TODO: Any point to checking one-line here? All models return sub-text...
            //       Things will be quicker if we dont call SubText here...
            bool oneLine = false ; // index.data(Cantata::Role_SubText).toString().isEmpty();
            bool showCapacity = !index.data(Cantata::Role_CapacityText).toString().isEmpty();
            int textHeight = QApplication::fontMetrics().height()*(oneLine ? 1 : 2);

            if (showCapacity) {
                imageSize=constDevImageSize;
            }
            return QSize(qMax(64, imageSize) + (constBorder * 2),
                         qMax(textHeight, imageSize) + (constBorder*2) + (int)((showCapacity ? textHeight*Utils::smallFontFactor(QApplication::font()) : 0)+0.5));
        }
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        if (!index.isValid()) {
            return;
        }
        bool mouseOver=option.state&QStyle::State_MouseOver;
        bool gtk=mouseOver && GtkStyle::isActive();
        bool selected=option.state&QStyle::State_Selected;
        bool active=option.state&QStyle::State_Active;
        bool drawBgnd=true;
        bool iconMode = view && QListView::IconMode==view->viewMode();
        QStyleOptionViewItemV4 opt(option);
        opt.showDecorationSelected=true;

        if (!underMouse) {
            if (mouseOver && !selected) {
                drawBgnd=false;
            }
            mouseOver=false;
        }

        if (drawBgnd) {
            if (mouseOver && gtk) {
                GtkStyle::drawSelection(opt, painter, selected ? 0.75 : 0.25);
            } else {
                QApplication::style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, itemView());
            }
        }

        QString capacityText=index.data(Cantata::Role_CapacityText).toString();
        bool showCapacity = !capacityText.isEmpty();
        QString text = iconMode ? index.data(Cantata::Role_BriefMainText).toString() : QString();
        if (text.isEmpty()) {
            text=index.data(Cantata::Role_MainText).toString();
        }
        if (text.isEmpty()) {
            text=index.data(Qt::DisplayRole).toString();
        }
        QRect r(option.rect);
        QRect r2(r);
        QString childText = index.data(Cantata::Role_SubText).toString();

        QPixmap pix;
        if (showCapacity) {
            const_cast<QAbstractItemModel *>(index.model())->setData(index, iconMode ? gridCoverSize : listCoverSize, Cantata::Role_Image);
            pix=index.data(Cantata::Role_Image).value<QPixmap>();
        }
        if (pix.isNull() && (iconMode || index.data(Cantata::Role_ListImage).toBool())) {
            Song cSong=index.data(iconMode ? Cantata::Role_GridCoverSong : Cantata::Role_CoverSong).value<Song>();
            if (!cSong.isEmpty()) {
                QPixmap *cp=Covers::self()->get(cSong, iconMode ? gridCoverSize : listCoverSize, getCoverInUiThread(index));
                if (cp) {
                    pix=*cp;
                }
            }
        }
        if (pix.isNull()) {
            int size=iconMode ? gridCoverSize : detailedViewDecorationSize;
            pix=index.data(Qt::DecorationRole).value<QIcon>().pixmap(size, size);
        }

        bool oneLine = childText.isEmpty();
        ActionPos actionPos = iconMode ? AP_VTop : AP_HMiddle;
        bool rtl = QApplication::isRightToLeft();

        if (childText==QLatin1String("-")) {
            childText.clear();
        }

        painter->save();
        painter->setClipRect(r);

        QFont textFont(index.data(Qt::FontRole).value<QFont>());
        QFontMetrics textMetrics(textFont);
        int textHeight=textMetrics.height();

        if (showCapacity) {
            r.adjust(2, 0, 0, -(textHeight+4));
        }

        if (AP_VTop==actionPos) {
            r.adjust(constBorder, constBorder*4, -constBorder, -constBorder);
            r2=r;
        } else {
            r.adjust(constBorder, 0, -constBorder, 0);
        }
        if (!pix.isNull()) {
            #if QT_VERSION >= 0x050100
            QSize layoutSize = pix.size() / pix.devicePixelRatio();
            #else
            QSize layoutSize = pix.size();
            #endif
            int adjust=qMax(layoutSize.width(), layoutSize.height());
            if (AP_VTop==actionPos) {
                int xpos=r.x()+((r.width()-layoutSize.width())/2);
                painter->drawPixmap(xpos, r.y(), layoutSize.width(), layoutSize.height(), pix);
                QColor color(option.palette.color(active ? QPalette::Active : QPalette::Inactive, QPalette::Text));
                double alphas[]={0.25, 0.125, 0.061};
                QRect border(xpos, r.y(), layoutSize.width(), layoutSize.height());
                QRect shadow(border);
                for (int i=0; i<3; ++i) {
                    shadow.adjust(1, 1, 1, 1);
                    color.setAlphaF(alphas[i]);
                    painter->setPen(color);
                    painter->drawLine(shadow.bottomLeft()+QPoint(i+1, 0),
                                      shadow.bottomRight()+QPoint(-((i*2)+2), 0));
                    painter->drawLine(shadow.bottomRight()+QPoint(0, -((i*2)+2)),
                                      shadow.topRight()+QPoint(0, i+1));
                    if (1==i) {
                        painter->drawPoint(shadow.bottomRight()-QPoint(2, 1));
                        painter->drawPoint(shadow.bottomRight()-QPoint(1, 2));
                        painter->drawPoint(shadow.bottomLeft()-QPoint(1, 1));
                        painter->drawPoint(shadow.topRight()-QPoint(1, 1));
                    } else if (2==i) {
                        painter->drawPoint(shadow.bottomRight()-QPoint(4, 1));
                        painter->drawPoint(shadow.bottomRight()-QPoint(1, 4));
                        painter->drawPoint(shadow.bottomLeft()-QPoint(0, 1));
                        painter->drawPoint(shadow.topRight()-QPoint(1, 0));
                        painter->drawPoint(shadow.bottomRight()-QPoint(2, 2));
                    }
                }
                color.setAlphaF(0.4);
                painter->setPen(color);
                painter->drawRect(border.adjusted(0, 0, -1, -1));
                r.adjust(0, adjust+3, 0, -3);
            } else {
                if (rtl) {
                    painter->drawPixmap(r.x()+r.width()-layoutSize.width(), r.y()+((r.height()-layoutSize.height())/2), layoutSize.width(), layoutSize.height(), pix);
                    r.adjust(3, 0, -(3+adjust), 0);
                } else {
                    painter->drawPixmap(r.x(), r.y()+((r.height()-layoutSize.height())/2), layoutSize.width(), layoutSize.height(), pix);
                    r.adjust(adjust+3, 0, -3, 0);
                }
            }
        }

        QRect textRect;
        #ifdef Q_OS_WIN
        QColor color(option.palette.color(active ? QPalette::Active : QPalette::Inactive, QPalette::Text));
        #else
        QColor color(option.palette.color(active ? QPalette::Active : QPalette::Inactive, selected ? QPalette::HighlightedText : QPalette::Text));
        #endif
        QTextOption textOpt(AP_VTop==actionPos ? Qt::AlignHCenter|Qt::AlignVCenter : Qt::AlignVCenter);

        textOpt.setWrapMode(QTextOption::NoWrap);
        if (oneLine) {
            textRect=QRect(r.x(), r.y()+((r.height()-textHeight)/2), r.width(), textHeight);
            text = textMetrics.elidedText(text, Qt::ElideRight, textRect.width(), QPalette::WindowText);
            painter->setPen(color);
            painter->setFont(textFont);
            painter->drawText(textRect, text, textOpt);
        } else {
            QFont childFont(Utils::smallFont(textFont));
            QFontMetrics childMetrics(childFont);
            QRect childRect;

            int childHeight=childMetrics.height();
            int totalHeight=textHeight+childHeight;
            textRect=QRect(r.x(), r.y()+((r.height()-totalHeight)/2), r.width(), textHeight);
            childRect=QRect(r.x(), r.y()+textHeight+((r.height()-totalHeight)/2), r.width(),
                            (iconMode ? childHeight-(2*constBorder) : childHeight));

            text = textMetrics.elidedText(text, Qt::ElideRight, textRect.width(), QPalette::WindowText);
            painter->setPen(color);
            painter->setFont(textFont);
            painter->drawText(textRect, text, textOpt);

            if (!childText.isEmpty()) {
                childText = childMetrics.elidedText(childText, Qt::ElideRight, childRect.width(), QPalette::WindowText);
                color.setAlphaF(subTextAlpha(selected));
                painter->setPen(color);
                painter->setFont(childFont);
                painter->drawText(childRect, childText, textOpt);
            }
        }

        if (showCapacity) {
            QStyleOptionProgressBar opt;
            double capacity=index.data(Cantata::Role_Capacity).toDouble();

            if (capacity<0.0) {
                capacity=0.0;
            } else if (capacity>1.0) {
                capacity=1.0;
            }
            opt.minimum=0;
            opt.maximum=1000;
            opt.progress=capacity*1000;
            opt.textVisible=true;
            opt.text=capacityText;
            opt.rect=QRect(r2.x()+4, r2.bottom()-(textHeight+4), r2.width()-8, textHeight);
            opt.state=QStyle::State_Enabled;
            opt.palette=option.palette;
            opt.direction=QApplication::layoutDirection();
            opt.fontMetrics=textMetrics;

            QApplication::style()->drawControl(QStyle::CE_ProgressBar, &opt, painter, 0L);
        }

        if ((drawBgnd && mouseOver) || Utils::touchFriendly()) {
            drawIcons(painter, AP_VTop==actionPos ? option.rect : r, mouseOver || (selected && Utils::touchFriendly()), rtl, actionPos, index);
        }
        if (!iconMode) {
            BasicItemDelegate::drawLine(painter, option.rect, color);
        }
        painter->restore();
    }

    virtual bool getCoverInUiThread(const QModelIndex &) const { return false; }
    virtual QWidget * itemView() const { return view; }

protected:
    ListView *view;
};

class TreeDelegate : public ListDelegate
{
public:
    TreeDelegate(QAbstractItemView *p)
        : ListDelegate(0, p)
        , simpleStyle(true)
        , treeView(p)
    {
    }

    virtual ~TreeDelegate()
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        if (noIcons) {
            return QStyledItemDelegate::sizeHint(option, index);
        }
        if (!simpleStyle || !index.data(Cantata::Role_CapacityText).toString().isEmpty()) {
            return ListDelegate::sizeHint(option, index);
        }

        QSize sz(QStyledItemDelegate::sizeHint(option, index));

        if (index.data(Cantata::Role_ListImage).toBool()) {
            sz.setHeight(qMax(sz.height(), listCoverSize));
        }
        int textHeight = QApplication::fontMetrics().height()*(Utils::touchFriendly() ? 1.5 : 1.25);
        sz.setHeight(qMax(sz.height(), textHeight)+(constBorder*2));
        return sz;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        if (!index.isValid()) {
            return;
        }

        if (!simpleStyle || !index.data(Cantata::Role_CapacityText).toString().isEmpty()) {
            ListDelegate::paint(painter, option, index);
            return;
        }

        QStringList text=index.data(Qt::DisplayRole).toString().split(Song::constFieldSep);
        bool gtk=GtkStyle::isActive();
        bool rtl = QApplication::isRightToLeft();
        bool selected=option.state&QStyle::State_Selected;
        bool active=option.state&QStyle::State_Active;
        bool mouseOver=underMouse && option.state&QStyle::State_MouseOver;

        if (mouseOver && gtk) {
            GtkStyle::drawSelection(option, painter, selected ? 0.75 : 0.25);
        } else {
            QApplication::style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter, itemView());
        }
        QRect r(option.rect);
        r.adjust(4, 0, -4, 0);

        if (!noIcons) {
            QPixmap pix;
            if (index.data(Cantata::Role_ListImage).toBool()) {
                Song cSong=index.data(Cantata::Role_CoverSong).value<Song>();
                if (!cSong.isEmpty()) {
                    QPixmap *cp=Covers::self()->get(cSong, listCoverSize);
                    if (cp) {
                        pix=*cp;
                    }
                }
            }

            if (pix.isNull()) {
                pix=index.data(Qt::DecorationRole).value<QIcon>().pixmap(simpleViewDecorationSize, simpleViewDecorationSize);
            }

            if (!pix.isNull()) {
                #if QT_VERSION >= 0x050100
                QSize layoutSize = pix.size() / pix.devicePixelRatio();
                #else
                QSize layoutSize = pix.size();
                #endif
                int adjust=qMax(layoutSize.width(), layoutSize.height());
                if (rtl) {
                    painter->drawPixmap(r.x()+r.width()-layoutSize.width(), r.y()+((r.height()-layoutSize.height())/2), layoutSize.width(), layoutSize.height(), pix);
                    r.adjust(3, 0, -(3+adjust), 0);
                } else {
                    painter->drawPixmap(r.x(), r.y()+((r.height()-layoutSize.height())/2), layoutSize.width(), layoutSize.height(), pix);
                    r.adjust(adjust+3, 0, -3, 0);
                }
            }
        }

        if (text.count()>0) {
            QFont textFont(QApplication::font());
            QFontMetrics textMetrics(textFont);
            int textHeight=textMetrics.height();
            #ifdef Q_OS_WIN
            QColor color(option.palette.color(active ? QPalette::Active : QPalette::Inactive, QPalette::Text));
            #else
            QColor color(option.palette.color(active ? QPalette::Active : QPalette::Inactive, selected ? QPalette::HighlightedText : QPalette::Text));
            #endif
            QTextOption textOpt(Qt::AlignVCenter);
            QRect textRect(r.x(), r.y()+((r.height()-textHeight)/2), r.width(), textHeight);
            QString str=textMetrics.elidedText(text.at(0), Qt::ElideRight, textRect.width(), QPalette::WindowText);

            painter->save();
            painter->setPen(color);
            painter->setFont(textFont);
            painter->drawText(textRect, str, textOpt);

            if (text.count()>1) {
                int mainWidth=textMetrics.width(str);
                text.takeFirst();
                str=text.join(" - ");
                textRect=QRect(r.x()+(mainWidth+8), r.y()+((r.height()-textHeight)/2), r.width()-(mainWidth+8), textHeight);
                if (textRect.width()>4) {
                    str = textMetrics.elidedText(str, Qt::ElideRight, textRect.width(), QPalette::WindowText);
                    color.setAlphaF(subTextAlpha(selected));
                    painter->setPen(color);
                    painter->drawText(textRect, str, textOpt/*QTextOption(Qt::AlignVCenter|Qt::AlignRight)*/);
                }
            }
            painter->restore();
        }

        if (mouseOver || Utils::touchFriendly()) {
            drawIcons(painter, option.rect, mouseOver || (selected && Utils::touchFriendly()), rtl, AP_HMiddle, index);
        }
        #ifdef Q_OS_WIN
        BasicItemDelegate::drawLine(painter, option.rect, option.palette.color(active ? QPalette::Active : QPalette::Inactive,
                                                                               QPalette::Text));
        #else
        BasicItemDelegate::drawLine(painter, option.rect, option.palette.color(active ? QPalette::Active : QPalette::Inactive,
                                                                               selected ? QPalette::HighlightedText : QPalette::Text));
        #endif
    }

    void setSimple(bool s) { simpleStyle=s; }
    void setNoIcons(bool n) { noIcons=n; }

    virtual bool getCoverInUiThread(const QModelIndex &idx) const
    {
        // Want album covers in artists view to load quickly...
        return idx.isValid() && idx.data(Cantata::Role_LoadCoverInUIThread).toBool();
    }

    virtual QWidget * itemView() const { return treeView; }

    bool simpleStyle;
    bool noIcons;
    QAbstractItemView *treeView;
};

ItemView::Mode ItemView::toMode(const QString &str)
{
    for (int i=0; i<Mode_Count; ++i) {
        if (modeStr((Mode)i)==str) {
            return (Mode)i;
        }
    }

    // Older versions of Cantata saved mode as an integer!!!
    int i=str.toInt();
    switch (i) {
        default:
        case 0: return Mode_SimpleTree;
        case 1: return Mode_List;
        case 2: return Mode_IconTop;
        case 3: return Mode_GroupedTree;
    }
}

QString ItemView::modeStr(Mode m)
{
    switch (m) {
    default:
    case Mode_SimpleTree:   return QLatin1String("simpletree");
    case Mode_BasicTree:    return QLatin1String("basictree");
    case Mode_DetailedTree: return QLatin1String("detailedtree");
    case Mode_List:         return QLatin1String("list");
    case Mode_IconTop:      return QLatin1String("icontop");
    case Mode_GroupedTree:  return QLatin1String("grouped");
    case Mode_Table:        return QLatin1String("table");
    }
}

static const char *constPageProp="page";
static const char *constAlwaysShowProp="always";
static Action *backAction=0;

const QLatin1String ItemView::constSearchActiveKey("searchActive");
const QLatin1String ItemView::constViewModeKey("viewMode");
const QLatin1String ItemView::constStartClosedKey("startClosed");
const QLatin1String ItemView::constSearchCategoryKey("searchCategory");

ItemView::ItemView(QWidget *p)
    : QWidget(p)
    , searchTimer(0)
    , itemModel(0)
    , currentLevel(0)
    , mode(Mode_SimpleTree)
    , groupedView(0)
    , tableView(0)
    , spinner(0)
    , msgOverlay(0)
    , performedSearch(false)
    , searchResetLevel(0)
    , openFirstLevelAfterSearch(false)
    , initialised(false)
{
    setupUi(this);
    if (!backAction) {
        backAction=ActionCollection::get()->createAction("itemview-goback", i18n("Go Back"), Icon("go-previous"));
        backAction->setShortcut(Qt::AltModifier+(Qt::LeftToRight==layoutDirection() ? Qt::Key_Left : Qt::Key_Right));
    }
    title->addAction(backAction);
    title->setVisible(false);
    Action::updateToolTip(backAction);
    QAction *sep=new QAction(this);
    sep->setSeparator(true);
    listView->addAction(sep);
    treeView->setPageDefaults();
    // Some styles, eg Cleanlooks/Plastique require that we explicitly set mouse tracking on the treeview.
    treeView->setAttribute(Qt::WA_MouseTracking);
    iconGridSize=listGridSize=listView->gridSize();
    ListDelegate *ld=new ListDelegate(listView, listView);
    TreeDelegate *td=new TreeDelegate(treeView);
    listView->setItemDelegate(ld);
    treeView->setItemDelegate(td);
    listView->setProperty(ProxyStyle::constModifyFrameProp, Utils::touchFriendly() ? ProxyStyle::VF_Top : (ProxyStyle::VF_Side|ProxyStyle::VF_Top));
    treeView->setProperty(ProxyStyle::constModifyFrameProp, Utils::touchFriendly() ? ProxyStyle::VF_Top : (ProxyStyle::VF_Side|ProxyStyle::VF_Top));
    ViewEventHandler *listViewEventHandler=new ViewEventHandler(ld, listView);
    ViewEventHandler *treeViewEventHandler=new ViewEventHandler(td, treeView);
    listView->installFilter(listViewEventHandler);
    treeView->installFilter(treeViewEventHandler);
    connect(searchWidget, SIGNAL(returnPressed()), this, SLOT(delaySearchItems()));
    connect(searchWidget, SIGNAL(textChanged(const QString)), this, SLOT(delaySearchItems()));
    connect(searchWidget, SIGNAL(active(bool)), this, SLOT(searchActive(bool)));
    connect(treeView, SIGNAL(itemsSelected(bool)), this, SIGNAL(itemsSelected(bool)));
    connect(treeView, SIGNAL(itemActivated(const QModelIndex &)), this, SLOT(itemActivated(const QModelIndex &)));
    connect(treeView, SIGNAL(doubleClicked(const QModelIndex &)), this, SIGNAL(doubleClicked(const QModelIndex &)));
    connect(treeView, SIGNAL(clicked(const QModelIndex &)),  this, SLOT(itemClicked(const QModelIndex &)));
    connect(listView, SIGNAL(itemsSelected(bool)), this, SIGNAL(itemsSelected(bool)));
    connect(listView, SIGNAL(activated(const QModelIndex &)), this, SLOT(activateItem(const QModelIndex &)));
    connect(listView, SIGNAL(itemDoubleClicked(const QModelIndex &)), this, SIGNAL(doubleClicked(const QModelIndex &)));
    connect(listView, SIGNAL(clicked(const QModelIndex &)),  this, SLOT(itemClicked(const QModelIndex &)));
    connect(backAction, SIGNAL(triggered()), this, SLOT(backActivated()));
    connect(listViewEventHandler, SIGNAL(backspacePressed()), this, SLOT(backActivated()));
    connect(title, SIGNAL(addToPlayQueue()), this, SLOT(addTitleButtonClicked()));
    connect(title, SIGNAL(replacePlayQueue()), this, SLOT(replaceTitleButtonClicked()));
    connect(Covers::self(), SIGNAL(loaded(Song,int)), this, SLOT(coverLoaded(Song,int)));
    searchWidget->setVisible(false);
    #ifdef Q_OS_MAC
    treeView->setAttribute(Qt::WA_MacShowFocusRect, 0);
    listView->setAttribute(Qt::WA_MacShowFocusRect, 0);
    #endif
}

ItemView::~ItemView()
{
}

void ItemView::alwaysShowHeader()
{
    title->setVisible(true);
    title->setProperty(constAlwaysShowProp, true);
    setTitle();
    controlViewFrame();
}

void ItemView::load(Configuration &config)
{
    if (config.get(constSearchActiveKey, false)) {
        focusSearch();
    }
    setMode(toMode(config.get(constViewModeKey, modeStr(mode))));
    setStartClosed(config.get(constStartClosedKey, isStartClosed()));
    setSearchCategory(config.get(constSearchCategoryKey, searchCategory()));
}

void ItemView::save(Configuration &config)
{
    config.set(constSearchActiveKey, searchWidget->isActive());
    config.set(constViewModeKey, modeStr(mode));
    if (groupedView) {
        config.set(constStartClosedKey, isStartClosed());
    }
    TableView *tv=qobject_cast<TableView *>(view());
    if (tv) {
        tv->saveHeader();
    }
    QString cat=searchCategory();
    if (!cat.isEmpty()) {
        config.set(constSearchCategoryKey, cat);
    }
}

void ItemView::allowGroupedView()
{
    if (!groupedView) {
        groupedView=new GroupedView(stackedWidget);
        stackedWidget->addWidget(groupedView);
        groupedView->setProperty(constPageProp, stackedWidget->count()-1);
        // Some styles, eg Cleanlooks/Plastique require that we explicitly set mouse tracking on the treeview.
        groupedView->setAttribute(Qt::WA_MouseTracking, true);
        ViewEventHandler *viewHandler=new ViewEventHandler(qobject_cast<ActionItemDelegate *>(groupedView->itemDelegate()), groupedView);
        groupedView->installFilter(viewHandler);
        groupedView->setAutoExpand(false);
        groupedView->setMultiLevel(true);
        connect(groupedView, SIGNAL(itemsSelected(bool)), this, SIGNAL(itemsSelected(bool)));
        connect(groupedView, SIGNAL(itemActivated(const QModelIndex &)), this, SLOT(itemActivated(const QModelIndex &)));
        connect(groupedView, SIGNAL(doubleClicked(const QModelIndex &)), this, SIGNAL(doubleClicked(const QModelIndex &)));
        connect(groupedView, SIGNAL(clicked(const QModelIndex &)), this, SLOT(itemClicked(const QModelIndex &)));
        groupedView->setProperty(ProxyStyle::constModifyFrameProp, Utils::touchFriendly() ? ProxyStyle::VF_Top : (ProxyStyle::VF_Side|ProxyStyle::VF_Top));
        #ifdef Q_OS_MAC
        groupedView->setAttribute(Qt::WA_MacShowFocusRect, 0);
        #endif
    }
}

void ItemView::allowTableView(TableView *v)
{
    if (!tableView) {
        tableView=v;
        tableView->setParent(stackedWidget);
        stackedWidget->addWidget(tableView);
        tableView->setProperty(constPageProp, stackedWidget->count()-1);
        ViewEventHandler *viewHandler=new ViewEventHandler(0, tableView);
        tableView->installFilter(viewHandler);
        connect(tableView, SIGNAL(itemsSelected(bool)), this, SIGNAL(itemsSelected(bool)));
        connect(tableView, SIGNAL(itemActivated(const QModelIndex &)), this, SLOT(itemActivated(const QModelIndex &)));
        connect(tableView, SIGNAL(doubleClicked(const QModelIndex &)), this, SIGNAL(doubleClicked(const QModelIndex &)));
        connect(tableView, SIGNAL(clicked(const QModelIndex &)), this, SLOT(itemClicked(const QModelIndex &)));
        tableView->setProperty(ProxyStyle::constModifyFrameProp, Utils::touchFriendly() ? ProxyStyle::VF_Top : (ProxyStyle::VF_Side|ProxyStyle::VF_Top));
        #ifdef Q_OS_MAC
        tableView->setAttribute(Qt::WA_MacShowFocusRect, 0);
        #endif
    }
}

void ItemView::addAction(QAction *act)
{
    treeView->addAction(act);
    listView->addAction(act);
    if (groupedView) {
        groupedView->addAction(act);
    }
    if (tableView) {
        tableView->addAction(act);
    }
}

void ItemView::addSeparator()
{
    QAction *act=new QAction(this);
    act->setSeparator(true);
    addAction(act);
}

void ItemView::setMode(Mode m)
{
    initialised=true;
    if (m<0 || m>=Mode_Count || (Mode_GroupedTree==m && !groupedView) || (Mode_Table==m && !tableView)) {
        m=Mode_SimpleTree;
    }

    if (m==mode) {
        return;
    }

    searchWidget->setText(QString());
    if (!title->property(constAlwaysShowProp).toBool()) {
        title->setVisible(false);
        controlViewFrame();
    }
    QIcon oldBgndIcon=bgndIcon;
    if (!bgndIcon.isNull()) {
        setBackgroundImage(QIcon());
    }

    mode=m;
    int stackIndex=0;
    if (usingTreeView()) {
        listView->setModel(0);
        if (groupedView) {
            groupedView->setModel(0);
        }
        if (tableView) {
            tableView->saveHeader();
            tableView->setModel(0);
        }
        treeView->setModel(itemModel);
        treeView->setHidden(false);
        static_cast<TreeDelegate *>(treeView->itemDelegate())->setSimple(Mode_SimpleTree==mode || Mode_BasicTree==mode);
        static_cast<TreeDelegate *>(treeView->itemDelegate())->setNoIcons(Mode_BasicTree==mode);
        if (dynamic_cast<ProxyModel *>(itemModel)) {
            static_cast<ProxyModel *>(itemModel)->setRootIndex(QModelIndex());
        }
        treeView->reset();
    } else if (Mode_GroupedTree==mode) {
        treeView->setModel(0);
        listView->setModel(0);
        if (tableView) {
            tableView->saveHeader();
            tableView->setModel(0);
        }
        groupedView->setHidden(false);
        treeView->setHidden(true);
        groupedView->setModel(itemModel);
        if (dynamic_cast<ProxyModel *>(itemModel)) {
            static_cast<ProxyModel *>(itemModel)->setRootIndex(QModelIndex());
        }
        stackIndex=groupedView->property(constPageProp).toInt();
    } else if (Mode_Table==mode) {
        int w=view()->width();
        treeView->setModel(0);
        listView->setModel(0);
        if (groupedView) {
            groupedView->setModel(0);
        }
        tableView->setHidden(false);
        treeView->setHidden(true);
        tableView->setModel(itemModel);
        tableView->initHeader();
        if (dynamic_cast<ProxyModel *>(itemModel)) {
            static_cast<ProxyModel *>(itemModel)->setRootIndex(QModelIndex());
        }
        tableView->resize(w, tableView->height());
        stackIndex=tableView->property(constPageProp).toInt();
    } else {
        stackIndex=1;
        treeView->setModel(0);
        if (groupedView) {
            groupedView->setModel(0);
        }
        if (tableView) {
            tableView->saveHeader();
            tableView->setModel(0);
        }
        listView->setModel(itemModel);
        goToTop();
        if (Mode_IconTop!=mode) {
            listView->setGridSize(listGridSize);
            listView->setViewMode(QListView::ListMode);
            listView->setResizeMode(QListView::Fixed);
//            listView->setAlternatingRowColors(true);
            listView->setWordWrap(false);
        }
    }

    stackedWidget->setCurrentIndex(stackIndex);
    if (spinner) {
        spinner->setWidget(view());
        if (spinner->isActive()) {
            spinner->start();
        }
    }
    if (msgOverlay) {
        msgOverlay->setWidget(view());
    }

    if (!oldBgndIcon.isNull()) {
        setBackgroundImage(oldBgndIcon);
    }
    controlViewFrame();
}

QModelIndexList ItemView::selectedIndexes(bool sorted) const
{
    if (usingTreeView()) {
        return treeView->selectedIndexes(sorted);
    } else if (Mode_GroupedTree==mode) {
        return groupedView->selectedIndexes(sorted);
    } else if (Mode_Table==mode) {
        return tableView->selectedIndexes(sorted);
    }
    return listView->selectedIndexes(sorted);
}

void ItemView::goToTop()
{
    setLevel(0);
    if (dynamic_cast<ProxyModel *>(itemModel)) {
        static_cast<ProxyModel *>(itemModel)->setRootIndex(QModelIndex());
    }
    listView->setRootIndex(QModelIndex());
    setTitle();
    emit rootIndexSet(QModelIndex());
}

void ItemView::setEnabled(bool en)
{
    if (treeView) {
        treeView->setEnabled(en);
    }
    if (groupedView) {
        groupedView->setEnabled(en);
    }
    if (tableView) {
        tableView->setEnabled(en);
    }
    if (listView) {
        listView->setEnabled(en);
    }
}

void ItemView::setLevel(int l, bool haveChildren)
{
    currentLevel=l;
    if (isVisible()) {
        backAction->setEnabled(0!=currentLevel);
    }

    if (Mode_IconTop==mode) {
        if (0==currentLevel || haveChildren) {
            if (QListView::IconMode!=listView->viewMode()) {
                listView->setGridSize(iconGridSize);
                listView->setViewMode(QListView::IconMode);
                listView->setResizeMode(QListView::Adjust);
//                listView->setAlternatingRowColors(false);
                listView->setWordWrap(true);
                listView->setDragDropMode(QAbstractItemView::DragOnly);
                static_cast<ActionItemDelegate *>(listView->itemDelegate())->setLargeIcons(true);
            }
        } else if(QListView::ListMode!=listView->viewMode()) {
            listView->setGridSize(listGridSize);
            listView->setViewMode(QListView::ListMode);
            listView->setResizeMode(QListView::Fixed);
//            listView->setAlternatingRowColors(true);
            listView->setWordWrap(false);
            listView->setDragDropMode(QAbstractItemView::DragOnly);
            static_cast<ActionItemDelegate *>(listView->itemDelegate())->setLargeIcons(false);
        }
    }

    if (view()->selectionModel()) {
        view()->selectionModel()->clearSelection();
    }

    if (!title->property(constAlwaysShowProp).toBool()) {
        title->setVisible(currentLevel>0);
        controlViewFrame();
    }
    setTitle();
}

QAbstractItemView * ItemView::view() const
{
    if (usingTreeView()) {
        return treeView;
    } else if(Mode_GroupedTree==mode) {
        return groupedView;
    } else if(Mode_Table==mode) {
        return tableView;
    } else {
        return listView;
    }
}

void ItemView::setModel(QAbstractItemModel *m)
{
    if (itemModel) {
        disconnect(itemModel, SIGNAL(modelReset()), this, SLOT(modelReset()));
        if (qobject_cast<QAbstractProxyModel *>(itemModel)) {
            disconnect(static_cast<QAbstractProxyModel *>(itemModel)->sourceModel(), SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(dataChanged(QModelIndex,QModelIndex)));
        } else {
            disconnect(itemModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(dataChanged(QModelIndex,QModelIndex)));
        }
    }
    itemModel=m;
    if (!initialised) {
        mode=Mode_List;
        setMode(Mode_SimpleTree);
    }
    if (m) {
        connect(m, SIGNAL(modelReset()), this, SLOT(modelReset()));
        if (qobject_cast<QAbstractProxyModel *>(m)) {
            connect(static_cast<QAbstractProxyModel *>(m)->sourceModel(), SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(dataChanged(QModelIndex,QModelIndex)));
        } else {
            connect(m, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(dataChanged(QModelIndex,QModelIndex)));
        }
    }
    view()->setModel(m);
}

bool ItemView::searchVisible() const
{
    return searchWidget->isVisible();
}

QString ItemView::searchText() const
{
    return searchWidget->isVisible() ? searchWidget->text() : QString();
}

QString ItemView::searchCategory() const
{
    return searchWidget->category();
}

void ItemView::clearSearchText()
{
    return searchWidget->setText(QString());
}

void ItemView::setUniformRowHeights(bool v)
{
    treeView->setUniformRowHeights(v);
}

void ItemView::setAcceptDrops(bool v)
{
    listView->setAcceptDrops(v);
    treeView->setAcceptDrops(v);
    if (groupedView) {
        groupedView->setAcceptDrops(v);
    }
    if (tableView) {
        tableView->setAcceptDrops(v);
    }
}

void ItemView::setDragDropOverwriteMode(bool v)
{
    listView->setDragDropOverwriteMode(v);
    treeView->setDragDropOverwriteMode(v);
    if (groupedView) {
        groupedView->setDragDropOverwriteMode(v);
    }
    if (tableView) {
        tableView->setDragDropOverwriteMode(v);
    }
}

void ItemView::setDragDropMode(QAbstractItemView::DragDropMode v)
{
    listView->setDragDropMode(v);
    treeView->setDragDropMode(v);
    if (groupedView) {
        groupedView->setDragDropMode(v);
    }
    if (tableView) {
        tableView->setDragDropMode(v);
    }
}

void ItemView::setGridSize(const QSize &sz)
{
    iconGridSize=sz;
    if (Mode_IconTop==mode && 0==currentLevel) {
        listView->setGridSize(listGridSize);
        static_cast<ActionItemDelegate *>(listView->itemDelegate())->setLargeIcons(iconGridSize.width()>(ActionItemDelegate::constLargeActionIconSize*6));
    }
}

void ItemView::update()
{
    view()->update();
}

void ItemView::setDeleteAction(QAction *act)
{
    if (!listView->filter() || !qobject_cast<KeyEventHandler *>(listView->filter())) {
        listView->installFilter(new KeyEventHandler(listView, act));
    } else {
        static_cast<KeyEventHandler *>(listView->filter())->setDeleteAction(act);
    }
    if (!treeView->filter() || !qobject_cast<KeyEventHandler *>(treeView->filter())) {
        treeView->installEventFilter(new KeyEventHandler(treeView, act));
    } else {
        static_cast<KeyEventHandler *>(treeView->filter())->setDeleteAction(act);
    }
    if (groupedView) {
        if (!groupedView->filter() || !qobject_cast<KeyEventHandler *>(groupedView->filter())) {
            groupedView->installEventFilter(new KeyEventHandler(groupedView, act));
        } else {
            static_cast<KeyEventHandler *>(groupedView->filter())->setDeleteAction(act);
        }
    }
    if (tableView) {
        if (!tableView->filter() || !qobject_cast<KeyEventHandler *>(tableView->filter())) {
            tableView->installEventFilter(new KeyEventHandler(tableView, act));
        } else {
            static_cast<KeyEventHandler *>(tableView->filter())->setDeleteAction(act);
        }
    }
}

void ItemView::showIndex(const QModelIndex &idx, bool scrollTo)
{
    if (usingTreeView() || Mode_GroupedTree==mode || Mode_Table==mode) {
        TreeView *v=static_cast<TreeView *>(view());
        QModelIndex i=idx;
        while (i.isValid()) {
            v->setExpanded(i, true);
            i=i.parent();
        }
        if (scrollTo) {
            v->scrollTo(idx, QAbstractItemView::PositionAtTop);
        }
    } else {
        if (idx.parent().isValid()) {
            QList<QModelIndex> indexes;
            QModelIndex i=idx.parent();
            QModelIndex p=idx;
            while (i.isValid()) {
                indexes.prepend(i);
                i=i.parent();
            }
            setLevel(0);
            listView->setRootIndex(QModelIndex());
            if (dynamic_cast<ProxyModel *>(itemModel)) {
                static_cast<ProxyModel *>(itemModel)->setRootIndex(QModelIndex());
            }
            foreach (const QModelIndex &i, indexes) {
                activateItem(i, false);
            }
            if (p.isValid()) {
                emit rootIndexSet(p);
            }
            if (scrollTo) {
                listView->scrollTo(idx, QAbstractItemView::PositionAtTop);
            }
            setTitle();
        }
    }

    if (view()->selectionModel()) {
        view()->selectionModel()->select(idx, QItemSelectionModel::Select|QItemSelectionModel::Rows);
    }
}

void ItemView::focusSearch(const QString &text)
{
    if (isEnabled()) {
        performedSearch=false;
        searchWidget->activate(searchWidget->text().isEmpty() ? text : QString());
    }
}

void ItemView::focusView()
{
    view()->setFocus();
}

void ItemView::setSearchLabelText(const QString &text)
{
    searchWidget->setLabel(text);
}

void ItemView::setSearchVisible(bool v)
{
    searchWidget->setVisible(v);
}

bool ItemView::isSearchActive() const
{
    return searchWidget->isActive();
}

void ItemView::closeSearch()
{
    if (searchWidget->isActive()) {
        searchWidget->close();
    }
}

void ItemView::setStartClosed(bool sc)
{
    if (groupedView) {
        groupedView->setStartClosed(sc);
    }
}

bool ItemView::isStartClosed()
{
    return groupedView ? groupedView->isStartClosed() : false;
}

void ItemView::updateRows()
{
    if (groupedView) {
        groupedView->updateCollectionRows();
    }
}

void ItemView::updateRows(const QModelIndex &idx)
{
    if (groupedView) {
        groupedView->updateRows(idx);
    }
}

void ItemView::expandAll(const QModelIndex &index)
{
    if (usingTreeView()) {
        treeView->expandAll(index);
    } else if (Mode_GroupedTree==mode && groupedView) {
        groupedView->expandAll(index);
    } else if (Mode_Table==mode && tableView) {
        tableView->expandAll(index);
    }
}

void ItemView::expand(const QModelIndex &index, bool singleOnly)
{
    if (usingTreeView()) {
        treeView->expand(index, singleOnly);
    } else if (Mode_GroupedTree==mode && groupedView) {
        groupedView->expand(index, singleOnly);
    } else if (Mode_Table==mode && tableView) {
        tableView->expand(index, singleOnly);
    }
}

void ItemView::showMessage(const QString &message, int timeout)
{
    if (!msgOverlay) {
        msgOverlay=new MessageOverlay(this);
        msgOverlay->setWidget(view());
    }
    msgOverlay->setText(message, timeout, false);
}

void ItemView::setBackgroundImage(const QIcon &icon)
{
    bgndIcon=icon;
    if (usingTreeView()) {
        treeView->setBackgroundImage(bgndIcon);
    } else if (Mode_GroupedTree==mode && groupedView) {
        groupedView->setBackgroundImage(bgndIcon);
    } else if (Mode_Table==mode && tableView) {
        tableView->setBackgroundImage(bgndIcon);
    } else if (Mode_List==mode || Mode_IconTop==mode) {
        listView->setBackgroundImage(bgndIcon);
    }
}

bool ItemView::isAnimated() const
{
    if (usingTreeView()) {
        return treeView->isAnimated();
    }
    if (Mode_GroupedTree==mode && groupedView) {
        return groupedView->isAnimated();
    }
    if (Mode_Table==mode && tableView) {
        return tableView->isAnimated();
    }
    return false;
}

void ItemView::setAnimated(bool a)
{
    if (usingTreeView()) {
        treeView->setAnimated(a);
    } else if (Mode_GroupedTree==mode && groupedView) {
        groupedView->setAnimated(a);
    } else if (Mode_Table==mode && tableView) {
        tableView->setAnimated(a);
    }
}

void ItemView::setPermanentSearch()
{
    searchWidget->setPermanent();
}

void ItemView::hideSearch()
{
    if (searchVisible()) {
        searchWidget->close();
    }
}

void ItemView::setSearchCategories(const QList<SearchWidget::Category> &categories)
{
    searchWidget->setCategories(categories);
}

void ItemView::setSearchCategory(const QString &id)
{
    searchWidget->setCategory(id);
}

void ItemView::showEvent(QShowEvent *ev)
{
    QWidget::showEvent(ev);
    backAction->setEnabled(0!=currentLevel);
}

void ItemView::showSpinner(bool v)
{
    if (v) {
        if (!spinner) {
            spinner=new Spinner(this);
        }
        spinner->setWidget(view());
        spinner->start();
    } else {
        hideSpinner();
    }
}

void ItemView::hideSpinner()
{
    if (spinner) {
        spinner->stop();
    }
}

void ItemView::updating()
{
    showSpinner();
    showMessage(i18n("Updating..."), -1);
}

void ItemView::updated()
{
    hideSpinner();
    showMessage(QString(), 0);
}

void ItemView::collectionRemoved(quint32 key)
{
    if (groupedView) {
        groupedView->collectionRemoved(key);
    }
}

void ItemView::backActivated()
{
    if (!isVisible()) {
        return;
    }

    emit headerClicked(currentLevel);

    if (!usingListView() || 0==currentLevel) {
        return;
    }
    setLevel(currentLevel-1);
    if (dynamic_cast<ProxyModel *>(itemModel)) {
        static_cast<ProxyModel *>(itemModel)->setRootIndex(listView->rootIndex().parent());
    }
    listView->setRootIndex(listView->rootIndex().parent());
    emit rootIndexSet(listView->rootIndex().parent());
    setTitle();

    if (qobject_cast<QSortFilterProxyModel *>(listView->model())) {
        QModelIndex idx=static_cast<QSortFilterProxyModel *>(listView->model())->mapFromSource(prevTopIndex);
        if (idx.isValid()) {
            listView->scrollTo(idx, QAbstractItemView::PositionAtTop);
        }
    } else {
        listView->scrollTo(prevTopIndex, QAbstractItemView::PositionAtTop);
    }
}

void ItemView::setExpanded(const QModelIndex &idx, bool exp)
{
    if (usingTreeView()) {
        treeView->setExpanded(idx, exp);
    }
}

QAction * ItemView::getAction(const QModelIndex &index)
{
    QAbstractItemDelegate *abs=view()->itemDelegate();
    ActionItemDelegate *d=abs ? qobject_cast<ActionItemDelegate *>(abs) : 0;
    return d ? d->getAction(index) : 0;
}

void ItemView::itemClicked(const QModelIndex &index)
{
    QAction *act=getAction(index);
    if (act) {
        act->trigger();
        return;
    }

    if (TreeView::getForceSingleClick()) {
        activateItem(index);
    }
}

void ItemView::itemActivated(const QModelIndex &index)
{
    if (!TreeView::getForceSingleClick()) {
        activateItem(index);
    }
}

void ItemView::activateItem(const QModelIndex &index, bool emitRootSet)
{
    if (getAction(index)) {
        return;
    }

    if (usingTreeView()) {
        treeView->setExpanded(index, !treeView->isExpanded(index));
    } else if (Mode_GroupedTree==mode) {
        if (!index.parent().isValid()) {
            groupedView->setExpanded(index, !groupedView->TreeView::isExpanded(index));
        }
    } else if (Mode_Table==mode) {
        if (!index.parent().isValid()) {
            tableView->setExpanded(index, !tableView->TreeView::isExpanded(index));
        }
    } else if (index.isValid() && (index.child(0, 0).isValid() || itemModel->canFetchMore(index)) && index!=listView->rootIndex()) {
        if (itemModel->canFetchMore(index)) {
            itemModel->fetchMore(index);
        }
        QModelIndex fistChild=index.child(0, 0);
        if (!fistChild.isValid()) {
            return;
        }

        prevTopIndex=listView->indexAt(QPoint(8, 8));
        if (qobject_cast<QSortFilterProxyModel *>(listView->model())) {
            prevTopIndex=static_cast<QSortFilterProxyModel *>(listView->model())->mapToSource(prevTopIndex);
        }
        setLevel(currentLevel+1, itemModel->canFetchMore(fistChild) || fistChild.child(0, 0).isValid());
        listView->setRootIndex(index);
        setTitle();

        if (dynamic_cast<ProxyModel *>(itemModel)) {
            static_cast<ProxyModel *>(itemModel)->setRootIndex(index);
        }
        if (emitRootSet) {
            emit rootIndexSet(index);
        }
        listView->scrollToTop();
    }
}

void ItemView::modelReset()
{
    if (Mode_List==mode || Mode_IconTop==mode) {
        goToTop();
    } else if (usingTreeView() && !searchText().isEmpty()) {
        for (int r=0; r<itemModel->rowCount(); ++r) {
            treeView->expand(itemModel->index(r, 0, QModelIndex()));
        }
    }
}

void ItemView::dataChanged(const QModelIndex &tl, const QModelIndex &br)
{
    if (!tl.isValid() && !br.isValid()) {
        setTitle();
    }
}

void ItemView::addTitleButtonClicked()
{
    if ((Mode_List==mode || Mode_IconTop==mode) && view()->rootIndex().isValid()) {
        emit updateToPlayQueue(view()->rootIndex(), false);
    }
}

void ItemView::replaceTitleButtonClicked()
{
    if ((Mode_List==mode || Mode_IconTop==mode) && view()->rootIndex().isValid()) {
        emit updateToPlayQueue(view()->rootIndex(), true);
    }
}

void ItemView::coverLoaded(const Song &song, int size)
{
    Q_UNUSED(song)
    if (Mode_BasicTree==mode || Mode_GroupedTree==mode || !isVisible() || (Mode_IconTop==mode && size!=gridCoverSize) || (Mode_IconTop!=mode && size!=listCoverSize)) {
        return;
    }
    view()->viewport()->update();
}

void ItemView::delaySearchItems()
{
    if (searchWidget->text().isEmpty()) {
        if (searchTimer) {
            searchTimer->stop();
        }
        if (performedSearch) {
            performedSearch=false;
        }
        emit searchItems();
    } else {
        if (!searchTimer) {
            searchTimer=new QTimer(this);
            searchTimer->setSingleShot(true);
            connect(searchTimer, SIGNAL(timeout()), this, SLOT(doSearch()));
        }
        int len=searchWidget->text().trimmed().length();
        searchTimer->start(len<2 ? 1000 : len<4 ? 750 : 500);
    }
}

void ItemView::doSearch()
{
    performedSearch=true;
    emit searchItems();
}

void ItemView::searchActive(bool a)
{
    emit searchIsActive(a);
    if (!a && performedSearch) {
        performedSearch=false;
    }
    if (!a && view()->isVisible()) {
        view()->setFocus();
    }
    controlViewFrame();
}

void ItemView::setTitle()
{
    QModelIndex index=view()->rootIndex();
    QAbstractItemModel *model=view()->model();
    if (!model) {
        return;
    }
    title->update(model->data(index, Mode_IconTop==mode ? Cantata::Role_GridCoverSong : Cantata::Role_CoverSong).value<Song>(),
                  model->data(index, Qt::DecorationRole).value<QIcon>(),
                  model->data(index, Cantata::Role_TitleText).toString(),
                  model->data(index, Cantata::Role_SubText).toString(),
                  model->data(index, Cantata::Role_TitleActions).toBool());
}

void ItemView::controlViewFrame()
{
    view()->setProperty(ProxyStyle::constModifyFrameProp,
                            title->isVisible() || title->property(constAlwaysShowProp).toBool()
                                ? Utils::touchFriendly() ? 0 : ProxyStyle::VF_Side
                                : Utils::touchFriendly()
                                    ? (searchWidget->isActive() ? 0 : ProxyStyle::VF_Top)
                                    : (searchWidget->isActive() ? ProxyStyle::VF_Side : (ProxyStyle::VF_Side|ProxyStyle::VF_Top)));
}
