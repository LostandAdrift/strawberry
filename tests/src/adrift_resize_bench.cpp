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

// A repeatable resize benchmark for the Listening Room. It is a measurement, not a pass/fail speed test:
// the only assertions are structural (the sequence ran, every step produced a frame), because absolute timings depend on the hardware.
// Run it directly (QT_QPA_PLATFORM=offscreen ./adrift_resize_bench) and compare the printed BENCH lines before and after a change.
// Environment: ADRIFT_BENCH_COVER (cover side in pixels, default 4000), ADRIFT_BENCH_ROUNDS (default 3).

#include "gtest_include.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include <QApplication>
#include <QEvent>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QPixmap>

#include "listeningroom/coverartwidget.h"
#include "listeningroom/listeningroom.h"
#include "listeningroom_fixture.h"

namespace {

using Clock = std::chrono::steady_clock;

double Ms(const Clock::time_point a, const Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// A photographic-like large cover: gradients plus noise, so scaling it costs what a real scan costs.
QImage LargeCover(const int side) {

  QImage image = RoomFixture::Cover(0, 900).scaled(side, side, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
  quint32 seed = 12345;
  for (int y = 0; y < image.height(); ++y) {
    QRgb *line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      seed = seed * 1664525u + 1013904223u;
      const int n = static_cast<int>((seed >> 24) & 0x1f) - 16;
      line[x] = qRgb(std::clamp(qRed(line[x]) + n, 0, 255), std::clamp(qGreen(line[x]) + n, 0, 255), std::clamp(qBlue(line[x]) + n, 0, 255));
    }
  }
  return image;

}

// Counts the events a resize causes, app-wide: layout requests are the re-layouts that were scheduled in addition to the resize itself.
class EventCounter : public QObject {
 public:
  bool eventFilter(QObject *watched, QEvent *e) override {
    Q_UNUSED(watched)
    if (e->type() == QEvent::LayoutRequest) ++layout_requests;
    else if (e->type() == QEvent::Resize) ++resizes;
    else if (e->type() == QEvent::UpdateRequest) ++update_requests;
    return false;
  }
  int layout_requests = 0;
  int resizes = 0;
  int update_requests = 0;
};

}  // namespace

TEST(AdriftResizeBench, ResizeSequenceWithLargeCover) {

  const int side = qEnvironmentVariableIntValue("ADRIFT_BENCH_COVER") > 0 ? qEnvironmentVariableIntValue("ADRIFT_BENCH_COVER") : 4000;
  const int rounds = qEnvironmentVariableIntValue("ADRIFT_BENCH_ROUNDS") > 0 ? qEnvironmentVariableIntValue("ADRIFT_BENCH_ROUNDS") : 3;

  // A long playlist, as a real collection produces (ADRIFT_BENCH_TRACKS, default 4000), with a track in the middle playing.
  const int tracks = qEnvironmentVariableIntValue("ADRIFT_BENCH_TRACKS") > 0 ? qEnvironmentVariableIntValue("ADRIFT_BENCH_TRACKS") : 4000;
  RoomFixture::QueueFixture queue(tracks);
  queue.SetCurrent(tracks / 2);

  ListeningRoom room;
  room.SetQueuePlaylist(queue.playlist(), QStringLiteral("Everything"));
  const int base_w = qEnvironmentVariableIntValue("ADRIFT_BENCH_WIDTH") > 0 ? qEnvironmentVariableIntValue("ADRIFT_BENCH_WIDTH") : 1440;
  const int base_h = qEnvironmentVariableIntValue("ADRIFT_BENCH_HEIGHT") > 0 ? qEnvironmentVariableIntValue("ADRIFT_BENCH_HEIGHT") : 900;
  room.resize(base_w, base_h);
  room.show();
  QCoreApplication::processEvents();

  RoomTrack track;
  track.title = QStringLiteral("Salt Light");
  track.artist = QStringLiteral("Marin Holt");
  track.album = QStringLiteral("Slow Tide");
  room.SetNowPlaying(track);
  room.SetPlaybackState(TransportBar::State::Playing);

  const QImage cover = LargeCover(side);
  const auto set_start = Clock::now();
  room.SetCover(cover);
  QCoreApplication::processEvents();
  const double set_ms = Ms(set_start, Clock::now());

  // A tiling window manager drags a divider: many small steps of both dimensions, then back (40 sizes per round).
  std::vector<QSize> sizes;
  const int step_w = base_w * 26 / 1000;
  const int step_h = base_h * 19 / 1000;
  for (int i = 0; i < 20; ++i) sizes.push_back(QSize(base_w - i * step_w, base_h - i * step_h));
  for (int i = 19; i >= 0; --i) sizes.push_back(QSize(base_w - i * step_w, base_h - i * step_h));

  // First frame at the first size, uncounted warm-up.
  (void)room.grab();

  EventCounter counter;
  qApp->installEventFilter(&counter);
  std::vector<double> steps;
  std::vector<double> cover_steps;
  double total = 0;
  for (int round = 0; round < rounds; ++round) {
    for (const QSize &size : sizes) {
      const auto t0 = Clock::now();
      room.resize(size);
      QCoreApplication::processEvents();
      (void)room.grab();
      const double ms = Ms(t0, Clock::now());
      steps.push_back(ms);
      total += ms;
      const auto c0 = Clock::now();
      (void)room.cover()->grab();
      cover_steps.push_back(Ms(c0, Clock::now()));
    }
  }

  qApp->removeEventFilter(&counter);
  double cover_total = 0;
  for (const double v : cover_steps) cover_total += v;

  // Reference: what one smooth scaling of this cover to a typical sleeve costs on its own.
  const auto scale_start = Clock::now();
  const QImage reference = cover.scaled(QSize(520, 520), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  const double scale_ms = Ms(scale_start, Clock::now());
  EXPECT_FALSE(reference.isNull());

  std::vector<double> sorted = steps;
  std::sort(sorted.begin(), sorted.end());
  const double median = sorted[sorted.size() / 2];
  const double p95 = sorted[static_cast<size_t>(static_cast<double>(sorted.size()) * 0.95)];
  const double worst = sorted.back();

  const auto f = [](const double v) { return QString::number(v, 'f', 2); };
  qInfo().noquote() << QStringLiteral("BENCH resize dpr=%15 cover=%1px queue=%14tracks steps=%2 (sizes=%3 rounds=%4 window %16x%17..%5x%6) set_cover_ms=%7 one_smooth_scale_ms=%8 per_step_ms mean=%9 median=%10 p95=%11 max=%12 total_ms=%13 cover_widget_grab_mean_ms=%18 layout_requests_per_step=%19 resize_events_per_step=%20")
                           .arg(side).arg(steps.size()).arg(sizes.size()).arg(rounds).arg(sizes[19].width()).arg(sizes[19].height())
                           .arg(f(set_ms), f(scale_ms), f(total / static_cast<double>(steps.size())), f(median), f(p95), f(worst), f(total)).arg(tracks).arg(room.devicePixelRatioF()).arg(base_w).arg(base_h).arg(f(cover_total / static_cast<double>(cover_steps.size()))).arg(static_cast<double>(counter.layout_requests) / static_cast<double>(steps.size()), 0, 'f', 1).arg(static_cast<double>(counter.resizes) / static_cast<double>(steps.size()), 0, 'f', 1);

  EXPECT_EQ(static_cast<size_t>(rounds) * sizes.size(), steps.size());
  EXPECT_GT(total, 0.0);

}
