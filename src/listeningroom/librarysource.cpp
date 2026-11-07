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

#include <QCoreApplication>
#include <QImage>
#include <QMetaObject>
#include <QSize>
#include <QThread>
#include <QString>
#include <QUrl>

#include "librarysource.h"
#include "collection/collectionbackend.h"
#include "collection/collectionfilteroptions.h"
#include "covermanager/albumcoverloader.h"
#include "covermanager/albumcoverloaderoptions.h"
#include "covermanager/albumcoverloaderresult.h"

namespace {

// The database is read where the backend lives. The backend has its own long-lived thread with its own database connection; a short-lived worker
// must not open connections of its own (thread pool threads come and go). The worker waits for the answer, the GUI thread never does.
template<typename Function>
auto OnBackendThread(QObject *backend, Function function) -> decltype(function()) {

  using Result = decltype(function());
  if (!backend || QThread::currentThread() == backend->thread()) return function();
  Result result{};
  QMetaObject::invokeMethod(backend, [&result, &function]() { result = function(); }, Qt::BlockingQueuedConnection);
  return result;

}

}  // namespace

QString LibraryAlbum::ArtistLabel() const {
  return artist.isEmpty() ? QCoreApplication::translate("LibraryView", "Various artists") : artist;
}

CollectionLibrarySource::CollectionLibrarySource(CollectionBackend *backend, SharedPtr<AlbumCoverLoader> cover_loader, QObject *parent) : LibrarySource(parent), backend_(backend), cover_loader_(cover_loader) {

  if (backend_) {
    QObject::connect(backend_, &CollectionBackend::SongsAdded, this, &LibrarySource::Changed);
    QObject::connect(backend_, &CollectionBackend::SongsDeleted, this, &LibrarySource::Changed);
    QObject::connect(backend_, &CollectionBackend::DatabaseReset, this, &LibrarySource::Changed);
    QObject::connect(backend_, &CollectionBackend::DirectoryDeleted, this, &LibrarySource::Changed);
  }
  if (cover_loader_) {
    QObject::connect(&*cover_loader_, &AlbumCoverLoader::AlbumCoverLoaded, this, &CollectionLibrarySource::AlbumCoverLoaded);
  }

}

LibraryAlbumList CollectionLibrarySource::LoadAlbums(const QString &filter_text) {

  LibraryAlbumList albums;
  if (!backend_) return albums;

  CollectionFilterOptions options;
  if (!filter_text.trimmed().isEmpty()) options.set_filter_text(filter_text.trimmed());

  const CollectionBackend::AlbumList list = OnBackendThread(backend_, [this, &options]() { return backend_->GetAllAlbums(options); });
  albums.reserve(list.count());
  for (const CollectionBackend::Album &item : list) {
    LibraryAlbum album;
    album.artist = item.album_artist;
    album.album = item.album;
    album.art_embedded = item.art_embedded;
    album.art_unset = item.art_unset;
    album.art_automatic = item.art_automatic;
    album.art_manual = item.art_manual;
    if (!item.urls.isEmpty()) album.first_url = item.urls.first();
    albums << album;
  }
  return albums;

}

SongList CollectionLibrarySource::LoadTracks(const LibraryAlbum &album) {

  if (!backend_) return SongList();
  SongList songs = OnBackendThread(backend_, [this, &album]() { return album.artist.isEmpty() ? backend_->GetCompilationSongs(album.album) : backend_->GetAlbumSongs(album.artist, album.album); });
  // Disc, then track number: the order of the record, whatever order the database returned.
  std::stable_sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) {
    if (a.disc() != b.disc()) return a.disc() < b.disc();
    return a.track() < b.track();
  });
  return songs;

}

void CollectionLibrarySource::RequestCover(const quint64 id, const LibraryAlbum &album, const int pixel_size) {

  if (!cover_loader_ || pixel_size <= 0) {
    Q_EMIT CoverLoaded(id, QImage());
    return;
  }

  AlbumCoverLoaderOptions options(AlbumCoverLoaderOptions::Option::ScaledImage, QSize(pixel_size, pixel_size), 1.0);
  const quint64 loader_id = cover_loader_->LoadImageAsync(options, album.art_embedded, album.art_automatic, album.art_manual, album.art_unset, album.first_url, Song::Source::Collection);
  pending_.insert(loader_id, id);

}

void CollectionLibrarySource::CancelCover(const quint64 id) {

  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    if (it.value() == id) {
      cover_loader_->CancelTask(it.key());
      pending_.erase(it);
      return;
    }
  }

}

void CollectionLibrarySource::AlbumCoverLoaded(const quint64 loader_id, const AlbumCoverLoaderResult &result) {

  const auto it = pending_.find(loader_id);
  if (it == pending_.end()) return;
  const quint64 id = it.value();
  pending_.erase(it);
  Q_EMIT CoverLoaded(id, result.success ? result.image_scaled : QImage());

}
