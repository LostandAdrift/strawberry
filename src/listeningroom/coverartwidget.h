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

#ifndef COVERARTWIDGET_H
#define COVERARTWIDGET_H

#include "config.h"

#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWidget>

class QEvent;
class QHideEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class RoomTween;

// The large current cover of the Listening Room: a square record sleeve with a soft shadow.
// Without artwork it draws a deliberate sleeve of its own (a quiet record with a label) instead of leaving a hole,
// with one line of text that says why: nothing is playing, or the track has no artwork.
//
// Performance. Artwork can be a 4000 px scan. It is reduced once, when it arrives, to a master of at most kMasterSide pixels and the original is let go.
// The sleeve (artwork, rounded corners, hairline and shadow) is rendered into one pixmap at the exact size on screen. While the window is being resized the
// last rendered pixmap is simply drawn at the new size (cheap, a little soft); once the size has not changed for kSettleMs the sleeve is rendered sharp again
// from the master, once. A resize therefore costs one blit per frame, and the final picture is always crisp.
//
// Motion. A new cover fades in over the old one; while a track plays, a record slides out from behind the sleeve and turns slowly.
// The turning repaints only the small crescent of the record, its timer runs only while playing, visible and with motion allowed.
class CoverArtWidget : public QWidget {
  Q_OBJECT

 public:
  explicit CoverArtWidget(QWidget *parent = nullptr);

  enum class Mode {
    Idle,       // nothing is playing
    NoArtwork,  // a track is playing but has no artwork
    Artwork
  };

  static constexpr int kMinimumSide = 160;
  static constexpr int kMaximumSide = 560;
  // The longest side of the kept master: enough for a 560 px sleeve at 2x scale.
  static constexpr int kMasterSide = 1280;
  static constexpr int kSettleMs = 130;

  // A null image shows the placeholder. `has_track` selects between the idle and the no-artwork wording.
  void SetImage(const QImage &image, const bool has_track);
  void SetIdleText(const QString &text) { idle_text_ = text; update(); }
  // Whether a track is playing: the record comes out from behind the sleeve.
  void SetPlaying(const bool playing);

  Mode mode() const { return mode_; }
  // The square (without the shadow margin) that the sleeve occupies for the current widget size.
  QRect SleeveRect() const;

  // The empty space around the sleeve that holds its shadow. Text placed under the cover uses it as its left margin so both line up.
  int ShadowMargin() const;

  // Evidence for tests and measurements.
  QSize master_size() const { return master_.size(); }
  int sharp_renders() const { return sharp_renders_; }
  int stretched_paints() const { return stretched_paints_; }
  bool settle_pending() const { return settle_timer_->isActive(); }
  // Renders the sharp sleeve now, as the settle timer would.
  void SettleNow();
  bool spin_timer_active() const { return spin_timer_->isActive(); }
  // How far the record is out (0 to 1) and how far the new cover has faded in (0 to 1).
  double record_amount() const;
  double fade_amount() const;
  bool fading() const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 protected:
  void paintEvent(QPaintEvent *e) override;
  void showEvent(QShowEvent *e) override;
  void hideEvent(QHideEvent *e) override;
  void changeEvent(QEvent *e) override;

 private:
  void DrawPlaceholder(QPainter *p, const QRect &rect) const;
  void RenderSharp();
  QSize SharpTarget() const;
  void DrawRecord(QPainter *p, const QRect &sleeve, const qreal amount) const;
  QRect RecordBounds(const QRect &sleeve) const;
  void SyncSpin();
  int ShadowPad() const;

  QImage master_;
  Mode mode_;
  QString idle_text_;

  // The rendered sleeve with its shadow, in device pixels.
  QPixmap sharp_;
  QSize sharp_target_;
  qint64 sharp_generation_ = 0;
  qint64 image_generation_ = 0;
  mutable QPixmap record_cache_;  // the still part of the record, rendered once per size
  mutable int record_cache_side_ = 0;
  mutable qreal record_cache_dpr_ = 0;
  QPixmap outgoing_;  // the previous sleeve, while the new cover fades in
  QTimer *settle_timer_;
  QTimer *spin_timer_;
  RoomTween *fade_;
  RoomTween *record_;
  bool playing_ = false;
  qreal angle_ = 0;
  int sharp_renders_ = 0;
  mutable int stretched_paints_ = 0;
};

#endif  // COVERARTWIDGET_H
