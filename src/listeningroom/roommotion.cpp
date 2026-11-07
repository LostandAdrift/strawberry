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

#include "config.h"

#include <QByteArray>
#include <QEasingCurve>
#include <QVariant>

#include "roommotion.h"

RoomMotion::RoomMotion(QObject *parent) : QObject(parent), reduced_(false) {

  const QByteArray env = qgetenv("ADRIFT_REDUCE_MOTION");
  reduced_ = !env.isEmpty() && env != "0";

}

RoomMotion *RoomMotion::Instance() {

  static RoomMotion *instance = new RoomMotion;
  return instance;

}

void RoomMotion::SetReduced(const bool reduced) {

  if (reduced_ == reduced) return;
  reduced_ = reduced;
  Q_EMIT ReducedChanged(reduced);

}

RoomTween::RoomTween(QObject *parent, const double initial, const int duration_ms, const QEasingCurve::Type curve) : QObject(parent), value_(initial), target_(initial), duration_ms_(duration_ms) {

  animation_.setEasingCurve(QEasingCurve(curve));
  QObject::connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    value_ = v.toDouble();
    Q_EMIT Changed();
  });
  QObject::connect(RoomMotion::Instance(), &RoomMotion::ReducedChanged, this, [this](const bool reduced) {
    if (reduced) Jump(target_);
  });

}

void RoomTween::SetTarget(const double target) {

  if (target == target_ && (running() || value_ == target)) return;
  target_ = target;
  const int duration = RoomMotion::Duration(duration_ms_);
  if (duration <= 0) {
    Jump(target);
    return;
  }
  animation_.stop();
  animation_.setDuration(duration);
  animation_.setStartValue(value_);
  animation_.setEndValue(target);
  animation_.start();

}

void RoomTween::Jump(const double value) {

  animation_.stop();
  target_ = value;
  if (value_ == value) return;
  value_ = value;
  Q_EMIT Changed();

}
