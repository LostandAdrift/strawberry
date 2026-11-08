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

// Motion and resizing behaviour of the Listening Room: the reduced-motion switch, eased navigation and hover, the cover transition and record,
// timers that only run when something is visibly moving, the cover cache that makes a drag-resize cheap, and the catalog / panel hosting.
// These are behaviour tests: they count renders, timers and layout passes; none of them depends on how fast the machine is.

#include "gtest_include.h"

#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QSignalSpy>
#include <QTest>

#include "listeningroom/coverartwidget.h"
#include "listeningroom/listeningroom.h"
#include "listeningroom/roommotion.h"
#include "listeningroom/roomwidgets.h"
#include "listeningroom_fixture.h"

namespace {

// Every test starts with motion allowed and leaves it that way.
class MotionTest : public ::testing::Test {
 protected:
  void SetUp() override { RoomMotion::Instance()->SetReduced(false); }
  void TearDown() override { RoomMotion::Instance()->SetReduced(false); }
};

RoomTrack SampleTrack() {
  RoomTrack track;
  track.title = QStringLiteral("Salt Light");
  track.artist = QStringLiteral("Marin Holt");
  track.album = QStringLiteral("Slow Tide");
  return track;
}

void ShowAt(ListeningRoom &room, const int width, const int height) {
  room.resize(width, height);
  room.show();
  QCoreApplication::processEvents();
  room.layout()->activate();
}

class LayoutCounter : public QObject {
 public:
  bool eventFilter(QObject *watched, QEvent *e) override {
    Q_UNUSED(watched)
    if (e->type() == QEvent::LayoutRequest) ++layout_requests;
    return false;
  }
  int layout_requests = 0;
};

}  // namespace

// --------------------------------------------------------------------------------------------------------------- the switch

TEST_F(MotionTest, TweenEasesToItsTargetAndRestsAndJumpsWhenMotionIsReduced) {

  RoomTween tween(nullptr, 0.0, 120);
  QSignalSpy changed(&tween, &RoomTween::Changed);
  tween.SetTarget(1.0);
  EXPECT_TRUE(tween.running());
  EXPECT_LT(tween.value(), 1.0);
  QTRY_VERIFY_WITH_TIMEOUT(!tween.running(), 2000);
  EXPECT_DOUBLE_EQ(1.0, tween.value());
  EXPECT_GT(changed.count(), 1);  // it passed through intermediate values

  // Reduced: the same request is an immediate jump, with no animation at all.
  RoomMotion::Instance()->SetReduced(true);
  changed.clear();
  tween.SetTarget(0.0);
  EXPECT_FALSE(tween.running());
  EXPECT_DOUBLE_EQ(0.0, tween.value());
  EXPECT_EQ(1, changed.count());

  // Switching to reduced while a motion is under way finishes it on the spot.
  RoomMotion::Instance()->SetReduced(false);
  tween.SetTarget(1.0);
  EXPECT_TRUE(tween.running());
  RoomMotion::Instance()->SetReduced(true);
  EXPECT_FALSE(tween.running());
  EXPECT_DOUBLE_EQ(1.0, tween.value());

}

TEST_F(MotionTest, ReducedMotionControlIsInTheRailAccessibleAndKeepsTheSwitchInSync) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  QSignalSpy changed(&room, &ListeningRoom::ReducedMotionChanged);

  RoomNavButton *button = room.motion_button();
  ASSERT_NE(nullptr, button);
  EXPECT_TRUE(button->isVisible());
  EXPECT_TRUE(button->isCheckable());
  EXPECT_FALSE(button->accessibleName().isEmpty());
  EXPECT_FALSE(button->accessibleDescription().isEmpty());
  EXPECT_EQ(Qt::StrongFocus, button->focusPolicy());
  EXPECT_FALSE(button->isChecked());
  EXPECT_FALSE(room.reduced_motion());

  button->click();
  EXPECT_TRUE(RoomMotion::Instance()->reduced());
  EXPECT_TRUE(room.reduced_motion());
  ASSERT_EQ(1, changed.count());
  EXPECT_TRUE(changed.last().first().toBool());

  // The keyboard works as well: Space toggles it back.
  button->setFocus();
  QTest::keyClick(button, Qt::Key_Space);
  EXPECT_FALSE(RoomMotion::Instance()->reduced());
  EXPECT_FALSE(button->isChecked());

  // A change from outside (the stored preference being applied at start-up) is shown by the control, without announcing a user change.
  changed.clear();
  room.SetReducedMotion(true);
  EXPECT_TRUE(button->isChecked());
  EXPECT_EQ(0, changed.count());

  // It is a control of the rail, so it is there in compact windows too (as an icon).
  room.SetReducedMotion(false);
  ShowAt(room, 1000, 700);
  EXPECT_TRUE(room.motion_button()->isVisible());
  EXPECT_TRUE(room.motion_button()->compact());

}

// --------------------------------------------------------------------------------------------------------------- navigation and buttons

TEST_F(MotionTest, NavigationIndicatorGlidesToTheNewDestinationAndRests) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetView(ListeningRoom::View::Listening);
  QTRY_VERIFY_WITH_TIMEOUT(!room.nav_indicator_gliding(), 2000);
  const QRectF start = room.nav_indicator();
  EXPECT_EQ(start, room.nav_indicator_target());
  EXPECT_FALSE(start.isEmpty());

  room.SetView(ListeningRoom::View::Streaming);
  const QRectF target = room.nav_indicator_target();
  EXPECT_NE(start, target);
  EXPECT_TRUE(room.nav_indicator_gliding());
  // On its way: between the two places, one pill (not two buttons switching).
  const QRectF now = room.nav_indicator();
  EXPECT_GE(now.top(), start.top());
  EXPECT_LE(now.top(), target.top());
  QTRY_VERIFY_WITH_TIMEOUT(!room.nav_indicator_gliding(), 2000);
  EXPECT_EQ(target, room.nav_indicator());
  // It rests exactly on the button of the destination.
  EXPECT_EQ(QRectF(room.nav_button(ListeningRoom::View::Streaming)->geometry()).adjusted(0, 1, 0, -1), target);

  // The advanced page keeps the Library lit, and Settings never takes the indicator.
  room.SetView(ListeningRoom::View::Classic);
  QTRY_VERIFY_WITH_TIMEOUT(!room.nav_indicator_gliding(), 2000);
  EXPECT_EQ(QRectF(room.nav_button(ListeningRoom::View::Library)->geometry()).adjusted(0, 1, 0, -1), room.nav_indicator_target());

}

TEST_F(MotionTest, NavigationJumpsWithoutAnimationWhenMotionIsReduced) {

  RoomMotion::Instance()->SetReduced(true);
  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetView(ListeningRoom::View::Playlists);
  EXPECT_FALSE(room.nav_indicator_gliding());
  EXPECT_EQ(room.nav_indicator(), room.nav_indicator_target());
  room.SetView(ListeningRoom::View::Streaming);
  EXPECT_FALSE(room.nav_indicator_gliding());
  EXPECT_EQ(room.nav_indicator(), room.nav_indicator_target());

}

TEST_F(MotionTest, ButtonsRespondToThePointerWithEasedHoverAndPress) {

  QWidget host;
  RoomButton *button = new RoomButton(QStringLiteral("Play"), true, &host);
  button->move(20, 20);
  host.resize(200, 100);
  host.show();
  QCoreApplication::processEvents();

  EXPECT_DOUBLE_EQ(0.0, button->hover_amount());
  QTest::mouseMove(button, QPoint(10, 10));
  QTRY_VERIFY_WITH_TIMEOUT(button->hover_amount() > 0.0, 1000);
  QTRY_COMPARE_WITH_TIMEOUT(button->hover_amount(), 1.0, 2000);

  // Pressing sinks it; releasing lets it come back (and the click still happens).
  QSignalSpy clicked(button, &QPushButton::clicked);
  QTest::mousePress(button, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
  QTRY_VERIFY_WITH_TIMEOUT(button->press_amount() > 0.0, 1000);
  QTest::mouseRelease(button, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
  EXPECT_EQ(1, clicked.count());
  QTRY_COMPARE_WITH_TIMEOUT(button->press_amount(), 0.0, 2000);

  // Reduced motion: the state is there at once.
  RoomMotion::Instance()->SetReduced(true);
  QTest::mouseMove(&host, QPoint(150, 80));
  QTRY_COMPARE_WITH_TIMEOUT(button->hover_amount(), 0.0, 2000);
  QTest::mouseMove(button, QPoint(10, 10));
  EXPECT_DOUBLE_EQ(1.0, button->hover_amount());
  EXPECT_FALSE(button->grab().isNull());

}

TEST_F(MotionTest, SegmentedControlGlidesToTheChosenSegmentAndHasKeyboardAccess) {

  RoomSegmented control(QStringList() << QStringLiteral("Albums") << QStringLiteral("Artists"));
  control.show();
  QSignalSpy changed(&control, &RoomSegmented::CurrentChanged);
  control.SetCurrent(1);
  ASSERT_EQ(1, changed.count());
  EXPECT_LT(control.highlight_position(), 1.0);
  QTRY_COMPARE_WITH_TIMEOUT(control.highlight_position(), 1.0, 2000);

  control.setFocus();
  QTest::keyClick(&control, Qt::Key_Left);
  EXPECT_EQ(0, control.current());
  EXPECT_EQ(2, changed.count());

  RoomMotion::Instance()->SetReduced(true);
  control.SetCurrent(1);
  EXPECT_DOUBLE_EQ(1.0, control.highlight_position());

}

// --------------------------------------------------------------------------------------------------------------- the cover

TEST_F(MotionTest, LargeArtworkIsReducedOnceAndNotKept) {

  CoverArtWidget cover;
  cover.resize(600, 600);
  cover.show();
  QImage large(4000, 3000, QImage::Format_RGB32);
  large.fill(QColor(30, 90, 120));
  cover.SetImage(large, true);

  // The master is bounded, whatever the source size; a small cover is kept as it is.
  EXPECT_LE(cover.master_size().width(), CoverArtWidget::kMasterSide);
  EXPECT_LE(cover.master_size().height(), CoverArtWidget::kMasterSide);
  EXPECT_EQ(CoverArtWidget::kMasterSide, cover.master_size().width());
  EXPECT_EQ(960, cover.master_size().height());  // the aspect ratio is kept

  cover.SetImage(RoomFixture::Cover(1, 500), true);
  EXPECT_EQ(QSize(500, 500), cover.master_size());

}

TEST_F(MotionTest, ResizingStretchesTheRenderedSleeveAndRendersSharpOnceWhenTheSizeSettles) {

  CoverArtWidget cover;
  cover.resize(640, 700);
  cover.show();
  QImage large(4000, 4000, QImage::Format_RGB32);
  large.fill(QColor(30, 90, 120));
  cover.SetImage(large, true);
  (void)cover.grab();
  const int renders_before = cover.sharp_renders();
  EXPECT_GE(renders_before, 1);
  EXPECT_FALSE(cover.settle_pending());

  // A drag-resize: 40 different sizes. Each frame is a stretched blit of what is there; no frame renders from the artwork.
  for (int i = 0; i < 40; ++i) {
    cover.resize(640 - i * 4, 700 - i * 6);
    (void)cover.grab();
  }
  EXPECT_EQ(renders_before, cover.sharp_renders());
  EXPECT_GE(cover.stretched_paints(), 20);  // the first sizes keep the clamped maximum sleeve, which needs no stretching
  EXPECT_TRUE(cover.settle_pending());

  // When the size stops changing, the sleeve is rendered sharp, once.
  QTRY_VERIFY_WITH_TIMEOUT(!cover.settle_pending(), 2000);
  EXPECT_EQ(renders_before + 1, cover.sharp_renders());
  const int stretched = cover.stretched_paints();
  (void)cover.grab();
  EXPECT_EQ(stretched, cover.stretched_paints());  // the settled sleeve is drawn one to one
  EXPECT_EQ(renders_before + 1, cover.sharp_renders());

  // Painting again at the same size renders nothing.
  for (int i = 0; i < 10; ++i) (void)cover.grab();
  EXPECT_EQ(renders_before + 1, cover.sharp_renders());

}

TEST_F(MotionTest, ASettledSleeveIsCrispAtTheFinalSize) {

  // The settled picture is rendered from the master at the exact size, not a stretched copy: a one-pixel pattern survives a resize round trip.
  QImage stripes(2400, 2400, QImage::Format_RGB32);
  for (int y = 0; y < stripes.height(); ++y) {
    for (int x = 0; x < stripes.width(); ++x) stripes.setPixel(x, y, ((x / 40) % 2) ? qRgb(240, 240, 240) : qRgb(20, 20, 20));
  }
  CoverArtWidget cover;
  cover.resize(600, 600);
  cover.show();
  cover.SetImage(stripes, true);
  (void)cover.grab();

  cover.resize(420, 420);
  (void)cover.grab();
  cover.resize(600, 600);
  (void)cover.grab();
  QTRY_VERIFY_WITH_TIMEOUT(!cover.settle_pending(), 2000);
  const QImage sharp = cover.grab().toImage().convertToFormat(QImage::Format_RGB32);

  // Edges between the stripes are as hard as the source's: neighbouring pixels across an edge differ strongly somewhere along a row.
  const QRect sleeve = cover.SleeveRect();
  int hard_edges = 0;
  const int y = sleeve.center().y();
  for (int x = sleeve.left() + 4; x < sleeve.right() - 4; ++x) {
    if (std::abs(qRed(sharp.pixel(x, y)) - qRed(sharp.pixel(x + 1, y))) > 150) ++hard_edges;
  }
  EXPECT_GE(hard_edges, 4);

}

TEST_F(MotionTest, ANewCoverFadesInOverTheOldOneAndReducedMotionDoesNot) {

  CoverArtWidget cover;
  cover.resize(600, 600);
  cover.show();
  cover.SetImage(RoomFixture::Cover(0, 600), true);
  (void)cover.grab();
  EXPECT_FALSE(cover.fading());

  cover.SetImage(RoomFixture::Cover(1, 600), true);
  EXPECT_TRUE(cover.fading());
  EXPECT_LT(cover.fade_amount(), 1.0);
  QTRY_VERIFY_WITH_TIMEOUT(!cover.fading(), 3000);
  EXPECT_DOUBLE_EQ(1.0, cover.fade_amount());

  RoomMotion::Instance()->SetReduced(true);
  cover.SetImage(RoomFixture::Cover(2, 600), true);
  EXPECT_FALSE(cover.fading());
  EXPECT_DOUBLE_EQ(1.0, cover.fade_amount());

}

TEST_F(MotionTest, TheRecordComesOutWhilePlayingAndTurnsOnlyWhenItIsVisiblyAllowedTo) {

  CoverArtWidget cover;
  cover.resize(700, 600);
  cover.show();
  cover.SetImage(RoomFixture::Cover(0, 600), true);
  (void)cover.grab();

  // Not playing: the record is behind the sleeve and nothing runs.
  EXPECT_DOUBLE_EQ(0.0, cover.record_amount());
  EXPECT_FALSE(cover.spin_timer_active());

  cover.SetPlaying(true);
  QTRY_COMPARE_WITH_TIMEOUT(cover.record_amount(), 1.0, 3000);
  EXPECT_TRUE(cover.spin_timer_active());

  // Hidden: the timer stops, and comes back with the widget.
  cover.hide();
  EXPECT_FALSE(cover.spin_timer_active());
  cover.show();
  EXPECT_TRUE(cover.spin_timer_active());

  // Reduced motion: the record may stay where it is, but nothing turns.
  RoomMotion::Instance()->SetReduced(true);
  EXPECT_FALSE(cover.spin_timer_active());
  RoomMotion::Instance()->SetReduced(false);

  // Paused or stopped: it slides back and the timer goes.
  cover.SetPlaying(false);
  QTRY_COMPARE_WITH_TIMEOUT(cover.record_amount(), 0.0, 3000);
  EXPECT_FALSE(cover.spin_timer_active());

  // Nothing to show (no track): playing is meaningless, the record stays in.
  cover.SetImage(QImage(), false);
  cover.SetPlaying(true);
  EXPECT_DOUBLE_EQ(0.0, cover.record_amount());
  EXPECT_FALSE(cover.spin_timer_active());

}

TEST_F(MotionTest, RoomMotionFollowsPlaybackAndNothingRunsWhileStoppedOrHidden) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetNowPlaying(SampleTrack());
  room.SetCover(RoomFixture::Cover(0, 600));

  // Stopped: no wave, no record, no timers.
  EXPECT_FALSE(room.wordmark_animating());
  EXPECT_FALSE(room.cover()->spin_timer_active());

  room.SetPlaybackState(TransportBar::State::Playing);
  EXPECT_TRUE(room.wordmark_animating());
  QTRY_VERIFY_WITH_TIMEOUT(room.cover()->spin_timer_active(), 3000);

  // The Library page hides the cover; its timer stops with it. The rail's wave is still in view.
  room.SetView(ListeningRoom::View::Library);
  EXPECT_FALSE(room.cover()->spin_timer_active());
  EXPECT_TRUE(room.wordmark_animating());
  room.SetView(ListeningRoom::View::Listening);
  EXPECT_TRUE(room.cover()->spin_timer_active());

  room.SetPlaybackState(TransportBar::State::Paused);
  EXPECT_FALSE(room.wordmark_animating());
  QTRY_VERIFY_WITH_TIMEOUT(!room.cover()->spin_timer_active(), 3000);

  room.SetPlaybackState(TransportBar::State::Playing);
  room.hide();
  EXPECT_FALSE(room.wordmark_animating());
  EXPECT_FALSE(room.cover()->spin_timer_active());
  room.show();
  EXPECT_TRUE(room.wordmark_animating());

  room.SetReducedMotion(true);
  EXPECT_FALSE(room.wordmark_animating());
  EXPECT_FALSE(room.cover()->spin_timer_active());

}

// --------------------------------------------------------------------------------------------------------------- resizing the room

TEST_F(MotionTest, ADragResizeDoesNotReapplyTheResponsiveLayoutForEverySize) {

  RoomMotion::Instance()->SetReduced(true);  // no animation frames mixed into the count
  ListeningRoom room;
  RoomFixture::QueueFixture queue(300);
  room.SetQueuePlaylist(queue.playlist(), QStringLiteral("Everything"));
  ShowAt(room, 1440, 900);
  (void)room.grab();

  // 40 sizes within one responsive layout (wide, rail with labels, queue beside the cover).
  LayoutCounter counter;
  qApp->installEventFilter(&counter);
  for (int i = 0; i < 40; ++i) {
    room.resize(1440 - i * 8, 900 - i * 5);
    QCoreApplication::processEvents();
  }
  qApp->removeEventFilter(&counter);
  // The resize itself lays out once per step; the responsive layout must not add more layout passes on top of that.
  EXPECT_LE(counter.layout_requests, 40 * 2);
  EXPECT_FALSE(room.compact());

  // Crossing a threshold applies it, exactly when needed.
  room.resize(1090, 700);
  QCoreApplication::processEvents();
  EXPECT_TRUE(room.compact());
  room.resize(1300, 800);
  QCoreApplication::processEvents();
  EXPECT_FALSE(room.compact());

}

// --------------------------------------------------------------------------------------------------------------- in-window hosting

TEST_F(MotionTest, ACatalogIsShownOnTheStreamingPageInsideTheWindowWithAWayBack) {

  ListeningRoom room;
  QLabel *classic = new QLabel(QStringLiteral("classic"));
  room.classic_container()->layout()->addWidget(classic);
  ShowAt(room, 1440, 900);
  room.SetStreamingServices({ { QStringLiteral("tidal"), QStringLiteral("Tidal"), QStringLiteral("Signed in"), true } });

  QLabel catalog(QStringLiteral("favorites of a service"));
  QSignalSpy closed(&room, &ListeningRoom::CatalogClosed);
  room.ShowCatalog(&catalog, QStringLiteral("Tidal"));

  EXPECT_EQ(ListeningRoom::View::Streaming, room.view());
  EXPECT_TRUE(room.catalog_visible());
  EXPECT_TRUE(catalog.isVisible());
  EXPECT_TRUE(room.page(ListeningRoom::View::Streaming)->isAncestorOf(&catalog));
  // Never the nested classic window, and the room's navigation and transport stay.
  EXPECT_FALSE(classic->isVisible());
  EXPECT_FALSE(room.classic_container()->isAncestorOf(&catalog));
  EXPECT_TRUE(room.transport()->isVisible());
  EXPECT_TRUE(room.nav_button(ListeningRoom::View::Streaming)->isChecked());
  EXPECT_TRUE(room.catalog_back_button()->isVisible());

  room.catalog_back_button()->click();
  EXPECT_FALSE(room.catalog_visible());
  EXPECT_FALSE(catalog.isVisible());
  EXPECT_EQ(nullptr, room.catalog_widget());
  EXPECT_EQ(1, closed.count());
  EXPECT_EQ(ListeningRoom::View::Streaming, room.view());

}

TEST_F(MotionTest, TheStreamingPageHostsItsWidgetWithoutStealingNavigation) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetView(ListeningRoom::View::Listening);

  // Setting the page widget (at start-up, or when something asynchronous finishes) only gives it its place. The user stays where they are.
  QLabel panel(QStringLiteral("account and import page"));
  room.SetStreamingWidget(&panel);
  EXPECT_EQ(ListeningRoom::View::Listening, room.view());
  EXPECT_FALSE(panel.isVisible());
  room.SetView(ListeningRoom::View::Streaming);
  EXPECT_TRUE(panel.isVisible());
  EXPECT_TRUE(room.page(ListeningRoom::View::Streaming)->isAncestorOf(&panel));

  room.SetStreamingWidget(nullptr);
  EXPECT_FALSE(panel.isVisible());
  EXPECT_EQ(nullptr, room.streaming_widget());

}
