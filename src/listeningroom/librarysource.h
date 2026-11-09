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

#ifndef LIBRARYSOURCE_H
#define LIBRARYSOURCE_H

#include "config.h"

#include <functional>

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "core/song.h"
#include "includes/shared_ptr.h"

class CollectionBackend;
class AlbumCoverLoader;
class AlbumCoverLoaderResult;

// One album of the library, as the Library page shows it.
struct LibraryAlbum {
  QString artist;  // the album artist, empty for compilations
  QString album;
  bool art_embedded = false;
  bool art_unset = false;
  QUrl art_automatic;
  QUrl art_manual;
  QUrl first_url;  // a file of the album, which the cover loader needs for embedded art

  // Identity inside one library: the same artist and album name.
  QString Key() const { return artist + QLatin1Char('\x1f') + album; }
  // The artist as shown: compilations have no album artist.
  QString ArtistLabel() const;
};
using LibraryAlbumList = QList<LibraryAlbum>;

// Where the Library page gets its data. There is exactly one store, the collection: this only reads it.
//
// Everything is asynchronous and used on the GUI thread only: a request is answered later by a signal carrying the request id, and nothing ever waits
// for another thread. A source answers each request exactly once (with ok=false when the store cannot be read, for example because its thread is gone),
// unless the source itself is destroyed first, in which case no answer arrives and none can dangle.
class LibrarySource : public QObject {
  Q_OBJECT

 public:
  explicit LibrarySource(QObject *parent = nullptr) : QObject(parent) {}

  // All albums, or (with a filter) the albums that have a track matching the text: title, artist or album. Answered by AlbumsLoaded(id, ...).
  virtual void RequestAlbums(const quint64 id, const QString &filter_text) = 0;
  // The tracks of one album, in disc and track order. Answered by TracksLoaded(id, ...).
  virtual void RequestTracks(const quint64 id, const LibraryAlbum &album) = 0;

  // Asks for a cover scaled to `pixel_size` (the long side, in device pixels). The answer arrives as CoverLoaded(id, image).
  virtual void RequestCover(const quint64 id, const LibraryAlbum &album, const int pixel_size) = 0;
  virtual void CancelCover(const quint64 id) { Q_UNUSED(id) }

 Q_SIGNALS:
  void AlbumsLoaded(const quint64 id, const LibraryAlbumList &albums, const bool ok);
  void TracksLoaded(const quint64 id, const SongList &songs, const bool ok);
  void CoverLoaded(const quint64 id, const QImage &image);
  // The collection changed (songs added, removed or the database was reset). The page reloads, coalesced.
  void Changed();
};

// The real source: the collection backend and the existing cover loader.
class CollectionLibrarySource : public LibrarySource {
  Q_OBJECT

 public:
  CollectionLibrarySource(CollectionBackend *backend, SharedPtr<AlbumCoverLoader> cover_loader, QObject *parent = nullptr);

  ~CollectionLibrarySource() override;

  // Shared with the work that runs on the backend's thread. The mutex makes "deliver to the source unless it is gone" atomic against the source's destruction.
  struct Mailbox;

  void RequestAlbums(const quint64 id, const QString &filter_text) override;
  void RequestTracks(const quint64 id, const LibraryAlbum &album) override;
  void RequestCover(const quint64 id, const LibraryAlbum &album, const int pixel_size) override;
  void CancelCover(const quint64 id) override;

 private Q_SLOTS:
  void AlbumCoverLoaded(const quint64 loader_id, const AlbumCoverLoaderResult &result);

 private:
  // Runs `read` on the backend's thread; it returns what to run on the GUI thread to announce the answer. `fail` runs there instead when the read cannot happen.
  void Run(std::function<std::function<void()>()> read, std::function<void()> fail);

  QPointer<CollectionBackend> backend_;
  SharedPtr<Mailbox> mailbox_;
  SharedPtr<AlbumCoverLoader> cover_loader_;
  QHash<quint64, quint64> pending_;  // loader task id -> request id
};

#endif  // LIBRARYSOURCE_H
