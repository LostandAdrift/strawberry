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

#include <QAbstractListModel>
#include <QModelIndex>
#include <QObject>
#include <QVariant>

#include "core/song.h"
#include "playlist/playlist.h"
#include "playlist/playlistitem.h"
#include "roomqueuemodel.h"

RoomQueueModel::RoomQueueModel(QObject *parent) : QAbstractListModel(parent), playlist_(nullptr), playback_active_(false) {}

void RoomQueueModel::SetPlaylist(Playlist *playlist) {

  if (playlist == playlist_) return;

  beginResetModel();
  if (playlist_) QObject::disconnect(playlist_, nullptr, this, nullptr);
  playlist_ = playlist;
  if (playlist_) {
    // Every structural change of the playlist, including the reset when a playlist is (re)loaded asynchronously, re-reads the rows.
    QObject::connect(playlist_, &QAbstractItemModel::rowsInserted, this, &RoomQueueModel::SourceStructureChanged);
    QObject::connect(playlist_, &QAbstractItemModel::rowsRemoved, this, &RoomQueueModel::SourceStructureChanged);
    QObject::connect(playlist_, &QAbstractItemModel::rowsMoved, this, &RoomQueueModel::SourceStructureChanged);
    QObject::connect(playlist_, &QAbstractItemModel::modelReset, this, &RoomQueueModel::SourceStructureChanged);
    QObject::connect(playlist_, &QAbstractItemModel::layoutChanged, this, &RoomQueueModel::SourceStructureChanged);
    QObject::connect(playlist_, &QAbstractItemModel::dataChanged, this, &RoomQueueModel::SourceDataChanged);
    QObject::connect(playlist_, &Playlist::CurrentSongChanged, this, &RoomQueueModel::SourceCurrentChanged);
    QObject::connect(playlist_, &Playlist::RestoreDone, this, &RoomQueueModel::SourceStructureChanged);
    QObject::connect(playlist_, &QObject::destroyed, this, &RoomQueueModel::SourceDestroyed);
  }
  endResetModel();

}

void RoomQueueModel::SourceDestroyed() {

  beginResetModel();
  playlist_ = nullptr;
  endResetModel();

}

void RoomQueueModel::SourceStructureChanged() {

  // Rows may have shifted, so the whole view is refreshed. Queues are a few thousand rows at most and the view is virtual.
  beginResetModel();
  endResetModel();

}

void RoomQueueModel::SourceDataChanged(const QModelIndex &top_left, const QModelIndex &bottom_right) {

  if (!top_left.isValid() || !bottom_right.isValid() || rowCount() == 0) return;
  Q_EMIT dataChanged(index(top_left.row()), index(std::min(bottom_right.row(), rowCount() - 1)));

}

void RoomQueueModel::SourceCurrentChanged() {

  // The current track decides the "up next" numbers of every row.
  if (rowCount() > 0) Q_EMIT dataChanged(index(0), index(rowCount() - 1));

}

void RoomQueueModel::SetPlaybackActive(const bool active) {

  if (playback_active_ == active) return;
  playback_active_ = active;
  if (rowCount() > 0) Q_EMIT dataChanged(index(0), index(rowCount() - 1));

}

int RoomQueueModel::current_row() const {
  return playlist_ ? playlist_->current_row() : -1;
}

int RoomQueueModel::rowCount(const QModelIndex &parent) const {

  if (parent.isValid() || !playlist_) return 0;
  return playlist_->rowCount(QModelIndex());

}

Qt::ItemFlags RoomQueueModel::flags(const QModelIndex &index) const {

  if (!index.isValid()) return Qt::NoItemFlags;
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;

}

QVariant RoomQueueModel::data(const QModelIndex &index, const int role) const {

  if (!index.isValid() || !playlist_ || index.row() < 0 || index.row() >= playlist_->rowCount(QModelIndex())) return QVariant();

  const int row = index.row();
  const PlaylistItemPtr &item = playlist_->item_at(row);
  if (!item) return QVariant();

  const int current = playlist_->current_row();

  switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
      return item->EffectiveMetadata().PrettyTitle();
    case ArtistRole: {
      const Song song = item->EffectiveMetadata();
      return song.artist().isEmpty() ? song.effective_albumartist() : song.artist();
    }
    case AlbumRole:
      return item->EffectiveMetadata().album();
    case LengthSecondsRole:
      return static_cast<int>(item->EffectiveMetadata().length_nanosec() / 1000000000LL);
    case IsCurrentRole:
      return playback_active_ && row == current;
    case UpNextRole:
      return current >= 0 && playback_active_ ? row - current : row + 1;
    case UnavailableRole:
      return item->EffectiveMetadata().url().isEmpty();
    default:
      break;
  }
  return QVariant();

}
