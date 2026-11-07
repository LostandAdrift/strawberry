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

#ifndef THEMEPALETTE_H
#define THEMEPALETTE_H

#include <QByteArray>
#include <QColor>
#include <QMap>
#include <QPalette>
#include <QString>
#include <QStringList>

// Pure palette logic for the Adrift color themes.
// Nothing here executes commands or touches the network.
namespace ThemePalette {

enum class Theme {
  System,
  AdriftDark,
  Omarchy
};

// Resolves the stored theme setting.
// An explicit stored theme wins. Without one, an install that already saved an explicit color choice keeps the system colors,
// and a fresh install gets Adrift Dark.
Theme ResolveTheme(const QString &stored_theme, const bool has_explicit_color_choice);

// Settings values for Theme.
QString ThemeToString(const Theme theme);
Theme ThemeFromString(const QString &value);

// Parses the flat "key = value" subset of TOML used by Omarchy's colors.toml.
// Supports comments, quoted and bare values, and "#rrggbb", "0xrrggbb" and "rrggbb" notations.
// Unknown or malformed lines are skipped.
QMap<QString, QColor> ParseColorsToml(const QByteArray &data);

// Candidate locations for the current Omarchy theme colors, in priority order:
// $XDG_STATE_HOME (default ~/.local/state) /omarchy/current/theme/colors.toml, then the legacy $XDG_CONFIG_HOME (default ~/.config) location.
// If home_dir is given, only paths below it are returned and the environment is ignored (for tests).
QStringList OmarchyColorsCandidates(const QString &home_dir = QString());

// Reads candidates in order and returns the first file that yields a usable palette (a valid background and foreground).
// Unreadable, oversized, invalid or incomplete files are skipped, so a broken state file falls through to the legacy location.
// Returns an empty map if none is usable.
QMap<QString, QColor> LoadOmarchyColors(const QStringList &candidates = OmarchyColorsCandidates());

// Applies theme colors on top of a copy of `base`, so every role not in `colors` (including Highlight, HighlightedText and Link)
// keeps the base value. Passing no colors returns the base unchanged. Text roles are dimmed in the disabled group.
QPalette BuildPalette(const QPalette &base, const QMap<QPalette::ColorRole, QColor> &colors);

// WCAG 2.x relative luminance and contrast ratio.
double RelativeLuminance(const QColor &color);
double ContrastRatio(const QColor &a, const QColor &b);

// Moves `foreground` towards white or black (whichever contrasts more with `background`) until `min_ratio` is reached.
// Returns the input unchanged if it already meets the ratio.
QColor EnsureContrast(const QColor &foreground, const QColor &background, const double min_ratio);

// Premium dark theme.
// Keyed by palette role, includes Highlight, HighlightedText and Link in addition to the configurable roles.
const QMap<QPalette::ColorRole, QColor> &AdriftDarkColors();

// Builds a palette from Omarchy theme colors (background, foreground, accent, color0-15).
// Text roles are corrected to at least WCAG AA contrast (4.5:1) against their background.
// Returns an empty map if the theme has no usable background and foreground.
QMap<QPalette::ColorRole, QColor> PaletteFromOmarchyColors(const QMap<QString, QColor> &colors);

// Resolves the colors for a theme. Empty for System, or if Omarchy has no theme available (callers fall back to the system palette).
QMap<QPalette::ColorRole, QColor> ColorsForTheme(const Theme theme);

}  // namespace ThemePalette

#endif  // THEMEPALETTE_H
