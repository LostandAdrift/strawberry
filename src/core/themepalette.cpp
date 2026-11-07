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
#include <cmath>

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QPalette>
#include <QString>
#include <QStringList>

#include "themepalette.h"

using namespace Qt::Literals::StringLiterals;

namespace ThemePalette {

namespace {

constexpr double kTextContrast = 4.5;         // WCAG AA, normal text
constexpr double kPlaceholderContrast = 3.0;  // WCAG AA, large text / UI components
constexpr qint64 kMaxThemeFileSize = 64 * 1024;

QColor Blend(const QColor &a, const QColor &b, const double amount_of_b) {

  const double t = std::clamp(amount_of_b, 0.0, 1.0);
  return QColor::fromRgbF(static_cast<float>(a.redF() * (1.0 - t) + b.redF() * t),
                          static_cast<float>(a.greenF() * (1.0 - t) + b.greenF() * t),
                          static_cast<float>(a.blueF() * (1.0 - t) + b.blueF() * t));

}

QColor ParseHexColor(QString value) {

  value = value.trimmed();
  if (value.startsWith(u'#')) {
    value.remove(0, 1);
  }
  else if (value.startsWith("0x"_L1, Qt::CaseInsensitive)) {
    value.remove(0, 2);
  }
  if (value.size() != 6) return QColor();
  for (const QChar c : std::as_const(value)) {
    if (!c.isDigit() && !(c.toLower() >= u'a' && c.toLower() <= u'f')) return QColor();
  }
  bool ok = false;
  const uint rgb = value.toUInt(&ok, 16);
  return ok ? QColor::fromRgb(static_cast<QRgb>(rgb)) : QColor();

}

QColor Pick(const QMap<QString, QColor> &colors, const QStringList &keys) {

  for (const QString &key : keys) {
    const QColor color = colors.value(key);
    if (color.isValid()) return color;
  }
  return QColor();

}

}  // namespace

QString ThemeToString(const Theme theme) {

  switch (theme) {
    case Theme::AdriftDark: return u"adrift-dark"_s;
    case Theme::Omarchy:    return u"omarchy"_s;
    case Theme::System:     break;
  }
  return u"system"_s;

}

Theme ThemeFromString(const QString &value) {

  if (value.compare("adrift-dark"_L1, Qt::CaseInsensitive) == 0) return Theme::AdriftDark;
  if (value.compare("omarchy"_L1, Qt::CaseInsensitive) == 0) return Theme::Omarchy;
  return Theme::System;

}

QMap<QString, QColor> ParseColorsToml(const QByteArray &data) {

  QMap<QString, QColor> colors;

  const QList<QByteArray> lines = data.split('\n');
  for (const QByteArray &raw_line : lines) {
    QString line = QString::fromUtf8(raw_line).trimmed();
    if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u'[')) continue;

    const qsizetype eq = line.indexOf(u'=');
    if (eq <= 0) continue;

    QString key = line.left(eq).trimmed();
    QString value = line.mid(eq + 1).trimmed();

    // Strip a trailing comment, but only outside of quotes.
    if (value.startsWith(u'"') || value.startsWith(u'\'')) {
      const QChar quote = value.at(0);
      const qsizetype end = value.indexOf(quote, 1);
      if (end < 0) continue;
      value = value.mid(1, end - 1);
    }
    else {
      const qsizetype comment = value.indexOf(u" #"_s);
      if (comment >= 0) value = value.left(comment).trimmed();
    }

    if (key.startsWith(u'"') && key.endsWith(u'"') && key.size() >= 2) key = key.mid(1, key.size() - 2);

    const QColor color = ParseHexColor(value);
    if (color.isValid()) {
      colors.insert(key.toLower(), color);
    }
  }

  return colors;

}

QStringList OmarchyColorsCandidates(const QString &home_dir) {

  const QString home = home_dir.isEmpty() ? QDir::homePath() : home_dir;

  QStringList config_dirs;
  if (home_dir.isEmpty()) {
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty() && QDir::isAbsolutePath(xdg)) config_dirs << xdg;
  }
  config_dirs << home + "/.config"_L1;

  QStringList candidates;
  for (const QString &dir : std::as_const(config_dirs)) {
    const QString path = dir + "/omarchy/current/theme/colors.toml"_L1;
    if (!candidates.contains(path)) candidates << path;
  }
  return candidates;

}

QMap<QString, QColor> LoadOmarchyColors(const QStringList &candidates) {

  for (const QString &path : candidates) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) continue;
    if (file.size() > kMaxThemeFileSize) continue;
    const QMap<QString, QColor> colors = ParseColorsToml(file.readAll());
    if (!colors.isEmpty()) return colors;
  }
  return QMap<QString, QColor>();

}

double RelativeLuminance(const QColor &color) {

  auto channel = [](const double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
  return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());

}

double ContrastRatio(const QColor &a, const QColor &b) {

  const double la = RelativeLuminance(a);
  const double lb = RelativeLuminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);

}

QColor EnsureContrast(const QColor &foreground, const QColor &background, const double min_ratio) {

  if (ContrastRatio(foreground, background) >= min_ratio) return foreground;

  const QColor target = ContrastRatio(Qt::white, background) >= ContrastRatio(Qt::black, background) ? QColor(Qt::white) : QColor(Qt::black);
  for (int step = 1; step <= 50; ++step) {
    const QColor candidate = Blend(foreground, target, step / 50.0);
    if (ContrastRatio(candidate, background) >= min_ratio) return candidate;
  }
  return target;

}

const QMap<QPalette::ColorRole, QColor> &AdriftDarkColors() {

  // Deep blue-black surfaces with a warm paper-white text and a single luminous accent.
  // Text on Window is ~14:1, accent on Base is ~7:1, HighlightedText on Highlight is above 7:1.
  static const QMap<QPalette::ColorRole, QColor> colors = {
    { QPalette::Window, QColor(0x14, 0x17, 0x1f) },
    { QPalette::WindowText, QColor(0xe8, 0xe6, 0xe1) },
    { QPalette::Base, QColor(0x0e, 0x10, 0x16) },
    { QPalette::AlternateBase, QColor(0x18, 0x1c, 0x26) },
    { QPalette::Text, QColor(0xe8, 0xe6, 0xe1) },
    { QPalette::Button, QColor(0x22, 0x27, 0x33) },
    { QPalette::ButtonText, QColor(0xe8, 0xe6, 0xe1) },
    { QPalette::BrightText, QColor(0xff, 0x8a, 0x8a) },
    { QPalette::PlaceholderText, QColor(0x8a, 0x91, 0xa3) },
    { QPalette::ToolTipBase, QColor(0x22, 0x27, 0x33) },
    { QPalette::ToolTipText, QColor(0xe8, 0xe6, 0xe1) },
    { QPalette::Highlight, QColor(0x7f, 0xb4, 0xff) },
    { QPalette::HighlightedText, QColor(0x0b, 0x0e, 0x14) },
    { QPalette::Link, QColor(0x8f, 0xc1, 0xff) }
  };
  return colors;

}

QMap<QPalette::ColorRole, QColor> PaletteFromOmarchyColors(const QMap<QString, QColor> &colors) {

  const QColor background = Pick(colors, { u"background"_s, u"color0"_s });
  const QColor foreground = Pick(colors, { u"foreground"_s, u"color7"_s, u"color15"_s });
  if (!background.isValid() || !foreground.isValid()) return QMap<QPalette::ColorRole, QColor>();

  QColor accent = Pick(colors, { u"accent"_s, u"color4"_s, u"color12"_s });
  if (!accent.isValid()) accent = foreground;
  QColor danger = Pick(colors, { u"color9"_s, u"color1"_s });
  if (!danger.isValid()) danger = QColor(0xff, 0x8a, 0x8a);

  const QColor window = background;
  const QColor base = Blend(background, Qt::black, 0.18);
  const QColor alternate_base = Blend(background, foreground, 0.05);
  const QColor button = Blend(background, foreground, 0.10);

  const QColor text = EnsureContrast(foreground, base, kTextContrast);
  const QColor window_text = EnsureContrast(foreground, window, kTextContrast);
  const QColor button_text = EnsureContrast(foreground, button, kTextContrast);
  const QColor placeholder = EnsureContrast(Blend(foreground, background, 0.45), base, kPlaceholderContrast);

  // Selection: accent as the background, best-contrasting of dark/light text on top.
  const QColor highlight = accent;
  const QColor highlighted_text = ContrastRatio(QColor(0x0b, 0x0e, 0x14), highlight) >= ContrastRatio(QColor(0xff, 0xff, 0xff), highlight) ? QColor(0x0b, 0x0e, 0x14) : QColor(0xff, 0xff, 0xff);

  QMap<QPalette::ColorRole, QColor> result = {
    { QPalette::Window, window },
    { QPalette::WindowText, window_text },
    { QPalette::Base, base },
    { QPalette::AlternateBase, alternate_base },
    { QPalette::Text, text },
    { QPalette::Button, button },
    { QPalette::ButtonText, button_text },
    { QPalette::BrightText, EnsureContrast(danger, window, kTextContrast) },
    { QPalette::PlaceholderText, placeholder },
    { QPalette::ToolTipBase, button },
    { QPalette::ToolTipText, button_text },
    { QPalette::Highlight, highlight },
    { QPalette::HighlightedText, highlighted_text },
    { QPalette::Link, EnsureContrast(accent, window, kTextContrast) }
  };
  return result;

}

QMap<QPalette::ColorRole, QColor> ColorsForTheme(const Theme theme) {

  switch (theme) {
    case Theme::AdriftDark: return AdriftDarkColors();
    case Theme::Omarchy:    return PaletteFromOmarchyColors(LoadOmarchyColors());
    case Theme::System:     break;
  }
  return QMap<QPalette::ColorRole, QColor>();

}

}  // namespace ThemePalette
