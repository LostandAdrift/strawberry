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

#ifndef ROOMQUEUEVIEW_H
#define ROOMQUEUEVIEW_H

#include "config.h"

#include <QListView>
#include <QSize>
#include <QStyledItemDelegate>
#include <QString>

class QPaintEvent;
class QResizeEvent;
class QShowEvent;

// Compact playlist rows: a gutter with the position (an equalizer glyph for the current track), title over artist, and the length.
// The current track has an accent bar. Every row shows its position in the playlist; no row is marked as next or played.
class RoomQueueDelegate : public QStyledItemDelegate {
  Q_OBJECT

 public:
  explicit RoomQueueDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

  static QString FormatLength(const int seconds);
};

class RoomQueueView : public QListView {
  Q_OBJECT

 public:
  explicit RoomQueueView(QWidget *parent = nullptr);

  void SetEmptyText(const QString &title, const QString &hint);
  // Scrolls so that the current track is visible with a few tracks of context after it.
  void ScrollToCurrent();

 Q_SIGNALS:
  // Double click or Enter on a row. The row is the playlist row.
  void RowActivated(const int row);

 protected:
  void paintEvent(QPaintEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;
  void showEvent(QShowEvent *e) override;

 private:
  bool ApplyScroll();

  QString empty_title_;
  QString empty_hint_;
  // True while the view keeps the current track in place: set when the track changes, cleared when the user scrolls.
  bool follow_current_ = false;
  bool applying_scroll_ = false;
};

#endif  // ROOMQUEUEVIEW_H
