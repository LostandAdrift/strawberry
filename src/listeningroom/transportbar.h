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

#ifndef TRANSPORTBAR_H
#define TRANSPORTBAR_H

#include "config.h"

#include <QAbstractButton>
#include <QSize>
#include <QString>
#include <QWidget>

class QLabel;
class QPaintEvent;
class QResizeEvent;
class ElidedLabel;
class QEnterEvent;
class QMouseEvent;
class RoomTween;
class SeekSlider;

// A transport button drawn from simple shapes with the palette colors (so it follows every theme and needs no icon theme).
class TransportButton : public QAbstractButton {
  Q_OBJECT

 public:
  enum class Kind {
    Previous,
    Play,
    Pause,
    Next,
    Stop,
    Speaker,
    SpeakerMuted
  };

  explicit TransportButton(const Kind kind, QWidget *parent = nullptr);

  Kind kind() const { return kind_; }
  void SetKind(const Kind kind);
  // The main play/pause button is larger and filled with the accent.
  void SetPrimary(const bool primary) { primary_ = primary; updateGeometry(); update(); }
  bool primary() const { return primary_; }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

 protected:
  void paintEvent(QPaintEvent *e) override;
  void enterEvent(QEnterEvent *e) override;
  void leaveEvent(QEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;

 private:
  static QString NameFor(const Kind kind);

  Kind kind_;
  bool primary_;
  RoomTween *hover_;
  RoomTween *press_;
};

// The persistent bar at the bottom of the Listening Room: what is playing, the transport buttons, seek with elapsed and duration, and volume.
// It only displays state and emits requests; MainWindow connects the requests to the player and the existing actions.
class TransportBar : public QWidget {
  Q_OBJECT

 public:
  enum class State {
    Stopped,
    Playing,
    Paused
  };

  explicit TransportBar(QWidget *parent = nullptr);

  void SetState(const State state);
  State state() const { return state_; }
  void SetNowPlaying(const QString &title, const QString &artist);
  void SetPosition(const int position_ms, const int length_ms);
  void SetCanSeek(const bool can_seek);
  // Volume 0-100 as set by the user. Setting it here never emits VolumeRequested.
  void SetVolume(const int volume);
  void SetVolumeEnabled(const bool enabled);
  void SetMuted(const bool muted);
  // Mirrors the enabled state of the application's own actions (previous, play/pause, next, stop), so a button never looks usable
  // when the action behind it would ignore the click. Once set, the playback state no longer decides these buttons by itself.
  void SetActionStates(const bool previous, const bool play_pause, const bool next, const bool stop);

  TransportButton *previous_button() const { return button_previous_; }
  TransportButton *play_button() const { return button_play_; }
  TransportButton *next_button() const { return button_next_; }
  TransportButton *stop_button() const { return button_stop_; }
  TransportButton *speaker_button() const { return button_speaker_; }
  SeekSlider *seek_slider() const { return seek_; }
  SeekSlider *volume_slider() const { return volume_; }
  QLabel *elapsed_label() const { return label_elapsed_; }
  QLabel *duration_label() const { return label_duration_; }
  ElidedLabel *title_label() const { return label_title_; }
  ElidedLabel *artist_label() const { return label_artist_; }
  bool compact() const { return compact_; }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 Q_SIGNALS:
  void PreviousRequested();
  void PlayPauseRequested();
  void NextRequested();
  void StopRequested();
  void MuteToggleRequested();
  void SeekRequested(const int position_ms);
  void VolumeRequested(const int volume);

 protected:
  void resizeEvent(QResizeEvent *e) override;
  void paintEvent(QPaintEvent *e) override;

 private:
  void ApplyLayoutMode();

  State state_;
  bool actions_mirrored_ = false;
  bool compact_;
  QWidget *widget_now_playing_;
  QWidget *widget_volume_;
  TransportButton *button_previous_;
  TransportButton *button_play_;
  TransportButton *button_next_;
  TransportButton *button_stop_;
  TransportButton *button_speaker_;
  SeekSlider *seek_;
  SeekSlider *volume_;
  QLabel *label_elapsed_;
  QLabel *label_duration_;
  ElidedLabel *label_title_;
  ElidedLabel *label_artist_;
};

#endif  // TRANSPORTBAR_H
