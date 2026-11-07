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
#include <QWidget>

class QPaintEvent;

// The large current cover of the Listening Room: a square record sleeve with a soft shadow.
// Without artwork it draws a deliberate sleeve of its own (a quiet record with a label) instead of leaving a hole,
// with one line of text that says why: nothing is playing, or the track has no artwork.
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

  // A null image shows the placeholder. `has_track` selects between the idle and the no-artwork wording.
  void SetImage(const QImage &image, const bool has_track);
  void SetIdleText(const QString &text) { idle_text_ = text; update(); }

  Mode mode() const { return mode_; }
  // The square (without the shadow margin) that the sleeve occupies for the current widget size.
  QRect SleeveRect() const;

  // The empty space around the sleeve that holds its shadow. Text placed under the cover uses it as its left margin so both line up.
  int ShadowMargin() const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 protected:
  void paintEvent(QPaintEvent *e) override;

 private:
  void DrawPlaceholder(QPainter *p, const QRect &rect) const;

  QImage image_;
  Mode mode_;
  QString idle_text_;
  mutable QPixmap cache_;
  mutable QSize cache_size_;
  mutable qint64 cache_key_;
};

#endif  // COVERARTWIDGET_H
