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

#ifndef ROOMMOTION_H
#define ROOMMOTION_H

#include "config.h"

#include <QEasingCurve>
#include <QObject>
#include <QVariantAnimation>

// The motion language of Adrift, in one place.
//
// - One switch: "reduced motion". When it is on, every animation in the Listening Room jumps straight to its end state: no easing, no timers, no drifting.
//   The preference is stored by MainWindow (and set by a control in the side rail); the environment variable ADRIFT_REDUCE_MOTION=1 starts the process with it on.
// - Motion is short and eased (about 120 to 320 ms), and it only ever repaints the small widget that moves. Nothing here animates the whole window,
//   and nothing keeps a timer running while the room is hidden or playback is stopped.
class RoomMotion : public QObject {
  Q_OBJECT

 public:
  static RoomMotion *Instance();

  bool reduced() const { return reduced_; }
  void SetReduced(const bool reduced);

  // The duration to use for an animation that would last `ms`: zero when motion is reduced.
  static int Duration(const int ms) { return Instance()->reduced_ ? 0 : ms; }

 Q_SIGNALS:
  void ReducedChanged(const bool reduced);

 private:
  explicit RoomMotion(QObject *parent = nullptr);

  bool reduced_;
};

// A value that eases toward a target and tells its owner to repaint. It does nothing while it is at rest.
// With reduced motion, SetTarget() jumps: value() is the target immediately and no timer runs.
class RoomTween : public QObject {
  Q_OBJECT

 public:
  explicit RoomTween(QObject *parent = nullptr, const double initial = 0.0, const int duration_ms = 200, const QEasingCurve::Type curve = QEasingCurve::OutCubic);

  double value() const { return value_; }
  double target() const { return target_; }
  bool running() const { return animation_.state() == QAbstractAnimation::Running; }
  void SetDuration(const int ms) { duration_ms_ = ms; }

  // Eases to `target`. Jumps when motion is reduced or the duration is zero.
  void SetTarget(const double target);
  // Sets the value without animating.
  void Jump(const double value);

 Q_SIGNALS:
  void Changed();

 private:
  QVariantAnimation animation_;
  double value_;
  double target_;
  int duration_ms_;
};

#endif  // ROOMMOTION_H
