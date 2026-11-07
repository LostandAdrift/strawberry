/*
 * Strawberry Music Player
 * Copyright 2026, Jonas Kvinge <jonas@jkvinge.net>
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

#include <QApplication>
#include <QObject>
#include <QThread>
#include <QList>
#include <QMap>
#include <QPalette>
#include <QColor>

#include "settings.h"
#include "appearance.h"
#include "themepalette.h"
#include "constants/appearancesettings.h"

Appearance::Appearance(QObject *parent) : QObject(parent), system_palette_(QApplication::palette()) {}

const QList<Appearance::ColorRole> &Appearance::ColorRoles() {

  static const QList<ColorRole> color_roles = {
    { QPalette::Window, QLatin1String(AppearanceSettings::kColorWindow) },
    { QPalette::WindowText, QLatin1String(AppearanceSettings::kColorWindowText) },
    { QPalette::Base, QLatin1String(AppearanceSettings::kColorBase) },
    { QPalette::AlternateBase, QLatin1String(AppearanceSettings::kColorAlternateBase) },
    { QPalette::Text, QLatin1String(AppearanceSettings::kColorText) },
    { QPalette::Button, QLatin1String(AppearanceSettings::kColorButton) },
    { QPalette::ButtonText, QLatin1String(AppearanceSettings::kColorButtonText) },
    { QPalette::BrightText, QLatin1String(AppearanceSettings::kColorBrightText) },
    { QPalette::PlaceholderText, QLatin1String(AppearanceSettings::kColorPlaceholderText) },
    { QPalette::ToolTipBase, QLatin1String(AppearanceSettings::kColorToolTipBase) },
    { QPalette::ToolTipText, QLatin1String(AppearanceSettings::kColorToolTipText) }
  };

  return color_roles;

}

const QMap<QPalette::ColorRole, QColor> &Appearance::DarkColors() {

  static const QMap<QPalette::ColorRole, QColor> dark_colors = {
    { QPalette::Window, QColor(53, 53, 53) },
    { QPalette::WindowText, QColor(240, 240, 240) },
    { QPalette::Base, QColor(35, 35, 35) },
    { QPalette::AlternateBase, QColor(53, 53, 53) },
    { QPalette::Text, QColor(240, 240, 240) },
    { QPalette::Button, QColor(53, 53, 53) },
    { QPalette::ButtonText, QColor(240, 240, 240) },
    { QPalette::BrightText, QColor(255, 80, 80) },
    { QPalette::PlaceholderText, QColor(140, 140, 140) },
    { QPalette::ToolTipBase, QColor(53, 53, 53) },
    { QPalette::ToolTipText, QColor(240, 240, 240) }
  };

  return dark_colors;

}

void Appearance::LoadCustomPaletteColors() {

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  const bool use_custom_color_set = s.value(AppearanceSettings::kUseCustomColorSet).toBool();
  const ThemePalette::Theme theme = StoredTheme();

  // A named theme takes precedence over the custom color set.
  // If the theme cannot be resolved (e.g. Omarchy is not installed) fall through to the custom/system colors.
  const QMap<QPalette::ColorRole, QColor> theme_colors = ThemePalette::ColorsForTheme(theme);
  if (!theme_colors.isEmpty()) {
    SetCustomPaletteColors(theme_colors);
  }
  else if (use_custom_color_set) {
    QMap<QPalette::ColorRole, QColor> colors;
    for (const ColorRole &color_role : ColorRoles()) {
      const QVariant value = s.value(color_role.settings_key);
      if (value.isValid()) {
        const QColor color = value.value<QColor>();
        if (color.isValid()) {
          colors.insert(color_role.role, color);
        }
      }
    }
    SetCustomPaletteColors(colors);
  }

  s.endGroup();

}

void Appearance::SetCustomPaletteColors(const QMap<QPalette::ColorRole, QColor> &colors) {

  Q_ASSERT(QThread::currentThread() == qApp->thread());

  QApplication::setPalette(ThemePalette::BuildPalette(QApplication::palette(), colors));

}

ThemePalette::Theme Appearance::StoredTheme() {

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  // An existing explicit color choice (custom or system) is honored; only installs without one get the premium default.
  const bool explicit_choice = s.contains(AppearanceSettings::kUseCustomColorSet);
  const ThemePalette::Theme theme = ThemePalette::ResolveTheme(s.value(AppearanceSettings::kColorTheme).toString(), explicit_choice);
  s.endGroup();
  return theme;

}
