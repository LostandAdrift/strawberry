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
#include <functional>

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QShowEvent>
#include <QSet>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "libraryview.h"
#include "libraryparts.h"
#include "roomstyle.h"
#include "roomwidgets.h"

namespace {

constexpr int kRoleArtist = Qt::UserRole + 1;
constexpr int kRoleAlbumCount = Qt::UserRole + 2;

QString Albums(const int n) {
  return n == 1 ? QCoreApplication::translate("LibraryView", "1 album") : QCoreApplication::translate("LibraryView", "%1 albums").arg(n);
}

QString Artists(const int n) {
  return n == 1 ? QCoreApplication::translate("LibraryView", "1 artist") : QCoreApplication::translate("LibraryView", "%1 artists").arg(n);
}

QString Tracks(const int n) {
  return n == 1 ? QCoreApplication::translate("LibraryView", "1 track") : QCoreApplication::translate("LibraryView", "%1 tracks").arg(n);
}

// The cover on the album page.
class AlbumCoverBox : public QWidget {
 public:
  explicit AlbumCoverBox(QWidget *parent = nullptr) : QWidget(parent) {
    setAccessibleName(QCoreApplication::translate("LibraryView", "Album cover"));
  }

  void Set(const QPixmap &pixmap, const QString &seed) {
    pixmap_ = pixmap;
    seed_ = seed;
    update();
  }
  void SetSide(const int side) {
    if (side_ == side) return;
    side_ = side;
    updateGeometry();
  }

  QSize sizeHint() const override { return QSize(side_ + 20, side_ + 20); }
  QSize minimumSizeHint() const override { return sizeHint(); }

 protected:
  void paintEvent(QPaintEvent *e) override {
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF sleeve(10, 6, side_, side_);
    QColor shadow = palette().color(QPalette::Shadow);
    for (int i = 4; i >= 1; --i) {
      shadow.setAlphaF(0.07 * (5 - i) / 4.0);
      p.setPen(Qt::NoPen);
      p.setBrush(shadow);
      p.drawRoundedRect(sleeve.adjusted(-i * 2, -i, i * 2, i * 3), 12 + i, 12 + i);
    }
    LibraryPaint::DrawSleeve(&p, sleeve, palette(), seed_, pixmap_, 12);
  }

 private:
  QPixmap pixmap_;
  QString seed_;
  int side_ = 220;
};

// Artist rows: the name over how many albums.
class ArtistDelegate : public QStyledItemDelegate {
 public:
  explicit ArtistDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    Q_UNUSED(index)
    return QSize(200, std::max(54, QFontMetrics(option.font).height() * 2 + 22));
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = option.palette;
    const QRect row = option.rect.adjusted(0, 3, 0, -3);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    painter->setPen(QPen(RoomStyle::Hairline(pal), 1));
    painter->setBrush(selected ? RoomStyle::Surface(pal, 0.12) : (hovered ? RoomStyle::Surface(pal, 0.08) : RoomStyle::Surface(pal, 0.04)));
    painter->drawRoundedRect(QRectF(row).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    if ((option.state & QStyle::State_HasFocus) && selected) {
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.75), 1.5));
      painter->drawRoundedRect(QRectF(row).adjusted(1.5, 1.5, -1.5, -1.5), 9, 9);
    }
    const QFontMetrics fm(option.font);
    const int left = row.left() + 18;
    const int width = std::max(0, row.width() - 36);
    const int top = row.top() + (row.height() - fm.height() * 2 - 3) / 2;
    QFont bold = option.font;
    bold.setWeight(QFont::DemiBold);
    painter->setFont(bold);
    painter->setPen(RoomStyle::Text(pal));
    painter->drawText(QRect(left, top, width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(bold).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, width));
    painter->setFont(option.font);
    painter->setPen(RoomStyle::Muted(pal));
    painter->drawText(QRect(left, top + fm.height() + 3, width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, Albums(index.data(kRoleAlbumCount).toInt()));
    painter->restore();

  }
};

QString Fold(const QString &text) {
  return text.normalized(QString::NormalizationForm_D).toCaseFolded();
}

QStringList Tokens(const QString &text) {
  return Fold(text.trimmed()).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

}  // namespace

LibraryView::LibraryView(QWidget *parent) : QWidget(parent), search_timer_(new QTimer(this)), reload_timer_(new QTimer(this)) {

  setAccessibleName(tr("Library"));
  search_timer_->setSingleShot(true);
  search_timer_->setInterval(140);
  reload_timer_->setSingleShot(true);
  reload_timer_->setInterval(600);
  QObject::connect(search_timer_, &QTimer::timeout, this, &LibraryView::ApplySearch);
  QObject::connect(reload_timer_, &QTimer::timeout, this, &LibraryView::Reload);
  adding_timer_ = new QTimer(this);
  adding_timer_->setSingleShot(true);
  adding_timer_->setInterval(2 * 60 * 1000);
  QObject::connect(adding_timer_, &QTimer::timeout, this, [this]() {
    adding_folder_.clear();
    UpdateSummary();
    if (state_ == State::Empty) SetState(State::Empty);
  });

  BuildUi();
  SetState(State::Loading);

}

LibraryView::~LibraryView() = default;

void LibraryView::BuildUi() {

  album_model_ = new LibraryAlbumModel(this);
  track_model_ = new LibraryTrackModel(this);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(40, 32, 40, 8);
  layout->setSpacing(10);

  // Header: the same heading as every other page, with the library's own actions at the right.
  QWidget *actions = new QWidget;
  QHBoxLayout *actions_layout = new QHBoxLayout(actions);
  actions_layout->setContentsMargins(0, 0, 0, 0);
  actions_layout->setSpacing(8);
  button_advanced_ = new RoomButton(tr("Advanced tools…"));
  button_advanced_->setToolTip(tr("The classic Strawberry window: tag editing, smart playlists, devices and more."));
  button_advanced_->setAccessibleName(tr("Open the advanced library tools"));
  button_add_music_ = new RoomButton(tr("Add music…"), true);
  button_add_music_->setToolTip(tr("Choose the folders Adrift reads your music from."));
  button_add_music_->setAccessibleName(tr("Add music folders"));
  actions_layout->addWidget(button_advanced_);
  actions_layout->addWidget(button_add_music_);
  label_title_ = AddPageHeader(layout, font(), tr("Library"), QString(), actions);
  label_summary_ = findChild<QLabel*>(QStringLiteral("page_description"));

  // Search and view.
  QHBoxLayout *tools = new QHBoxLayout;
  tools->setSpacing(12);
  search_ = new RoomSearchField;
  search_->setMaximumWidth(380);
  mode_switch_ = new RoomSegmented(QStringList() << tr("Albums") << tr("Artists"));
  mode_switch_->setAccessibleName(tr("Browse by"));
  button_clear_artist_ = new RoomButton(QString());
  button_clear_artist_->setVisible(false);
  tools->addWidget(search_, 1);
  tools->addWidget(mode_switch_);
  tools->addWidget(button_clear_artist_);
  tools->addStretch(2);
  layout->addLayout(tools);
  layout->addSpacing(6);

  // Body.
  body_ = new RoomPageStack;
  layout->addWidget(body_, 1);

  loading_notice_ = new LibraryNotice;
  loading_notice_->SetContent(tr("Reading your library…"), tr("Just a moment."), QString(), QString());
  page_loading_ = loading_notice_;
  body_->addWidget(page_loading_);

  notice_ = new LibraryNotice;
  page_notice_ = notice_;
  body_->addWidget(page_notice_);

  // Browse: albums or artists.
  browse_stack_ = new RoomPageStack;
  grid_ = new LibraryAlbumGrid;
  grid_->setModel(album_model_);
  artist_list_ = new QListWidget;
  artist_list_->setItemDelegate(new ArtistDelegate(artist_list_));
  artist_list_->setFrameShape(QFrame::NoFrame);
  artist_list_->setMouseTracking(true);
  artist_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  artist_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  artist_list_->setAccessibleName(tr("Artists"));
  artist_list_->viewport()->setAutoFillBackground(false);
  artist_list_->setAutoFillBackground(false);
  browse_stack_->addWidget(grid_);
  browse_stack_->addWidget(artist_list_);
  page_browse_ = browse_stack_;
  body_->addWidget(page_browse_);

  // One album.
  page_album_ = new QWidget;
  QVBoxLayout *album_layout = new QVBoxLayout(page_album_);
  album_layout->setContentsMargins(0, 0, 0, 0);
  album_layout->setSpacing(8);
  button_back_ = new RoomButton(tr("‹  Albums"));
  button_back_->setAccessibleName(tr("Back to all albums"));
  QHBoxLayout *back_row = new QHBoxLayout;
  back_row->addWidget(button_back_);
  back_row->addStretch(1);
  album_layout->addLayout(back_row);

  QHBoxLayout *head = new QHBoxLayout;
  head->setSpacing(24);
  album_cover_ = new AlbumCoverBox;
  head->addWidget(album_cover_, 0, Qt::AlignTop);
  QVBoxLayout *info = new QVBoxLayout;
  info->setSpacing(4);
  label_album_title_ = MakeLabel(QString(), true);
  label_album_title_->setObjectName(QStringLiteral("album_title"));
  StyleLabel(label_album_title_, RoomStyle::Scaled(font(), 1.8, true), ElidedLabel::Tone::Normal);
  label_album_artist_ = MakeLabel(QString(), false);
  label_album_artist_->setObjectName(QStringLiteral("album_artist"));
  StyleLabel(label_album_artist_, RoomStyle::Scaled(font(), 1.2), ElidedLabel::Tone::Muted);
  label_album_meta_ = MakeLabel(QString(), false);
  label_album_meta_->setObjectName(QStringLiteral("album_meta"));
  StyleLabel(label_album_meta_, font(), ElidedLabel::Tone::Faint);
  button_play_ = new RoomButton(tr("Play"), true);
  button_play_next_ = new RoomButton(tr("Play next"));
  button_add_ = new RoomButton(tr("Add to playlist"));
  button_play_->setAccessibleName(tr("Play this album"));
  button_play_next_->setAccessibleName(tr("Play this album next"));
  button_add_->setAccessibleName(tr("Add this album to the playlist"));
  QHBoxLayout *album_actions = new QHBoxLayout;
  album_actions->setSpacing(10);
  album_actions->addWidget(button_play_);
  album_actions->addWidget(button_play_next_);
  album_actions->addWidget(button_add_);
  album_actions->addStretch(1);
  info->addStretch(1);
  info->addWidget(label_album_title_);
  info->addWidget(label_album_artist_);
  info->addWidget(label_album_meta_);
  info->addSpacing(10);
  info->addLayout(album_actions);
  info->addStretch(1);
  head->addLayout(info, 1);
  album_layout->addLayout(head);
  album_layout->addSpacing(8);

  track_view_ = new LibraryTrackView;
  track_view_->setModel(track_model_);
  album_layout->addWidget(track_view_, 1);
  body_->addWidget(page_album_);

  // Wiring.
  QObject::connect(search_, &RoomSearchField::TextChanged, this, [this]() { search_timer_->start(); });
  QObject::connect(mode_switch_, &RoomSegmented::CurrentChanged, this, [this](const int index) { SetMode(index == 1 ? Mode::Artists : Mode::Albums); });
  QObject::connect(button_add_music_, &QPushButton::clicked, this, &LibraryView::AddMusicRequested);
  QObject::connect(button_advanced_, &QPushButton::clicked, this, &LibraryView::AdvancedRequested);
  QObject::connect(button_clear_artist_, &QPushButton::clicked, this, [this]() { SetArtistFilter(QString()); });
  QObject::connect(button_back_, &QPushButton::clicked, this, &LibraryView::CloseAlbum);
  QObject::connect(button_play_, &QPushButton::clicked, this, [this]() { PlayTrackOfOpenAlbum(0); });
  QObject::connect(button_play_next_, &QPushButton::clicked, this, [this]() { if (!track_model_->songs().isEmpty()) Q_EMIT AddRequested(track_model_->songs(), AddMode::PlayNext); });
  QObject::connect(button_add_, &QPushButton::clicked, this, [this]() { if (!track_model_->songs().isEmpty()) Q_EMIT AddRequested(track_model_->songs(), AddMode::Append); });
  QObject::connect(grid_, &LibraryAlbumGrid::AlbumActivated, this, [this](const int row) { if (row >= 0 && row < album_model_->rowCount()) OpenAlbum(album_model_->album(row)); });
  QObject::connect(grid_, &LibraryAlbumGrid::PlayClicked, this, [this](const int row) { if (row >= 0 && row < album_model_->rowCount()) PlayAlbum(album_model_->album(row)); });
  QObject::connect(grid_, &LibraryAlbumGrid::AddClicked, this, [this](const int row, const bool next) { if (row >= 0 && row < album_model_->rowCount()) AddAlbum(album_model_->album(row), next ? AddMode::PlayNext : AddMode::Append); });
  QObject::connect(artist_list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { SetArtistFilter(item->data(kRoleArtist).toString()); });
  QObject::connect(artist_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { SetArtistFilter(item->data(kRoleArtist).toString()); });
  QObject::connect(track_view_, &LibraryTrackView::RowActivated, this, &LibraryView::PlayTrackOfOpenAlbum);
  QObject::connect(track_view_, &LibraryTrackView::AddRowRequested, this, [this](const int row, const bool next) {
    const SongList &songs = track_model_->songs();
    if (row >= 0 && row < songs.count()) Q_EMIT AddRequested(SongList() << songs.at(row), next ? AddMode::PlayNext : AddMode::Append);
  });
  QObject::connect(notice_->primary_button(), &QPushButton::clicked, this, [this]() {
    if (state_ == State::Empty) Q_EMIT AddMusicRequested();
    else SetSearchText(QString());
  });
  QObject::connect(notice_->secondary_button(), &QPushButton::clicked, this, &LibraryView::ShowStreamingRequested);

  setFocusProxy(search_);

}

void LibraryView::SetSource(LibrarySource *source) {

  if (source_) {
    QObject::disconnect(source_, nullptr, this, nullptr);
  }
  source_ = source;
  album_model_->SetSource(source);
  if (source_) {
    QObject::connect(source_, &LibrarySource::Changed, this, [this]() { if (first_load_started_) reload_timer_->start(); });
    QObject::connect(source_, &LibrarySource::CoverLoaded, this, &LibraryView::OnCoverLoaded);
    QObject::connect(source_, &LibrarySource::AlbumsLoaded, this, &LibraryView::OnAlbumsLoaded);
    QObject::connect(source_, &LibrarySource::TracksLoaded, this, &LibraryView::OnTracksLoaded);
    QObject::connect(source_, &QObject::destroyed, this, &LibraryView::OnSourceDestroyed);
  }
  // Answers to the old source's requests are not wanted.
  load_request_ = 0;
  search_request_ = 0;
  tracks_requests_.clear();
  loading_ = false;
  loaded_ = false;
  first_load_started_ = false;
  all_.clear();
  album_model_->SetAlbums(LibraryAlbumList());
  SetState(State::Loading);
  if (isVisible() && source_) Reload();

}

void LibraryView::showEvent(QShowEvent *e) {

  QWidget::showEvent(e);
  // The library is read when it is first shown, not when the application starts.
  if (source_ && !first_load_started_) QMetaObject::invokeMethod(this, [this]() { if (!first_load_started_) Reload(); }, Qt::QueuedConnection);

}

void LibraryView::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.fillRect(rect(), RoomStyle::Window(palette()));

}

void LibraryView::keyPressEvent(QKeyEvent *e) {

  if (e->key() == Qt::Key_Escape && state_ == State::Album) {
    CloseAlbum();
    return;
  }
  if (e->key() == Qt::Key_Escape && !search_->text().isEmpty()) {
    SetSearchText(QString());
    return;
  }
  if ((e->key() == Qt::Key_Slash || (e->key() == Qt::Key_F && (e->modifiers() & Qt::ControlModifier))) && !search_->edit()->hasFocus()) {
    search_->edit()->setFocus();
    search_->edit()->selectAll();
    return;
  }
  QWidget::keyPressEvent(e);

}

QWidget *LibraryView::page_for(const State state) const {

  switch (state) {
    case State::Loading:   return page_loading_;
    case State::Empty:
    case State::NoMatches: return page_notice_;
    case State::Browsing:  return page_browse_;
    case State::Album:     return page_album_;
  }
  return nullptr;

}

void LibraryView::SetState(const State state) {

  const bool changed = state != state_;
  state_ = state;
  body_->setCurrentWidget(page_for(state));
  loading_notice_->SetSpinning(state == State::Loading);

  const bool browsing = state == State::Browsing || state == State::NoMatches;
  search_->setVisible(state != State::Album);
  mode_switch_->setVisible(state == State::Browsing || state == State::NoMatches);
  Q_UNUSED(browsing)
  button_add_music_->setVisible(state != State::Album);
  if (state == State::Empty && !adding_folder_.isEmpty()) {
    notice_->SetContent(tr("Reading your music…"), tr("Adrift is looking through “%1”. Albums appear here as they are found; nothing in the folder is changed.").arg(adding_folder_), tr("Add another folder…"), tr("Connect a streaming service"));
    notice_->SetSpinning(true);
  }
  else if (state == State::Empty) {
    notice_->SetContent(tr("Your library is empty"), already_added_folder_.isEmpty() ? tr("Add the folders where your music lives and Adrift will gather it here. Nothing is moved or changed.") : tr("“%1” is already part of your library, and it has no music Adrift can read yet.").arg(already_added_folder_), tr("Add music…"), tr("Connect a streaming service"));
    notice_->SetSpinning(false);
  }
  else if (state == State::NoMatches) {
    notice_->SetContent(tr("Nothing matches “%1”").arg(search_->text().trimmed()), tr("Try fewer words, or search by artist, album or track title."), tr("Clear search"), QString());
  }
  UpdateSummary();
  if (changed) Q_EMIT StateChanged(state);

}

void LibraryView::UpdateSummary() {

  if (!label_summary_) return;
  QString text;
  switch (state_) {
    case State::Loading:   text = tr("Reading your library…"); break;
    case State::Empty:     text = adding_folder_.isEmpty() ? tr("No music yet.") : tr("Reading “%1”…").arg(adding_folder_); break;
    case State::NoMatches: text = tr("No matches."); break;
    case State::Browsing:
      if (!artist_filter_.isEmpty()) text = tr("%1 by %2").arg(Albums(album_model_->rowCount()), artist_filter_);
      else if (!search_->text().trimmed().isEmpty()) text = tr("%1 match “%2”").arg(Albums(album_model_->rowCount()), search_->text().trimmed());
      else text = QStringLiteral("%1 · %2").arg(Albums(static_cast<int>(all_.count())), Artists(artist_count()));
      if (!adding_folder_.isEmpty()) text += tr(" · reading “%1”").arg(adding_folder_);
      break;
    case State::Album:
      text = tr("Album");
      break;
  }
  label_summary_->setText(text);
  button_clear_artist_->setVisible(!artist_filter_.isEmpty() && state_ != State::Album);
  button_clear_artist_->setText(tr("✕  %1").arg(artist_filter_));
  button_clear_artist_->setAccessibleName(tr("Show all artists again, now showing %1").arg(artist_filter_));
  button_clear_artist_->updateGeometry();

}

int LibraryView::album_count() const {
  return album_model_->rowCount();
}

int LibraryView::artist_count() const {

  QSet<QString> artists;
  for (const LibraryAlbum &album : all_) artists.insert(album.ArtistLabel());
  return static_cast<int>(artists.count());

}

const SongList &LibraryView::current_tracks() const {
  return track_model_->songs();
}

// ------------------------------------------------------------------------------------------------------------ loading

void LibraryView::Reload() {

  if (!source_) return;
  if (loading_) {
    reload_when_idle_ = true;
    return;
  }
  StartLoad();

}

void LibraryView::StartLoad() {

  first_load_started_ = true;
  loading_ = true;
  reload_when_idle_ = false;
  if (!loaded_) SetState(State::Loading);

  load_request_ = ++next_request_;
  source_->RequestAlbums(load_request_, QString());

}

void LibraryView::OnAlbumsLoaded(const quint64 id, const LibraryAlbumList &list, const bool ok) {

  if (id == load_request_ && id != 0) {
    load_request_ = 0;
    loading_ = false;
    loaded_ = true;
    // A failed read keeps what is shown; the next change of the collection reads again.
    if (ok) {
      // Music arrived: the "reading this folder" note has done its job once the library has more than it had.
      if (!adding_folder_.isEmpty() && list.count() > all_.count()) adding_folder_.clear();
      all_ = list;
      remote_matches_.clear();
      remote_text_.clear();
    }
    RefreshShown();
    if (!search_->text().trimmed().isEmpty()) ApplySearch();
    if (reload_when_idle_) Reload();
    Q_EMIT AlbumsSettled();
  } else if (id == search_request_ && id != 0) {
    search_request_ = 0;
    if (!ok || search_->text().trimmed() != search_request_text_) return;
    remote_matches_ = list;
    remote_text_ = search_request_text_;
    RefreshShown();
    Q_EMIT AlbumsSettled();
  }

}

void LibraryView::OnTracksLoaded(const quint64 id, const SongList &songs, const bool ok) {

  const auto it = tracks_requests_.find(id);
  if (it == tracks_requests_.end()) return;
  const TracksRequest request = it->second;
  tracks_requests_.erase(it);
  if (!ok) return;
  // The album page shows only the album that was asked for last; actions on a tile do not depend on it.
  if (request.page && request.generation != tracks_generation_) return;
  request.then(songs);

}

void LibraryView::OnSourceDestroyed() {

  // Nothing will answer any more: do not wait for it.
  source_.clear();
  load_request_ = 0;
  search_request_ = 0;
  tracks_requests_.clear();
  loading_ = false;

}

// ------------------------------------------------------------------------------------------------------------ search and filter

QString LibraryView::search_text() const {
  return search_->text();
}

void LibraryView::SetSearchText(const QString &text) {

  search_->SetText(text);
  search_timer_->stop();
  ApplySearch();

}

void LibraryView::ApplySearch() {

  search_timer_->stop();
  const QString text = search_->text().trimmed();
  if (state_ == State::Album) CloseAlbum();

  // What is already loaded is filtered at once; tracks that match are found by the source and added when they arrive.
  if (text != remote_text_) {
    remote_matches_.clear();
    remote_text_.clear();
  }
  RefreshShown();
  if (text.length() < 2 || !source_ || !loaded_) {
    Q_EMIT AlbumsSettled();
    return;
  }

  search_request_ = ++next_request_;
  search_request_text_ = text;
  source_->RequestAlbums(search_request_, text);

}

void LibraryView::SetArtistFilter(const QString &artist) {

  if (artist == artist_filter_) return;
  artist_filter_ = artist;
  if (!artist.isEmpty() && mode_ == Mode::Artists) {
    mode_ = Mode::Albums;
    mode_switch_->SetCurrent(0);
  }
  if (state_ == State::Album) CloseAlbum();
  RefreshShown();

}

void LibraryView::SetMode(const Mode mode) {

  if (mode == mode_) return;
  mode_ = mode;
  mode_switch_->SetCurrent(mode == Mode::Artists ? 1 : 0);
  if (mode == Mode::Artists) artist_filter_.clear();
  RefreshShown();

}

void LibraryView::RefreshShown() {

  const QString text = search_->text().trimmed();
  const QStringList tokens = Tokens(text);
  QSet<QString> remote;
  if (!text.isEmpty() && remote_text_ == text) {
    for (const LibraryAlbum &album : std::as_const(remote_matches_)) remote.insert(album.Key());
  }

  LibraryAlbumList matching;
  matching.reserve(all_.count());
  for (const LibraryAlbum &album : std::as_const(all_)) {
    if (!tokens.isEmpty()) {
      const QString haystack = Fold(album.ArtistLabel() + QLatin1Char(' ') + album.album);
      bool all_tokens = true;
      for (const QString &token : tokens) {
        if (!haystack.contains(token)) {
          all_tokens = false;
          break;
        }
      }
      if (!all_tokens && !remote.contains(album.Key())) continue;
    }
    matching << album;
  }

  LibraryAlbumList shown;
  if (artist_filter_.isEmpty()) {
    shown = matching;
  }
  else {
    for (const LibraryAlbum &album : std::as_const(matching)) {
      if (album.ArtistLabel() == artist_filter_) shown << album;
    }
  }

  // The artists come from what matches the text, whatever artist is chosen.
  QHash<QString, int> counts;
  QStringList order;
  for (const LibraryAlbum &album : std::as_const(matching)) {
    const QString artist = album.ArtistLabel();
    if (!counts.contains(artist)) order << artist;
    ++counts[artist];
  }
  bool same_artists = artist_list_->count() == order.count();
  for (int i = 0; same_artists && i < order.count(); ++i) {
    same_artists = artist_list_->item(i)->data(kRoleArtist).toString() == order.at(i) && artist_list_->item(i)->data(kRoleAlbumCount).toInt() == counts.value(order.at(i));
  }
  if (!same_artists) {
    artist_list_->clear();
    for (const QString &artist : order) {
      QListWidgetItem *item = new QListWidgetItem(artist, artist_list_);
      item->setData(kRoleArtist, artist);
      item->setData(kRoleAlbumCount, counts.value(artist));
      item->setToolTip(artist);
    }
  }

  album_model_->SetAlbums(shown);

  if (state_ == State::Album) {
    UpdateSummary();
    return;
  }
  browse_stack_->setCurrentWidget(mode_ == Mode::Artists && artist_filter_.isEmpty() ? static_cast<QWidget*>(artist_list_) : static_cast<QWidget*>(grid_));
  if (!loaded_) SetState(State::Loading);
  else if (all_.isEmpty()) SetState(State::Empty);
  else if (matching.isEmpty()) SetState(State::NoMatches);
  else if (shown.isEmpty()) {
    // The chosen artist no longer has albums (the library changed): back to everyone.
    artist_filter_.clear();
    RefreshShown();
    return;
  }
  else SetState(State::Browsing);

}

// ------------------------------------------------------------------------------------------------------------ one album

void LibraryView::NoteMusicAdded(const QString &folder) {

  adding_folder_ = folder;
  already_added_folder_.clear();
  adding_timer_->start();
  // The scan reports songs as it finds them; each report reloads once (coalesced).
  UpdateSummary();
  if (state_ == State::Empty) SetState(State::Empty);

}

void LibraryView::NoteFolderAlreadyAdded(const QString &folder) {

  already_added_folder_ = folder;
  if (state_ == State::Empty) SetState(State::Empty);

}

void LibraryView::OpenAlbum(const LibraryAlbum &album) {

  current_album_ = album;
  label_album_title_->setText(album.album.isEmpty() ? tr("Unknown album") : album.album);
  label_album_artist_->setText(album.ArtistLabel());
  label_album_meta_->setText(tr("Reading tracks…"));
  track_model_->set_album_artist(album.artist);
  track_model_->SetSongs(SongList());
  button_play_->setEnabled(false);
  button_play_next_->setEnabled(false);
  button_add_->setEnabled(false);
  static_cast<AlbumCoverBox*>(album_cover_)->Set(QPixmap(), album.Key());
  static_cast<AlbumCoverBox*>(album_cover_)->SetSide(width() < 760 ? 150 : 220);
  SetState(State::Album);

  if (source_) {
    detail_cover_id_ = LibraryAlbumModel::NextCoverId();
    source_->RequestCover(detail_cover_id_, album, 512);
  }
  LoadTracksThen(album, [this](const SongList &songs) { ShowTracks(songs); });
  button_back_->setFocus();

}

void LibraryView::CloseAlbum() {

  if (state_ != State::Album) return;
  ++tracks_generation_;
  detail_cover_id_ = 0;
  state_ = State::Browsing;  // replaced right away by the real state
  RefreshShown();
  // Back where the user was: the album they came from is the current tile.
  const int row = album_model_->row_of(current_album_.Key());
  if (row >= 0) {
    grid_->setCurrentIndex(album_model_->index(row));
    grid_->scrollTo(album_model_->index(row));
  }

}

void LibraryView::ShowTracks(const SongList &songs) {

  track_model_->set_album_artist(current_album_.artist);
  track_model_->SetSongs(songs);
  qint64 nanosec = 0;
  for (const Song &song : songs) nanosec += std::max<qint64>(0, song.length_nanosec());
  const int minutes = static_cast<int>(nanosec / 60000000000LL);
  label_album_meta_->setText(songs.isEmpty() ? tr("No tracks found") : tr("%1 · %2 min").arg(Tracks(static_cast<int>(songs.count())), QString::number(std::max(1, minutes))));
  const bool any = !songs.isEmpty();
  button_play_->setEnabled(any);
  button_play_next_->setEnabled(any);
  button_add_->setEnabled(any);
  if (any) track_view_->setCurrentIndex(track_model_->index(0));

}

void LibraryView::OnCoverLoaded(const quint64 id, const QImage &image) {

  if (id == 0 || id != detail_cover_id_ || state_ != State::Album) return;
  static_cast<AlbumCoverBox*>(album_cover_)->Set(image.isNull() ? QPixmap() : QPixmap::fromImage(image), current_album_.Key());

}

void LibraryView::LoadTracksThen(const LibraryAlbum &album, std::function<void(const SongList&)> then) {

  if (!source_) return;
  const bool page = state_ == State::Album && album.Key() == current_album_.Key();
  const quint64 generation = page ? ++tracks_generation_ : 0;
  const quint64 id = ++next_request_;
  tracks_requests_[id] = TracksRequest{page, generation, std::move(then)};
  source_->RequestTracks(id, album);

}

void LibraryView::PlayAlbum(const LibraryAlbum &album) {

  if (state_ == State::Album && album.Key() == current_album_.Key() && !track_model_->songs().isEmpty()) {
    Q_EMIT PlayRequested(track_model_->songs(), 0);
    return;
  }
  LoadTracksThen(album, [this](const SongList &songs) { if (!songs.isEmpty()) Q_EMIT PlayRequested(songs, 0); });

}

void LibraryView::PlayTrackOfOpenAlbum(const int row) {

  const SongList &songs = track_model_->songs();
  if (row < 0 || row >= songs.count()) return;
  Q_EMIT PlayRequested(songs, row);

}

void LibraryView::AddAlbum(const LibraryAlbum &album, const AddMode mode) {

  LoadTracksThen(album, [this, mode](const SongList &songs) { if (!songs.isEmpty()) Q_EMIT AddRequested(songs, mode); });

}
