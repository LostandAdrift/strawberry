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

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QProgressBar>
#include <QShowEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/song.h"
#include "playlistimport.h"
#include "playlistimportdialog.h"

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr int kPlaylistRole = Qt::UserRole + 1;

// Plain singular/plural wording (Qt's %n plural forms only apply when a translation is loaded).
QString Tracks(const int n) {
  return n == 1 ? QObject::tr("1 track") : QObject::tr("%1 tracks").arg(n);
}

QString Playlists(const int n) {
  return n == 1 ? QObject::tr("1 playlist") : QObject::tr("%1 playlists").arg(n);
}

}  // namespace

PlaylistImportDialog::PlaylistImportDialog(QWidget *parent)
    : QDialog(parent),
      combo_service_(new QComboBox),
      list_(new QListWidget),
      label_status_(new QLabel),
      label_info_(new QLabel),
      progress_(new QProgressBar),
      button_load_(new QPushButton(tr("Load playlists"))),
      button_import_(new QPushButton(tr("Import"))),
      button_cancel_(new QPushButton(tr("Cancel loading"))),
      button_close_(new QPushButton(tr("Close"))),
      loading_(false) {

  setWindowTitle(tr("Import streaming playlists"));
  setMinimumSize(460, 420);

  // Remote names and server error text must never be interpreted as rich text.
  label_info_->setTextFormat(Qt::PlainText);
  label_status_->setTextFormat(Qt::PlainText);
  label_info_->setWordWrap(true);
  label_info_->setText(tr("Adrift reads your playlists and creates a local copy. Nothing is changed on the streaming service. Importing again replaces that local copy with the current remote list; it is a refresh, not a two-way sync."));

  label_status_->setWordWrap(true);
  label_status_->setAccessibleName(tr("Import status"));
  label_status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  list_->setAccessibleName(tr("Remote playlists"));
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  progress_->setVisible(false);
  progress_->setTextVisible(true);

  QVBoxLayout *layout = new QVBoxLayout(this);
  QHBoxLayout *top = new QHBoxLayout;
  QLabel *service_label = new QLabel(tr("Service:"));
  service_label->setBuddy(combo_service_);
  top->addWidget(service_label);
  top->addWidget(combo_service_, 1);
  top->addWidget(button_load_);
  layout->addLayout(top);
  layout->addWidget(list_, 1);
  layout->addWidget(progress_);
  layout->addWidget(label_status_);
  layout->addWidget(label_info_);

  QHBoxLayout *buttons = new QHBoxLayout;
  buttons->addWidget(button_import_);
  buttons->addWidget(button_cancel_);
  buttons->addStretch(1);
  buttons->addWidget(button_close_);
  layout->addLayout(buttons);

  QObject::connect(combo_service_, &QComboBox::currentIndexChanged, this, &PlaylistImportDialog::ServiceChanged);
  QObject::connect(button_load_, &QPushButton::clicked, this, &PlaylistImportDialog::LoadPlaylists);
  QObject::connect(button_import_, &QPushButton::clicked, this, &PlaylistImportDialog::ImportSelected);
  QObject::connect(button_cancel_, &QPushButton::clicked, this, &PlaylistImportDialog::CancelLoading);
  QObject::connect(button_close_, &QPushButton::clicked, this, &QDialog::reject);
  QObject::connect(list_, &QListWidget::itemSelectionChanged, this, &PlaylistImportDialog::SelectionChanged);
  QObject::connect(list_, &QListWidget::itemDoubleClicked, this, [this]() { ImportSelected(); });

  UpdateControls();

}

PlaylistImportDialog::~PlaylistImportDialog() {

  // Stop any request that is still running before the services (and the objects they point to) go away.
  for (Service *service : std::as_const(services_)) {
    if (service->importer->busy()) service->importer->Cancel();
  }
  qDeleteAll(services_);
  services_.clear();

}

QString PlaylistImportDialog::Identity(const QString &url_scheme, const QString &playlist_id) {
  return url_scheme + u':' + playlist_id;
}

void PlaylistImportDialog::AddService(const ServiceEntry &entry) {

  if (!entry.page_source) return;

  Service *service = new Service;
  service->entry = entry;
  service->importer = new PlaylistImport::Importer(entry.page_source, this);
  service->account_key = entry.account_key ? entry.account_key() : QString();
  services_ << service;

  PlaylistImport::Importer *importer = service->importer;
  QObject::connect(importer, &PlaylistImport::Importer::StateChanged, this, &PlaylistImportDialog::ImporterStateChanged);
  QObject::connect(importer, &PlaylistImport::Importer::Progress, this, &PlaylistImportDialog::ProgressChanged);
  QObject::connect(importer, &PlaylistImport::Importer::PlaylistsLoaded, this, &PlaylistImportDialog::PlaylistsLoaded);
  QObject::connect(importer, &PlaylistImport::Importer::PlaylistImported, this, &PlaylistImportDialog::PlaylistImported);
  QObject::connect(importer, &PlaylistImport::Importer::Failed, this, &PlaylistImportDialog::Failed);
  QObject::connect(importer, &PlaylistImport::Importer::Cancelled, this, &PlaylistImportDialog::Cancelled);

  combo_service_->addItem(entry.name);
  if (services_.count() == 1) ServiceChanged();

}

void PlaylistImportDialog::SelectService(const QString &name) {

  if (loading_) return;
  const int index = combo_service_->findText(name, Qt::MatchFixedString);
  if (index >= 0 && index != combo_service_->currentIndex()) combo_service_->setCurrentIndex(index);

}

PlaylistImportDialog::Service *PlaylistImportDialog::current_service() {

  const int index = combo_service_->currentIndex();
  return index >= 0 && index < services_.count() ? services_[index] : nullptr;

}

void PlaylistImportDialog::SetStatus(const QString &text, const bool error) {

  label_status_->setText(text);
  // Errors are announced in words ("Error:") and not only by color.
  // Non-errors use the dialog's own palette again, so a previous error color never sticks.
  if (error) {
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::BrightText));
    label_status_->setPalette(pal);
  }
  else {
    label_status_->setPalette(QPalette());
  }

}

void PlaylistImportDialog::UpdateControls() {

  Service *service = current_service();
  const bool have_service = service != nullptr;
  const bool logged_in = have_service && (!service->entry.logged_in || service->entry.logged_in());
  const QListWidgetItem *selected = list_->currentItem();

  combo_service_->setEnabled(!loading_ && services_.count() > 1);
  list_->setEnabled(!loading_);
  button_load_->setEnabled(!loading_ && have_service && logged_in);
  button_import_->setEnabled(!loading_ && have_service && logged_in && selected && selected->isSelected());
  button_cancel_->setEnabled(loading_);
  progress_->setVisible(loading_);

  QString import_text = tr("Import");
  if (have_service && selected) {
    const PlaylistImport::PlaylistInfo info = selected->data(kPlaylistRole).value<PlaylistImport::PlaylistInfo>();
    if (snapshot_exists_ && snapshot_exists_(Identity(service->entry.url_scheme, info.id))) import_text = tr("Refresh snapshot");
  }
  button_import_->setText(import_text);

}

bool PlaylistImportDialog::DiscardIfAccountChanged(Service *service) {

  if (!service) return false;
  const QString key = service->entry.account_key ? service->entry.account_key() : QString();
  if (key == service->account_key) return false;

  service->account_key = key;
  service->loaded = false;
  service->playlists.clear();
  if (service == current_service()) list_->clear();
  return true;

}

void PlaylistImportDialog::showEvent(QShowEvent *e) {

  QDialog::showEvent(e);
  // The dialog is reused. If the user logged out or into another account meanwhile, do not show the old account's playlists.
  if (!loading_ && current_service()) ServiceChanged();

}

void PlaylistImportDialog::ServiceChanged() {

  Service *service = current_service();
  DiscardIfAccountChanged(service);
  list_->clear();
  if (!service) {
    SetStatus(tr("No streaming service is available."));
  }
  else if (service->entry.logged_in && !service->entry.logged_in()) {
    SetStatus(tr("Error: you are not logged in to %1. Log in from the settings first.").arg(service->entry.name), true);
  }
  else if (service->loaded) {
    PlaylistsLoaded(service->playlists);
  }
  else {
    SetStatus(tr("Press “Load playlists” to read your %1 playlists.").arg(service->entry.name));
  }
  UpdateControls();

}

void PlaylistImportDialog::LoadPlaylists() {

  Service *service = current_service();
  if (!service || loading_) return;
  DiscardIfAccountChanged(service);
  list_->clear();
  service->loaded = false;
  service->importer->LoadPlaylists();

}

void PlaylistImportDialog::ImportSelected() {

  Service *service = current_service();
  const QListWidgetItem *item = list_->currentItem();
  if (!service || !item || loading_) return;
  service->importer->ImportPlaylist(item->data(kPlaylistRole).value<PlaylistImport::PlaylistInfo>());

}

void PlaylistImportDialog::CancelLoading() {

  Service *service = current_service();
  if (service) service->importer->Cancel();

}

void PlaylistImportDialog::SelectionChanged() {
  UpdateControls();
}

void PlaylistImportDialog::ImporterStateChanged() {

  loading_ = false;
  for (const Service *service : std::as_const(services_)) {
    if (service->importer->busy()) loading_ = true;
  }

  const Service *service = current_service();
  if (loading_ && service) {
    progress_->setRange(0, 0);  // busy until the first page says how many entries to expect
    SetStatus(service->importer->state() == PlaylistImport::Importer::State::LoadingPlaylists ? tr("Loading playlists…") : tr("Loading tracks…"));
  }
  UpdateControls();

}

void PlaylistImportDialog::ProgressChanged(const int loaded, const int total) {

  if (total > 0) {
    progress_->setRange(0, total);
    progress_->setValue(std::min(loaded, total));
    SetStatus(tr("Loading… %1 of %2").arg(loaded).arg(total));
  }
  else {
    progress_->setRange(0, 0);
    if (loaded > 0) SetStatus(tr("Loading… %1 received").arg(loaded));
  }

}

void PlaylistImportDialog::PlaylistsLoaded(const QList<PlaylistImport::PlaylistInfo> &playlists) {

  Service *service = current_service();
  if (service) {
    service->playlists = playlists;
    service->loaded = true;
  }

  list_->clear();
  for (const PlaylistImport::PlaylistInfo &info : playlists) {
    QListWidgetItem *item = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(info.name, Tracks(info.track_count)), list_);
    item->setData(kPlaylistRole, QVariant::fromValue(info));
  }

  if (playlists.isEmpty()) {
    SetStatus(tr("No playlists found on this account."));
  }
  else {
    SetStatus(tr("%1 found. Select one to import.").arg(Playlists(static_cast<int>(playlists.count()))));
  }
  UpdateControls();

}

void PlaylistImportDialog::PlaylistImported(const PlaylistImport::PlaylistInfo &playlist, const QList<PlaylistImport::Track> &tracks, const int skipped) {

  Service *service = current_service();
  if (!service) return;

  SongList songs;
  songs.reserve(tracks.count());
  for (const PlaylistImport::Track &track : tracks) {
    songs << PlaylistImport::TrackToSong(track, service->entry.song_source, service->entry.url_scheme);
  }

  // Provisional text; the caller refines it through ShowImported() once the local playlist exists.
  SetStatus(tr("Read %1 from “%2”.").arg(Tracks(static_cast<int>(songs.count())), playlist.name));
  Q_EMIT SnapshotReady(service->entry.name, Identity(service->entry.url_scheme, playlist.id), QStringLiteral("%1: %2").arg(service->entry.name, playlist.name), songs, skipped);
  UpdateControls();

}

void PlaylistImportDialog::ShowImported(const QString &playlist_name, const bool replaced, const int track_count, const int skipped) {

  QString text = replaced ? tr("Refreshed “%1” with %2.").arg(playlist_name, Tracks(track_count))
                          : tr("Imported “%1” with %2.").arg(playlist_name, Tracks(track_count));
  if (skipped > 0) text += u' ' + tr("Unavailable and left out: %1.").arg(Tracks(skipped));
  SetStatus(text);
  UpdateControls();

}

void PlaylistImportDialog::Failed(const QString &message) {

  SetStatus(tr("Error: %1 Nothing was imported.").arg(message), true);
  UpdateControls();

}

void PlaylistImportDialog::Cancelled() {

  SetStatus(tr("Cancelled. Nothing was imported."));
  UpdateControls();

}

void PlaylistImportDialog::reject() {

  // Closing the dialog during a request cancels it.
  for (Service *service : std::as_const(services_)) {
    if (service->importer->busy()) service->importer->Cancel();
  }
  QDialog::reject();

}
