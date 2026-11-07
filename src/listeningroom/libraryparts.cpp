/*
 * Adrift - a fork of Strawberry Music Player
 * Copyright 2026, LostandAdrift
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QScrollBar>
#include <QShowEvent>
#include <QVBoxLayout>

#include "libraryparts.h"
#include "roomstyle.h"
#include "roomwidgets.h"

namespace {

std::atomic<quint64> g_cover_id{1000};

int TextBlockHeight(const QFontMetrics &fm) {
  return fm.height() * LibraryAlbumGrid::kTextHeightLines + 8;
}

QString FormatSeconds(const int seconds) {

  if (seconds <= 0) return QString();
  const int hours = seconds / 3600;
  const int minutes = (seconds / 60) % 60;
  const int secs = seconds % 60;
  if (hours > 0) return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
  return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));

}

}  // namespace

// ------------------------------------------------------------------------------------------------------------ sleeve

void LibraryPaint::DrawSleeve(QPainter *p, const QRectF &rect, const QPalette &pal, const QString &seed, const QPixmap &cover, const qreal radius) {

  p->save();
  p->setRenderHint(QPainter::Antialiasing);
  QPainterPath clip;
  clip.addRoundedRect(rect, radius, radius);

  if (!cover.isNull()) {
    p->setClipPath(clip);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    // Covers are normally square; a different shape is cropped to the middle, never stretched.
    const QSizeF size = cover.deviceIndependentSize();
    const qreal side = std::min(size.width(), size.height());
    const QRectF source((size.width() - side) / 2, (size.height() - side) / 2, side, side);
    const qreal dpr = cover.devicePixelRatio();
    p->drawPixmap(rect, cover, QRectF(source.left() * dpr, source.top() * dpr, source.width() * dpr, source.height() * dpr));
    p->setClipping(false);
  }
  else {
    // Without artwork every album still gets its own quiet sleeve: a tint chosen from the name, with a record on it.
    const int hue = static_cast<int>(qHash(seed) % 360);
    const QColor tint = QColor::fromHsl(hue, 70, 110);
    const QColor top = RoomStyle::Mix(RoomStyle::Window(pal), tint, 0.34);
    const QColor bottom = RoomStyle::Mix(RoomStyle::Window(pal), tint, 0.14);
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    p->setPen(Qt::NoPen);
    p->setBrush(gradient);
    p->drawPath(clip);
    const QPointF centre = rect.center();
    const qreal r = rect.width() * 0.30;
    p->setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.06));
    p->drawEllipse(centre, r, r);
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.10), 1));
    p->drawEllipse(centre, r * 0.72, r * 0.72);
    p->drawEllipse(centre, r * 0.52, r * 0.52);
    p->setPen(Qt::NoPen);
    p->setBrush(RoomStyle::Mix(top, RoomStyle::Accent(pal), 0.55));
    p->drawEllipse(centre, r * 0.26, r * 0.26);
  }
  // A hairline keeps very dark or very light covers from dissolving into the page.
  p->setBrush(Qt::NoBrush);
  p->setPen(QPen(RoomStyle::Hairline(pal), 1));
  p->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
  p->restore();

}

// ------------------------------------------------------------------------------------------------------------ album model

LibraryAlbumModel::LibraryAlbumModel(QObject *parent) : QAbstractListModel(parent) {}

void LibraryAlbumModel::SetSource(LibrarySource *source) {

  if (source_) QObject::disconnect(source_, &LibrarySource::CoverLoaded, this, &LibraryAlbumModel::OnCoverLoaded);
  source_ = source;
  if (source_) QObject::connect(source_, &LibrarySource::CoverLoaded, this, &LibraryAlbumModel::OnCoverLoaded);

}

int LibraryAlbumModel::BucketFor(const int pixels) {
  return std::clamp(((pixels + 63) / 64) * 64, 128, 512);
}

void LibraryAlbumModel::SetCoverSize(const int pixels) {

  const int bucket = BucketFor(pixels);
  if (bucket == cover_size_) return;
  cover_size_ = bucket;
  cache_.clear();
  lru_.clear();
  failed_.clear();
  requested_.clear();
  queue_.clear();
  in_flight_.clear();
  if (!albums_.isEmpty()) Q_EMIT dataChanged(index(0), index(static_cast<int>(albums_.count()) - 1));

}

bool LibraryAlbumModel::SetAlbums(const LibraryAlbumList &albums) {

  bool same = albums.count() == albums_.count();
  for (qsizetype i = 0; same && i < albums.count(); ++i) same = albums.at(i).Key() == albums_.at(i).Key();
  if (same) {
    // Same albums in the same order: only the details (artwork locations) may have changed. No reset, so scrolling and selection stay.
    albums_ = albums;
    return false;
  }

  beginResetModel();
  albums_ = albums;
  row_of_key_.clear();
  for (qsizetype i = 0; i < albums_.count(); ++i) row_of_key_.insert(albums_.at(i).Key(), static_cast<int>(i));
  // Requests for albums that are gone are dropped when their turn comes.
  endResetModel();
  ++resets_;
  return true;

}

int LibraryAlbumModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(albums_.count());
}

QVariant LibraryAlbumModel::data(const QModelIndex &index, const int role) const {

  if (!index.isValid() || index.row() < 0 || index.row() >= albums_.count()) return QVariant();
  const LibraryAlbum &album = albums_.at(index.row());
  switch (role) {
    case Qt::DisplayRole:
    case TitleRole:  return album.album.isEmpty() ? tr("Unknown album") : album.album;
    case ArtistRole: return album.ArtistLabel();
    case KeyRole:    return album.Key();
    case Qt::AccessibleTextRole: return tr("%1 by %2").arg(album.album.isEmpty() ? tr("Unknown album") : album.album, album.ArtistLabel());
    case Qt::ToolTipRole: return tr("%1 by %2").arg(album.album.isEmpty() ? tr("Unknown album") : album.album, album.ArtistLabel());
  }
  return QVariant();

}

void LibraryAlbumModel::Touch(const QString &key) {

  lru_.removeAll(key);
  lru_.append(key);

}

QPixmap LibraryAlbumModel::Cover(const int row) {

  if (row < 0 || row >= albums_.count() || !source_) return QPixmap();
  const QString key = albums_.at(row).Key();
  const auto it = cache_.constFind(key);
  if (it != cache_.constEnd()) {
    return *it;
  }
  if (failed_.contains(key) || requested_.contains(key)) return QPixmap();

  requested_.insert(key);
  queue_.append(key);
  // Never inside a paint: the request is made on the next turn of the event loop.
  QMetaObject::invokeMethod(this, [this]() { Pump(); }, Qt::QueuedConnection);
  return QPixmap();

}

bool LibraryAlbumModel::CoverFailed(const int row) const {
  return row >= 0 && row < albums_.count() && failed_.contains(albums_.at(row).Key());
}

void LibraryAlbumModel::Pump() {

  while (source_ && in_flight_.count() < kMaxInFlight && !queue_.isEmpty()) {
    const QString key = queue_.takeLast();
    const int row = row_of_key_.value(key, -1);
    if (row < 0) {
      requested_.remove(key);
      continue;
    }
    const quint64 id = NextCoverId();
    in_flight_.insert(id, key);
    ++cover_requests_;
    source_->RequestCover(id, albums_.at(row), cover_size_);
  }

}

quint64 LibraryAlbumModel::NextCoverId() {
  return ++g_cover_id;
}

void LibraryAlbumModel::OnCoverLoaded(const quint64 id, const QImage &image) {

  const auto it = in_flight_.find(id);
  if (it == in_flight_.end()) return;  // not ours (or from before a size change)
  const QString key = it.value();
  in_flight_.erase(it);
  requested_.remove(key);

  if (image.isNull()) {
    failed_.insert(key);
  }
  else {
    cache_.insert(key, QPixmap::fromImage(image));
    Touch(key);
    while (lru_.count() > kMaxCached) cache_.remove(lru_.takeFirst());
  }
  const int row = row_of_key_.value(key, -1);
  if (row >= 0) Q_EMIT dataChanged(index(row), index(row));
  Pump();

}

// ------------------------------------------------------------------------------------------------------------ album grid

class LibraryAlbumDelegate : public QStyledItemDelegate {
 public:
  explicit LibraryAlbumDelegate(LibraryAlbumGrid *grid) : QStyledItemDelegate(grid), grid_(grid) {}

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    Q_UNUSED(index)
    const QFontMetrics fm(option.font);
    return QSize(grid_->tile_width(), grid_->tile_width() + TextBlockHeight(fm) + 6);
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {

    LibraryAlbumModel *model = qobject_cast<LibraryAlbumModel*>(const_cast<QAbstractItemModel*>(index.model()));
    if (!model) return;
    const QPalette &pal = option.palette;
    const int row = index.row();
    const bool hovered = row == grid_->hover_row_;
    const qreal h = hovered ? grid_->hover_->value() : 0.0;
    const bool pressed = row == grid_->pressed_row_ && !grid_->pressed_play_;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF cover(grid_->CoverRect(row));
    // Lift on hover, sink a little when pressed.
    cover.translate(0, -2.5 * h);
    if (pressed) cover = cover.adjusted(2, 2, -2, -2);

    if (h > 0.01) {
      QColor shadow = pal.color(QPalette::Shadow);
      for (int i = 3; i >= 1; --i) {
        shadow.setAlphaF(0.10 * h * (4 - i) / 3.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(shadow);
        painter->drawRoundedRect(cover.adjusted(-i * 2, -i, i * 2, i * 3 + 2), 12 + i, 12 + i);
      }
    }
    LibraryPaint::DrawSleeve(painter, cover, pal, index.data(LibraryAlbumModel::KeyRole).toString(), model->Cover(row), 10);

    const bool focused = (option.state & QStyle::State_HasFocus) && (option.state & QStyle::State_Selected);
    if (focused) {
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.85), 2));
      painter->drawRoundedRect(cover.adjusted(-2, -2, 2, 2), 12, 12);
    }

    // The play button grows out of the corner of the cover.
    if (h > 0.01) {
      const qreal size = 40 * (0.8 + 0.2 * h);
      const QRectF button(cover.right() - 10 - size, cover.bottom() - 10 - size, size, size);
      QColor fill = RoomStyle::Accent(pal);
      fill.setAlphaF(std::min(1.0, h * 1.2));
      painter->setPen(Qt::NoPen);
      painter->setBrush(fill);
      painter->drawEllipse(button);
      QColor ink = RoomStyle::AccentText(pal);
      ink.setAlphaF(std::min(1.0, h * 1.2));
      painter->setBrush(ink);
      QPainterPath triangle;
      const QPointF c = button.center() + QPointF(1.5, 0);
      triangle.moveTo(c.x() - size * 0.16, c.y() - size * 0.22);
      triangle.lineTo(c.x() + size * 0.24, c.y());
      triangle.lineTo(c.x() - size * 0.16, c.y() + size * 0.22);
      triangle.closeSubpath();
      painter->drawPath(triangle);
    }

    // Title and artist.
    const QFontMetrics fm(option.font);
    const QRect tile = option.rect;
    const int text_left = tile.left() + LibraryAlbumGrid::kTilePadding + 2;
    const int text_width = std::max(0, tile.width() - LibraryAlbumGrid::kTilePadding * 2 - 4);
    const int text_top = static_cast<int>(grid_->CoverRect(row).bottom()) + 8;
    QFont title_font = option.font;
    title_font.setWeight(QFont::DemiBold);
    painter->setFont(title_font);
    painter->setPen(RoomStyle::Text(pal));
    painter->drawText(QRect(text_left, text_top, text_width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(title_font).elidedText(index.data(LibraryAlbumModel::TitleRole).toString(), Qt::ElideRight, text_width));
    painter->setFont(option.font);
    painter->setPen(RoomStyle::Muted(pal));
    painter->drawText(QRect(text_left, text_top + fm.height() + 1, text_width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(index.data(LibraryAlbumModel::ArtistRole).toString(), Qt::ElideRight, text_width));

    painter->restore();

  }

 private:
  LibraryAlbumGrid *grid_;
};

LibraryAlbumGrid::LibraryAlbumGrid(QWidget *parent) : QListView(parent), hover_(new RoomTween(this, 0.0, 170)), settle_timer_(new QTimer(this)) {

  setViewMode(QListView::IconMode);
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setUniformItemSizes(true);
  setWrapping(true);
  setFlow(QListView::LeftToRight);
  setSpacing(0);
  setLayoutMode(QListView::Batched);
  setBatchSize(200);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setFrameShape(QFrame::NoFrame);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(tr("Albums"));
  viewport()->setAutoFillBackground(false);
  setAutoFillBackground(false);
  setItemDelegate(new LibraryAlbumDelegate(this));

  settle_timer_->setSingleShot(true);
  settle_timer_->setInterval(90);
  QObject::connect(settle_timer_, &QTimer::timeout, this, &LibraryAlbumGrid::SettleLayout);
  QObject::connect(hover_, &RoomTween::Changed, this, [this]() {
    if (hover_row_ >= 0 && model()) viewport()->update(visualRect(model()->index(hover_row_, 0)).adjusted(-8, -8, 8, 8));
  });
  QObject::connect(this, &QAbstractItemView::clicked, this, [this](const QModelIndex &index) {
    if (index.isValid() && !pressed_play_) Q_EMIT AlbumActivated(index.row());
  });
  QObject::connect(this, &QAbstractItemView::activated, this, [this](const QModelIndex &index) {
    if (index.isValid()) Q_EMIT AlbumActivated(index.row());
  });

}

void LibraryAlbumGrid::ComputeGrid() {

  // The scroll bar takes its width when it is needed, so the width the tiles can use is the viewport's.
  const int available = std::max(1, viewport()->width());
  const int columns = std::max(1, available / 190);
  const int width = std::clamp(available / columns, kMinTile - 20, 250);
  const bool columns_changed = columns != columns_;
  columns_ = columns;
  if (width != tile_width_) {
    tile_width_ = width;
    const QFontMetrics fm(font());
    setGridSize(QSize(tile_width_, tile_width_ + TextBlockHeight(fm) + 6));
    ++layout_passes_;
  }
  Q_UNUSED(columns_changed)

}

void LibraryAlbumGrid::SettleLayout() {

  settle_timer_->stop();
  ComputeGrid();
  scheduleDelayedItemsLayout();

}

void LibraryAlbumGrid::resizeEvent(QResizeEvent *e) {

  QListView::resizeEvent(e);
  // The first size is applied at once; later sizes (a drag-resize) only once the width has stopped changing.
  if (layout_passes_ == 0) SettleLayout();
  else settle_timer_->start();

}

QRect LibraryAlbumGrid::CoverRect(const int row) const {

  if (!model() || row < 0 || row >= model()->rowCount()) return QRect();
  const QRect tile = visualRect(model()->index(row, 0));
  const int side = std::max(8, tile.width() - kTilePadding * 2);
  return QRect(tile.left() + kTilePadding, tile.top() + kTilePadding / 2 + 2, side, side);

}

QRect LibraryAlbumGrid::PlayButtonRect(const int row) const {

  const QRect cover = CoverRect(row);
  return QRect(cover.right() - 10 - 40, cover.bottom() - 10 - 40, 40, 40);

}

void LibraryAlbumGrid::SetHover(const int row) {

  if (row == hover_row_) return;
  if (hover_row_ >= 0 && model()) viewport()->update(visualRect(model()->index(hover_row_, 0)).adjusted(-8, -8, 8, 8));
  hover_row_ = row;
  hover_->Jump(0.0);
  if (row >= 0) hover_->SetTarget(1.0);
  if (row >= 0 && model()) viewport()->update(visualRect(model()->index(row, 0)).adjusted(-8, -8, 8, 8));

}

void LibraryAlbumGrid::mouseMoveEvent(QMouseEvent *e) {

  QListView::mouseMoveEvent(e);
  const QModelIndex index = indexAt(e->position().toPoint());
  SetHover(index.isValid() ? index.row() : -1);
  const bool on_play = index.isValid() && PlayButtonRect(index.row()).contains(e->position().toPoint());
  viewport()->setCursor(on_play || index.isValid() ? Qt::PointingHandCursor : Qt::ArrowCursor);

}

void LibraryAlbumGrid::leaveEvent(QEvent *e) {

  QListView::leaveEvent(e);
  SetHover(-1);
  pressed_row_ = -1;

}

void LibraryAlbumGrid::mousePressEvent(QMouseEvent *e) {

  const QModelIndex index = indexAt(e->position().toPoint());
  pressed_row_ = index.isValid() && e->button() == Qt::LeftButton ? index.row() : -1;
  pressed_play_ = pressed_row_ >= 0 && PlayButtonRect(pressed_row_).contains(e->position().toPoint());
  if (pressed_row_ >= 0 && model()) viewport()->update(visualRect(model()->index(pressed_row_, 0)));
  QListView::mousePressEvent(e);

}

void LibraryAlbumGrid::mouseReleaseEvent(QMouseEvent *e) {

  const QModelIndex index = indexAt(e->position().toPoint());
  const int row = pressed_row_;
  const bool play = pressed_play_;
  QListView::mouseReleaseEvent(e);
  if (play && index.isValid() && index.row() == row && PlayButtonRect(row).contains(e->position().toPoint())) {
    pressed_play_ = false;
    pressed_row_ = -1;
    Q_EMIT PlayClicked(row);
    return;
  }
  pressed_play_ = false;
  if (row >= 0 && model()) viewport()->update(visualRect(model()->index(row, 0)));
  pressed_row_ = -1;

}

void LibraryAlbumGrid::keyPressEvent(QKeyEvent *e) {

  if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && currentIndex().isValid()) {
    if (e->modifiers() & Qt::ShiftModifier) Q_EMIT PlayClicked(currentIndex().row());
    else Q_EMIT AlbumActivated(currentIndex().row());
    return;
  }
  QListView::keyPressEvent(e);

}

void LibraryAlbumGrid::contextMenuEvent(QContextMenuEvent *e) {

  const QModelIndex index = indexAt(e->pos());
  if (!index.isValid()) return;
  const int row = index.row();
  QMenu menu(this);
  QAction *open = menu.addAction(tr("Open album"));
  QAction *play = menu.addAction(tr("Play"));
  QAction *next = menu.addAction(tr("Play next"));
  QAction *add = menu.addAction(tr("Add to playlist"));
  QAction *chosen = menu.exec(e->globalPos());
  if (chosen == open) Q_EMIT AlbumActivated(row);
  else if (chosen == play) Q_EMIT PlayClicked(row);
  else if (chosen == next) Q_EMIT AddClicked(row, true);
  else if (chosen == add) Q_EMIT AddClicked(row, false);

}

// ------------------------------------------------------------------------------------------------------------ tracks

void LibraryTrackModel::SetSongs(const SongList &songs) {

  beginResetModel();
  songs_ = songs;
  endResetModel();

}

int LibraryTrackModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(songs_.count());
}

QVariant LibraryTrackModel::data(const QModelIndex &index, const int role) const {

  if (!index.isValid() || index.row() < 0 || index.row() >= songs_.count()) return QVariant();
  const Song &song = songs_.at(index.row());
  switch (role) {
    case Qt::DisplayRole:
    case TitleRole:   return song.title().isEmpty() ? tr("Untitled") : song.title();
    case ArtistRole:  return song.artist() != album_artist_ ? song.artist() : QString();
    case NumberRole:  return song.track() > 0 ? song.track() : index.row() + 1;
    case SecondsRole: return static_cast<int>(std::max<qint64>(0, song.length_nanosec()) / 1000000000LL);
    case DiscRole:    return song.disc();
    case Qt::AccessibleTextRole: return tr("%1, %2").arg(song.title(), song.artist());
  }
  return QVariant();

}

class LibraryTrackDelegate : public QStyledItemDelegate {
 public:
  explicit LibraryTrackDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    Q_UNUSED(index)
    return QSize(200, std::max(38, QFontMetrics(option.font).height() + 18));
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = option.palette;
    const QRect row = option.rect.adjusted(2, 1, -2, -1);
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool selected = option.state & QStyle::State_Selected;
    if (hovered || selected) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(RoomStyle::Surface(pal, selected ? 0.10 : 0.06));
      painter->drawRoundedRect(row, 8, 8);
    }
    if ((option.state & QStyle::State_HasFocus) && selected) {
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.7), 1.5));
      painter->drawRoundedRect(row.adjusted(1, 1, -1, -1), 8, 8);
    }
    const QFontMetrics fm(option.font);
    const int gutter = std::max(36, fm.horizontalAdvance(QStringLiteral("000")) + 12);
    painter->setFont(option.font);
    painter->setPen(RoomStyle::Faint(pal));
    painter->drawText(QRect(row.left() + 8, row.top(), gutter, row.height()), Qt::AlignCenter, QString::number(index.data(LibraryTrackModel::NumberRole).toInt()));

    const QString length = FormatSeconds(index.data(LibraryTrackModel::SecondsRole).toInt());
    const int length_width = fm.horizontalAdvance(QStringLiteral("0:00:00"));
    const QRect length_rect(row.right() - 12 - length_width, row.top(), length_width, row.height());
    painter->setPen(RoomStyle::Muted(pal));
    painter->drawText(length_rect, Qt::AlignRight | Qt::AlignVCenter, length);

    const int text_left = row.left() + 8 + gutter + 6;
    const int text_width = std::max(0, length_rect.left() - 10 - text_left);
    const QString artist = index.data(LibraryTrackModel::ArtistRole).toString();
    const QString title = index.data(LibraryTrackModel::TitleRole).toString();
    painter->setPen(RoomStyle::Text(pal));
    int title_width = text_width;
    if (!artist.isEmpty()) title_width = std::max(text_width / 2, text_width - fm.horizontalAdvance(artist) - 14);
    painter->drawText(QRect(text_left, row.top(), title_width, row.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(title, Qt::ElideRight, title_width));
    if (!artist.isEmpty() && text_width - title_width > 20) {
      painter->setPen(RoomStyle::Muted(pal));
      painter->drawText(QRect(text_left + title_width + 10, row.top(), text_width - title_width - 10, row.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(artist, Qt::ElideRight, text_width - title_width - 10));
    }
    painter->restore();

  }
};

LibraryTrackView::LibraryTrackView(QWidget *parent) : QListView(parent) {

  setItemDelegate(new LibraryTrackDelegate(this));
  setUniformItemSizes(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setFrameShape(QFrame::NoFrame);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(tr("Album tracks"));
  viewport()->setAutoFillBackground(false);
  setAutoFillBackground(false);
  QObject::connect(this, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex &index) { if (index.isValid()) Q_EMIT RowActivated(index.row()); });

}

void LibraryTrackView::keyPressEvent(QKeyEvent *e) {

  if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && currentIndex().isValid()) {
    Q_EMIT RowActivated(currentIndex().row());
    return;
  }
  QListView::keyPressEvent(e);

}

void LibraryTrackView::contextMenuEvent(QContextMenuEvent *e) {

  const QModelIndex index = indexAt(e->pos());
  if (!index.isValid()) return;
  QMenu menu(this);
  QAction *play = menu.addAction(tr("Play from here"));
  QAction *next = menu.addAction(tr("Play next"));
  QAction *add = menu.addAction(tr("Add to playlist"));
  QAction *chosen = menu.exec(e->globalPos());
  if (chosen == play) Q_EMIT RowActivated(index.row());
  else if (chosen == next) Q_EMIT AddRowRequested(index.row(), true);
  else if (chosen == add) Q_EMIT AddRowRequested(index.row(), false);

}

// ------------------------------------------------------------------------------------------------------------ notice

LibraryNotice::LibraryNotice(QWidget *parent)
    : QWidget(parent), title_(MakeLabel(QString(), false)), hint_(MakeLabel(QString(), true)), primary_(new RoomButton(QString(), true)), secondary_(new RoomButton(QString())), timer_(new QTimer(this)) {

  setAccessibleName(tr("Library message"));
  StyleLabel(title_, RoomStyle::Scaled(font(), 1.5, true), ElidedLabel::Tone::Normal);
  title_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  StyleLabel(hint_, RoomStyle::Scaled(font(), 1.08), ElidedLabel::Tone::Muted);
  hint_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  hint_->setMaximumWidth(460);
  hint_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  QHBoxLayout *buttons = new QHBoxLayout;
  buttons->setSpacing(10);
  buttons->addStretch(1);
  buttons->addWidget(primary_);
  buttons->addWidget(secondary_);
  buttons->addStretch(1);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 12, 24, 12);
  layout->setSpacing(8);
  layout->addStretch(2);
  layout->addSpacing(150);
  layout->addWidget(title_);
  layout->addWidget(hint_, 0, Qt::AlignHCenter);
  layout->addSpacing(8);
  layout->addLayout(buttons);
  layout->addStretch(3);

  timer_->setInterval(40);
  QObject::connect(timer_, &QTimer::timeout, this, [this]() {
    angle_ = std::fmod(angle_ + 3.0, 360.0);
    update(MotifRect().adjusted(-4, -4, 4, 4));
  });
  QObject::connect(RoomMotion::Instance(), &RoomMotion::ReducedChanged, this, [this]() { SyncTimer(); });

}

void LibraryNotice::SetContent(const QString &title, const QString &hint, const QString &primary_text, const QString &secondary_text) {

  title_->setText(title);
  hint_->setText(hint);
  hint_->setVisible(!hint.isEmpty());
  primary_->setText(primary_text);
  primary_->setVisible(!primary_text.isEmpty());
  secondary_->setText(secondary_text);
  secondary_->setVisible(!secondary_text.isEmpty());
  primary_->updateGeometry();
  secondary_->updateGeometry();
  setAccessibleDescription(hint);
  FitHint();

}

void LibraryNotice::FitHint() {

  // A wrapped label inside a centred layout item does not get its wrapped height by itself, so the size is decided here: as wide as the page allows
  // (up to a comfortable line length), and as tall as the text needs at that width.
  const int width = std::clamp(this->width() - 64, 220, 460);
  const QFontMetrics fm(hint_->font());
  const int height = fm.boundingRect(0, 0, width, 10000, Qt::TextWordWrap | Qt::AlignHCenter, hint_->text()).height() + 6;
  hint_->setFixedSize(width, height);

}

void LibraryNotice::resizeEvent(QResizeEvent *e) {

  QWidget::resizeEvent(e);
  FitHint();

}

void LibraryNotice::SetSpinning(const bool spinning) {

  spinning_ = spinning;
  SyncTimer();

}

void LibraryNotice::SyncTimer() {

  // The timer exists only while the record is turning in front of the user.
  const bool want = spinning_ && isVisible() && !RoomMotion::Instance()->reduced();
  if (want && !timer_->isActive()) timer_->start();
  else if (!want && timer_->isActive()) timer_->stop();

}

void LibraryNotice::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  SyncTimer();
}

void LibraryNotice::hideEvent(QHideEvent *e) {
  QWidget::hideEvent(e);
  SyncTimer();
}

QRect LibraryNotice::MotifRect() const {

  const QRect title = title_->geometry();
  const int side = 132;
  return QRect(width() / 2 - side / 2, std::max(8, title.top() - side - 14), side, side);

}

void LibraryNotice::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPalette &pal = palette();
  const QRectF box = MotifRect();
  const QPointF centre = box.center();
  const qreal radius = box.width() / 2.0;

  // The sleeve, a little behind the record.
  const QRectF sleeve(centre.x() - radius * 1.15, centre.y() - radius * 0.95, radius * 1.5, radius * 1.9);
  Q_UNUSED(sleeve)

  p.setPen(Qt::NoPen);
  p.setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.05));
  p.drawEllipse(centre, radius, radius);
  p.setBrush(Qt::NoBrush);
  for (int i = 1; i <= 5; ++i) {
    const qreal r = radius * (0.42 + i * 0.11);
    p.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), i % 2 ? 0.09 : 0.06), 1));
    p.drawEllipse(centre, r, r);
  }
  // The label and the sheen are what turn.
  p.save();
  p.translate(centre);
  p.rotate(angle_);
  p.setPen(Qt::NoPen);
  p.setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Accent(pal), 0.6));
  p.drawEllipse(QPointF(0, 0), radius * 0.32, radius * 0.32);
  p.setBrush(RoomStyle::Window(pal));
  p.drawEllipse(QPointF(radius * 0.13, 0), radius * 0.035, radius * 0.035);
  QColor sheen = RoomStyle::Text(pal);
  sheen.setAlphaF(0.12);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(sheen, 3, Qt::SolidLine, Qt::RoundCap));
  p.drawArc(QRectF(-radius * 0.82, -radius * 0.82, radius * 1.64, radius * 1.64), 20 * 16, 38 * 16);
  p.restore();

}
