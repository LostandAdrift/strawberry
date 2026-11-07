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

#ifndef URLCATCHER_H
#define URLCATCHER_H

#include <QDesktopServices>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

// Stands in for the desktop browser in tests: takes over the handler of one URL scheme, so QDesktopServices::openUrl() never starts a real browser.
class UrlCatcher : public QObject {
  Q_OBJECT

 public:
  explicit UrlCatcher(const QString &scheme, QObject *parent = nullptr) : QObject(parent), scheme_(scheme) {
    QDesktopServices::setUrlHandler(scheme_, this, "Caught");
  }
  ~UrlCatcher() override { QDesktopServices::unsetUrlHandler(scheme_); }

  QString State() const { return urls.isEmpty() ? QString() : QUrlQuery(urls.last()).queryItemValue(QStringLiteral("state")); }

  QList<QUrl> urls;

 public Q_SLOTS:
  void Caught(const QUrl &url) { urls << url; }

 private:
  QString scheme_;
};

#endif  // URLCATCHER_H
