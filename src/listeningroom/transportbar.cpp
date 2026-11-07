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

#include <algorithm>

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "transportbar.h"
#include "elidedlabel.h"
#include "roomstyle.h"
#include "seekslider.h"

// ------------------------------------------------------------------------------------------------------------ TransportButton

TransportButton::TransportButton(const Kind kind, QWidget *parent) : QAbstractButton(parent), kind_(kind), primary_(kind == Kind::Play || kind == Kind::Pause) {

  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setAccessibleName(NameFor(kind));
  setToolTip(NameFor(kind));

}

QString TransportButton::NameFor(const Kind kind) {

  switch (kind) {
    case Kind::Previous:     return tr("Previous track");
    case Kind::Play:         return tr("Play");
    case Kind::Pause:        return tr("Pause");
    case Kind::Next:         return tr("Next track");
    case Kind::Stop:         return tr("Stop");
    case Kind::Speaker:      return tr("Mute");
    case Kind::SpeakerMuted: return tr("Unmute");
  }
  return QString();

}

void TransportButton::SetKind(const Kind kind) {

  kind_ = kind;
  setAccessibleName(NameFor(kind));
  setToolTip(NameFor(kind));
  update();

}

QSize TransportButton::sizeHint() const {

  const int unit = std::max(28, fontMetrics().height() * 2);
  const int side = primary_ ? static_cast<int>(unit * 1.55) : unit;
  return QSize(side, side);

}

void TransportButton::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QPalette &pal = palette();
  const bool enabled = isEnabled();
  const QRectF bounds = QRectF(rect()).adjusted(2, 2, -2, -2);
  const QPointF centre = bounds.center();
  const qreal radius = std::min(bounds.width(), bounds.height()) / 2.0;

  QColor ink = enabled ? RoomStyle::Text(pal) : RoomStyle::Faint(pal);

  if (primary_) {
    QColor fill = RoomStyle::Accent(pal);
    if (!enabled) fill = RoomStyle::Mix(RoomStyle::Window(pal), fill, 0.35);
    else if (isDown()) fill = RoomStyle::Mix(fill, RoomStyle::Window(pal), 0.18);
    else if (underMouse()) fill = RoomStyle::Mix(fill, RoomStyle::Text(pal), 0.12);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawEllipse(centre, radius, radius);
    ink = RoomStyle::AccentText(pal);
  }
  else if (underMouse() || isDown()) {
    p.setPen(Qt::NoPen);
    p.setBrush(RoomStyle::Surface(pal, isDown() ? 0.16 : 0.10));
    p.drawEllipse(centre, radius, radius);
  }

  if (hasFocus()) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.85), 2));
    p.drawEllipse(centre, radius, radius);
  }

  const qreal g = radius * (primary_ ? 0.46 : 0.42);  // glyph half size
  p.setPen(Qt::NoPen);
  p.setBrush(ink);

  auto triangle = [&p, &centre](const qreal half, const qreal dx, const bool pointing_right) {
    QPolygonF poly;
    const qreal dir = pointing_right ? 1.0 : -1.0;
    poly << QPointF(centre.x() + dx - dir * half * 0.8, centre.y() - half) << QPointF(centre.x() + dx + dir * half * 0.9, centre.y()) << QPointF(centre.x() + dx - dir * half * 0.8, centre.y() + half);
    p.drawPolygon(poly);
  };

  switch (kind_) {
    case Kind::Play:
      triangle(g * 1.05, g * 0.12, true);
      break;
    case Kind::Pause: {
      const qreal w = g * 0.55;
      p.drawRoundedRect(QRectF(centre.x() - g * 0.75, centre.y() - g, w, g * 2), 1.2, 1.2);
      p.drawRoundedRect(QRectF(centre.x() + g * 0.75 - w, centre.y() - g, w, g * 2), 1.2, 1.2);
      break;
    }
    case Kind::Previous:
      triangle(g * 0.95, g * 0.25, false);
      p.drawRoundedRect(QRectF(centre.x() - g * 1.0, centre.y() - g * 0.95, g * 0.28, g * 1.9), 1, 1);
      break;
    case Kind::Next:
      triangle(g * 0.95, -g * 0.25, true);
      p.drawRoundedRect(QRectF(centre.x() + g * 0.72, centre.y() - g * 0.95, g * 0.28, g * 1.9), 1, 1);
      break;
    case Kind::Stop:
      p.drawRoundedRect(QRectF(centre.x() - g * 0.8, centre.y() - g * 0.8, g * 1.6, g * 1.6), 2, 2);
      break;
    case Kind::Speaker:
    case Kind::SpeakerMuted: {
      QPolygonF cone;
      cone << QPointF(centre.x() - g, centre.y() - g * 0.35) << QPointF(centre.x() - g * 0.45, centre.y() - g * 0.35) << QPointF(centre.x() + g * 0.2, centre.y() - g * 0.95) << QPointF(centre.x() + g * 0.2, centre.y() + g * 0.95)
           << QPointF(centre.x() - g * 0.45, centre.y() + g * 0.35) << QPointF(centre.x() - g, centre.y() + g * 0.35);
      p.drawPolygon(cone);
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap));
      if (kind_ == Kind::Speaker) {
        p.drawArc(QRectF(centre.x() - g * 0.2, centre.y() - g * 0.55, g * 1.1, g * 1.1), -50 * 16, 100 * 16);
        p.drawArc(QRectF(centre.x() - g * 0.2 - g * 0.3, centre.y() - g * 0.85, g * 1.7, g * 1.7), -50 * 16, 100 * 16);
      }
      else {
        p.drawLine(QPointF(centre.x() + g * 0.45, centre.y() - g * 0.5), QPointF(centre.x() + g * 1.15, centre.y() + g * 0.5));
        p.drawLine(QPointF(centre.x() + g * 1.15, centre.y() - g * 0.5), QPointF(centre.x() + g * 0.45, centre.y() + g * 0.5));
      }
      break;
    }
  }

}

// ------------------------------------------------------------------------------------------------------------ TransportBar

TransportBar::TransportBar(QWidget *parent)
    : QWidget(parent),
      state_(State::Stopped),
      compact_(false),
      widget_now_playing_(new QWidget),
      widget_volume_(new QWidget),
      button_previous_(new TransportButton(TransportButton::Kind::Previous)),
      button_play_(new TransportButton(TransportButton::Kind::Play)),
      button_next_(new TransportButton(TransportButton::Kind::Next)),
      button_stop_(new TransportButton(TransportButton::Kind::Stop)),
      button_speaker_(new TransportButton(TransportButton::Kind::Speaker)),
      seek_(new SeekSlider),
      volume_(new SeekSlider),
      label_elapsed_(new QLabel(QStringLiteral("0:00"))),
      label_duration_(new QLabel(QStringLiteral("0:00"))),
      label_title_(new ElidedLabel),
      label_artist_(new ElidedLabel) {

  setAccessibleName(tr("Playback controls"));
  setAutoFillBackground(false);

  button_stop_->SetPrimary(false);
  button_speaker_->SetPrimary(false);

  // Left: what is playing, so the transport still tells you the track on the Playlists and Streaming pages.
  QVBoxLayout *now_playing = new QVBoxLayout(widget_now_playing_);
  now_playing->setContentsMargins(0, 0, 0, 0);
  now_playing->setSpacing(1);
  label_title_->setFont(RoomStyle::Scaled(font(), 1.0, true));
  label_artist_->setFont(font());
  now_playing->addStretch(1);
  now_playing->addWidget(label_title_);
  now_playing->addWidget(label_artist_);
  now_playing->addStretch(1);
  label_title_->setAccessibleName(tr("Now playing title"));
  label_artist_->setAccessibleName(tr("Now playing artist"));

  // Centre: buttons over the seek line.
  QHBoxLayout *buttons = new QHBoxLayout;
  buttons->setSpacing(8);
  buttons->addStretch(1);
  buttons->addWidget(button_stop_);
  buttons->addWidget(button_previous_);
  buttons->addWidget(button_play_);
  buttons->addWidget(button_next_);
  buttons->addSpacing(button_stop_->sizeHint().width());  // keeps the play button optically centred
  buttons->addStretch(1);

  QHBoxLayout *seek_row = new QHBoxLayout;
  seek_row->setSpacing(10);
  label_elapsed_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  label_duration_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  label_elapsed_->setAccessibleName(tr("Elapsed time"));
  label_duration_->setAccessibleName(tr("Track length"));
  const QFontMetrics fm(font());
  label_elapsed_->setMinimumWidth(fm.horizontalAdvance(QStringLiteral("00:00:00")));
  label_duration_->setMinimumWidth(fm.horizontalAdvance(QStringLiteral("00:00:00")));
  seek_row->addWidget(label_elapsed_);
  seek_row->addWidget(seek_, 1);
  seek_row->addWidget(label_duration_);

  QVBoxLayout *centre = new QVBoxLayout;
  centre->setSpacing(2);
  centre->addLayout(buttons);
  centre->addLayout(seek_row);

  // Right: volume. The slider shows the user's own setting; it is only ever changed by the user (or the player's own volume signal).
  QHBoxLayout *volume = new QHBoxLayout(widget_volume_);
  volume->setContentsMargins(0, 0, 0, 0);
  volume->setSpacing(6);
  volume_->setRange(0, 100);
  volume_->SetSteps(5, 10);
  volume_->SetLive(true);
  volume_->SetCanSeek(true);
  volume_->setAccessibleName(tr("Volume"));
  volume_->setMinimumWidth(90);
  volume_->setMaximumWidth(150);
  volume->addStretch(1);
  volume->addWidget(button_speaker_);
  volume->addWidget(volume_, 1);

  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(20, 10, 20, 10);
  layout->setSpacing(20);
  layout->addWidget(widget_now_playing_, 3);
  layout->addLayout(centre, 5);
  layout->addWidget(widget_volume_, 3);

  QObject::connect(button_previous_, &QAbstractButton::clicked, this, &TransportBar::PreviousRequested);
  QObject::connect(button_play_, &QAbstractButton::clicked, this, &TransportBar::PlayPauseRequested);
  QObject::connect(button_next_, &QAbstractButton::clicked, this, &TransportBar::NextRequested);
  QObject::connect(button_stop_, &QAbstractButton::clicked, this, &TransportBar::StopRequested);
  QObject::connect(button_speaker_, &QAbstractButton::clicked, this, &TransportBar::MuteToggleRequested);
  QObject::connect(seek_, &SeekSlider::SeekRequested, this, &TransportBar::SeekRequested);
  QObject::connect(volume_, &SeekSlider::SeekRequested, this, &TransportBar::VolumeRequested);

  SetState(State::Stopped);
  SetPosition(0, 0);
  SetNowPlaying(QString(), QString());

}

void TransportBar::SetState(const State state) {

  state_ = state;
  button_play_->SetKind(state == State::Playing ? TransportButton::Kind::Pause : TransportButton::Kind::Play);
  if (!actions_mirrored_) button_stop_->setEnabled(state != State::Stopped);
  if (state == State::Stopped) SetPosition(0, 0);

}

void TransportBar::SetNowPlaying(const QString &title, const QString &artist) {

  label_title_->setText(title.isEmpty() ? tr("Nothing playing") : title);
  label_title_->SetTone(title.isEmpty() ? ElidedLabel::Tone::Muted : ElidedLabel::Tone::Normal);
  label_artist_->setText(artist);
  label_artist_->SetTone(ElidedLabel::Tone::Muted);

}

void TransportBar::SetPosition(const int position_ms, const int length_ms) {

  seek_->SetPosition(position_ms, length_ms);
  label_elapsed_->setText(length_ms > 0 ? SeekSlider::FormatTime(position_ms) : QStringLiteral("–:––"));
  label_duration_->setText(length_ms > 0 ? SeekSlider::FormatTime(length_ms) : QStringLiteral("–:––"));

}

void TransportBar::SetCanSeek(const bool can_seek) {
  seek_->SetCanSeek(can_seek);
}

void TransportBar::SetVolume(const int volume) {

  const QSignalBlocker blocker(volume_);
  volume_->SetPosition(std::clamp(volume, 0, 100), 100);

}

void TransportBar::SetVolumeEnabled(const bool enabled) {

  widget_volume_->setEnabled(enabled);
  volume_->SetCanSeek(enabled);

}

void TransportBar::SetActionStates(const bool previous, const bool play_pause, const bool next, const bool stop) {

  actions_mirrored_ = true;
  button_previous_->setEnabled(previous);
  button_play_->setEnabled(play_pause);
  button_next_->setEnabled(next);
  button_stop_->setEnabled(stop);

}

void TransportBar::SetMuted(const bool muted) {
  button_speaker_->SetKind(muted ? TransportButton::Kind::SpeakerMuted : TransportButton::Kind::Speaker);
}

QSize TransportBar::sizeHint() const {
  return QSize(900, std::max(84, button_play_->sizeHint().height() + fontMetrics().height() + 34));
}

QSize TransportBar::minimumSizeHint() const {
  return QSize(300, sizeHint().height());
}

void TransportBar::ApplyLayoutMode() {

  // Narrow windows give up the secondary parts first: the now-playing text, then the volume slider (the speaker stays).
  compact_ = width() < 900;
  widget_now_playing_->setVisible(width() >= 820);
  volume_->setVisible(width() >= 640);
  label_elapsed_->setVisible(width() >= 380);
  label_duration_->setVisible(width() >= 380);

}

void TransportBar::resizeEvent(QResizeEvent *e) {

  QWidget::resizeEvent(e);
  ApplyLayoutMode();

}

void TransportBar::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  // A hairline separates the bar from the page above it.
  p.setPen(RoomStyle::Hairline(palette()));
  p.drawLine(0, 0, width(), 0);

}
