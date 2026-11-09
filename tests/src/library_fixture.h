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

#ifndef LIBRARY_FIXTURE_H
#define LIBRARY_FIXTURE_H

// A synthetic library for the Library page tests and renders: invented artists and albums, tracks and covers drawn by code.
// It is a LibrarySource, so the production LibraryView, model and delegates are exercised with it. Nothing here reads the user's library,
// configuration or accounts.

#include <atomic>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QSemaphore>
#include <QStringList>
#include <QUrl>

#include "core/song.h"
#include "listeningroom/librarysource.h"
#include "listeningroom_fixture.h"

class FakeLibrarySource : public LibrarySource {
 public:
  explicit FakeLibrarySource(const int album_count = 30, QObject *parent = nullptr) : LibrarySource(parent) {
    pool_.setMaxThreadCount(2);
    Generate(album_count);
  }
  ~FakeLibrarySource() override {
    // Workers waiting on a gate are let go; then the pool (declared last, so destroyed first) is waited for before this object goes.
    ReleaseAlbums();
    ReleaseTracks();
    pool_.waitForDone();
  }

  static QStringList Artists() {
    return { QStringLiteral("Marin Holt"), QStringLiteral("The Paper Lanterns"), QStringLiteral("Ines Varga"), QStringLiteral("Oren & Wilde"), QStringLiteral("Kestrel Quartet"),
             QStringLiteral("Tamsin Reed"), QStringLiteral("Low Harbour"), QStringLiteral("Juniper Vale"), QStringLiteral("Dara Okonjo"), QStringLiteral("The Salt Choir") };
  }
  static QStringList AlbumNames() {
    return { QStringLiteral("Slow Tide"), QStringLiteral("Night Ferry"), QStringLiteral("Weather for Strings"), QStringLiteral("Small Hours"), QStringLiteral("Open Water"),
             QStringLiteral("Copper Hour"), QStringLiteral("Paper Boats"), QStringLiteral("Glass Orchard"), QStringLiteral("Fog Signals"), QStringLiteral("Evening, Eventually"),
             QStringLiteral("Lantern Fish"), QStringLiteral("The Long Way Round") };
  }
  static QStringList TrackWords() {
    return { QStringLiteral("Salt"), QStringLiteral("Harbour"), QStringLiteral("Lamps"), QStringLiteral("Low"), QStringLiteral("Water"), QStringLiteral("Quiet"), QStringLiteral("Engine"),
             QStringLiteral("Overland"), QStringLiteral("Copper"), QStringLiteral("Fog"), QStringLiteral("Orchard"), QStringLiteral("Tidewrack"), QStringLiteral("Ferry"), QStringLiteral("Home") };
  }

  // Replaces the library (call before the view loads, or follow with Q_EMIT Changed()).
  void Generate(const int album_count) {
    QMutexLocker lock(&mutex_);
    albums_.clear();
    tracks_.clear();
    const QStringList artists = Artists();
    const QStringList names = AlbumNames();
    const QStringList words = TrackWords();
    for (int i = 0; i < album_count; ++i) {
      LibraryAlbum album;
      album.artist = artists.at(i % artists.size());
      album.album = names.at((i * 5 + i / artists.size()) % names.size()) + (i >= names.size() ? QStringLiteral(" ") + QString::number(i / names.size() + 1) : QString());
      album.first_url = QUrl(QStringLiteral("file:///music/%1/%2/01.flac").arg(album.artist, album.album));
      albums_ << album;
      SongList songs;
      const int count = 5 + (i % 5);
      for (int t = 0; t < count; ++t) {
        Song song(Song::Source::Collection);
        const QString title = words.at((i + t) % words.size()) + QLatin1Char(' ') + words.at((i * 3 + t * 2 + 1) % words.size());
        song.Init(title, album.artist, album.album, static_cast<qint64>(170 + ((i * 31 + t * 47) % 200)) * 1000000000LL);
        song.set_albumartist(album.artist);
        song.set_track(t + 1);
        song.set_disc(1);
        song.set_url(QUrl(QStringLiteral("file:///music/%1/%2/%3.flac").arg(album.artist, album.album).arg(t + 1, 2, 10, QLatin1Char('0'))));
        songs << song;
      }
      tracks_.insert(album.Key(), songs);
    }
  }

  // Keeps only the first `n` albums (a smaller library), without regenerating.
  void Clear() {
    QMutexLocker lock(&mutex_);
    albums_.clear();
    tracks_.clear();
  }

  // The synchronous reads (they wait on the gates, so they run on a worker thread of the fake, never on the caller's).
  LibraryAlbumList LoadAlbums(const QString &filter_text) {
    ++load_calls;
    if (gated_) gate.tryAcquire(1, 5000);
    QMutexLocker lock(&mutex_);
    if (filter_text.trimmed().isEmpty()) return albums_;
    // A search matches artist, album or any track title.
    const QString needle = filter_text.trimmed().toLower();
    LibraryAlbumList matching;
    for (const LibraryAlbum &album : std::as_const(albums_)) {
      bool match = album.artist.toLower().contains(needle) || album.album.toLower().contains(needle);
      const SongList songs = tracks_.value(album.Key());
      for (const Song &song : songs) {
        if (song.title().toLower().contains(needle)) match = true;
      }
      if (match) matching << album;
    }
    ++search_calls;
    return matching;
  }

  SongList LoadTracks(const LibraryAlbum &album) {
    ++track_calls;
    if (gated_tracks_) track_gate.tryAcquire(1, 5000);
    QMutexLocker lock(&mutex_);
    return tracks_.value(album.Key());
  }

  // The asynchronous interface: the read happens on a thread of the fake and the answer is queued to the source's own thread.
  void RequestAlbums(const quint64 id, const QString &filter_text) override {
    pool_.start([this, id, filter_text]() {
      const LibraryAlbumList list = LoadAlbums(filter_text);
      QMetaObject::invokeMethod(this, [this, id, list]() { Q_EMIT AlbumsLoaded(id, list, true); }, Qt::QueuedConnection);
    });
  }
  void RequestTracks(const quint64 id, const LibraryAlbum &album) override {
    pool_.start([this, id, album]() {
      const SongList songs = LoadTracks(album);
      QMetaObject::invokeMethod(this, [this, id, songs]() { Q_EMIT TracksLoaded(id, songs, true); }, Qt::QueuedConnection);
    });
  }

  void RequestCover(const quint64 id, const LibraryAlbum &album, const int pixel_size) override {
    ++cover_requests;
    last_cover_size = pixel_size;
    if (no_covers) {
      QMetaObject::invokeMethod(this, [this, id]() { Q_EMIT CoverLoaded(id, QImage()); }, Qt::QueuedConnection);
      return;
    }
    const int variant = static_cast<int>(qHash(album.Key()) % 3);
    QMetaObject::invokeMethod(this, [this, id, variant, pixel_size]() { Q_EMIT CoverLoaded(id, RoomFixture::Cover(variant, std::max(64, pixel_size))); }, Qt::QueuedConnection);
  }

  SongList AllTracksOf(const int album_index) {
    QMutexLocker lock(&mutex_);
    return tracks_.value(albums_.at(album_index).Key());
  }
  LibraryAlbum AlbumAt(const int index) {
    QMutexLocker lock(&mutex_);
    return albums_.at(index);
  }

  // Makes the next reads wait until the test releases them (for lifecycle tests).
  void GateAlbums() { gated_ = true; }
  void ReleaseAlbums() { gated_ = false; gate.release(10); }
  void GateTracks() { gated_tracks_ = true; }
  void ReleaseTracks() { gated_tracks_ = false; track_gate.release(10); }

  std::atomic<int> load_calls{0};
  std::atomic<int> search_calls{0};
  std::atomic<int> track_calls{0};
  std::atomic<int> cover_requests{0};
  std::atomic<int> last_cover_size{0};
  bool no_covers = false;
  QSemaphore gate;
  QSemaphore track_gate;

 private:
  QMutex mutex_;
  LibraryAlbumList albums_;
  QHash<QString, SongList> tracks_;
  std::atomic<bool> gated_{false};
  std::atomic<bool> gated_tracks_{false};
  QThreadPool pool_;
};

#endif  // LIBRARY_FIXTURE_H
