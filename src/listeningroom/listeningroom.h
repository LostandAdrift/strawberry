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

#ifndef LISTENINGROOM_H
#define LISTENINGROOM_H

#include "config.h"

#include <QAbstractButton>
#include <QImage>
#include <QList>
#include <QMetaType>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>

#include "engine/signalpath.h"
#include "transportbar.h"

class QBoxLayout;
class QLabel;
class QStackedLayout;
class QListWidget;
class QEnterEvent;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QVBoxLayout;
class CoverArtWidget;
class ElidedLabel;
class LibraryView;
class RoomTween;
class Playlist;
class RoomQueueModel;
class RoomQueueView;
class SignalPathWidget;

// What is playing, as the Listening Room shows it. Built from the current song by the caller.
struct RoomTrack {
  QString title;
  QString artist;
  QString album;
  QString detail;  // e.g. "2019 · Track 3" or the streaming service
  bool valid() const { return !title.isEmpty() || !artist.isEmpty() || !album.isEmpty(); }
};

struct RoomPlaylistCard {
  int id = -1;
  QString name;
  int track_count = 0;
  bool is_current = false;  // the playlist shown in the queue
  bool is_playing = false;  // the playlist playback is coming from
  QString kind;             // e.g. "Tidal snapshot", empty for ordinary playlists
};

struct RoomServiceCard {
  QString key;     // "tidal", "qobuz"
  QString name;    // "Tidal"
  QString status;  // the real state, e.g. "Signed in" or "Not signed in"
  bool signed_in = false;
};

// A navigation button of the side rail.
class RoomNavButton : public QAbstractButton {
  Q_OBJECT

 public:
  enum class Glyph {
    Listening,
    Library,
    Playlists,
    Streaming,
    Settings,
    Motion
  };

  RoomNavButton(const QString &text, const Glyph glyph, QWidget *parent = nullptr);

  void SetCompact(const bool compact);
  bool compact() const { return compact_; }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

 protected:
  void paintEvent(QPaintEvent *e) override;
  void enterEvent(QEnterEvent *e) override;
  void leaveEvent(QEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;

 private:
  Glyph glyph_;
  bool compact_;
  RoomTween *hover_;
  RoomTween *press_;
};

// The main surface of Adrift. A side rail with the Adrift wordmark and the four destinations (Listening, Library, Playlists, Streaming),
// the page of the selected destination, and a persistent transport bar.
//
// Every destination is part of the same app: the Library is a native page (LibraryView over the one collection), not the classic window.
// The classic Strawberry window (collection tabs, tag editing, smart playlists, devices, ...) is kept for the cases that need it, as a separate
// "advanced" page that MainWindow moves into classic_container(). It is only reachable through the explicit Advanced action of the Library; no default
// route, account flow or import flow leads into it. Streaming catalogs are shown on the Streaming page itself (ShowCatalog), inside this window.
//
// The widget has no dependency on the application: it displays what it is given and emits requests. MainWindow connects those to the player,
// the playlist manager and the existing actions. The queue shows a real Playlist through RoomQueueModel.
class ListeningRoom : public QWidget {
  Q_OBJECT

 public:
  enum class View {
    Listening,
    Library,
    Playlists,
    Streaming,
    Classic  // the classic Strawberry window, only through the Library's advanced action; the rail keeps "Library" lit
  };

  explicit ListeningRoom(QWidget *parent = nullptr);

  // Which playlist the queue shows. This is the rule MainWindow applies every time playlists or playback change.
  struct QueueChoice {
    int shown = -1;     // the playlist shown in the queue, or -1
    int playing = -1;   // the playlist playback comes from, or -1
    int browsed = -1;   // the browsed selection to keep, or -1 when it ended
    bool browsing() const { return shown >= 0 && playing >= 0 && shown != playing; }
  };
  // - A playlist the user chose to look at (`browsed_id`) is shown while a different one plays. Playback is not touched.
  // - Otherwise the playlist that plays is shown while something plays or is paused.
  // - Otherwise the playlist the user selected (`current_id`).
  // Ids that are not in `open_ids` (closed or deleted playlists) are ignored.
  static QueueChoice ChooseQueue(const QList<int> &open_ids, const bool playback_active, const int active_id, const int current_id, const int browsed_id);

  // Navigation
  void SetView(const View view);
  View view() const { return view_; }
  static QString ViewToString(const View view);
  static View ViewFromString(const QString &value, const View fallback = View::Listening);
  RoomNavButton *nav_button(const View view) const;
  RoomNavButton *settings_button() const { return button_settings_; }
  LibraryView *library() const { return library_; }
  // Where MainWindow puts the classic Strawberry window (the advanced page).
  QWidget *classic_container() const { return classic_container_; }
  RoomNavButton *motion_button() const { return button_motion_; }
  // The gliding navigation indicator of the rail (rail coordinates): where it is drawn now, where it is going, and whether it is on its way.
  QRectF nav_indicator() const;
  QRectF nav_indicator_target() const;
  bool nav_indicator_gliding() const;
  // Whether the wordmark's wave is drifting (its timer runs only while playing, visible and with motion allowed).
  bool wordmark_animating() const;
  QWidget *wordmark() const { return wordmark_; }
  QPushButton *queue_back_button() const { return button_queue_back_; }
  bool compact() const { return compact_; }

  // Now playing
  void SetNowPlaying(const RoomTrack &track);
  void SetCover(const QImage &image);
  void SetPlaybackState(const TransportBar::State state);
  void SetPosition(const int position_ms, const int length_ms);
  void SetCanSeek(const bool can_seek);
  void SetVolume(const int volume);
  void SetVolumeEnabled(const bool enabled);
  void SetMuted(const bool muted);

  // Queue (a view of a real playlist)
  void SetQueuePlaylist(Playlist *playlist, const QString &name);
  // Called while the queue shows a different playlist than the one playing. `playing_name` names the playlist playback comes from;
  // an empty name means the queue is the playing one.
  void SetQueueBrowsing(const QString &playing_name);
  void SetTransportActionStates(const bool previous, const bool play_pause, const bool next, const bool stop);

  // Playlists and streaming pages
  void SetPlaylists(const QList<RoomPlaylistCard> &playlists);
  void SetStreamingServices(const QList<RoomServiceCard> &services);

  // A service catalog (for example the favorites of Tidal), shown on the Streaming page inside this window with a way back to the cards.
  // The room does not own the widget: it takes it as a child while shown and gives it up (hides it) when the user goes back.
  void ShowCatalog(QWidget *widget, const QString &title);
  void CloseCatalog();
  QWidget *catalog_widget() const { return catalog_widget_; }
  bool catalog_visible() const;
  QPushButton *catalog_back_button() const { return button_catalog_back_; }
  // The panel the account and import flow lives in: shown on the Streaming page below the service cards, in this window. The room does not own it.
  void SetStreamingPanel(QWidget *panel);
  QWidget *streaming_panel() const { return streaming_panel_; }

  // Motion. The preference itself is global (RoomMotion); the room only offers the control and tells the application when the user changes it.
  void SetReducedMotion(const bool reduced);
  bool reduced_motion() const;

  // Parts, for the signal path feed and for tests
  SignalPathWidget *signal_path_widget() const { return signal_path_; }
  TransportBar *transport() const { return transport_; }
  RoomQueueView *queue_view() const { return queue_view_; }
  RoomQueueModel *queue_model() const { return queue_model_; }
  CoverArtWidget *cover() const { return cover_; }
  QListWidget *playlists_list() const { return playlists_list_; }
  ElidedLabel *title_label() const { return label_title_; }
  ElidedLabel *artist_label() const { return label_artist_; }
  ElidedLabel *album_label() const { return label_album_; }
  QLabel *status_label() const { return label_status_; }
  QPushButton *empty_library_button() const { return button_empty_library_; }
  QPushButton *empty_import_button() const { return button_empty_import_; }
  QPushButton *playlists_import_button() const { return button_playlists_import_; }
  QWidget *page(const View view) const;
  QWidget *streaming_cards() const { return streaming_cards_; }
  const RoomTrack &track() const { return track_; }

  // The responsive layout (compact rail, queue beside or below the cover) is decided from the real size just before the room is shown.
  // Deciding it only on resize events would leave the first minimum size of the window computed from the default size.
  void setVisible(const bool visible) override;

  QSize sizeHint() const override { return QSize(1440, 900); }

 Q_SIGNALS:
  void ViewChanged(const ListeningRoom::View view);
  void QueueRowActivated(const int row);
  void PlaylistActivated(const int playlist_id);
  void ImportPlaylistsRequested(const QString &service_key);
  void BrowseServiceRequested(const QString &service_key);
  void ServiceSettingsRequested(const QString &service_key);
  void SettingsRequested();
  void SignalPathDetailsRequested();
  void ReducedMotionChanged(const bool reduced);
  void CatalogClosed();
  // The user asked to go back from a browsed playlist to the queue of what is playing.
  void ShowPlayingQueueRequested();

 protected:
  void resizeEvent(QResizeEvent *e) override;
  void paintEvent(QPaintEvent *e) override;

 private:
  void BuildRail();
  void BuildListeningPage();
  void BuildPlaylistsPage();
  void BuildStreamingPage();
  void ApplyResponsiveLayout();
  void RetargetRailIndicator();
  void UpdateNowPlayingTexts();
  void UpdateQueueHeader();
  void RebuildStreamingCards();

  View view_;
  bool compact_;
  RoomTrack track_;
  TransportBar::State state_;
  QList<RoomServiceCard> services_;

  // Rail
  QWidget *rail_;
  RoomNavButton *button_listening_;
  RoomNavButton *button_library_;
  RoomNavButton *button_playlists_;
  RoomNavButton *button_streaming_;
  RoomNavButton *button_settings_;
  RoomNavButton *button_motion_;
  QWidget *wordmark_;
  bool layout_compact_ = false;
  bool layout_stacked_ = false;
  bool layout_applied_ = false;

  // Pages
  QStackedWidget *stack_;
  QWidget *page_listening_;
  LibraryView *library_;
  QWidget *classic_container_;
  QWidget *page_playlists_;
  QWidget *page_streaming_;
  TransportBar *transport_;

  // Listening page
  QBoxLayout *listening_layout_;
  CoverArtWidget *cover_;
  QLabel *label_status_;
  ElidedLabel *label_title_;
  ElidedLabel *label_artist_;
  ElidedLabel *label_album_;
  ElidedLabel *label_detail_;
  SignalPathWidget *signal_path_;
  QWidget *widget_empty_actions_;
  QPushButton *button_empty_library_;
  QPushButton *button_empty_import_;
  QLabel *label_empty_hint_;
  QWidget *widget_meta_;
  QLabel *label_queue_heading_;
  QLabel *label_queue_sub_;
  QLabel *label_queue_note_;
  QPushButton *button_queue_back_ = nullptr;
  QString browsing_note_;
  RoomQueueModel *queue_model_;
  RoomQueueView *queue_view_;

  // Playlists page
  QListWidget *playlists_list_;
  QPushButton *button_playlists_import_;
  QLabel *label_playlists_empty_;

  // Streaming page
  QWidget *streaming_cards_;
  QVBoxLayout *streaming_layout_;
  QStackedLayout *streaming_stack_ = nullptr;
  QWidget *streaming_main_ = nullptr;
  QWidget *streaming_catalog_page_ = nullptr;
  QVBoxLayout *catalog_layout_ = nullptr;
  QPushButton *button_catalog_back_ = nullptr;
  QLabel *label_catalog_title_ = nullptr;
  QWidget *catalog_widget_ = nullptr;
  QVBoxLayout *panel_layout_ = nullptr;
  QWidget *streaming_panel_ = nullptr;
};

Q_DECLARE_METATYPE(ListeningRoom::View)

#endif  // LISTENINGROOM_H
