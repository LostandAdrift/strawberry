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

#ifndef ROOMQUEUEMODEL_H
#define ROOMQUEUEMODEL_H

#include "config.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QString>
#include <QVariant>

#include "playlist/playlist.h"

// A read-only view of a real Playlist for the Listening Room's queue. It owns no tracks: every row is read from the playlist's own items,
// and the playlist's changes (inserts, removals, moves, resets while it loads, metadata updates, the current track) are forwarded,
// so the queue can never disagree with the playlist or the Library view. Switching playlists rebinds the model.
class RoomQueueModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    TitleRole = Qt::UserRole + 1,
    ArtistRole,
    AlbumRole,
    LengthSecondsRole,
    IsCurrentRole,
    // Negative for tracks before the current one, 0 for the current one, 1 for the next track, 2 for the one after, ...
    // Without a current track every row counts as played-never: the value is the row + 1.
    UpNextRole,
    UnavailableRole
  };

  explicit RoomQueueModel(QObject *parent = nullptr);

  void SetPlaylist(Playlist *playlist);
  Playlist *playlist() const { return playlist_; }
  QString playlist_name() const { return playlist_name_; }
  void set_playlist_name(const QString &name) { playlist_name_ = name; }

  // True while the current track is playing or paused, as opposed to merely being the playlist's remembered current row.
  void SetPlaybackActive(const bool active);
  bool playback_active() const { return playback_active_; }

  // The row of the track being played, or -1.
  int current_row() const;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

 private Q_SLOTS:
  void SourceStructureChanged();
  void SourceDataChanged(const QModelIndex &top_left, const QModelIndex &bottom_right);
  void SourceCurrentChanged();
  void SourceDestroyed();

 private:
  QPointer<Playlist> playlist_;
  QString playlist_name_;
  bool playback_active_;
};

#endif  // ROOMQUEUEMODEL_H
