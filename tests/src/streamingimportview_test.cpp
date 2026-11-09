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

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
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
  QString setup_help() const override { return help_text; }
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
  void StartSignIn(const quint64 attempt) override {
    ++start_calls;
    last_attempt = attempt;
    BeginAttempt(attempt);
    EndStart(attempt * 1000);  // the fake service's own numbering
  }
  void CancelSignIn() override {
    ++cancel_calls;
    ReleaseAttempt();
  }
  void SignOut() override { signed_in = false; ++signout_calls; }

  // What the real service would do when the browser returns.
  void FinishSuccess(const QString &new_account = QStringLiteral("42")) {
    signed_in = true;
    account = new_account;
    Q_EMIT SignInSucceeded(last_attempt);
  }
  void FinishFailure(const QString &message) { Q_EMIT SignInFailed(last_attempt, message); }
  void LoseSession() {
    signed_in = false;
    Q_EMIT SessionChanged();
  }

  QString help_text = QStringLiteral("Needs a developer client ID.");
  bool signed_in = false;
  bool is_configured = true;
  QString account = QStringLiteral("42");
  QString saved_client_id;
  quint64 last_attempt = 0;
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
  Q_EMIT account.SignInSucceeded(account.last_attempt);
  EXPECT_EQ(finished.count(), 1);
  EXPECT_EQ(session.state(), AccountSession::State::SignedOut);
}

TEST(AccountSessionTest, AStaleSuccessDoesNotEndTheNextAttempt) {
  QObject holder;
  FakeAccount account(&holder);
  AccountSession session(&account);
  QSignalSpy finished(&session, &AccountSession::SignInFinished);
  session.SignIn();
  const quint64 first = account.last_attempt;
  session.Cancel();
  session.SignIn();
  const int second = session.attempt();
  ASSERT_NE(first, account.last_attempt);
  // The first attempt's success arrives while the second one waits and the service holds no session: ignored. Even when the service DOES hold a session
  // (a background refresh finished meanwhile) an event that carries the first attempt's number cannot complete the second sign-in.
  Q_EMIT account.SignInSucceeded(first);
  EXPECT_EQ(session.state(), AccountSession::State::SigningIn);
  account.signed_in = true;
  Q_EMIT account.SignInSucceeded(first);
  Q_EMIT account.SignInFailed(first, QStringLiteral("stale failure"));
  account.signed_in = false;
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
  EXPECT_NE(errors.at(0).indexOf(QStringLiteral("different sign-in attempt")), -1);
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

// ------------------------------------------------------------------------------------------------- real authenticator behind a session

#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>

#include "tidal/tidalaccount.h"
#include "tidal/tidalservice.h"

namespace {

// A token endpoint on localhost that counts what it is asked and can hold its answers, so interleavings can be driven exactly.
class FakeTokenServer : public QObject {
 public:
  struct Answer {
    int status = 200;
    QByteArray body = R"({"token_type":"Bearer","access_token":"test-access","refresh_token":"test-refresh","expires_in":3600,"user":{"countryCode":"NO","userId":7}})";
  };

  FakeTokenServer() {
    EXPECT_TRUE(server_.listen(QHostAddress::LocalHost));
    QObject::connect(&server_, &QTcpServer::newConnection, this, [this]() {
      while (QTcpSocket *socket = server_.nextPendingConnection()) {
        socket->setParent(this);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { Read(socket); });
      }
    });
  }

  QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/token").arg(server_.serverPort())); }

  // Answers are used in order; the last one repeats.
  void Queue(const Answer &answer) { answers_ << answer; }
  void Release() {
    const QList<QTcpSocket*> held = held_;
    held_.clear();
    for (QTcpSocket *socket : held) Respond(socket);
  }

  int requests = 0;
  QStringList bodies;
  bool hold = false;

 private:
  void Read(QTcpSocket *socket) {
    QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
    socket->setProperty("buffer", buffer);
    const int header_end = static_cast<int>(buffer.indexOf("\r\n\r\n"));
    if (header_end < 0 || socket->property("handled").toBool()) return;
    int length = 0;
    const QList<QByteArray> lines = buffer.left(header_end).split('\n');
    for (const QByteArray &line : lines) {
      if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
    }
    if (buffer.size() < header_end + 4 + length) return;
    socket->setProperty("handled", true);
    ++requests;
    bodies << QString::fromUtf8(buffer.mid(header_end + 4, length));
    if (hold) held_ << socket;
    else Respond(socket);
  }

  void Respond(QTcpSocket *socket) {
    const Answer answer = answers_.isEmpty() ? Answer() : (answers_.count() > responded_ ? answers_.at(responded_) : answers_.last());
    ++responded_;
    socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(answer.status) + " X\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(answer.body.size()) + "\r\nConnection: close\r\n\r\n" + answer.body);
    socket->disconnectFromHost();
  }

  QTcpServer server_;
  QList<Answer> answers_;
  QList<QTcpSocket*> held_;
  int responded_ = 0;
};

// What TidalService does with its OAuthenticator, without the service: numbered interactive attempts only, the address only while starting.
class OAuthAccount : public AccountBackend {
 public:
  explicit OAuthAccount(OAuthenticator *oauth, QObject *parent = nullptr) : AccountBackend(parent), oauth_(oauth) {
    QObject::connect(oauth_, &OAuthenticator::InteractiveFinished, this, [this](const quint64 attempt, const bool success, const QString &error) { ReportFinished(attempt, success, error); });
    QObject::connect(oauth_, &OAuthenticator::AuthorizationUrlReady, this, [this](const QUrl &url, const bool opened) { ReportLink(url, opened); });
    QObject::connect(oauth_, &OAuthenticator::CallbackIgnored, this, [this](const QString &reason) { ignored << reason; });
    QObject::connect(oauth_, &OAuthenticator::AuthenticationFinished, this, [this]() { ++legacy_finished; });
  }
  QString key() const override { return QStringLiteral("tidal"); }
  QString service_name() const override { return QStringLiteral("Tidal"); }
  bool authenticated() const override { return oauth_->authenticated(); }
  bool configured() const override { return true; }
  QString account_key() const override { return oauth_->authenticated() ? QString::number(oauth_->user_id()) : QString(); }
  QList<SetupField> setup_fields() const override { return QList<SetupField>(); }
  QString setup_help() const override { return QString(); }
  QString signin_help() const override { return QString(); }
  bool SaveSetup(const QMap<QString, QString> &, QString *) override { return true; }
  void StartSignIn(const quint64 attempt) override {
    BeginAttempt(attempt);
    oauth_->Authenticate();
    EndStart(oauth_->attempt());
  }
  void CancelSignIn() override {
    ReleaseAttempt();
    oauth_->CancelAuthentication();
  }
  void SignOut() override { oauth_->ClearSession(); }

  QStringList ignored;
  int legacy_finished = 0;

 private:
  OAuthenticator *oauth_;
};

class OAuthSessionTest : public ::testing::Test {
 protected:
  OAuthSessionTest() : network(std::make_shared<NetworkAccessManager>()), oauth(network), account(&oauth), session(&account) {
    oauth.set_settings_group(QStringLiteral("adrift_oauth_session_test"));
    oauth.set_type(OAuthenticator::Type::Authorization_Code);
    oauth.set_authorize_url(QUrl(QStringLiteral("https://login.example.invalid/authorize")));
    oauth.set_redirect_url(QUrl(QStringLiteral("tidal://login/auth")));
    oauth.set_access_token_url(server.url());
    oauth.set_client_id(QStringLiteral("test-client"));
    oauth.set_scope(QStringLiteral("r_usr"));
    oauth.set_use_local_redirect_server(false);
    oauth.set_use_pkce(true);
    QObject::connect(&session, &AccountSession::SignInFinished, &session, [this](const bool success, const QString &message, const bool cancelled) {
      outcomes << (cancelled ? QStringLiteral("cancelled") : (success ? QStringLiteral("ok") : QStringLiteral("failed: ") + message));
    });
  }

  QUrl Callback(const QString &state, const QString &extra = QStringLiteral("code=abc")) const {
    return QUrl(QStringLiteral("tidal://login/auth?%1&state=%2").arg(extra, state));
  }
  void Deliver(const QString &state, const QString &extra = QStringLiteral("code=abc")) { oauth.ExternalAuthorizationUrlReceived(Callback(state, extra)); }

  FakeTokenServer server;
  SharedPtr<NetworkAccessManager> network;
  OAuthenticator oauth;
  OAuthAccount account;
  AccountSession session;
  UrlCatcher catcher{ QStringLiteral("https") };
  QStringList outcomes;
};

}  // namespace

TEST_F(OAuthSessionTest, AStaleCallbackOfACancelledAttemptNeverCancelsTheNewOne) {

  session.SignIn();
  const QString state_a = catcher.State();
  session.Cancel();
  session.SignIn();
  const QString state_b = catcher.State();
  ASSERT_NE(state_a, state_b);
  ASSERT_EQ(QStringList() << QStringLiteral("cancelled"), outcomes);

  // The browser of attempt A answers late. B is still waiting.
  Deliver(state_a);
  Deliver(state_a, QStringLiteral("error=access_denied"));
  EXPECT_EQ(AccountSession::State::SigningIn, session.state());
  EXPECT_TRUE(oauth.authentication_pending());
  EXPECT_EQ(QStringList() << QStringLiteral("cancelled"), outcomes);
  EXPECT_EQ(0, server.requests);
  EXPECT_EQ(2, account.ignored.count());

  // B's own callback still works.
  Deliver(state_b);
  WaitFor([this]() { return session.state() == AccountSession::State::SignedIn; }, 5000);
  EXPECT_EQ(AccountSession::State::SignedIn, session.state());
  EXPECT_EQ((QStringList() << QStringLiteral("cancelled") << QStringLiteral("ok")), outcomes);
  EXPECT_EQ(1, server.requests);

}

TEST_F(OAuthSessionTest, CallbacksThatCannotBeAttributedAreIgnoredEvenWhenTheyCarryAProviderError) {

  session.SignIn();
  const QString state = catcher.State();

  oauth.ExternalAuthorizationUrlReceived(QUrl(QStringLiteral("tidal://login/auth?code=abc&state=wrong")));
  oauth.ExternalAuthorizationUrlReceived(QUrl(QStringLiteral("tidal://login/auth?code=abc&state=")));
  oauth.ExternalAuthorizationUrlReceived(QUrl(QStringLiteral("tidal://login/auth?code=abc")));
  oauth.ExternalAuthorizationUrlReceived(QUrl(QStringLiteral("tidal://login/auth?error=access_denied&state=wrong")));
  oauth.ExternalAuthorizationUrlReceived(QUrl(QStringLiteral("tidal://login/auth?error=server_error&error_description=boom")));
  oauth.ExternalAuthorizationUrlReceived(QUrl(QStringLiteral("tidal://login/auth")));
  EXPECT_EQ(6, account.ignored.count());
  // Nothing was echoed: not the expected state, not the one the reply sent.
  for (const QString &reason : std::as_const(account.ignored)) {
    EXPECT_EQ(-1, reason.indexOf(state));
    EXPECT_EQ(-1, reason.indexOf(QStringLiteral("wrong")));
  }
  EXPECT_EQ(AccountSession::State::SigningIn, session.state());
  EXPECT_TRUE(outcomes.isEmpty());
  EXPECT_EQ(0, server.requests);
  EXPECT_EQ(0, account.legacy_finished);  // ignoring is not a sign-in result for anybody

  // A provider error that carries the right state is the answer to this sign-in: it ends it, with the provider's reason.
  Deliver(state, QStringLiteral("error=access_denied&error_description=The%20user%20said%20no"));
  EXPECT_EQ(AccountSession::State::SignedOut, session.state());
  ASSERT_EQ(1, outcomes.count());
  EXPECT_EQ(QStringLiteral("failed: The user said no"), outcomes.at(0));
  EXPECT_EQ(0, server.requests);

}

TEST_F(OAuthSessionTest, ARepeatedCallbackProducesOnlyOneExchangeAndDoesNotFailIt) {

  server.hold = true;
  session.SignIn();
  const QString state = catcher.State();
  Deliver(state);
  WaitFor([this]() { return server.requests == 1; }, 5000);
  ASSERT_EQ(1, server.requests);

  // The browser (or a second desktop activation) repeats the callback while the code is being exchanged.
  Deliver(state);
  Deliver(state);
  QCoreApplication::processEvents();
  EXPECT_EQ(AccountSession::State::SigningIn, session.state());
  EXPECT_TRUE(outcomes.isEmpty());
  EXPECT_EQ(1, server.requests);
  EXPECT_EQ(2, account.ignored.count());

  server.hold = false;
  server.Release();
  WaitFor([this]() { return session.state() == AccountSession::State::SignedIn; }, 5000);
  EXPECT_EQ(AccountSession::State::SignedIn, session.state());
  EXPECT_EQ(QStringList() << QStringLiteral("ok"), outcomes);
  EXPECT_EQ(1, server.requests);

}

TEST_F(OAuthSessionTest, CancellingDuringTheTokenExchangeStoresNoSession) {

  server.hold = true;
  session.SignIn();
  Deliver(catcher.State());
  WaitFor([this]() { return server.requests == 1; }, 5000);
  ASSERT_EQ(1, server.requests);

  session.Cancel();
  EXPECT_EQ(AccountSession::State::SignedOut, session.state());
  // The service answers anyway; nobody is listening for it any more.
  server.hold = false;
  server.Release();
  QTest::qWait(150);
  EXPECT_FALSE(oauth.authenticated());
  EXPECT_EQ(AccountSession::State::SignedOut, session.state());
  EXPECT_EQ(QStringList() << QStringLiteral("cancelled"), outcomes);

}

TEST_F(OAuthSessionTest, ANewAttemptAbandonsTheExchangeOfTheEarlierOne) {

  server.hold = true;
  session.SignIn();
  Deliver(catcher.State());
  WaitFor([this]() { return server.requests == 1; }, 5000);
  session.Cancel();
  session.SignIn();  // B
  ASSERT_EQ(AccountSession::State::SigningIn, session.state());

  server.hold = false;
  server.Release();  // A's answer, late
  QTest::qWait(150);
  EXPECT_EQ(AccountSession::State::SigningIn, session.state());
  EXPECT_FALSE(oauth.authenticated());
  EXPECT_EQ(QStringList() << QStringLiteral("cancelled"), outcomes);

}

TEST_F(OAuthSessionTest, ABackgroundRefreshNeverCompletesOrFailsAManualReconnect) {

  // First a normal sign-in, which leaves a session with a refresh token.
  session.SignIn();
  Deliver(catcher.State());
  WaitFor([this]() { return session.state() == AccountSession::State::SignedIn; }, 5000);
  ASSERT_EQ(AccountSession::State::SignedIn, session.state());
  ASSERT_EQ(1, server.requests);

  // The user starts a manual reconnect and the browser has not answered yet.
  session.SignIn();
  ASSERT_EQ(AccountSession::State::SigningIn, session.state());
  const QString state = catcher.State();
  outcomes.clear();

  // Meanwhile a background refresh succeeds. The service holds a session and reports AuthenticationFinished(true), but that is not this sign-in.
  const int legacy_before = account.legacy_finished;
  ASSERT_TRUE(oauth.RefreshAccessToken());
  WaitFor([this]() { return server.requests == 2; }, 5000);
  WaitFor([this, legacy_before]() { return account.legacy_finished > legacy_before; }, 5000);
  EXPECT_GT(account.legacy_finished, legacy_before);
  EXPECT_TRUE(oauth.authenticated());
  EXPECT_EQ(AccountSession::State::SigningIn, session.state());
  EXPECT_TRUE(outcomes.isEmpty());

  // A refresh that the service refuses for good clears the stored session and reports a failure to legacy listeners, but it is not a failed sign-in either.
  FakeTokenServer::Answer refused;
  refused.status = 400;
  refused.body = R"({"error":"invalid_grant","error_description":"expired"})";
  server.Queue(FakeTokenServer::Answer());
  server.Queue(FakeTokenServer::Answer());
  server.Queue(refused);
  server.Queue(FakeTokenServer::Answer());  // and the answer to the reconnect's own code exchange
  ASSERT_TRUE(oauth.RefreshAccessToken());
  WaitFor([this]() { return server.requests == 3; }, 5000);
  QTest::qWait(150);
  EXPECT_EQ(AccountSession::State::SigningIn, session.state());
  EXPECT_TRUE(outcomes.isEmpty());

  // The browser finally answers the reconnect: that completes it.
  Deliver(state);
  WaitFor([this]() { return !outcomes.isEmpty(); }, 5000);
  ASSERT_EQ(1, outcomes.count());
  EXPECT_EQ(QStringLiteral("ok"), outcomes.at(0));

}

TEST_F(OAuthSessionTest, TheBrowserAddressIsAnnouncedForTheAttemptThatStartedIt) {

  QSignalSpy link(&session, &AccountSession::LinkChanged);
  session.SignIn();
  EXPECT_EQ(catcher.urls.last(), session.link());
  EXPECT_TRUE(session.link_opened());
  EXPECT_GE(link.count(), 1);
  session.Cancel();
  EXPECT_TRUE(session.link().isEmpty());

}

TEST(OAuthLegacyConsumerTest, AConsumerThatDoesNotKnowAboutAttemptsStillSeesAFailureForABadCallback) {

  // Spotify, ListenBrainz and the others listen to AuthenticationFinished only. They keep the visible failure they always had.
  SharedPtr<NetworkAccessManager> network = std::make_shared<NetworkAccessManager>();
  OAuthenticator oauth(network);
  oauth.set_settings_group(QStringLiteral("adrift_oauth_legacy_test"));
  oauth.set_type(OAuthenticator::Type::Authorization_Code);
  oauth.set_authorize_url(QUrl(QStringLiteral("https://login.example.invalid/authorize")));
  oauth.set_redirect_url(QUrl(QStringLiteral("app://cb")));
  oauth.set_access_token_url(QUrl(QStringLiteral("http://127.0.0.1:1/token")));
  oauth.set_client_id(QStringLiteral("c"));
  oauth.set_use_local_redirect_server(false);
  UrlCatcher catcher(QStringLiteral("https"));
  QStringList errors;
  QObject::connect(&oauth, &OAuthenticator::AuthenticationFinished, &oauth, [&errors](const bool success, const QString &error, const bool) {
    if (!success) errors << error;
  });
  oauth.Authenticate();
  oauth.ExternalAuthorizationUrlReceived(QUrl(QStringLiteral("app://cb?code=x&state=nope")));
  ASSERT_EQ(1, errors.count());
  EXPECT_FALSE(errors.at(0).isEmpty());
  // And with a browser address handler nobody connected, the modal fallback is what shows the address: nothing is emitted for it.
  EXPECT_EQ(1, catcher.urls.count());

}

TEST(TidalAccountBuildTest, CompiledApplicationConfigurationIsUsable) {

  if (!TidalService::HasCompiledCredentials()) GTEST_SKIP() << "Custom build without release application configuration";
  // Exercise the production decoding path without printing application configuration.
  const QString client_id = TidalService::CompiledClientId();
  EXPECT_FALSE(client_id.isEmpty());
  EXPECT_FALSE(client_id.startsWith(QStringLiteral("ENC:")));
  EXPECT_FALSE(client_id.contains(QChar::Space));

}

TEST(TidalAccountTextTest, SetupGuidanceIsTruthfulAboutTheCompatibleClient) {

  const QString help = TidalAccount::SetupHelpText();
  // It says what this integration is and that dashboard client IDs do not work with it.
  EXPECT_NE(-1, help.indexOf(QStringLiteral("not compatible")));
  EXPECT_NE(-1, help.indexOf(QStringLiteral("older")));
  EXPECT_NE(-1, help.indexOf(QStringLiteral("works with Strawberry")));
  // It never sends the user down the registration path that does not work, and never offers to take credentials from other apps.
  EXPECT_EQ(-1, help.indexOf(QStringLiteral("Create an app")));
  EXPECT_EQ(-1, help.indexOf(QStringLiteral("add exactly this redirect")));
  EXPECT_NE(-1, help.indexOf(QStringLiteral("does not take one from other apps")));

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
    Q_EMIT account.LinkReady(account.last_attempt, QUrl(QStringLiteral("https://login.example.invalid/authorize?client_id=example&state=abc")), false);
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

// ------------------------------------------------------------------------------------------------- the combined shell

#include "listeningroom/coverartwidget.h"
#include "listeningroom/libraryparts.h"
#include "listeningroom/libraryview.h"
#include "listeningroom/listeningroom.h"
#include "listeningroom/roommotion.h"
#include "listeningroom/transportbar.h"
#include "library_fixture.h"
#include "listeningroom_fixture.h"

// The production ListeningRoom with the production Library (synthetic collection) AND the production StreamingImportView hosted on its Streaming page,
// rendered together at wide and compact sizes. This is the shell as MainWindow assembles it, without the classic window.
class CombinedShellRenderTest : public StreamingImportRenderTest {
 protected:
  void RenderShell(const QString &theme, const QString &size_name, const QSize &size, const QString &state, const ListeningRoom::View page, const Arrange &arrange) {

    QObject holder;
    FakeAccount *account = new FakeAccount(&holder);
    FakeSource *source = new FakeSource;
    source->setParent(&holder);
    source->responder = HappyPath;
    FakeSource *other_source = new FakeSource;
    other_source->setParent(&holder);
    other_source->responder = HappyPath;

    RoomFixture::QueueFixture queue(14);
    FakeLibrarySource library(36);
    ListeningRoom room;
    if (theme == QLatin1String("adrift-dark")) room.setPalette(ThemePalette::BuildPalette(QApplication::style()->standardPalette(), ThemePalette::AdriftDarkColors()));

    StreamingImportView *view = new StreamingImportView;
    StreamingImportView::ServiceEntry tidal;
    tidal.name = QStringLiteral("Tidal");
    tidal.url_scheme = QStringLiteral("tidal");
    tidal.song_source = Song::Source::Tidal;
    tidal.page_source = source;
    tidal.account = account;
    view->AddService(tidal);
    StreamingImportView::ServiceEntry qobuz;
    qobuz.name = QStringLiteral("Qobuz");
    qobuz.url_scheme = QStringLiteral("qobuz");
    qobuz.song_source = Song::Source::Qobuz;
    qobuz.page_source = other_source;
    qobuz.account = new FakeQobuzAccount(&holder);
    view->AddService(qobuz);
    view->SelectService(QStringLiteral("tidal"));

    RoomServiceCard tidal_card;
    tidal_card.key = QStringLiteral("tidal");
    tidal_card.name = QStringLiteral("Tidal");
    RoomServiceCard qobuz_card;
    qobuz_card.key = QStringLiteral("qobuz");
    qobuz_card.name = QStringLiteral("Qobuz");
    room.SetStreamingServices({ tidal_card, qobuz_card });
    room.SetStreamingWidget(view);
    room.library()->SetSource(&library);

    RoomTrack track;
    track.title = QStringLiteral("A Quiet Engine");
    track.artist = QStringLiteral("The Paper Lanterns");
    track.album = QStringLiteral("Night Ferry");
    room.SetQueuePlaylist(queue.playlist(), QStringLiteral("Evening Drift"));
    queue.SetCurrent(3);
    room.SetVolume(62);
    room.SetPlaybackState(TransportBar::State::Playing);
    room.SetNowPlaying(track);
    room.SetCover(RoomFixture::Cover(0));
    room.SetCanSeek(true);
    room.SetPosition(94000, 281000);

    room.resize(size);
    room.show();
    room.SetView(page);
    Pump();
    if (page == ListeningRoom::View::Library) {
      QSignalSpy settled(room.library(), &LibraryView::AlbumsSettled);
      if (!room.library()->loaded()) settled.wait(5000);
    }
    arrange(*view, *account, *source);
    Pump();

    // Settled: the indicator and the record are at rest and the covers on screen have arrived.
    for (int i = 0; i < 300; ++i) {
      QCoreApplication::processEvents();
      const bool rest = !room.nav_indicator_gliding() && !room.cover()->fading() && !room.cover()->settle_pending() && (room.cover()->record_amount() == 0.0 || room.cover()->record_amount() == 1.0) && room.library()->album_model()->in_flight() == 0;
      if (rest && i > 4) {
        QTest::qWait(40);
        if (room.library()->album_model()->in_flight() == 0) break;
      }
      QTest::qWait(10);
    }
    if (QWidget *focused = QApplication::focusWidget()) focused->clearFocus();
    QCoreApplication::processEvents();

    EXPECT_EQ(size, room.size()) << state.toStdString() << " " << size_name.toStdString();
    EXPECT_TRUE(FitsHorizontally(*view)) << state.toStdString() << " " << size_name.toStdString();
    // One shell: one transport, and the Streaming page is the hosted widget.
    int transports = 0;
    for (TransportBar *bar : room.findChildren<TransportBar*>()) transports += bar->isVisible() ? 1 : 0;
    EXPECT_EQ(1, transports);
    if (page == ListeningRoom::View::Streaming) {
      EXPECT_EQ(view, room.streaming_widget());
      EXPECT_TRUE(view->isVisible());
      EXPECT_TRUE(room.page(ListeningRoom::View::Streaming)->isAncestorOf(view));
    }
    const QImage image = room.grab().toImage();
    EXPECT_TRUE(NotBlank(image)) << state.toStdString();
    SaveRender(image, QStringLiteral("combined-%1-%2-%3").arg(state, size_name, theme));

  }

  void RenderStreamingStates(const QString &state, const Arrange &arrange) {
    for (const QString &theme : { QStringLiteral("adrift-dark"), QStringLiteral("system") }) {
      RenderShell(theme, QStringLiteral("wide"), QSize(1440, 900), state, ListeningRoom::View::Streaming, arrange);
      RenderShell(theme, QStringLiteral("compact"), QSize(1000, 700), state, ListeningRoom::View::Streaming, arrange);
    }
  }
};

TEST_F(CombinedShellRenderTest, LibraryPopulatedInTheCombinedShell) {
  for (const QString &theme : { QStringLiteral("adrift-dark"), QStringLiteral("system") }) {
    RenderShell(theme, QStringLiteral("wide"), QSize(1440, 900), QStringLiteral("library"), ListeningRoom::View::Library, [](StreamingImportView &, FakeAccount &, FakeSource &) {});
    RenderShell(theme, QStringLiteral("compact"), QSize(1000, 700), QStringLiteral("library"), ListeningRoom::View::Library, [](StreamingImportView &, FakeAccount &, FakeSource &) {});
  }
}

TEST_F(CombinedShellRenderTest, StreamingNotSignedIn) {
  RenderStreamingStates(QStringLiteral("streaming-1-not-signed-in"), [](StreamingImportView &, FakeAccount &, FakeSource &) {});
}

TEST_F(CombinedShellRenderTest, StreamingSetupNeeded) {
  RenderStreamingStates(QStringLiteral("streaming-2-setup-needed"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    account.is_configured = false;
    view.session(QStringLiteral("tidal"))->Refresh();
  });
}

TEST_F(CombinedShellRenderTest, StreamingSetupNeededWithTheRealTidalGuidance) {
  RenderStreamingStates(QStringLiteral("streaming-2b-setup-tidal-text"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    account.is_configured = false;
    account.help_text = TidalAccount::SetupHelpText();
    view.session(QStringLiteral("tidal"))->Refresh();
  });
}

TEST_F(CombinedShellRenderTest, StreamingWaitingForTheBrowserAndAddress) {
  RenderStreamingStates(QStringLiteral("streaming-3-waiting-browser"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    view.account_button()->click();
    Q_EMIT account.LinkReady(account.last_attempt, QUrl(QStringLiteral("https://login.example.invalid/authorize?client_id=example&state=abc")), false);
  });
}

TEST_F(CombinedShellRenderTest, StreamingPlaylistList) {
  RenderStreamingStates(QStringLiteral("streaming-4-list"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.current_page() == StreamingImportView::PageList; });
    view.playlist_list()->setCurrentRow(1);
    view.playlist_list()->item(1)->setSelected(true);
  });
}

TEST_F(CombinedShellRenderTest, StreamingExpiredSession) {
  RenderStreamingStates(QStringLiteral("streaming-5-expired"), [](StreamingImportView &view, FakeAccount &account, FakeSource &source) {
    source.responder = [](const bool, const QString &, const int) { return Http(401); };
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.banner()->isVisible(); });
    view.banner_details_toggle()->click();
  });
}

TEST_F(CombinedShellRenderTest, StreamingOfflineStaleListAndRateLimit) {
  RenderStreamingStates(QStringLiteral("streaming-6-offline-stale"), [](StreamingImportView &view, FakeAccount &account, FakeSource &source) {
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.Activate();
    WaitFor([&view]() { return view.current_page() == StreamingImportView::PageList; });
    source.responder = [](const bool, const QString &, const int) { return Net(QNetworkReply::HostNotFoundError); };
    view.reload_button()->click();
    WaitFor([&view]() { return view.banner()->isVisible(); });
  });
  RenderStreamingStates(QStringLiteral("streaming-7-rate-limited"), [](StreamingImportView &view, FakeAccount &account, FakeSource &source) {
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

TEST_F(CombinedShellRenderTest, StreamingImported) {
  RenderStreamingStates(QStringLiteral("streaming-8-imported"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
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

// ---------------------------------------------------------------------------------------------- automatic refresh of snapshots

class AutoRefreshTest : public StreamingImportViewTest {
 protected:
  AutoRefreshTest() {
    // The host: a snapshot exists for pl-1 and pl-3 (pl-2 was never imported), nothing is playing, every replacement succeeds.
    view.SetSnapshotExistsFunction([this](const QString &identity) { return existing.contains(identity); });
    view.SetSnapshotPlayingFunction([this](const QString &identity) { return playing.contains(identity); });
    QObject::connect(&view, &StreamingImportView::SnapshotReady, &view, [this](const QString &, const QString &, const QString &name, const SongList &songs, const int skipped) {
      view.ShowImported(name, true, static_cast<int>(songs.count()), skipped);
    });
    QObject::connect(&view, &StreamingImportView::AutoRefreshChanged, &view, [this](const QString &, const bool enabled, const QString &account_key) {
      stored_enabled = enabled;
      stored_account = account_key;
      ++stored_changes;
    });
    existing << QStringLiteral("tidal:pl-1") << QStringLiteral("tidal:pl-3");
    source->responder = [this](const bool tracks, const QString &playlist_id, const int) {
      if (!tracks) {
        ++list_reads;
        if (on_list) on_list();
        return Ok(PlaylistsPage(3));
      }
      track_order << playlist_id;
      overlapping |= view.importer(QStringLiteral("tidal"))->state() != Importer::State::LoadingTracks;
      if (on_tracks) on_tracks(playlist_id);
      if (fail_tracks) return fail_tracks();
      return Ok(TracksPage(QList<int>() << 1 << 2));
    };
  }

  ~AutoRefreshTest() override {
    // The host functions point into this fixture, which is destroyed before the view.
    view.SetSnapshotExistsFunction(nullptr);
    view.SetSnapshotPlayingFunction(nullptr);
  }

  void Enable() {
    SignInAndLoad();
    list_reads = 0;
    source->requests = 0;
    source->track_requests = 0;
    view.auto_check()->click();
    ASSERT_TRUE(view.auto_check()->isChecked());
  }

  bool WaitRunDone() { return WaitFor([this]() { return !view.auto_refresh_running(); }); }

  QSet<QString> existing;
  QSet<QString> playing;
  QStringList track_order;
  std::function<void(const QString &)> on_tracks;
  std::function<void()> on_list;
  std::function<PageResult()> fail_tracks;
  int list_reads = 0;
  bool overlapping = false;
  bool stored_enabled = false;
  QString stored_account;
  int stored_changes = 0;
};

TEST_F(AutoRefreshTest, IsOffByDefaultAndNothingIsReadWithoutOptIn) {
  SignInAndLoad();
  EXPECT_FALSE(view.auto_check()->isChecked());
  EXPECT_FALSE(view.auto_refresh_enabled(QStringLiteral("tidal")));
  EXPECT_TRUE(view.auto_status_label()->text().startsWith(QStringLiteral("Off.")));
  const int before = source->requests;
  view.SetAutoRefreshInterval(20);
  view.RunAutoRefreshIfDue();
  view.Activate();
  WaitFor([]() { return false; }, 150);
  EXPECT_EQ(source->requests, before);
  EXPECT_TRUE(snapshots.isEmpty());
  EXPECT_EQ(stored_changes, 0);
}

TEST_F(AutoRefreshTest, TurningItOnIsStoredWithTheAccountAndWaitsForTheNextActivation) {
  Enable();
  EXPECT_TRUE(stored_enabled);
  EXPECT_EQ(stored_account, QStringLiteral("42"));
  EXPECT_FALSE(view.auto_refresh_running());
  EXPECT_EQ(source->requests, 0);  // not in the middle of the click
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("read-only")), -1);
  // Concise checkbox, the details wrap in the status line.
  EXPECT_LT(view.auto_check()->text().size(), 45);
}

TEST_F(AutoRefreshTest, RefreshesOnlyExistingSnapshotsOneAfterTheOther) {
  Enable();
  const QString manual_status = view.status_label()->text();
  view.RunAutoRefreshIfDue();
  EXPECT_TRUE(view.auto_refresh_running());
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(list_reads, 1);
  EXPECT_EQ(track_order, QStringList() << QStringLiteral("pl-1") << QStringLiteral("pl-3"));  // pl-2 was never imported: never read
  EXPECT_EQ(snapshots, QStringList() << QStringLiteral("tidal:pl-1") << QStringLiteral("tidal:pl-3"));
  EXPECT_FALSE(overlapping);
  EXPECT_EQ(manual_status, view.status_label()->text());
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("Updated 2 imported playlists")), -1);
  // Done: the next run is a whole interval away.
  const int before = source->requests;
  view.RunAutoRefreshIfDue();
  Pump();
  EXPECT_EQ(source->requests, before);
  // The user's own status line and the "Open" button were not used by the automatic run.
  for (QPushButton *button : view.findChildren<QPushButton*>()) {
    if (button->text() == QStringLiteral("Open imported playlist")) EXPECT_FALSE(button->isVisible());
  }
}

TEST_F(AutoRefreshTest, ARunStartsAgainAfterTheInterval) {
  view.SetAutoRefreshInterval(40);
  Enable();
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  ASSERT_TRUE(WaitFor([this]() { return track_order.count() >= 4; }));  // the timer started the second run by itself
}

TEST_F(AutoRefreshTest, NeverOverlapsAManualReadOrAnotherRun) {
  SignInAndLoad();
  SelectFirst();
  view.auto_check()->click();
  source->hold = true;
  view.import_button()->click();  // a manual import is reading
  ASSERT_TRUE(view.importer(QStringLiteral("tidal"))->busy());
  const int before = source->requests;
  view.RunAutoRefreshIfDue();
  EXPECT_FALSE(view.auto_refresh_running());
  EXPECT_EQ(source->requests, before);
  view.cancel_button()->click();
  source->hold = false;

  // During a run, nothing manual can start and a second run is not started.
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(view.auto_refresh_running());
  EXPECT_FALSE(view.import_button()->isEnabled());
  EXPECT_FALSE(view.reload_button()->isEnabled());
  const int tracks_before = source->track_requests;
  view.import_button()->click();
  view.reload_button()->click();
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(source->track_requests - tracks_before, 2);  // exactly the two snapshots
  EXPECT_FALSE(overlapping);
}

TEST_F(AutoRefreshTest, CancelStopsBeforeTheNextPlaylistAndKeepsTheOpportunityForTheNextInterval) {
  Enable();
  source->hold = true;
  view.RunAutoRefreshIfDue();
  Pump();
  ASSERT_TRUE(view.auto_refresh_running());
  ASSERT_TRUE(view.cancel_button()->isVisible());
  view.cancel_button()->click();
  EXPECT_FALSE(view.auto_refresh_running());
  EXPECT_FALSE(view.importer(QStringLiteral("tidal"))->busy());
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("Cancelled")), -1);
  EXPECT_TRUE(view.auto_refresh_enabled(QStringLiteral("tidal")));  // cancelling a run is not disabling the feature
  source->hold = false;
  const int before = source->requests;
  view.RunAutoRefreshIfDue();
  Pump();
  EXPECT_EQ(source->requests, before);
  EXPECT_TRUE(snapshots.isEmpty());
}

TEST_F(AutoRefreshTest, CancellingBetweenTwoPlaylistsReadsNoFurtherOne) {
  Enable();
  on_tracks = [this](const QString &id) {
    if (id == QStringLiteral("pl-1")) QMetaObject::invokeMethod(view.cancel_button(), &QPushButton::click, Qt::QueuedConnection);
  };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(track_order, QStringList() << QStringLiteral("pl-1"));
  EXPECT_EQ(snapshots, QStringList() << QStringLiteral("tidal:pl-1"));  // the one that was already read is kept, the next one is not read
  EXPECT_FALSE(view.auto_refresh_running());
}

TEST_F(AutoRefreshTest, DisablingStopsARunningRefreshAndIsStored) {
  Enable();
  source->hold = true;
  view.RunAutoRefreshIfDue();
  Pump();
  ASSERT_TRUE(view.auto_refresh_running());
  view.auto_check()->click();
  EXPECT_FALSE(view.auto_refresh_running());
  EXPECT_FALSE(view.auto_refresh_enabled(QStringLiteral("tidal")));
  EXPECT_FALSE(stored_enabled);
  EXPECT_TRUE(stored_account.isEmpty());
  EXPECT_TRUE(view.auto_status_label()->text().startsWith(QStringLiteral("Off.")));
}

TEST_F(AutoRefreshTest, AFailureStopsTheRunPausesAndOffersResume) {
  Enable();
  fail_tracks = []() { return Http(500, QStringLiteral("boom")); };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(track_order.count(), 1);  // stopped at the first failure
  EXPECT_TRUE(snapshots.isEmpty());   // the last snapshots are untouched
  EXPECT_TRUE(view.auto_refresh_paused(QStringLiteral("tidal")));
  EXPECT_TRUE(view.banner()->isVisible());
  EXPECT_FALSE(view.banner_retry()->isVisible());  // Retry would repeat a manual read; the automatic refresh has its own Resume
  EXPECT_TRUE(view.banner_message()->text().contains(QStringLiteral("paused")));
  EXPECT_TRUE(view.auto_resume_button()->isVisible());
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("Paused")), -1);
  const int before = source->requests;
  view.SetAutoRefreshInterval(10);
  WaitFor([]() { return false; }, 80);
  view.RunAutoRefreshIfDue();
  view.Activate();
  Pump();
  EXPECT_EQ(source->requests, before);  // no loop of retries

  fail_tracks = nullptr;
  view.auto_resume_button()->click();
  ASSERT_TRUE(WaitFor([this]() { return snapshots.count() == 2; }));
  ASSERT_TRUE(WaitRunDone());
  EXPECT_FALSE(view.auto_refresh_paused(QStringLiteral("tidal")));
  EXPECT_FALSE(view.auto_resume_button()->isVisible());
}

TEST_F(AutoRefreshTest, ExpiredLoginPausesTheRunAndKeepsTheOptIn) {
  Enable();
  fail_tracks = []() { return Http(401); };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_TRUE(view.auto_refresh_paused(QStringLiteral("tidal")));
  EXPECT_TRUE(view.auto_refresh_enabled(QStringLiteral("tidal")));  // the opt-in stays; the pause is the visible state
  EXPECT_TRUE(snapshots.isEmpty());
}

TEST_F(AutoRefreshTest, RateLimitIsHonoredAndDoesNotPauseForever) {
  view.SetAutoRefreshInterval(30);
  Enable();
  fail_tracks = []() {
    PageResult result = Http(429);
    result.retry_after_sec = 3600;
    return result;
  };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_FALSE(view.auto_refresh_paused(QStringLiteral("tidal")));
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("asked Adrift to wait")), -1);
  const int before = source->requests;
  WaitFor([]() { return false; }, 250);  // several intervals pass, the wait is an hour
  view.RunAutoRefreshIfDue();
  view.Activate();
  Pump();
  EXPECT_EQ(source->requests, before);
  EXPECT_TRUE(snapshots.isEmpty());
}

TEST_F(AutoRefreshTest, ThePlayingSnapshotIsNotReadWhenItAlreadyPlays) {
  Enable();
  playing << QStringLiteral("tidal:pl-1");
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(track_order, QStringList() << QStringLiteral("pl-3"));
  EXPECT_EQ(snapshots, QStringList() << QStringLiteral("tidal:pl-3"));
  const QString text = view.auto_status_label()->text();
  EXPECT_NE(text.indexOf(QStringLiteral("Playing now, so not changed: Mix 1")), -1);
}

TEST_F(AutoRefreshTest, ASnapshotThatStartsPlayingWhileItIsReadIsDeferredAtCommit) {
  Enable();
  on_tracks = [this](const QString &id) {
    if (id == QStringLiteral("pl-1")) playing << QStringLiteral("tidal:pl-1");  // playback started during the read
  };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(track_order.count(), 2);                                   // it was read...
  EXPECT_EQ(snapshots, QStringList() << QStringLiteral("tidal:pl-3"));  // ...but not committed
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("Mix 1")), -1);

  // Later, when it no longer plays, the next run refreshes it and the note goes away.
  playing.clear();
  on_tracks = nullptr;
  snapshots.clear();
  view.SetAutoRefreshInterval(20);
  ASSERT_TRUE(WaitFor([this]() { return snapshots.contains(QStringLiteral("tidal:pl-1")); }));
  ASSERT_TRUE(WaitRunDone());
  EXPECT_EQ(view.auto_status_label()->text().indexOf(QStringLiteral("Playing now")), -1);
}

TEST_F(AutoRefreshTest, ASnapshotDeletedWhileItIsReadIsNotCreatedAgain) {
  Enable();
  on_tracks = [this](const QString &id) {
    if (id == QStringLiteral("pl-1")) existing.remove(QStringLiteral("tidal:pl-1"));
  };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(snapshots, QStringList() << QStringLiteral("tidal:pl-3"));
}

TEST_F(AutoRefreshTest, AHostThatCannotStoreItPausesAndSaysSo) {
  Enable();
  QObject::disconnect(&view, &StreamingImportView::SnapshotReady, &view, nullptr);
  int answers = 0;
  QObject::connect(&view, &StreamingImportView::SnapshotReady, &view, [this, &answers](const QString &, const QString &, const QString &name, const SongList &, const int) {
    ++answers;
    view.ShowImportFailed(name, QStringLiteral("disk"));
  });
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(answers, 1);
  EXPECT_TRUE(view.auto_refresh_paused(QStringLiteral("tidal")));
  EXPECT_TRUE(view.banner()->isVisible());
  EXPECT_NE(view.banner_message()->text().indexOf(QStringLiteral("last snapshot")), -1);
}

TEST_F(AutoRefreshTest, AnotherAccountSigningInDropsTheOptInAndReadsNothing) {
  Enable();
  EXPECT_TRUE(stored_enabled);
  account->account = QStringLiteral("99");  // someone else is signed in now
  view.Activate();
  Pump();
  EXPECT_FALSE(view.auto_refresh_enabled(QStringLiteral("tidal")));
  EXPECT_FALSE(view.auto_check()->isChecked());
  EXPECT_FALSE(stored_enabled);
  EXPECT_TRUE(stored_account.isEmpty());
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("different account")), -1);
  const int tracks_before = source->track_requests;
  view.RunAutoRefreshIfDue();
  Pump();
  EXPECT_EQ(source->track_requests, tracks_before);
  EXPECT_TRUE(snapshots.isEmpty());
}

TEST_F(AutoRefreshTest, ARestoredOptInOnlyAppliesToTheSameAccount) {
  view.RestoreAutoRefresh(QStringLiteral("tidal"), QStringLiteral("7"));  // stored for account 7, account 42 signs in
  SignInAndLoad();
  list_reads = 0;
  view.RunAutoRefreshIfDue();
  Pump();
  EXPECT_FALSE(view.auto_refresh_enabled(QStringLiteral("tidal")));
  EXPECT_EQ(list_reads, 0);
  EXPECT_TRUE(snapshots.isEmpty());
  EXPECT_FALSE(stored_enabled);
}

TEST_F(AutoRefreshTest, ARestoredOptInOfTheSameAccountRunsOnActivation) {
  view.RestoreAutoRefresh(QStringLiteral("tidal"), QStringLiteral("42"));
  EXPECT_TRUE(view.auto_check()->isChecked());
  SignInAndLoad();  // activation: the initial read, then the automatic run
  ASSERT_TRUE(WaitFor([this]() { return snapshots.count() == 2; }));
  ASSERT_TRUE(WaitRunDone());
  EXPECT_TRUE(view.auto_refresh_enabled(QStringLiteral("tidal")));
}

TEST_F(AutoRefreshTest, LosingTheSessionStopsARunningRefreshAndKeepsTheOptInForTheSameAccount) {
  Enable();
  source->hold = true;
  view.RunAutoRefreshIfDue();
  Pump();
  ASSERT_TRUE(view.auto_refresh_running());
  EXPECT_FALSE(view.signout_button()->isEnabled());  // signing out by hand is not possible while reading
  account->LoseSession();
  Pump();
  EXPECT_FALSE(view.auto_refresh_running());
  EXPECT_FALSE(view.importer(QStringLiteral("tidal"))->busy());
  EXPECT_TRUE(view.auto_refresh_enabled(QStringLiteral("tidal")));
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("sign in")), -1);
  source->hold = false;
  const int before = source->requests;
  view.RunAutoRefreshIfDue();
  Pump();
  EXPECT_EQ(source->requests, before);  // signed out: nothing is read

  // Signing in as somebody else does not inherit it.
  account->signed_in = true;
  account->account = QStringLiteral("99");
  view.session(QStringLiteral("tidal"))->Refresh();
  view.RunAutoRefreshIfDue();
  Pump();
  EXPECT_FALSE(view.auto_refresh_enabled(QStringLiteral("tidal")));
}

TEST_F(AutoRefreshTest, SwitchingTheServiceViewDuringARunIsRefusedAndRunGoesOn) {
  Enable();
  source->hold = true;
  view.RunAutoRefreshIfDue();
  Pump();
  ASSERT_TRUE(view.auto_refresh_running());
  view.SelectService(QStringLiteral("qobuz"));  // unknown here, and ignored while reading
  EXPECT_EQ(view.current_service_key(), QStringLiteral("tidal"));
  view.cancel_button()->click();
  EXPECT_FALSE(view.auto_refresh_running());
}

TEST_F(AutoRefreshTest, NothingIsEverWrittenToTheServiceNorTheAccountChanged) {
  Enable();
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  // The page source has only read requests; the account was neither signed in again nor set up by the refresh.
  EXPECT_EQ(account->start_calls, 0);
  EXPECT_EQ(account->save_calls, 0);
  EXPECT_EQ(account->signout_calls, 0);
}

TEST_F(AutoRefreshTest, DestroyingTheViewDuringARunIsHarmless) {
  Enable();
  source->hold = true;
  view.RunAutoRefreshIfDue();
  Pump();
  ASSERT_TRUE(view.auto_refresh_running());
  // The fixture destroys the view with the run active: the destructor cancels it without further requests.
  view.hide();
}

TEST_F(AutoRefreshTest, AnAccountChangeWhileATrackReadIsInFlightCommitsNothing) {
  Enable();
  on_tracks = [this](const QString &id) {
    if (id == QStringLiteral("pl-1")) account->account = QStringLiteral("99");  // still signed in, but as somebody else
  };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_TRUE(snapshots.isEmpty());  // the old account's data is not committed
  EXPECT_EQ(track_order, QStringList() << QStringLiteral("pl-1"));
  EXPECT_FALSE(view.auto_refresh_enabled(QStringLiteral("tidal")));
  EXPECT_FALSE(stored_enabled);
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("different account")), -1);
}

TEST_F(AutoRefreshTest, AnAccountChangeWhileTheListIsReadDoesNotCacheItForTheNewAccount) {
  Enable();
  on_list = [this]() { account->account = QStringLiteral("99"); };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  on_list = nullptr;
  EXPECT_EQ(source->track_requests, 0);
  EXPECT_TRUE(snapshots.isEmpty());
  EXPECT_EQ(view.playlist_list()->count(), 0);  // the earlier list was the other account's and the answer is not attributed to the new one
  EXPECT_FALSE(view.auto_refresh_enabled(QStringLiteral("tidal")));
}

TEST_F(AutoRefreshTest, AManualImportThatCrossesAnAccountChangeIsNotCommitted) {
  SignInAndLoad();
  SelectFirst();
  on_tracks = [this](const QString &) { account->account = QStringLiteral("99"); };
  view.import_button()->click();
  Pump();
  WaitFor([this]() { return !view.importer(QStringLiteral("tidal"))->busy(); });
  Pump();
  EXPECT_TRUE(snapshots.isEmpty());
}

TEST_F(AutoRefreshTest, ProviderWaitsLongerThanAnHourAreHonoredAndSurviveToggling) {
  view.SetAutoRefreshInterval(30);
  Enable();
  fail_tracks = []() {
    PageResult result = Http(429);
    result.retry_after_sec = 7200;
    return result;
  };
  const QDateTime start = QDateTime::currentDateTime();
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  const QDateTime cooldown = view.auto_refresh_cooldown_until(QStringLiteral("tidal"));
  ASSERT_TRUE(cooldown.isValid());
  EXPECT_GE(start.secsTo(cooldown), 7190);  // not clamped to an hour
  fail_tracks = nullptr;

  // Turning it off and on again does not reset the provider's wait.
  view.auto_check()->click();
  view.auto_check()->click();
  ASSERT_TRUE(view.auto_check()->isChecked());
  EXPECT_EQ(view.auto_refresh_cooldown_until(QStringLiteral("tidal")), cooldown);
  const int before = source->requests;
  view.RunAutoRefreshIfDue();
  view.Activate();
  WaitFor([]() { return false; }, 150);
  EXPECT_EQ(source->requests, before);
  // Resume cannot bypass it either.
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("asked Adrift to wait")), -1);
}

TEST_F(AutoRefreshTest, AManualRateLimitAlsoStopsAutomaticReadsForTheFullWait) {
  Enable();
  source->responder = [this](const bool, const QString &, const int) {
    PageResult result = Http(429);
    result.retry_after_sec = 5000;
    return result;
  };
  view.reload_button()->click();
  ASSERT_TRUE(WaitFor([this]() { return view.banner()->isVisible(); }));
  EXPECT_GE(QDateTime::currentDateTime().secsTo(view.auto_refresh_cooldown_until(QStringLiteral("tidal"))), 4990);
  const int before = source->requests;
  view.RunAutoRefreshIfDue();
  Pump();
  EXPECT_EQ(source->requests, before);
}

TEST_F(AutoRefreshTest, ADeferredSnapshotIsNoLongerReportedOnceItWasRefreshed) {
  Enable();
  playing << QStringLiteral("tidal:pl-1");
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  ASSERT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("Playing now, so not changed: Mix 1")), -1);
  playing.clear();
  view.SetAutoRefreshInterval(20);
  ASSERT_TRUE(WaitFor([this]() { return snapshots.contains(QStringLiteral("tidal:pl-1")); }));
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(view.auto_status_label()->text().indexOf(QStringLiteral("Playing now")), -1);
  EXPECT_EQ(view.auto_status_label()->text().indexOf(QStringLiteral("Mix 1")), -1);
}

TEST_F(AutoRefreshTest, AHostThatNeverAnswersIsNotReportedAsSuccess) {
  Enable();
  QObject::disconnect(&view, &StreamingImportView::SnapshotReady, &view, nullptr);
  int emitted = 0;
  QObject::connect(&view, &StreamingImportView::SnapshotReady, &view, [&emitted](const QString &, const QString &, const QString &, const SongList &, const int) { ++emitted; });
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(emitted, 1);  // stopped at the first unconfirmed one
  EXPECT_TRUE(view.auto_refresh_paused(QStringLiteral("tidal")));
  EXPECT_TRUE(view.banner()->isVisible());
  EXPECT_EQ(view.auto_status_label()->text().indexOf(QStringLiteral("Updated")), -1);
  EXPECT_NE(view.banner_title()->text().indexOf(QStringLiteral("not confirmed")), -1);
}

TEST_F(AutoRefreshTest, AClosedSnapshotIsNeitherReadNorReplacedAndSaysWhy) {
  QSet<QString> open;
  open << QStringLiteral("tidal:pl-3");
  view.SetSnapshotOpenFunction([&open](const QString &identity) { return open.contains(identity); });
  Enable();
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(track_order, QStringList() << QStringLiteral("pl-3"));
  EXPECT_EQ(snapshots, QStringList() << QStringLiteral("tidal:pl-3"));
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("Not open as a playlist, so not changed: Mix 1")), -1);
  EXPECT_NE(view.auto_status_label()->text().indexOf(QStringLiteral("Nothing is opened for you")), -1);
  view.SetSnapshotOpenFunction(nullptr);
}

TEST_F(AutoRefreshTest, ASnapshotClosedWhileItIsReadIsNotReplaced) {
  QSet<QString> open;
  open << QStringLiteral("tidal:pl-1") << QStringLiteral("tidal:pl-3");
  view.SetSnapshotOpenFunction([&open](const QString &identity) { return open.contains(identity); });
  Enable();
  on_tracks = [&open](const QString &id) {
    if (id == QStringLiteral("pl-1")) open.remove(QStringLiteral("tidal:pl-1"));
  };
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  EXPECT_EQ(snapshots, QStringList() << QStringLiteral("tidal:pl-3"));
  view.SetSnapshotOpenFunction(nullptr);
}

TEST_F(AutoRefreshTest, TheCheckboxStaysConciseAtCompactWidth) {
  Enable();
  playing << QStringLiteral("tidal:pl-1");
  QSet<QString> open;
  view.SetSnapshotOpenFunction([&open](const QString &) { return false; });
  view.RunAutoRefreshIfDue();
  ASSERT_TRUE(WaitRunDone());
  Pump();
  view.resize(340, 640);
  Pump();
  EXPECT_TRUE(view.compact());
  EXPECT_TRUE(FitsHorizontally(view));
  EXPECT_LE(view.auto_check()->sizeHint().width(), 340 - 16);
  // The automatic refresh row adds nothing to the minimum width of the page.
  const int with_row = view.minimumSizeHint().width();
  view.auto_check()->hide();
  view.auto_status_label()->hide();
  view.auto_resume_button()->hide();
  view.layout()->invalidate();
  EXPECT_EQ(with_row, view.minimumSizeHint().width());
  EXPECT_TRUE(view.auto_status_label()->wordWrap());
  view.SetSnapshotOpenFunction(nullptr);
}

TEST_F(StreamingImportRenderTest, AutomaticRefreshStates) {
  RenderAll(QStringLiteral("streaming-9-auto-refresh"), [](StreamingImportView &view, FakeAccount &account, FakeSource &) {
    account.signed_in = true;
    view.session(QStringLiteral("tidal"))->Refresh();
    view.SetSnapshotExistsFunction([](const QString &) { return true; });
    view.SetSnapshotOpenFunction([](const QString &identity) { return identity != QStringLiteral("tidal:pl-2"); });
    view.SetSnapshotPlayingFunction([](const QString &identity) { return identity == QStringLiteral("tidal:pl-1"); });
    QObject::connect(&view, &StreamingImportView::SnapshotReady, &view, [&view](const QString &, const QString &, const QString &name, const SongList &songs, const int skipped) {
      view.ShowImported(name, true, static_cast<int>(songs.count()), skipped);
    });
    view.Activate();
    WaitFor([&view]() { return view.current_page() == StreamingImportView::PageList; });
    view.auto_check()->click();
    view.RunAutoRefreshIfDue();
    WaitFor([&view]() { return !view.auto_refresh_running(); });
  });
}
