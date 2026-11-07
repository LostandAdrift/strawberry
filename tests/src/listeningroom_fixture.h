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

#ifndef LISTENINGROOM_FIXTURE_H
#define LISTENINGROOM_FIXTURE_H

// Synthetic data for the Listening Room tests and renders: invented artists and albums, covers drawn by code, and a REAL Playlist model
// (no database, no collection). Nothing here reads the user's configuration, library or accounts.

#include <QColor>
#include <QImage>
#include <QLinearGradient>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QRadialGradient>
#include <QString>
#include <QUrl>

#include "core/song.h"
#include "playlist/playlist.h"
#include "playlist/playlistitem.h"
#include "playlist/playlistsequence.h"
#include "mock_settingsprovider.h"

namespace RoomFixture {

struct Track {
  QString title;
  QString artist;
  QString album;
  int seconds;
};

inline QList<Track> Tracks() {
  return {
    { QStringLiteral("Salt Light"), QStringLiteral("Marin Holt"), QStringLiteral("Slow Tide"), 252 },
    { QStringLiteral("Harbour Lamps"), QStringLiteral("Marin Holt"), QStringLiteral("Slow Tide"), 318 },
    { QStringLiteral("Low Water"), QStringLiteral("Marin Holt"), QStringLiteral("Slow Tide"), 207 },
    { QStringLiteral("A Quiet Engine"), QStringLiteral("The Paper Lanterns"), QStringLiteral("Night Ferry"), 281 },
    { QStringLiteral("Overland"), QStringLiteral("The Paper Lanterns"), QStringLiteral("Night Ferry"), 344 },
    { QStringLiteral("Copper Hour"), QStringLiteral("Ines Varga"), QStringLiteral("Weather for Strings"), 196 },
    { QStringLiteral("Fog Signals"), QStringLiteral("Ines Varga"), QStringLiteral("Weather for Strings"), 263 },
    { QStringLiteral("The Long Way Round"), QStringLiteral("Ines Varga"), QStringLiteral("Weather for Strings"), 411 },
    { QStringLiteral("Glass Orchard"), QStringLiteral("Oren & Wilde"), QStringLiteral("Small Hours"), 229 },
    { QStringLiteral("Tidewrack"), QStringLiteral("Oren & Wilde"), QStringLiteral("Small Hours"), 302 },
    { QStringLiteral("Paper Boats"), QStringLiteral("Oren & Wilde"), QStringLiteral("Small Hours"), 175 },
    { QStringLiteral("Lantern Fish"), QStringLiteral("Kestrel Quartet"), QStringLiteral("Open Water"), 289 },
    { QStringLiteral("Evening, Eventually"), QStringLiteral("Kestrel Quartet"), QStringLiteral("Open Water"), 356 },
    { QStringLiteral("Last Ferry Home"), QStringLiteral("Kestrel Quartet"), QStringLiteral("Open Water"), 241 },
  };
}

inline Song MakeSong(const Track &track) {

  Song song(Song::Source::Stream);
  song.Init(track.title, track.artist, track.album, static_cast<qint64>(track.seconds) * 1000000000LL);
  song.set_url(QUrl(QStringLiteral("http://example.invalid/") + QString::fromLatin1(QUrl::toPercentEncoding(track.title))));
  return song;

}

// A sleeve drawn by code: a dusk gradient, a low sun and layered swells. `variant` changes the hues.
inline QImage Cover(const int variant = 0, const int size = 900) {

  QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);

  const QList<QPair<QColor, QColor>> palettes = {
    { QColor(0x14, 0x32, 0x3c), QColor(0xc9, 0x8a, 0x52) },
    { QColor(0x2a, 0x1f, 0x3d), QColor(0xd1, 0x7a, 0x6a) },
    { QColor(0x1d, 0x2f, 0x26), QColor(0xd8, 0xc0, 0x7a) },
  };
  const auto colors = palettes.at(variant % palettes.size());

  QLinearGradient sky(0, 0, 0, size);
  sky.setColorAt(0.0, colors.first.darker(150));
  sky.setColorAt(0.62, colors.first);
  sky.setColorAt(1.0, colors.second.darker(120));
  p.fillRect(image.rect(), sky);

  QRadialGradient sun(size * 0.62, size * 0.52, size * 0.26);
  sun.setColorAt(0.0, colors.second.lighter(140));
  sun.setColorAt(0.5, colors.second);
  sun.setColorAt(1.0, QColor(colors.second.red(), colors.second.green(), colors.second.blue(), 0));
  p.setPen(Qt::NoPen);
  p.setBrush(sun);
  p.drawEllipse(QPointF(size * 0.62, size * 0.52), size * 0.26, size * 0.26);

  for (int layer = 0; layer < 5; ++layer) {
    QPainterPath wave;
    const qreal base = size * (0.58 + layer * 0.09);
    wave.moveTo(0, size);
    wave.lineTo(0, base);
    for (int x = 0; x <= size; x += 12) {
      wave.lineTo(x, base + std::sin(x / (size * 0.11) + layer * 1.4 + variant) * size * (0.018 + layer * 0.004));
    }
    wave.lineTo(size, size);
    wave.closeSubpath();
    QColor tone = colors.first;
    tone.setAlphaF(0.30 + layer * 0.14);
    p.setBrush(tone.darker(100 + layer * 22));
    p.drawPath(wave);
  }
  return image;

}

// Owns a real Playlist filled with the synthetic tracks. The playlist has no backend: nothing is stored anywhere.
class QueueFixture {
 public:
  explicit QueueFixture(const int count = 14, const int playlist_id = 1)
      : sequence_(nullptr, new DummySettingsProvider),
        playlist_(nullptr, nullptr, nullptr, nullptr, nullptr, playlist_id) {

    playlist_.set_sequence(&sequence_);
    const QList<Track> tracks = Tracks();
    PlaylistItemPtrList items;
    for (int i = 0; i < count; ++i) items << PlaylistItem::NewFromSong(MakeSong(tracks.at(i % tracks.size())));
    playlist_.InsertItems(items);

  }

  Playlist *playlist() { return &playlist_; }
  void SetCurrent(const int row) { playlist_.set_current_row(row); }

 private:
  PlaylistSequence sequence_;
  Playlist playlist_;
};

}  // namespace RoomFixture

#endif  // LISTENINGROOM_FIXTURE_H
