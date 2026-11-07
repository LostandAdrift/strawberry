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

#ifndef SEEKSLIDER_H
#define SEEKSLIDER_H

#include "config.h"

#include <QAbstractSlider>
#include <QSize>
#include <QString>

class QEvent;
class QEnterEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

// A thin, keyboard-accessible seek bar. Values are milliseconds.
// SetPosition() follows playback and never emits anything; only the user's click, drag or keys emit SeekRequested().
class SeekSlider : public QAbstractSlider {
  Q_OBJECT

 public:
  explicit SeekSlider(QWidget *parent = nullptr);

  static constexpr int kKeyStepMs = 5000;
  static constexpr int kPageStepMs = 30000;

  // Key steps in slider units (milliseconds when used as a seek bar). A live slider emits while dragging, a seek bar only on release.
  void SetSteps(const int step, const int page_step) { step_ = step; page_step_ = page_step; }
  void SetLive(const bool live) { live_ = live; }

  // Follows playback. Ignored while the user drags the handle.
  void SetPosition(const int position_ms, const int length_ms);
  void SetCanSeek(const bool can_seek);
  bool can_seek() const { return can_seek_; }
  bool dragging() const { return dragging_; }

  // The position that a click at x would seek to.
  int PositionForX(const int x) const;
  static QString FormatTime(const int ms);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 Q_SIGNALS:
  void SeekRequested(const int position_ms);

 protected:
  void paintEvent(QPaintEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void enterEvent(QEnterEvent *e) override;
  void leaveEvent(QEvent *e) override;

 private:
  int TrackLeft() const;
  int TrackRight() const;
  void UpdateAccessibleText();

  int step_ = kKeyStepMs;
  int page_step_ = kPageStepMs;
  bool live_ = false;
  bool can_seek_;
  bool dragging_;
  bool hovered_;
};

#endif  // SEEKSLIDER_H
