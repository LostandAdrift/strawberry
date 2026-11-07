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

#ifndef LIBRARYPARTS_H
#define LIBRARYPARTS_H

#include "config.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QListView>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QWidget>

#include "core/song.h"
#include "librarysource.h"
#include "roommotion.h"

class QContextMenuEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QResizeEvent;
class RoomButton;

// The albums on display. It owns no data of its own beyond the list it was given and a bounded cache of small cover thumbnails.
//
// Covers are requested at one fixed size (a "bucket" that depends on the tile size and the screen scale, not on every intermediate window width),
// at most a few at a time, newest request first (the tiles on screen now are the ones asked for last). A cover that has arrived is cached and
// painted scaled to the tile, which is cheap; resizing the window never asks for a cover again.
class LibraryAlbumModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    TitleRole = Qt::UserRole + 1,
    ArtistRole,
    KeyRole
  };
  static constexpr int kMaxCached = 320;
  static constexpr int kMaxInFlight = 6;

  explicit LibraryAlbumModel(QObject *parent = nullptr);

  void SetSource(LibrarySource *source);
  // Replaces the albums. Returns false (and does not reset the model) when the list is the same albums in the same order.
  bool SetAlbums(const LibraryAlbumList &albums);
  const LibraryAlbum &album(const int row) const { return albums_.at(row); }
  int row_of(const QString &key) const { return row_of_key_.value(key, -1); }

  // The size, in device pixels, covers are requested at. A different bucket drops the cached thumbnails.
  void SetCoverSize(const int pixels);
  int cover_size() const { return cover_size_; }
  static int BucketFor(const int pixels);
  // Ids for cover requests, unique in the process, so several consumers can share one source.
  static quint64 NextCoverId();

  // The cover of a row if it is cached; otherwise a null pixmap, and the cover is requested (once).
  QPixmap Cover(const int row);
  bool CoverFailed(const int row) const;

  int cover_requests() const { return cover_requests_; }
  int cached_covers() const { return static_cast<int>(cache_.count()); }
  int in_flight() const { return static_cast<int>(in_flight_.count()); }
  int resets() const { return resets_; }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

 private Q_SLOTS:
  void OnCoverLoaded(const quint64 id, const QImage &image);

 private:
  void Pump();
  void Touch(const QString &key);

  QPointer<LibrarySource> source_;
  LibraryAlbumList albums_;
  QHash<QString, int> row_of_key_;
  int cover_size_ = 256;
  QHash<QString, QPixmap> cache_;
  QList<QString> lru_;                  // most recently used last
  QSet<QString> failed_;
  QHash<quint64, QString> in_flight_;   // request id -> key
  QSet<QString> requested_;             // keys in flight or queued
  QList<QString> queue_;                // waiting; the last one is asked for first
  int cover_requests_ = 0;
  int resets_ = 0;
};

// The album tiles. A grid of squares with the cover, title and artist; the tile under the pointer lifts a little and shows a play button.
// The tile width is decided from the available width, but only after the window has stopped changing size for a moment, so a drag-resize is not
// a series of full re-layouts of every tile.
class LibraryAlbumGrid : public QListView {
  Q_OBJECT

 public:
  explicit LibraryAlbumGrid(QWidget *parent = nullptr);

  int tile_width() const { return tile_width_; }
  int columns() const { return columns_; }
  // Applies the grid size for the current width now (otherwise it is done once the width has settled).
  void SettleLayout();
  int layout_passes() const { return layout_passes_; }

  int hover_row() const { return hover_row_; }
  double hover_amount() const { return hover_->value(); }
  // The play button of the tile at `row` (viewport coordinates), for hit testing and tests.
  QRect PlayButtonRect(const int row) const;
  QRect CoverRect(const int row) const;

  static constexpr int kMinTile = 172;
  static constexpr int kTilePadding = 10;
  static constexpr int kTextHeightLines = 2;

 Q_SIGNALS:
  void AlbumActivated(const int row);
  void PlayClicked(const int row);
  void AddClicked(const int row, const bool play_next);

 protected:
  void resizeEvent(QResizeEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void leaveEvent(QEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;

 private:
  void ComputeGrid();
  void SetHover(const int row);

  int tile_width_ = kMinTile;
  int columns_ = 1;
  int layout_passes_ = 0;
  int hover_row_ = -1;
  int pressed_row_ = -1;
  bool pressed_play_ = false;
  RoomTween *hover_;
  QTimer *settle_timer_;
  friend class LibraryAlbumDelegate;
};

// One album's track list: number, title (artist when it differs), length. Enter or a double click plays from the row.
class LibraryTrackModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    TitleRole = Qt::UserRole + 1,
    ArtistRole,
    NumberRole,
    SecondsRole,
    DiscRole
  };
  explicit LibraryTrackModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

  void SetSongs(const SongList &songs);
  const SongList &songs() const { return songs_; }
  void set_album_artist(const QString &artist) { album_artist_ = artist; }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

 private:
  SongList songs_;
  QString album_artist_;
};

class LibraryTrackView : public QListView {
  Q_OBJECT

 public:
  explicit LibraryTrackView(QWidget *parent = nullptr);

 Q_SIGNALS:
  void RowActivated(const int row);
  void AddRowRequested(const int row, const bool play_next);

 protected:
  void keyPressEvent(QKeyEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;
};

// A quiet full-page message with a record motif: used while the library is read, when there is no music, and when a search finds nothing.
class LibraryNotice : public QWidget {
  Q_OBJECT

 public:
  explicit LibraryNotice(QWidget *parent = nullptr);

  void SetContent(const QString &title, const QString &hint, const QString &primary_text, const QString &secondary_text);
  // The record turns slowly while it waits. The timer runs only while this is true, the notice is visible, and motion is not reduced.
  void SetSpinning(const bool spinning);
  bool spinning() const { return spinning_; }
  bool timer_active() const { return timer_->isActive(); }

  RoomButton *primary_button() const { return primary_; }
  RoomButton *secondary_button() const { return secondary_; }
  QLabel *title_label() const { return title_; }
  QLabel *hint_label() const { return hint_; }

  QSize minimumSizeHint() const override { return QSize(280, 260); }

 protected:
  void paintEvent(QPaintEvent *e) override;
  void showEvent(QShowEvent *e) override;
  void hideEvent(QHideEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;

 private:
  void SyncTimer();
  void FitHint();
  QRect MotifRect() const;

  QLabel *title_;
  QLabel *hint_;
  RoomButton *primary_;
  RoomButton *secondary_;
  QTimer *timer_;
  bool spinning_ = false;
  qreal angle_ = 0;
};

// Paints one cover-like square with the palette: used for albums without artwork (a deterministic tint per album, so every empty sleeve is distinct),
// and by the album page. Public so the Listening Room can use the same sleeve.
namespace LibraryPaint {
void DrawSleeve(QPainter *p, const QRectF &rect, const QPalette &pal, const QString &seed, const QPixmap &cover, const qreal radius);
}

#endif  // LIBRARYPARTS_H
