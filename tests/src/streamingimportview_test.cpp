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

#include "gtest_include.h"

#include <functional>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QToolButton>
#include <QUrlQuery>

#include "includes/shared_ptr.h"
#include "core/networkaccessmanager.h"
#include "core/oauthenticator.h"
#include "urlcatcher.h"
#include "streaming/playlistimport.h"
#include "streaming/streamingaccount.h"
#include "streaming/streamingimportview.h"

using namespace PlaylistImport;

namespace {

template<typename Predicate>
bool WaitFor(Predicate predicate, const int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate()) {
    if (timer.elapsed() > timeout_ms) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  return true;
}


// No visible part may stick out of the widget on the right or left: nothing is clipped at compact widths.
bool FitsHorizontally(QWidget &view) {
  for (QWidget *child : view.findChildren<QWidget*>()) {
    if (!child->isVisibleTo(&view) || child->isWindow()) continue;
    const QPoint left = child->mapTo(&view, QPoint(0, 0));
    const QPoint right = child->mapTo(&view, QPoint(child->width() - 1, 0));
    if (left.x() < 0 || right.x() >= view.width()) {
      qWarning() << "Clipped:" << child->metaObject()->className() << child->objectName() << left << right << view.width();
      return false;
    }
  }
  return true;
}

void Pump() {
  for (int i = 0; i < 5; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// An account backend that never touches a network, a browser or the settings.
class FakeAccount : public AccountBackend {
 public:
  explicit FakeAccount(QObject *parent = nullptr) : AccountBackend(parent) {}

  QString key() const override { return QStringLiteral("tidal"); }
  QString service_name() const override { return QStringLiteral("Tidal"); }
  bool authenticated() const override { return signed_in; }
  bool configured() const override { return is_configured; }
  QString account_key() const override { return signed_in ? account : QString(); }
  QList<SetupField> setup_fields() const override {
    SetupField field;
    field.key = QStringLiteral("client_id");
    field.label = QStringLiteral("Client ID");
    return QList<SetupField>() << field;
  }
  QString setup_help() const override { return QStringLiteral("Needs a developer client ID."); }
  QString signin_help() const override { return QStringLiteral("Your browser opens the sign-in page."); }
  bool SaveSetup(const QMap<QString, QString> &values, QString *error) override {
    ++save_calls;
    if (values.value(QStringLiteral("client_id")).trimmed().size() < 4) {
      if (error) *error = QStringLiteral("Too short.");
      return false;
    }
    saved_client_id = values.value(QStringLiteral("client_id"));
    is_configured = true;
    return true;
  }
  void StartSignIn() override { ++start_calls; }
  void CancelSignIn() override { ++cancel_calls; }
  void SignOut() override { signed_in = false; ++signout_calls; }

  // What the real service would do when the browser returns.
  void FinishSuccess(const QString &new_account = QStringLiteral("42")) {
    signed_in = true;
    account = new_account;
    Q_EMIT SignInSucceeded();
  }
  void FinishFailure(const QString &message) { Q_EMIT SignInFailed(message); }
  void LoseSession() {
    signed_in = false;
    Q_EMIT SessionChanged();
  }

  bool signed_in = false;
  bool is_configured = true;
  QString account = QStringLiteral("42");
  QString saved_client_id;
  int start_calls = 0;
  int cancel_calls = 0;
  int signout_calls = 0;
  int save_calls = 0;
};

// A scripted playlist source (see playlistimport_test.cpp).
class FakeSource : public PageSource {
 public:
  using Responder = std::function<PageResult(const bool tracks, const QString &playlist_id, const int offset)>;

  Service service() const override { return Service::Tidal; }
  void RequestPlaylists(const int offset, const int) override { Enqueue(false, QString(), offset); }
  void RequestTracks(const QString &playlist_id, const int offset, const int) override { Enqueue(true, playlist_id, offset); }
  void Cancel() override { ++generation_; }

  Responder responder;
  bool hold = false;
  int requests = 0;
  int track_requests = 0;

 private:
  void Enqueue(const bool tracks, const QString &playlist_id, const int offset) {
    ++requests;
    if (tracks) ++track_requests;
    if (hold) return;
    const quint64 generation = generation_;
    QMetaObject::invokeMethod(this, [this, tracks, playlist_id, offset, generation]() {
      if (generation != generation_) return;
      Q_EMIT PageReady(responder(tracks, playlist_id, offset));
    }, Qt::QueuedConnection);
  }

  quint64 generation_ = 0;
};

PageResult Ok(const QJsonObject &json) {
  PageResult result;
  result.ok = true;
  result.http_status = 200;
  result.json = json;
  return result;
}

PageResult Http(const int status, const QString &message = QString()) {
  PageResult result;
  result.ok = false;
  result.http_status = status;
  result.error = message;
  return result;
}

PageResult Net(const int network_error) {
  PageResult result;
  result.ok = false;
  result.network_error = network_error;
  result.error = QStringLiteral("Network error");
  return result;
}

QJsonObject PlaylistsPage(const int count) {
  QJsonArray items;
  for (int i = 0; i < count; ++i) {
    items.append(QJsonObject{ { QStringLiteral("uuid"), QStringLiteral("pl-%1").arg(i + 1) }, { QStringLiteral("title"), QStringLiteral("Mix %1").arg(i + 1) }, { QStringLiteral("numberOfTracks"), 2 } });
  }
  return QJsonObject{ { QStringLiteral("offset"), 0 }, { QStringLiteral("totalNumberOfItems"), count }, { QStringLiteral("items"), items } };
}

QJsonObject TracksPage(const QList<int> &ids) {
  QJsonArray items;
  for (const int id : ids) {
    QJsonObject item{ { QStringLiteral("id"), id }, { QStringLiteral("title"), QStringLiteral("T%1").arg(id) }, { QStringLiteral("duration"), 100 },
                      { QStringLiteral("artist"), QJsonObject{ { QStringLiteral("id"), 7 }, { QStringLiteral("name"), QStringLiteral("A") } } },
                      { QStringLiteral("album"), QJsonObject{ { QStringLiteral("id"), 8 }, { QStringLiteral("title"), QStringLiteral("B") } } } };
    items.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("track") }, { QStringLiteral("item"), item } });
  }
  return QJsonObject{ { QStringLiteral("offset"), 0 }, { QStringLiteral("totalNumberOfItems"), static_cast<int>(ids.count()) }, { QStringLiteral("items"), items } };
}

PageResult HappyPath(const bool tracks, const QString &, const int) {
  return tracks ? Ok(TracksPage(QList<int>() << 1 << 2 << 2)) : Ok(PlaylistsPage(3));
}

}  // namespace

class StreamingImportViewTest : public ::testing::Test {
 protected:
  StreamingImportViewTest() : view(), account(new FakeAccount(&holder)), source(new FakeSource) {
    source->setParent(&holder);
    source->responder = HappyPath;
    StreamingImportView::ServiceEntry entry;
    entry.name = QStringLiteral("Tidal");
    entry.url_scheme = QStringLiteral("tidal");
    entry.song_source = Song::Source::Tidal;
    entry.page_source = source;
    entry.account = account;
    view.AddService(entry);
    view.resize(900, 640);
    QObject::connect(&view, &StreamingImportView::SnapshotReady, &view, [this](const QString &, const QString &identity, const QString &name, const SongList &songs, const int skipped) {
      snapshots << identity;
      snapshot_names << name;
      snapshot_sizes << static_cast<int>(songs.count());
      snapshot_skipped << skipped;
    });
    view.show();
    Pump();
  }

  // Signs in and reads the list.
  void SignInAndLoad() {
    account->signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    ASSERT_TRUE(WaitFor([this]() { return view.current_page() == StreamingImportView::PageList; }));
  }

  void SelectFirst() {
    view.playlist_list()->setCurrentRow(0);
    view.playlist_list()->item(0)->setSelected(true);
  }

  QObject holder;
  StreamingImportView view;
  FakeAccount *account;
  FakeSource *source;
  QStringList snapshots;
  QStringList snapshot_names;
  QList<int> snapshot_sizes;
  QList<int> snapshot_skipped;
};

// -------------------------------------------------------------------------------------------------------- account session

TEST(AccountSessionTest, InitialStateFollowsTheService) {
  QObject holder;
  FakeAccount account(&holder);
  account.is_configured = false;
  EXPECT_EQ(AccountSession(&account).state(), AccountSession::State::NeedsSetup);
  account.is_configured = true;
  EXPECT_EQ(AccountSession(&account).state(), AccountSession::State::SignedOut);
  account.signed_in = true;
  EXPECT_EQ(AccountSession(&account).state(), AccountSession::State::SignedIn);
}

TEST(AccountSessionTest, SuccessfulSignInReportsOnceAndRefreshesTheState) {
  QObject holder;
  FakeAccount account(&holder);
  AccountSession session(&account);
  QSignalSpy finished(&session, &AccountSession::SignInFinished);
  session.SignIn();
  EXPECT_EQ(session.state(), AccountSession::State::SigningIn);
  EXPECT_EQ(account.start_calls, 1);
  session.SignIn();  // ignored while waiting
  EXPECT_EQ(account.start_calls, 1);
  account.FinishSuccess();
  EXPECT_EQ(session.state(), AccountSession::State::SignedIn);
  ASSERT_EQ(finished.count(), 1);
  EXPECT_TRUE(finished.at(0).at(0).toBool());
  account.FinishSuccess();  // a repeated callback changes nothing and reports nothing
  EXPECT_EQ(finished.count(), 1);
}

TEST(AccountSessionTest, FailureEndsTheAttemptAndKeepsTheMessage) {
  QObject holder;
  FakeAccount account(&holder);
  AccountSession session(&account);
  QSignalSpy finished(&session, &AccountSession::SignInFinished);
  session.SignIn();
  account.FinishFailure(QStringLiteral("access_denied"));
  EXPECT_EQ(session.state(), AccountSession::State::SignedOut);
  ASSERT_EQ(finished.count(), 1);
  EXPECT_FALSE(finished.at(0).at(0).toBool());
  EXPECT_EQ(finished.at(0).at(1).toString(), QStringLiteral("access_denied"));
  EXPECT_FALSE(finished.at(0).at(2).toBool());
  EXPECT_GE(account.cancel_calls, 1);  // the service is told to forget the attempt
}

TEST(AccountSessionTest, CancelStopsTheFlowAndLateCallbacksAreIgnored) {
  QObject holder;
  FakeAccount account(&holder);
  AccountSession session(&account);
  QSignalSpy finished(&session, &AccountSession::SignInFinished);
  session.SignIn();
  session.Cancel();
  EXPECT_EQ(account.cancel_calls, 1);
  EXPECT_EQ(session.state(), AccountSession::State::SignedOut);
  ASSERT_EQ(finished.count(), 1);
  EXPECT_TRUE(finished.at(0).at(2).toBool());
  // The browser answers after the user gave up: neither a failure nor a success may show up.
  account.FinishFailure(QStringLiteral("late"));
  account.signed_in = false;
  Q_EMIT account.SignInSucceeded();
  EXPECT_EQ(finished.count(), 1);
  EXPECT_EQ(session.state(), AccountSession::State::SignedOut);
}

TEST(AccountSessionTest, AStaleSuccessDoesNotEndTheNextAttempt) {
  QObject holder;
  FakeAccount account(&holder);
  AccountSession session(&account);
  QSignalSpy finished(&session, &AccountSession::SignInFinished);
  session.SignIn();
  session.Cancel();
  session.SignIn();
  const int second = session.attempt();
  // The first attempt's success arrives while the second one waits and the service holds no session: ignored.
  Q_EMIT account.SignInSucceeded();
  EXPECT_EQ(session.state(), AccountSession::State::SigningIn);
  EXPECT_EQ(session.attempt(), second);
  EXPECT_EQ(finished.count(), 1);  // only the cancel
  account.FinishSuccess();
  EXPECT_EQ(session.state(), AccountSession::State::SignedIn);
  EXPECT_EQ(finished.count(), 2);
}

TEST(AccountSessionTest, TimeoutAbandonsTheAttempt) {
  QObject holder;
  FakeAccount account(&holder);
  AccountSession session(&account, nullptr, 40);
  QSignalSpy finished(&session, &AccountSession::SignInFinished);
  session.SignIn();
  ASSERT_TRUE(WaitFor([&]() { return finished.count() == 1; }));
  EXPECT_FALSE(finished.at(0).at(0).toBool());
  EXPECT_EQ(account.cancel_calls, 1);
  EXPECT_EQ(session.state(), AccountSession::State::SignedOut);
  account.FinishSuccess();  // far too late, but the service really has a session now: the state follows the truth, no new report
  EXPECT_EQ(finished.count(), 1);
}

TEST(AccountSessionTest, SetupIsRequiredBeforeSignIn) {
  QObject holder;
  FakeAccount account(&holder);
  account.is_configured = false;
  AccountSession session(&account);
  session.SignIn();
  EXPECT_EQ(account.start_calls, 0);
  EXPECT_EQ(session.state(), AccountSession::State::NeedsSetup);
}

TEST(AccountSessionTest, SessionLostBehindTheBackIsNoticed) {
  QObject holder;
  FakeAccount account(&holder);
  account.signed_in = true;
  AccountSession session(&account);
  QSignalSpy changed(&session, &AccountSession::StateChanged);
  account.LoseSession();
  EXPECT_EQ(session.state(), AccountSession::State::SignedOut);
  EXPECT_EQ(changed.count(), 1);
}

// ------------------------------------------------------------------------------------------------------ failure kinds

TEST(FailureClassificationTest, StatusCodesMapToActionableKinds) {
  EXPECT_EQ(ClassifyFailure(Http(401), QStringLiteral("Tidal")).kind, FailureKind::AuthExpired);
  EXPECT_TRUE(ClassifyFailure(Http(401), QStringLiteral("Tidal")).needs_signin);
  EXPECT_EQ(ClassifyFailure(Http(403), QStringLiteral("Tidal")).kind, FailureKind::PermissionDenied);
  EXPECT_EQ(ClassifyFailure(Http(429), QStringLiteral("Tidal")).kind, FailureKind::RateLimited);
  EXPECT_TRUE(ClassifyFailure(Http(429), QStringLiteral("Tidal")).retryable);
  EXPECT_EQ(ClassifyFailure(Http(503), QStringLiteral("Tidal")).kind, FailureKind::ServerError);
  EXPECT_EQ(ClassifyFailure(Net(QNetworkReply::TimeoutError), QStringLiteral("Tidal")).kind, FailureKind::Timeout);
  EXPECT_EQ(ClassifyFailure(Net(QNetworkReply::HostNotFoundError), QStringLiteral("Tidal")).kind, FailureKind::Offline);
  EXPECT_FALSE(ClassifyFailure(Http(401), QStringLiteral("Tidal")).retryable);
  PageResult missing;
  missing.not_signed_in = true;
  EXPECT_EQ(ClassifyFailure(missing, QStringLiteral("Qobuz")).kind, FailureKind::NotSignedIn);
  // Every message says that nothing was imported and names the service.
  EXPECT_TRUE(ClassifyFailure(Http(500), QStringLiteral("Qobuz")).message.contains(QStringLiteral("Qobuz")));
  EXPECT_TRUE(ClassifyFailure(Http(500), QStringLiteral("Qobuz")).message.contains(QStringLiteral("Nothing was imported")));
  // Technical text only in the details.
  EXPECT_TRUE(ClassifyFailure(Http(500, QStringLiteral("boom")), QStringLiteral("Qobuz")).details.contains(QStringLiteral("HTTP 500")));
  EXPECT_FALSE(ClassifyFailure(Http(500, QStringLiteral("boom")), QStringLiteral("Qobuz")).message.contains(QStringLiteral("boom")));
}

// ------------------------------------------------------------------------------------------------------------- the view

TEST_F(StreamingImportViewTest, SignedOutShowsInlineSignIn) {
  EXPECT_EQ(view.current_page(), StreamingImportView::PageSignedOut);
  EXPECT_EQ(view.account_status_label()->text(), QStringLiteral("Not signed in"));
  EXPECT_EQ(view.account_button()->text(), QStringLiteral("Sign in"));
  EXPECT_FALSE(view.import_button()->isEnabled());
  EXPECT_FALSE(view.signout_button()->isVisible());
  EXPECT_EQ(source->requests, 0);  // nothing is read without a session
}

TEST_F(StreamingImportViewTest, SigningInWaitsThenLoadsThePlaylistsAutomatically) {
  QSignalSpy finished(&view, &StreamingImportView::SignInFinished);
  view.account_button()->click();
  EXPECT_EQ(account->start_calls, 1);
  EXPECT_EQ(view.current_page(), StreamingImportView::PageWaiting);
  EXPECT_EQ(view.account_button()->text(), QStringLiteral("Cancel sign-in"));
  account->FinishSuccess();
  ASSERT_TRUE(WaitFor([this]() { return view.current_page() == StreamingImportView::PageList; }));
  EXPECT_EQ(view.playlist_list()->count(), 3);
  EXPECT_EQ(view.account_status_label()->text(), QStringLiteral("Signed in"));
  EXPECT_TRUE(view.signout_button()->isVisible());
  ASSERT_EQ(finished.count(), 1);
  EXPECT_TRUE(finished.at(0).at(1).toBool());
  EXPECT_FALSE(view.banner()->isVisible());
}

TEST_F(StreamingImportViewTest, CancellingTheSignInReturnsWithoutAnError) {
  view.account_button()->click();
  view.account_button()->click();  // the same button now cancels
  EXPECT_EQ(account->cancel_calls, 1);
  EXPECT_EQ(view.current_page(), StreamingImportView::PageSignedOut);
  EXPECT_FALSE(view.banner()->isVisible());
  EXPECT_NE(view.status_label()->text().indexOf(QStringLiteral("cancelled")), -1);
}

TEST_F(StreamingImportViewTest, FailedSignInIsShownUntilDismissedAndOffersTryAgain) {
  view.account_button()->click();
  account->FinishFailure(QStringLiteral("access_denied (User denied)"));
  ASSERT_TRUE(view.banner()->isVisible());
  EXPECT_NE(view.banner_title()->text().indexOf(QStringLiteral("Problem")), -1);
  EXPECT_EQ(view.banner_title()->text(), QStringLiteral("Problem: Access was declined"));
  EXPECT_EQ(view.banner_details()->toPlainText(), QStringLiteral("access_denied (User denied)"));
  ASSERT_TRUE(view.banner_primary()->isVisible());
  view.banner_primary()->click();  // Sign in again
  EXPECT_EQ(account->start_calls, 2);
  account->FinishSuccess();
  ASSERT_TRUE(WaitFor([this]() { return view.current_page() == StreamingImportView::PageList; }));
  EXPECT_FALSE(view.banner()->isVisible());  // resolved
}

TEST_F(StreamingImportViewTest, ErrorStaysAcrossServiceViewUpdatesUntilDismissed) {
  view.account_button()->click();
  account->FinishFailure(QStringLiteral("boom"));
  ASSERT_TRUE(view.banner()->isVisible());
  view.Activate();
  Pump();
  EXPECT_TRUE(view.banner()->isVisible());
  view.banner_dismiss()->click();
  EXPECT_FALSE(view.banner()->isVisible());
}

TEST_F(StreamingImportViewTest, MissingDeveloperCredentialsGetAnInlineHonestSetup) {
  account->is_configured = false;
  view.session(QStringLiteral("tidal"))->Refresh();
  Pump();
  EXPECT_EQ(view.current_page(), StreamingImportView::PageSetup);
  EXPECT_EQ(view.account_button()->text(), QStringLiteral("Set up sign-in"));
  ASSERT_NE(view.setup_field(QStringLiteral("client_id")), nullptr);
  view.save_setup_button()->click();
  EXPECT_EQ(account->save_calls, 1);
  EXPECT_EQ(view.current_page(), StreamingImportView::PageSetup);  // refused, still asks
  view.setup_field(QStringLiteral("client_id"))->setText(QStringLiteral("abcdef123456"));
  view.save_setup_button()->click();
  EXPECT_EQ(account->saved_client_id, QStringLiteral("abcdef123456"));
  EXPECT_EQ(view.current_page(), StreamingImportView::PageSignedOut);
  EXPECT_TRUE(view.setup_field(QStringLiteral("client_id")) == nullptr || view.setup_field(QStringLiteral("client_id"))->text().isEmpty());
}

TEST_F(StreamingImportViewTest, ListShowsCardsSearchAndSelection) {
  SignInAndLoad();
  EXPECT_EQ(view.playlist_list()->count(), 3);
  view.search_edit()->setText(QStringLiteral("mix 2"));
  int visible = 0;
  for (int i = 0; i < 3; ++i) visible += view.playlist_list()->item(i)->isHidden() ? 0 : 1;
  EXPECT_EQ(visible, 1);
  EXPECT_FALSE(view.import_button()->isEnabled());  // nothing selected
  view.search_edit()->clear();
  SelectFirst();
  EXPECT_TRUE(view.import_button()->isEnabled());
  EXPECT_EQ(view.import_button()->text(), QStringLiteral("Import"));
}

TEST_F(StreamingImportViewTest, ImportDeliversTheCompleteOrderedListOnceAndRefreshRelabels) {
  view.SetSnapshotExistsFunction([this](const QString &identity) { return snapshots.contains(identity); });
  SignInAndLoad();
  SelectFirst();
  QSignalSpy state(view.importer(QStringLiteral("tidal")), &Importer::StateChanged);
  view.import_button()->click();
  view.import_button()->click();  // a second click while reading is ignored
  ASSERT_TRUE(WaitFor([this]() { return !snapshots.isEmpty(); }));
  Pump();
  ASSERT_EQ(snapshots.count(), 1);
  EXPECT_EQ(snapshots.at(0), QStringLiteral("tidal:pl-1"));
  EXPECT_EQ(snapshot_names.at(0), QStringLiteral("Tidal: Mix 1"));
  EXPECT_EQ(snapshot_sizes.at(0), 3);  // 1, 2, 2: the intentional duplicate is kept
  EXPECT_EQ(source->track_requests, 1);
  // Reading is not importing: nothing claims success before the host confirms.
  EXPECT_EQ(view.status_label()->text().indexOf(QStringLiteral("Imported")), -1);
  view.ShowImported(QStringLiteral("Tidal: Mix 1"), false, 3, 0);
  EXPECT_NE(view.status_label()->text().indexOf(QStringLiteral("Imported")), -1);
  EXPECT_EQ(view.import_button()->text(), QStringLiteral("Refresh snapshot"));
  EXPECT_FALSE(view.banner()->isVisible());
  QSignalSpy open(&view, &StreamingImportView::OpenSnapshotRequested);
  // The "Open imported playlist" button is part of the footer.
  for (QPushButton *button : view.findChildren<QPushButton*>()) {
    if (button->text() == QStringLiteral("Open imported playlist")) {
      EXPECT_TRUE(button->isVisible());
      button->click();
    }
  }
  ASSERT_EQ(open.count(), 1);
  EXPECT_EQ(open.at(0).at(0).toString(), QStringLiteral("tidal:pl-1"));
}

TEST_F(StreamingImportViewTest, HostFailureToStoreNeverLooksSuccessful) {
  SignInAndLoad();
  SelectFirst();
  view.import_button()->click();
  ASSERT_TRUE(WaitFor([this]() { return !snapshots.isEmpty(); }));
  view.ShowImportFailed(QStringLiteral("Tidal: Mix 1"), QStringLiteral("database is locked"));
  ASSERT_TRUE(view.banner()->isVisible());
  EXPECT_EQ(view.status_label()->text().indexOf(QStringLiteral("Imported")), -1);
  EXPECT_EQ(view.banner_details()->toPlainText(), QStringLiteral("database is locked"));
}

TEST_F(StreamingImportViewTest, CancellingAnImportImportsNothingAndShowsProgressControls) {
  SignInAndLoad();
  SelectFirst();
  source->hold = true;
  view.import_button()->click();
  Pump();
  EXPECT_TRUE(view.cancel_button()->isVisible());
  EXPECT_TRUE(view.progress_bar()->isVisible());
  EXPECT_FALSE(view.import_button()->isEnabled());
  view.cancel_button()->click();
  Pump();
  EXPECT_TRUE(snapshots.isEmpty());
  EXPECT_NE(view.status_label()->text().indexOf(QStringLiteral("Cancelled")), -1);
  EXPECT_FALSE(view.cancel_button()->isVisible());
  EXPECT_EQ(view.playlist_list()->count(), 3);  // the list is untouched
  EXPECT_FALSE(view.banner()->isVisible());     // cancelling is not an error
}

TEST_F(StreamingImportViewTest, ExpiredLoginOnLoadOffersReconnectAndNoRetry) {
  account->signed_in = true;
  view.session(QStringLiteral("tidal"))->Refresh();
  source->responder = [](const bool, const QString &, const int) { return Http(401); };
  view.Activate();
  ASSERT_TRUE(WaitFor([this]() { return view.banner()->isVisible(); }));
  EXPECT_EQ(view.banner_title()->text(), QStringLiteral("Problem: Sign-in expired"));
  EXPECT_FALSE(view.banner_message()->text().contains(QStringLiteral("401")));
  EXPECT_TRUE(view.banner_details()->toPlainText().contains(QStringLiteral("HTTP 401")));
  EXPECT_TRUE(view.banner_primary()->isVisible());
  EXPECT_EQ(view.banner_primary()->text(), QStringLiteral("Reconnect"));
  EXPECT_FALSE(view.banner_retry()->isVisible());
  EXPECT_EQ(view.current_page(), StreamingImportView::PageUnloaded);
  // Reconnect runs the in-app sign-in, and success loads again without any other step.
  source->responder = HappyPath;
  view.banner_primary()->click();
  EXPECT_EQ(account->start_calls, 1);
  account->FinishSuccess();
  ASSERT_TRUE(WaitFor([this]() { return view.current_page() == StreamingImportView::PageList; }));
  EXPECT_FALSE(view.banner()->isVisible());
}

TEST_F(StreamingImportViewTest, PermissionDeniedExplainsAndOffersReconnect) {
  account->signed_in = true;
  view.session(QStringLiteral("tidal"))->Refresh();
  source->responder = [](const bool, const QString &, const int) { return Http(403); };
  view.Activate();
  ASSERT_TRUE(WaitFor([this]() { return view.banner()->isVisible(); }));
  EXPECT_EQ(view.banner_title()->text(), QStringLiteral("Problem: Access denied"));
  EXPECT_TRUE(view.banner_message()->text().contains(QStringLiteral("subscription")));
  EXPECT_TRUE(view.banner_primary()->isVisible());
}

TEST_F(StreamingImportViewTest, TemporaryFailuresOfferRetryWhichRepeatsOnlyTheRead) {
  account->signed_in = true;
  view.session(QStringLiteral("tidal"))->Refresh();
  source->responder = [](const bool, const QString &, const int) { return Net(QNetworkReply::HostNotFoundError); };
  view.Activate();
  ASSERT_TRUE(WaitFor([this]() { return view.banner()->isVisible(); }));
  EXPECT_EQ(view.banner_title()->text(), QStringLiteral("Problem: No connection"));
  ASSERT_TRUE(view.banner_retry()->isVisible());
  ASSERT_TRUE(view.banner_retry()->isEnabled());
  const int before = source->requests;
  EXPECT_EQ(before, 1);  // exactly one automatic read, no loop
  Pump();
  EXPECT_EQ(source->requests, 1);
  source->responder = HappyPath;
  view.banner_retry()->click();
  ASSERT_TRUE(WaitFor([this]() { return view.current_page() == StreamingImportView::PageList; }));
  EXPECT_FALSE(view.banner()->isVisible());
  EXPECT_EQ(source->requests, 2);
}

TEST_F(StreamingImportViewTest, RateLimitBlocksRetryForTheRequestedWait) {
  account->signed_in = true;
  view.session(QStringLiteral("tidal"))->Refresh();
  source->responder = [](const bool, const QString &, const int) {
    PageResult result = Http(429);
    result.retry_after_sec = 60;
    return result;
  };
  view.Activate();
  ASSERT_TRUE(WaitFor([this]() { return view.banner()->isVisible(); }));
  EXPECT_EQ(view.banner_title()->text(), QStringLiteral("Problem: Too many requests"));
  EXPECT_TRUE(view.banner_message()->text().contains(QStringLiteral("60 seconds")));
  EXPECT_TRUE(view.banner_retry()->isVisible());
  EXPECT_FALSE(view.banner_retry()->isEnabled());
  view.banner_retry()->click();  // disabled: no new request
  Pump();
  EXPECT_EQ(source->requests, 1);
}

TEST_F(StreamingImportViewTest, FailedRefreshKeepsTheLastUsefulListAndSaysSo) {
  SignInAndLoad();
  source->responder = [](const bool, const QString &, const int) { return Http(503, QStringLiteral("Service Unavailable")); };
  view.reload_button()->click();
  ASSERT_TRUE(WaitFor([this]() { return view.banner()->isVisible(); }));
  EXPECT_EQ(view.current_page(), StreamingImportView::PageList);
  EXPECT_EQ(view.playlist_list()->count(), 3);
  EXPECT_NE(view.status_label()->text().indexOf(QStringLiteral("earlier list is still shown")), -1);
  EXPECT_TRUE(view.banner_retry()->isVisible());
  // The list stays usable for import while signed in.
  SelectFirst();
  EXPECT_TRUE(view.import_button()->isEnabled());
}

TEST_F(StreamingImportViewTest, FailedImportNeverImportsAndOffersRetry) {
  SignInAndLoad();
  SelectFirst();
  source->responder = [](const bool tracks, const QString &, const int) { return tracks ? Net(QNetworkReply::TimeoutError) : Ok(PlaylistsPage(3)); };
  view.import_button()->click();
  ASSERT_TRUE(WaitFor([this]() { return view.banner()->isVisible(); }));
  EXPECT_TRUE(snapshots.isEmpty());
  EXPECT_EQ(view.status_label()->text().indexOf(QStringLiteral("Imported")), -1);
  EXPECT_EQ(view.playlist_list()->count(), 3);
  source->responder = HappyPath;
  view.banner_retry()->click();
  ASSERT_TRUE(WaitFor([this]() { return snapshots.count() == 1; }));
  EXPECT_FALSE(view.banner()->isVisible());
}

TEST_F(StreamingImportViewTest, SessionLostKeepsTheListButDisablesImportAndExplains) {
  SignInAndLoad();
  SelectFirst();
  account->LoseSession();
  Pump();
  EXPECT_EQ(view.current_page(), StreamingImportView::PageList);
  EXPECT_EQ(view.playlist_list()->count(), 3);
  EXPECT_FALSE(view.import_button()->isEnabled());
  ASSERT_TRUE(view.banner()->isVisible());
  EXPECT_EQ(view.banner_title()->text(), QStringLiteral("Note"));
  EXPECT_EQ(view.account_button()->text(), QStringLiteral("Sign in"));
}

TEST_F(StreamingImportViewTest, SigningInWithAnotherAccountDiscardsTheOldList) {
  SignInAndLoad();
  account->signed_in = false;
  view.session(QStringLiteral("tidal"))->Refresh();
  source->responder = [](const bool tracks, const QString &, const int) {
    return tracks ? Ok(TracksPage(QList<int>() << 9)) : Ok(PlaylistsPage(1));
  };
  view.account_button()->click();
  account->FinishSuccess(QStringLiteral("777"));
  ASSERT_TRUE(WaitFor([this]() { return view.playlist_list()->count() == 1; }));
  EXPECT_EQ(view.playlist_list()->count(), 1);
}

TEST_F(StreamingImportViewTest, SignOutKeepsImportedPlaylistsAndShowsSignedOut) {
  SignInAndLoad();
  view.signout_button()->click();
  EXPECT_EQ(account->signout_calls, 1);
  EXPECT_FALSE(view.signout_button()->isVisible());
  EXPECT_EQ(view.account_status_label()->text(), QStringLiteral("Not signed in"));
  EXPECT_FALSE(view.import_button()->isEnabled());
}

TEST_F(StreamingImportViewTest, RemoteTextIsNeverInterpretedAsRichText) {
  account->signed_in = true;
  view.session(QStringLiteral("tidal"))->Refresh();
  source->responder = [](const bool, const QString &, const int) {
    return Ok(QJsonObject{ { QStringLiteral("offset"), 0 }, { QStringLiteral("totalNumberOfItems"), 1 },
                           { QStringLiteral("items"), QJsonArray{ QJsonObject{ { QStringLiteral("uuid"), QStringLiteral("x") }, { QStringLiteral("title"), QStringLiteral("<b>bold</b><img src=x>") }, { QStringLiteral("numberOfTracks"), 1 } } } } });
  };
  view.Activate();
  ASSERT_TRUE(WaitFor([this]() { return view.current_page() == StreamingImportView::PageList; }));
  EXPECT_EQ(view.playlist_list()->item(0)->data(Qt::UserRole + 1).value<PlaylistInfo>().name, QStringLiteral("<b>bold</b><img src=x>"));
  EXPECT_EQ(view.banner_message()->textFormat(), Qt::PlainText);
  EXPECT_EQ(view.status_label()->textFormat(), Qt::PlainText);
}

TEST_F(StreamingImportViewTest, CompactAndWideLayoutsBothFit) {
  view.resize(380, 600);
  Pump();
  EXPECT_TRUE(view.compact());
  EXPECT_TRUE(FitsHorizontally(view));
  view.resize(1000, 600);
  Pump();
  EXPECT_FALSE(view.compact());
}

TEST_F(StreamingImportViewTest, IsAnOrdinaryChildWidgetNotAWindow) {
  EXPECT_FALSE(view.isWindow() && view.windowModality() != Qt::NonModal);
  EXPECT_EQ(view.windowModality(), Qt::NonModal);
  EXPECT_FALSE(view.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
}

TEST(StreamingImportViewLifecycleTest, CallbacksAfterTheViewIsGoneAreHarmless) {
  QObject holder;
  FakeAccount *account = new FakeAccount(&holder);
  FakeSource *source = new FakeSource;
  source->setParent(&holder);
  source->responder = HappyPath;
  QPointer<StreamingImportView> view = new StreamingImportView;
  StreamingImportView::ServiceEntry entry;
  entry.name = QStringLiteral("Tidal");
  entry.url_scheme = QStringLiteral("tidal");
  entry.page_source = source;
  entry.account = account;
  view->AddService(entry);
  view->show();
  view->account_button()->click();
  EXPECT_EQ(account->start_calls, 1);
  delete view.data();
  // The destroyed view cancelled the sign-in; whatever the browser still delivers goes nowhere.
  EXPECT_EQ(account->cancel_calls, 1);
  account->FinishSuccess();
  account->FinishFailure(QStringLiteral("late"));
  Pump();
  SUCCEED();
}

TEST(StreamingImportViewLifecycleTest, DestroyingTheViewCancelsARunningRead) {
  QObject holder;
  FakeAccount *account = new FakeAccount(&holder);
  account->signed_in = true;
  FakeSource *source = new FakeSource;
  source->setParent(&holder);
  source->hold = true;
  source->responder = HappyPath;
  QPointer<StreamingImportView> view = new StreamingImportView;
  StreamingImportView::ServiceEntry entry;
  entry.name = QStringLiteral("Tidal");
  entry.url_scheme = QStringLiteral("tidal");
  entry.page_source = source;
  entry.account = account;
  view->AddService(entry);
  view->show();
  Pump();
  EXPECT_TRUE(view->busy());
  delete view.data();
  Q_EMIT source->PageReady(Ok(PlaylistsPage(1)));  // a late reply reaches nobody
  Pump();
  SUCCEED();
}

// ------------------------------------------------------------------------------------------------- OAuth callback handling

namespace {

}  // namespace

class OAuthCallbackTest : public ::testing::Test {
 protected:
  OAuthCallbackTest() : network(std::make_shared<NetworkAccessManager>()), oauth(network) {
    oauth.set_settings_group(QStringLiteral("adrift_oauth_test_never_written"));
    oauth.set_type(OAuthenticator::Type::Authorization_Code);
    oauth.set_authorize_url(QUrl(QStringLiteral("https://login.example.invalid/authorize")));
    oauth.set_redirect_url(QUrl(QStringLiteral("tidal://login/auth")));
    // Nothing listens here: a token exchange that wrongly starts fails at once and never leaves this machine.
    oauth.set_access_token_url(QUrl(QStringLiteral("http://127.0.0.1:1/token")));
    oauth.set_client_id(QStringLiteral("test-client"));
    oauth.set_scope(QStringLiteral("r_usr"));
    oauth.set_use_local_redirect_server(false);
    oauth.set_use_pkce(true);
    QObject::connect(&oauth, &OAuthenticator::AuthenticationFinished, &oauth, [this](const bool success, const QString &error, const bool) {
      results << success;
      errors << error;
    });
  }

  QUrl Callback(const QString &state, const QString &extra = QStringLiteral("code=abc")) const {
    return QUrl(QStringLiteral("tidal://login/auth?%1&state=%2").arg(extra, state));
  }

  SharedPtr<NetworkAccessManager> network;
  OAuthenticator oauth;
  UrlCatcher catcher{ QStringLiteral("https") };
  QList<bool> results;
  QStringList errors;
};

TEST_F(OAuthCallbackTest, CallbackWithoutAPendingSignInIsRefusedAndStartsNothing) {
  EXPECT_FALSE(oauth.authentication_pending());
  oauth.ExternalAuthorizationUrlReceived(Callback(QStringLiteral("whatever")));
  ASSERT_EQ(results.count(), 1);
  EXPECT_FALSE(results.at(0));
  EXPECT_NE(errors.at(0).indexOf(QStringLiteral("No sign-in is waiting")), -1);
  EXPECT_FALSE(oauth.authenticated());
}

TEST_F(OAuthCallbackTest, SignInOpensTheBrowserAddressAndWaits) {
  oauth.Authenticate();
  ASSERT_EQ(catcher.urls.count(), 1);
  EXPECT_TRUE(oauth.authentication_pending());
  EXPECT_FALSE(catcher.State().isEmpty());
  EXPECT_EQ(oauth.pending_authorize_url(), catcher.urls.at(0));
  EXPECT_TRUE(QUrlQuery(catcher.urls.at(0)).hasQueryItem(QStringLiteral("code_challenge")));
}

TEST_F(OAuthCallbackTest, CancelledSignInRefusesTheLateCallback) {
  oauth.Authenticate();
  const QString state = catcher.State();
  oauth.CancelAuthentication();
  EXPECT_FALSE(oauth.authentication_pending());
  oauth.ExternalAuthorizationUrlReceived(Callback(state));
  ASSERT_EQ(results.count(), 1);
  EXPECT_FALSE(results.at(0));
  EXPECT_NE(errors.at(0).indexOf(QStringLiteral("No sign-in is waiting")), -1);
}

TEST_F(OAuthCallbackTest, CallbackOfAnEarlierAttemptIsIgnoredWithoutEchoingStates) {
  oauth.Authenticate();
  const QString old_state = catcher.State();
  oauth.Authenticate();  // replaced
  const QString new_state = catcher.State();
  ASSERT_NE(old_state, new_state);
  oauth.ExternalAuthorizationUrlReceived(Callback(old_state));
  ASSERT_EQ(results.count(), 1);
  EXPECT_FALSE(results.at(0));
  EXPECT_NE(errors.at(0).indexOf(QStringLiteral("earlier sign-in attempt")), -1);
  EXPECT_EQ(errors.at(0).indexOf(old_state), -1);
  EXPECT_EQ(errors.at(0).indexOf(new_state), -1);
  EXPECT_TRUE(oauth.authentication_pending());  // the current attempt is still waiting
}

TEST_F(OAuthCallbackTest, ProviderDeniedCallbackIsReportedWithItsReason) {
  oauth.Authenticate();
  oauth.ExternalAuthorizationUrlReceived(Callback(catcher.State(), QStringLiteral("error=access_denied")));
  ASSERT_EQ(results.count(), 1);
  EXPECT_FALSE(results.at(0));
  EXPECT_EQ(errors.at(0), QStringLiteral("access_denied"));
}

TEST_F(OAuthCallbackTest, ACallbackCanOnlyBeUsedOnce) {
  oauth.Authenticate();
  const QString state = catcher.State();
  oauth.ExternalAuthorizationUrlReceived(Callback(state));  // valid: starts the (refused) token exchange
  EXPECT_FALSE(oauth.authentication_pending());
  oauth.ExternalAuthorizationUrlReceived(Callback(state));  // replay
  ASSERT_GE(results.count(), 1);
  EXPECT_FALSE(results.at(0));
  EXPECT_NE(errors.at(0).indexOf(QStringLiteral("No sign-in is waiting")), -1);
  // Let the refused exchange finish: it fails, it never signs in.
  WaitFor([this]() { return results.count() >= 2; }, 3000);
  for (const bool ok : std::as_const(results)) EXPECT_FALSE(ok);
  EXPECT_FALSE(oauth.authenticated());
}

// ------------------------------------------------------------------------------------------------------------------ renders

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

#include "core/themepalette.h"

#ifndef ADRIFT_SCREENSHOT_DIR
#  define ADRIFT_SCREENSHOT_DIR ""
#endif

namespace {

void SaveRender(const QImage &image, const QString &name) {
  ASSERT_FALSE(image.isNull());
  const QString dir = QString::fromUtf8(ADRIFT_SCREENSHOT_DIR);
  if (dir.isEmpty()) return;
  QDir().mkpath(dir);
  ASSERT_TRUE(image.save(QDir(dir).filePath(name + QStringLiteral(".png")))) << name.toStdString();
}

bool NotBlank(const QImage &image) {
  const QRgb first = image.pixel(2, 2);
  for (int y = 0; y < image.height(); y += 3) {
    for (int x = 0; x < image.width(); x += 3) {
      if (image.pixel(x, y) != first) return true;
    }
  }
  return false;
}

class FakeQobuzAccount : public FakeAccount {
 public:
  explicit FakeQobuzAccount(QObject *parent = nullptr) : FakeAccount(parent) {}
  QString key() const override { return QStringLiteral("qobuz"); }
  QString service_name() const override { return QStringLiteral("Qobuz"); }
};

using Arrange = std::function<void(StreamingImportView &, FakeAccount &, FakeSource &)>;

}  // namespace

// Renders the production widget with synthetic service answers. The images show the real states, there is no mock-up.
class StreamingImportRenderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    saved_style_ = QApplication::style()->objectName();
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  }

  void Render(const QString &theme, const QString &size_name, const QSize &size, const QString &state, const Arrange &arrange) {
    QObject holder;
    FakeAccount *account = new FakeAccount(&holder);
    FakeSource *source = new FakeSource;
    source->setParent(&holder);
    source->responder = HappyPath;
    StreamingImportView view;
    if (theme == QLatin1String("adrift-dark")) view.setPalette(ThemePalette::BuildPalette(QApplication::style()->standardPalette(), ThemePalette::AdriftDarkColors()));
    StreamingImportView::ServiceEntry tidal;
    tidal.name = QStringLiteral("Tidal");
    tidal.url_scheme = QStringLiteral("tidal");
    tidal.song_source = Song::Source::Tidal;
    tidal.page_source = source;
    tidal.account = account;
    view.AddService(tidal);
    // A second service so the switch is part of the picture.
    FakeSource *other_source = new FakeSource;
    other_source->setParent(&holder);
    other_source->responder = HappyPath;
    StreamingImportView::ServiceEntry qobuz;
    qobuz.name = QStringLiteral("Qobuz");
    qobuz.url_scheme = QStringLiteral("qobuz");
    qobuz.song_source = Song::Source::Qobuz;
    qobuz.page_source = other_source;
    qobuz.account = new FakeQobuzAccount(&holder);
    view.AddService(qobuz);
    view.SelectService(QStringLiteral("tidal"));

    view.resize(size);
    view.show();
    Pump();
    arrange(view, *account, *source);
    Pump();
    view.resize(size);
    Pump();

    EXPECT_TRUE(FitsHorizontally(view)) << state.toStdString() << " " << size_name.toStdString();
    const QImage image = view.grab().toImage();
    EXPECT_TRUE(NotBlank(image)) << state.toStdString();
    SaveRender(image, QStringLiteral("services-%1-%2-%3").arg(state, size_name, theme));
  }

  void RenderAll(const QString &state, const Arrange &arrange) {
    for (const QString &theme : { QStringLiteral("adrift-dark"), QStringLiteral("system") }) {
      Render(theme, QStringLiteral("wide"), QSize(1000, 680), state, arrange);
      Render(theme, QStringLiteral("compact"), QSize(380, 680), state, arrange);
    }
  }

  void TearDown() override { QApplication::setStyle(saved_style_); }

  QString saved_style_;
};

TEST_F(StreamingImportRenderTest, NotSignedIn) {
  RenderAll(QStringLiteral("1-not-signed-in"), [](StreamingImportView &, FakeAccount &, FakeSource &) {});
}

TEST_F(StreamingImportRenderTest, SetupNeeded) {
  RenderAll(QStringLiteral("2-setup-needed"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    account.is_configured = false;
    view.session(QStringLiteral("tidal"))->Refresh();
  });
}

TEST_F(StreamingImportRenderTest, WaitingForTheBrowser) {
  RenderAll(QStringLiteral("3-waiting-browser"), [](StreamingImportView &view, FakeAccount &, FakeSource &) { view.account_button()->click(); });
}

TEST_F(StreamingImportRenderTest, BrowserCouldNotBeOpened) {
  RenderAll(QStringLiteral("4-browser-address"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    view.account_button()->click();
    Q_EMIT account.LinkReady(QUrl(QStringLiteral("https://login.example.invalid/authorize?client_id=example&state=abc")), false);
  });
}

TEST_F(StreamingImportRenderTest, LoadingWithProgress) {
  RenderAll(QStringLiteral("5-loading"), [](StreamingImportView &view, FakeAccount &account, FakeSource &source) {
    source.hold = true;
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
  });
}

TEST_F(StreamingImportRenderTest, ListWithSelection) {
  RenderAll(QStringLiteral("6-list"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.current_page() == StreamingImportView::PageList; });
    view.playlist_list()->setCurrentRow(1);
    view.playlist_list()->item(1)->setSelected(true);
  });
}

TEST_F(StreamingImportRenderTest, EmptyAccount) {
  RenderAll(QStringLiteral("7-empty"), [](StreamingImportView &view, FakeAccount &account, FakeSource &source) {
    source.responder = [](const bool, const QString &, const int) { return Ok(PlaylistsPage(0)); };
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.current_page() == StreamingImportView::PageEmpty; });
  });
}

TEST_F(StreamingImportRenderTest, ExpiredLoginRemediation) {
  RenderAll(QStringLiteral("8-expired-login"), [](StreamingImportView &view, FakeAccount &account, FakeSource &source) {
    source.responder = [](const bool, const QString &, const int) { return Http(401); };
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.banner()->isVisible(); });
    view.banner_details_toggle()->click();
  });
}

TEST_F(StreamingImportRenderTest, OfflineWithStaleList) {
  RenderAll(QStringLiteral("9-offline-stale-list"), [](StreamingImportView &view, FakeAccount &account, FakeSource &source) {
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.current_page() == StreamingImportView::PageList; });
    source.responder = [](const bool, const QString &, const int) { return Net(QNetworkReply::HostNotFoundError); };
    view.reload_button()->click();
    WaitFor([&view]() { return view.banner()->isVisible(); });
  });
}

TEST_F(StreamingImportRenderTest, RateLimited) {
  RenderAll(QStringLiteral("10-rate-limited"), [](StreamingImportView &view, FakeAccount &account, FakeSource &source) {
    source.responder = [](const bool, const QString &, const int) {
      PageResult result = Http(429);
      result.retry_after_sec = 45;
      return result;
    };
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.banner()->isVisible(); });
  });
}

TEST_F(StreamingImportRenderTest, ImportedSuccess) {
  RenderAll(QStringLiteral("11-imported"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.current_page() == StreamingImportView::PageList; });
    view.playlist_list()->setCurrentRow(0);
    view.playlist_list()->item(0)->setSelected(true);
    QObject::connect(&view, &StreamingImportView::SnapshotReady, &view, [&view](const QString &, const QString &, const QString &name, const SongList &songs, const int skipped) {
      view.ShowImported(name, false, static_cast<int>(songs.count()), skipped);
    });
    view.import_button()->click();
    WaitFor([&view]() { return view.status_label()->text().contains(QStringLiteral("Imported")); });
  });
}
