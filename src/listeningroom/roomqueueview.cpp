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

#include <QAbstractItemView>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QListView>
#include <QMetaObject>
#include <QModelIndex>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QRect>
#include <QScrollBar>
#include <QSize>
#include <QString>
#include <QStyleOptionViewItem>

#include "roomqueueview.h"
#include "roomqueuemodel.h"
#include "roomstyle.h"

// ------------------------------------------------------------------------------------------------------------ delegate

QString RoomQueueDelegate::FormatLength(const int seconds) {

  if (seconds <= 0) return QString();
  const int hours = seconds / 3600;
  const int minutes = (seconds / 60) % 60;
  const int secs = seconds % 60;
  if (hours > 0) return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
  return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));

}

QSize RoomQueueDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {

  Q_UNUSED(index)
  const QFontMetrics fm(option.font);
  return QSize(200, std::max(44, fm.height() * 2 + 14));

}

void RoomQueueDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);

  const QPalette &pal = option.palette;
  const bool current = index.data(RoomQueueModel::IsCurrentRole).toBool();
  const bool hovered = option.state & QStyle::State_MouseOver;
  const bool focused = (option.state & QStyle::State_HasFocus) && (option.state & QStyle::State_Selected);
  const bool unavailable = index.data(RoomQueueModel::UnavailableRole).toBool();

  const QRect row = option.rect.adjusted(6, 1, -6, -1);

  // Background: the current track is lifted and tinted with the accent; hover and keyboard selection use a quiet surface.
  if (current) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Accent(pal), 0.16));
    painter->drawRoundedRect(row, 8, 8);
    painter->setBrush(RoomStyle::Accent(pal));
    painter->drawRoundedRect(QRect(row.left(), row.top() + 8, 3, row.height() - 16), 1.5, 1.5);
  }
  else if (hovered || (option.state & QStyle::State_Selected)) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(RoomStyle::Surface(pal, (option.state & QStyle::State_Selected) ? 0.10 : 0.06));
    painter->drawRoundedRect(row, 8, 8);
  }
  if (focused) {
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.7), 1.5));
    painter->drawRoundedRect(row.adjusted(1, 1, -1, -1), 8, 8);
  }

  const QFontMetrics fm(option.font);
  const int gutter = std::max(36, fm.horizontalAdvance(QStringLiteral("0000")) + 8);
  const QRect gutter_rect(row.left() + 8, row.top(), gutter, row.height());

  QColor title_color = RoomStyle::Text(pal);
  QColor artist_color = RoomStyle::Muted(pal);
  QColor number_color = RoomStyle::Faint(pal);
  if (unavailable) {
    title_color = RoomStyle::Muted(pal);
    artist_color = RoomStyle::Faint(pal);
    number_color = RoomStyle::Faint(pal);
  }

  // Gutter: an equalizer glyph for the current track, otherwise its position in the playlist. No row is marked as "next" or "played":
  // the playlist order does not say what plays next.
  if (current) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(RoomStyle::Accent(pal));
    const int bar_w = 3;
    const int base_y = gutter_rect.center().y() + 7;
    const int heights[3] = { 9, 14, 6 };
    for (int i = 0; i < 3; ++i) {
      painter->drawRoundedRect(QRect(gutter_rect.center().x() - 8 + i * 6, base_y - heights[i], bar_w, heights[i]), 1.2, 1.2);
    }
  }
  else {
    painter->setFont(option.font);
    painter->setPen(number_color);
    painter->drawText(gutter_rect, Qt::AlignCenter, QString::number(index.data(RoomQueueModel::PositionRole).toInt()));
  }

  const int right_padding = 10;
  const QString length = RoomQueueDelegate::FormatLength(index.data(RoomQueueModel::LengthSecondsRole).toInt());
  const int length_width = fm.horizontalAdvance(QStringLiteral("0:00:00"));
  const QRect length_rect(row.right() - right_padding - length_width, row.top(), length_width, row.height());
  painter->setPen(artist_color);
  painter->drawText(length_rect, Qt::AlignRight | Qt::AlignVCenter, length);

  const int text_left = gutter_rect.right() + 6;
  const int text_width = std::max(0, length_rect.left() - 10 - text_left);
  const int line_height = fm.height();
  const int top = row.top() + (row.height() - line_height * 2 - 2) / 2;

  QFont title_font = option.font;
  if (current) title_font.setWeight(QFont::DemiBold);
  painter->setFont(title_font);
  painter->setPen(title_color);
  const QString title = index.data(RoomQueueModel::TitleRole).toString();
  painter->drawText(QRect(text_left, top, text_width, line_height), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(title_font).elidedText(title.isEmpty() ? QObject::tr("Untitled") : title, Qt::ElideRight, text_width));

  painter->setFont(option.font);
  painter->setPen(artist_color);
  QString artist = index.data(RoomQueueModel::ArtistRole).toString();
  const QString album = index.data(RoomQueueModel::AlbumRole).toString();
  if (!album.isEmpty()) artist = artist.isEmpty() ? album : artist + QStringLiteral(" · ") + album;
  painter->drawText(QRect(text_left, top + line_height + 2, text_width, line_height), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(artist, Qt::ElideRight, text_width));

  painter->restore();

}

// ------------------------------------------------------------------------------------------------------------ view

RoomQueueView::RoomQueueView(QWidget *parent) : QListView(parent) {

  setItemDelegate(new RoomQueueDelegate(this));
  setUniformItemSizes(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setFrameShape(QFrame::NoFrame);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(tr("Playlist tracks"));
  // The list paints its own background so it blends with the page.
  viewport()->setAutoFillBackground(false);
  setAutoFillBackground(false);

  // A scroll bar action from the user (drag, wheel, keys) means they want to look elsewhere, so the view stops following the current track.
  QObject::connect(verticalScrollBar(), &QScrollBar::actionTriggered, this, [this]() { if (!applying_scroll_) follow_current_ = false; });

  QObject::connect(this, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex &index) { if (index.isValid()) Q_EMIT RowActivated(index.row()); });

}

void RoomQueueView::SetEmptyText(const QString &title, const QString &hint) {

  empty_title_ = title;
  empty_hint_ = hint;
  viewport()->update();

}

void RoomQueueView::ScrollToCurrent() {

  // The view may not have its final size yet (it is created hidden), so the scroll is retried when it is shown or resized.
  follow_current_ = true;
  ApplyScroll();
  // The list lays out its items later; try again once it has.
  QMetaObject::invokeMethod(this, [this]() { if (follow_current_) ApplyScroll(); }, Qt::QueuedConnection);

}

bool RoomQueueView::ApplyScroll() {

  RoomQueueModel *queue = qobject_cast<RoomQueueModel*>(model());
  if (!queue || !follow_current_) return false;
  const int current = queue->current_row();
  if (current < 0 || current >= queue->rowCount() || !queue->playback_active()) {
    follow_current_ = false;
    return false;
  }
  if (viewport()->height() < 40) return false;  // not laid out yet
  doItemsLayout();  // the scroll range is only valid after the items are laid out

  // Whole rows: one played track is kept above the current one for context, the tracks that follow fill the rest.
  // Near the end of a long list the current row cannot reach the top; then the scroll stops at the last whole row that fits.
  const int row_height = std::max(1, sizeHintForRow(0));
  const int wanted = std::max(0, (current - 1) * row_height);
  const int limit = verticalScrollBar()->maximum();
  const int value = wanted <= limit ? wanted : (limit / row_height) * row_height;
  applying_scroll_ = true;
  verticalScrollBar()->setValue(value);
  applying_scroll_ = false;
  return true;

}

void RoomQueueView::resizeEvent(QResizeEvent *e) {

  QListView::resizeEvent(e);
  if (follow_current_) ApplyScroll();

}

void RoomQueueView::showEvent(QShowEvent *e) {

  QListView::showEvent(e);
  if (follow_current_) ApplyScroll();

}

void RoomQueueView::keyPressEvent(QKeyEvent *e) {

  if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && currentIndex().isValid()) {
    Q_EMIT RowActivated(currentIndex().row());
    return;
  }
  QListView::keyPressEvent(e);

}

void RoomQueueView::paintEvent(QPaintEvent *e) {

  QListView::paintEvent(e);

  if (model() && model()->rowCount() > 0) return;

  // An empty playlist explains itself instead of showing a blank area.
  QPainter p(viewport());
  p.setRenderHint(QPainter::Antialiasing);
  const QPalette &pal = palette();
  const QRect area = viewport()->rect().adjusted(24, 0, -24, 0);

  p.setFont(RoomStyle::Scaled(font(), 1.15, true));
  p.setPen(RoomStyle::Muted(pal));
  const int title_height = QFontMetrics(p.font()).height();
  const int top = area.height() / 3;
  p.drawText(QRect(area.left(), top, area.width(), title_height), Qt::AlignHCenter | Qt::AlignVCenter, empty_title_);
  p.setFont(font());
  p.setPen(RoomStyle::Faint(pal));
  p.drawText(QRect(area.left(), top + title_height + 6, area.width(), area.height() - top - title_height - 6), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, empty_hint_);

}
