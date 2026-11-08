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

#ifndef LIBRARYVIEW_H
#define LIBRARYVIEW_H

#include "config.h"

#include <functional>

#include <QHash>
#include <QList>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include "core/song.h"
#include "librarysource.h"

class QLabel;
class QListView;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;
class LibraryAlbumGrid;
class LibraryAlbumModel;
class LibraryNotice;
class LibraryTrackModel;
class LibraryTrackView;
class RoomButton;
class RoomSearchField;
class RoomSegmented;

// The Library of Adrift: a header with search, a grid of album covers (or a list of artists), and a page for one album with its tracks.
//
// It shows the one collection the application already has (through a LibrarySource); it keeps no store of its own. Everything it does to the playlists
// is a request (PlayRequested, AddRequested) that MainWindow carries out with the existing playlist manager, so this widget has no dependency on the application.
//
// Nothing here blocks the GUI thread on the database: albums, search results and tracks are read on a worker thread, and a result that arrives after something
// newer was asked (another search, another album, the widget being destroyed) is dropped.
class LibraryView : public QWidget {
  Q_OBJECT

 public:
  enum class Mode {
    Albums,
    Artists
  };
  enum class State {
    Loading,    // the first read of the library is under way
    Empty,      // there is no music
    NoMatches,  // there is music, but not for this search
    Browsing,   // albums or artists
    Album       // one album with its tracks
  };
  enum class AddMode {
    Append,
    PlayNext
  };

  explicit LibraryView(QWidget *parent = nullptr);
  ~LibraryView() override;

  // The source is not owned and must outlive the view's use of it (the view waits for its own worker tasks in the destructor).
  void SetSource(LibrarySource *source);
  LibrarySource *source() const { return source_; }

  // Reads the library again (once, whatever the number of calls before the read starts). The first call happens when the view is first shown.
  void Reload();

  State state() const { return state_; }
  Mode mode() const { return mode_; }
  void SetMode(const Mode mode);
  QString search_text() const;
  void SetSearchText(const QString &text);
  QString artist_filter() const { return artist_filter_; }
  void SetArtistFilter(const QString &artist);

  int album_count() const;      // albums shown
  int total_album_count() const { return static_cast<int>(all_.count()); }
  int artist_count() const;
  bool loaded() const { return loaded_; }

  // The host started reading a music folder (or found it was already part of the library). The page says so until albums arrive, instead of looking broken
  // while the scan has not found anything yet.
  void NoteMusicAdded(const QString &folder);
  void NoteFolderAlreadyAdded(const QString &folder);
  QString adding_folder() const { return adding_folder_; }

  void OpenAlbum(const LibraryAlbum &album);
  void CloseAlbum();
  const LibraryAlbum &current_album() const { return current_album_; }
  const SongList &current_tracks() const;

  // Plays the album from its first track, or the open album from a track. Loads the tracks first when needed, off the GUI thread.
  void PlayAlbum(const LibraryAlbum &album);
  void PlayTrackOfOpenAlbum(const int row);
  void AddAlbum(const LibraryAlbum &album, const AddMode mode);

  // Parts, for tests.
  LibraryAlbumGrid *grid() const { return grid_; }
  LibraryAlbumModel *album_model() const { return album_model_; }
  LibraryTrackView *track_view() const { return track_view_; }
  LibraryTrackModel *track_model() const { return track_model_; }
  RoomSearchField *search_field() const { return search_; }
  RoomSegmented *mode_switch() const { return mode_switch_; }
  RoomButton *add_music_button() const { return button_add_music_; }
  RoomButton *advanced_button() const { return button_advanced_; }
  RoomButton *back_button() const { return button_back_; }
  RoomButton *play_button() const { return button_play_; }
  RoomButton *add_button() const { return button_add_; }
  RoomButton *play_next_button() const { return button_play_next_; }
  LibraryNotice *notice() const { return notice_; }
  QListWidget *artist_list() const { return artist_list_; }
  QLabel *summary_label() const { return label_summary_; }
  QLabel *album_title_label() const { return label_album_title_; }
  QWidget *page_for(const State state) const;

  QSize sizeHint() const override { return QSize(1000, 700); }

 Q_SIGNALS:
  void PlayRequested(const SongList &songs, const int start_index);
  void AddRequested(const SongList &songs, const LibraryView::AddMode mode);
  void AddMusicRequested();
  void AdvancedRequested();
  void ShowStreamingRequested();
  // The state changed (for tests and for the page to follow with the focus).
  void StateChanged(const LibraryView::State state);
  // A read finished and the albums on display are final for the current search (for tests).
  void AlbumsSettled();

 protected:
  void showEvent(QShowEvent *e) override;
  void paintEvent(QPaintEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;

 private Q_SLOTS:
  void StartLoad();
  void ApplySearch();
  void OnCoverLoaded(const quint64 id, const QImage &image);

 private:
  void BuildUi();
  void SetState(const State state);
  void UpdateSummary();
  void RefreshShown();
  void RebuildArtists();
  void RunInPool(std::function<void()> work);
  void LoadTracksThen(const LibraryAlbum &album, std::function<void(const SongList&)> then);
  void ShowTracks(const SongList &songs);
  void LayoutAlbumPage();

  QPointer<LibrarySource> source_;
  QThreadPool pool_;
  quint64 load_generation_ = 0;
  quint64 search_generation_ = 0;
  quint64 tracks_generation_ = 0;
  bool loaded_ = false;
  bool load_pending_ = false;
  bool reload_when_idle_ = false;
  bool loading_ = false;
  bool first_load_started_ = false;
  bool destroying_ = false;
  QString adding_folder_;
  QString already_added_folder_;
  QTimer *adding_timer_ = nullptr;

  LibraryAlbumList all_;
  LibraryAlbumList remote_matches_;  // albums found by the source for the current text (track matches)
  QString remote_text_;
  Mode mode_ = Mode::Albums;
  State state_ = State::Loading;
  QString artist_filter_;
  LibraryAlbum current_album_;
  QTimer *search_timer_;
  QTimer *reload_timer_;
  quint64 detail_cover_id_ = 0;

  // Header
  QLabel *label_title_ = nullptr;
  QLabel *label_summary_ = nullptr;
  RoomSearchField *search_ = nullptr;
  RoomSegmented *mode_switch_ = nullptr;
  RoomButton *button_add_music_ = nullptr;
  RoomButton *button_advanced_ = nullptr;
  RoomButton *button_clear_artist_ = nullptr;

  // Body
  QStackedWidget *body_ = nullptr;
  QWidget *page_loading_ = nullptr;
  QWidget *page_notice_ = nullptr;
  QWidget *page_browse_ = nullptr;
  QWidget *page_album_ = nullptr;
  QStackedWidget *browse_stack_ = nullptr;
  LibraryNotice *notice_ = nullptr;
  LibraryNotice *loading_notice_ = nullptr;
  LibraryAlbumGrid *grid_ = nullptr;
  LibraryAlbumModel *album_model_ = nullptr;
  QListWidget *artist_list_ = nullptr;

  // Album page
  RoomButton *button_back_ = nullptr;
  QWidget *album_cover_ = nullptr;
  QLabel *label_album_title_ = nullptr;
  QLabel *label_album_artist_ = nullptr;
  QLabel *label_album_meta_ = nullptr;
  RoomButton *button_play_ = nullptr;
  RoomButton *button_add_ = nullptr;
  RoomButton *button_play_next_ = nullptr;
  LibraryTrackView *track_view_ = nullptr;
  LibraryTrackModel *track_model_ = nullptr;
};

Q_DECLARE_METATYPE(LibraryView::AddMode)
Q_DECLARE_METATYPE(LibraryView::State)

#endif  // LIBRARYVIEW_H
