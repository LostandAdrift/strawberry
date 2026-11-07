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

#ifndef PLAYLISTIMPORTDIALOG_H
#define PLAYLISTIMPORTDIALOG_H

#include "config.h"

#include <functional>

#include <QDialog>
#include <QList>
#include <QString>

#include "core/song.h"
#include "playlistimport.h"

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QShowEvent;
class QPushButton;

// Native dialog to read the user's streaming playlists and import them as local snapshots.
// Read-only towards the service. A refresh replaces the dedicated local snapshot after a complete, successful read.
class PlaylistImportDialog : public QDialog {
  Q_OBJECT

 public:
  struct ServiceEntry {
    QString name;        // "Tidal"
    QString url_scheme;  // "tidal", also the prefix of the stored identity
    Song::Source song_source = Song::Source::Unknown;
    PlaylistImport::PageSource *page_source = nullptr;  // owned by the caller (dialog parent)
    std::function<bool()> logged_in;
    // Identifies the signed-in account (for example the user id). When it changes, the cached playlist list of that service is discarded.
    std::function<QString()> account_key;
  };

  explicit PlaylistImportDialog(QWidget *parent = nullptr);
  ~PlaylistImportDialog() override;

  void AddService(const ServiceEntry &entry);
  // Used to label the button "Import" or "Refresh snapshot".
  void SetSnapshotExistsFunction(std::function<bool(const QString &identity)> function) { snapshot_exists_ = function; }

  // The caller reports the result of creating/replacing the local playlist.
  void ShowImported(const QString &playlist_name, const bool replaced, const int track_count, const int skipped);

  // Accessors for tests.
  QListWidget *playlist_list() const { return list_; }
  QLabel *status_label() const { return label_status_; }
  QPushButton *load_button() const { return button_load_; }
  QPushButton *import_button() const { return button_import_; }
  QPushButton *cancel_button() const { return button_cancel_; }
  QComboBox *service_combo() const { return combo_service_; }
  QProgressBar *progress_bar() const { return progress_; }
  bool loading() const { return loading_; }

  static QString Identity(const QString &url_scheme, const QString &playlist_id);

 Q_SIGNALS:
  // A complete remote playlist was read. The caller creates or replaces the local snapshot (see PlaylistManager::ImportSnapshot).
  void SnapshotReady(const QString &service_name, const QString &identity, const QString &playlist_name, const SongList &songs, const int skipped);

 protected:
  void reject() override;
  void showEvent(QShowEvent *e) override;

 private Q_SLOTS:
  void ServiceChanged();
  void LoadPlaylists();
  void ImportSelected();
  void CancelLoading();
  void SelectionChanged();
  void ImporterStateChanged();
  void ProgressChanged(const int loaded, const int total);
  void PlaylistsLoaded(const QList<PlaylistImport::PlaylistInfo> &playlists);
  void PlaylistImported(const PlaylistImport::PlaylistInfo &playlist, const QList<PlaylistImport::Track> &tracks, const int skipped);
  void Failed(const QString &message);
  void Cancelled();

 private:
  struct Service {
    ServiceEntry entry;
    PlaylistImport::Importer *importer = nullptr;
    QList<PlaylistImport::PlaylistInfo> playlists;
    bool loaded = false;
    QString account_key;
  };

  Service *current_service();
  // Drops the cached playlists of a service if the signed-in account changed since they were loaded. Returns true if it did.
  bool DiscardIfAccountChanged(Service *service);
  void SetStatus(const QString &text, const bool error = false);
  void UpdateControls();

  QComboBox *combo_service_;
  QListWidget *list_;
  QLabel *label_status_;
  QLabel *label_info_;
  QProgressBar *progress_;
  QPushButton *button_load_;
  QPushButton *button_import_;
  QPushButton *button_cancel_;
  QPushButton *button_close_;

  QList<Service*> services_;
  std::function<bool(const QString &)> snapshot_exists_;
  bool loading_;
};

#endif  // PLAYLISTIMPORTDIALOG_H
