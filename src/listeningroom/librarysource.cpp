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
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
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

struct CollectionLibrarySource::Mailbox {
  QMutex mutex;
  CollectionLibrarySource *owner = nullptr;

  // Queues `function` on the source's (GUI) thread unless the source is gone. Posting never blocks, so holding the lock here is cheap.
  void Post(std::function<void()> function) {
    QMutexLocker lock(&mutex);
    if (owner) QMetaObject::invokeMethod(owner, std::move(function), Qt::QueuedConnection);
  }
};

namespace {

// One request to the backend's thread. It answers exactly once: with the read, or with the failure when it is destroyed without having run (the backend
// or its event loop is gone, so the queued call is dropped). Nothing waits for the backend's thread, and nothing is leaked or retried.
class Job {
 public:
  Job(SharedPtr<CollectionLibrarySource::Mailbox> mailbox, std::function<std::function<void()>()> read, std::function<void()> fail) : mailbox_(mailbox), read_(std::move(read)), fail_(std::move(fail)) {}
  ~Job() { if (!done_) mailbox_->Post(std::move(fail_)); }

  void Run() {
    done_ = true;
    mailbox_->Post(read_());
  }

 private:
  SharedPtr<CollectionLibrarySource::Mailbox> mailbox_;
  std::function<std::function<void()>()> read_;
  std::function<void()> fail_;
  bool done_ = false;
};

}  // namespace

QString LibraryAlbum::ArtistLabel() const {
  return artist.isEmpty() ? QCoreApplication::translate("LibraryView", "Various artists") : artist;
}

CollectionLibrarySource::CollectionLibrarySource(CollectionBackend *backend, SharedPtr<AlbumCoverLoader> cover_loader, QObject *parent) : LibrarySource(parent), backend_(backend), mailbox_(std::make_shared<Mailbox>()), cover_loader_(cover_loader) {

  mailbox_->owner = this;

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

CollectionLibrarySource::~CollectionLibrarySource() {

  // From here on no answer is delivered to this object, whichever thread tries.
  QMutexLocker lock(&mailbox_->mutex);
  mailbox_->owner = nullptr;

}

void CollectionLibrarySource::Run(std::function<std::function<void()>()> read, std::function<void()> fail) {

  // The queued call holds the job; if the call is dropped (the backend was deleted, its thread is gone) the job's destructor reports the failure.
  auto job = std::make_shared<Job>(mailbox_, std::move(read), std::move(fail));
  CollectionBackend *backend = backend_;
  QThread *thread = backend ? backend->thread() : nullptr;
  if (!thread || !thread->isRunning()) return;  // the job dies here and reports the failure
  QMetaObject::invokeMethod(backend, [job]() { job->Run(); }, Qt::QueuedConnection);

}

void CollectionLibrarySource::RequestAlbums(const quint64 id, const QString &filter_text) {

  CollectionFilterOptions options;
  if (!filter_text.trimmed().isEmpty()) options.set_filter_text(filter_text.trimmed());

  // The work holds the backend by guarded pointer and the source only through the mailbox: whichever goes away first, nothing dangling is touched.
  const QPointer<CollectionBackend> backend = backend_;
  Run([this, backend, options, id]() -> std::function<void()> {
    // On the backend's thread: it has its own long-lived database connection, which is why the read happens here.
    const CollectionBackend::AlbumList list = backend ? backend->GetAllAlbums(options) : CollectionBackend::AlbumList();
    LibraryAlbumList albums;
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
    return [this, id, albums]() { Q_EMIT AlbumsLoaded(id, albums, true); };
  }, [this, id]() { Q_EMIT AlbumsLoaded(id, LibraryAlbumList(), false); });

}

void CollectionLibrarySource::RequestTracks(const quint64 id, const LibraryAlbum &album) {

  const QPointer<CollectionBackend> backend = backend_;
  Run([this, backend, album, id]() -> std::function<void()> {
    SongList songs = !backend ? SongList() : (album.artist.isEmpty() ? backend->GetCompilationSongs(album.album) : backend->GetAlbumSongs(album.artist, album.album));
    // Disc, then track number: the order of the record, whatever order the database returned.
    std::stable_sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) {
      if (a.disc() != b.disc()) return a.disc() < b.disc();
      return a.track() < b.track();
    });
    return [this, id, songs]() { Q_EMIT TracksLoaded(id, songs, true); };
  }, [this, id]() { Q_EMIT TracksLoaded(id, SongList(), false); });

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
