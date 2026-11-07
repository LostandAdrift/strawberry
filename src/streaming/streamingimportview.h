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

#ifndef STREAMINGIMPORTVIEW_H
#define STREAMINGIMPORTVIEW_H

#include "config.h"

#include <functional>

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QWidget>

#include "core/song.h"
#include "playlistimport.h"
#include "streamingaccount.h"

class QButtonGroup;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;
class QToolButton;
class QBoxLayout;

// Inline account and playlist import page for Tidal and Qobuz. A plain QWidget that the main window hosts as a page, not a dialog and not a window.
//
// - An account header per service: signed in, signed out, waiting for the browser, or setup needed, with Sign in / Reconnect / Cancel / Sign out.
// - The remote playlists as cards with search, import and refresh. Progress and Cancel while reading.
// - Failures stay visible in a banner until they are resolved or dismissed, with the actions that fit the cause (Reconnect, Retry) and collapsible technical details.
// - A failed refresh never throws away the last list that was loaded.
// - Everything is read-only towards the service. Importing creates or replaces a local snapshot (see PlaylistManager::ImportSnapshot).
class StreamingImportView : public QWidget {
  Q_OBJECT

 public:
  struct ServiceEntry {
    QString name;        // "Tidal"
    QString url_scheme;  // "tidal", also the prefix of the stored snapshot identity
    Song::Source song_source = Song::Source::Unknown;
    PlaylistImport::PageSource *page_source = nullptr;  // owned by the view when it has no parent, the caller must keep it alive as long as the view
    AccountBackend *account = nullptr;                  // same
  };

  explicit StreamingImportView(QWidget *parent = nullptr);
  ~StreamingImportView() override;

  void AddService(const ServiceEntry &entry);
  // Shows a service by its key ("tidal") or its name. Ignored while a read is running.
  void SelectService(const QString &key_or_name);
  QString current_service_key() const;

  // Used to label the button "Import" or "Refresh snapshot" and to mark already imported playlists.
  void SetSnapshotExistsFunction(std::function<bool(const QString &identity)> function) { snapshot_exists_ = function; }

  // Call when the page becomes visible. Brings the account state up to date and reads the playlists once if the service is signed in and nothing was loaded yet.
  void Activate();

  // The caller reports the result of creating/replacing the local playlist.
  void ShowImported(const QString &playlist_name, const bool replaced, const int track_count, const int skipped);
  void ShowImportFailed(const QString &playlist_name, const QString &reason);

  static QString Identity(const QString &url_scheme, const QString &playlist_id);

  // Parts for tests and for the host's focus handling.
  AccountSession *session(const QString &key) const;
  PlaylistImport::Importer *importer(const QString &key) const;
  QStackedWidget *pages() const { return pages_; }
  enum Page { PageSetup, PageSignedOut, PageWaiting, PageLoading, PageList, PageEmpty, PageUnloaded };
  Page current_page() const;
  QListWidget *playlist_list() const { return list_; }
  QLineEdit *search_edit() const { return search_; }
  QPushButton *account_button() const { return button_account_; }
  QPushButton *signout_button() const { return button_signout_; }
  QPushButton *import_button() const { return button_import_; }
  QPushButton *cancel_button() const { return button_cancel_; }
  QPushButton *reload_button() const { return button_reload_; }
  QLabel *status_label() const { return label_status_; }
  QLabel *account_status_label() const { return label_account_status_; }
  QFrame *banner() const { return banner_; }
  QLabel *banner_title() const { return banner_title_; }
  QLabel *banner_message() const { return banner_message_; }
  QPushButton *banner_primary() const { return banner_primary_; }
  QPushButton *banner_retry() const { return banner_retry_; }
  QPushButton *banner_dismiss() const { return banner_dismiss_; }
  QToolButton *banner_details_toggle() const { return banner_details_toggle_; }
  QPlainTextEdit *banner_details() const { return banner_details_; }
  QProgressBar *progress_bar() const { return progress_; }
  QPushButton *save_setup_button() const { return button_save_setup_; }
  QLineEdit *setup_field(const QString &key) const { return setup_edits_.value(key); }
  QPushButton *service_button(const QString &key) const;
  QLineEdit *link_edit() const { return link_edit_; }
  bool compact() const { return compact_; }
  bool busy() const;

 Q_SIGNALS:
  // A complete remote playlist was read. The caller creates or replaces the local snapshot (see PlaylistManager::ImportSnapshot)
  // and answers with ShowImported() or ShowImportFailed().
  void SnapshotReady(const QString &service_name, const QString &identity, const QString &playlist_name, const SongList &songs, const int skipped);
  // The user wants to browse the catalog of the service (the host opens its Library tab).
  void BrowseServiceRequested(const QString &service_key);
  // The user picked another service in the header.
  void ServiceSelected(const QString &service_key);
  // The user wants to look at an imported snapshot (the host opens that playlist).
  void OpenSnapshotRequested(const QString &identity);
  // The advanced account settings were requested from the details area. Never sent for ordinary errors.
  void ServiceSettingsRequested(const QString &service_key);
  // A sign-in ended (browser return, failure, timeout or cancel). The host may bring this page forward, it does not need to do anything else.
  void SignInFinished(const QString &service_key, const bool success);

 protected:
  void showEvent(QShowEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;

 private:
  struct Op {
    enum class Kind { None, List, Import };
    Kind kind = Kind::None;
    PlaylistImport::PlaylistInfo playlist;
  };

  struct Svc {
    ServiceEntry entry;
    AccountSession *session = nullptr;
    PlaylistImport::Importer *importer = nullptr;
    QList<PlaylistImport::PlaylistInfo> playlists;
    bool loaded = false;
    QDateTime loaded_at;
    bool refresh_failed = false;  // the last refresh failed, the list shown is older
    QString account_key;
    bool has_failure = false;
    PlaylistImport::Failure failure;
    QString notice;  // an information that stays until dismissed (not an error)
    Op last_op;
    bool auto_tried = false;
    bool force_setup = false;
    QString setup_error;
    QSet<QString> imported;  // identities imported in this session
    QString status;
    QString opened_identity;  // last successfully imported snapshot, for "Open"
    QDateTime retry_blocked_until;
  };

  Svc *current();
  const Svc *current() const;
  Svc *find(const QString &key) const;
  void Build();
  void BuildBanner();
  void BuildPages();
  void ConnectService(Svc *svc);
  void SetService(const int index);
  void UpdateAll();
  void UpdateHeader(const Svc *svc);
  void UpdateBanner(const Svc *svc);
  void UpdatePage(const Svc *svc);
  void UpdateFooter(const Svc *svc);
  void UpdateList(const Svc *svc);
  void ApplyFilter();
  void SetFailure(Svc *svc, const PlaylistImport::Failure &failure);
  void ClearFailure(Svc *svc);
  void StartList(Svc *svc);
  void StartImport(Svc *svc, const PlaylistImport::PlaylistInfo &playlist);
  void Retry();
  void PrimaryAccountAction();
  void SignInFinishedFor(Svc *svc, const bool success, const QString &message, const bool cancelled);
  void MaybeAutoLoad(Svc *svc);
  void DiscardIfAccountChanged(Svc *svc);
  void SaveSetup();
  void ApplyCompact();
  void FitBannerText();
  bool SnapshotExists(const Svc *svc, const QString &playlist_id) const;
  PlaylistImport::PlaylistInfo SelectedPlaylist() const;
  static PlaylistImport::Failure SignInFailure(const QString &service, const QString &message, const bool timeout);

  QList<Svc*> services_;
  int current_ = -1;
  std::function<bool(const QString &)> snapshot_exists_;
  bool compact_ = false;

  // Header
  QFrame *header_;
  QBoxLayout *header_layout_;
  QBoxLayout *header_actions_ = nullptr;
  QWidget *monogram_;
  QLabel *label_account_title_;
  QLabel *label_account_status_;
  QWidget *service_switch_;
  QButtonGroup *service_group_;
  QPushButton *button_account_;
  QPushButton *button_signout_;

  // Banner
  QFrame *banner_;
  QLabel *banner_title_;
  QLabel *banner_message_;
  QPushButton *banner_primary_;
  QPushButton *banner_retry_;
  QPushButton *banner_dismiss_;
  QToolButton *banner_details_toggle_;
  QPlainTextEdit *banner_details_;

  // Pages
  QStackedWidget *pages_;
  QLabel *label_setup_help_;
  QLabel *label_setup_error_;
  QWidget *setup_form_;
  QMap<QString, QLineEdit*> setup_edits_;
  QPushButton *button_save_setup_;
  QPushButton *button_advanced_;
  QLabel *label_signedout_title_;
  QLabel *label_signedout_help_;
  QPushButton *button_signin_page_;
  QLabel *label_waiting_help_;
  QLineEdit *link_edit_;
  QWidget *link_row_;
  QPushButton *button_copy_link_;
  QPushButton *button_cancel_signin_;
  QLabel *label_loading_;
  QLabel *label_unloaded_;
  QLabel *label_empty_;
  QPushButton *button_load_;
  QLineEdit *search_;
  QLabel *label_list_summary_;
  QPushButton *button_reload_;
  QListWidget *list_;
  QPushButton *button_empty_reload_;

  // Footer
  QLabel *label_status_;
  QProgressBar *progress_;
  QPushButton *button_cancel_;
  QPushButton *button_import_;
  QPushButton *button_browse_;
  QPushButton *button_open_;
  QBoxLayout *footer_buttons_ = nullptr;
  QString list_signature_;
};

#endif  // STREAMINGIMPORTVIEW_H
