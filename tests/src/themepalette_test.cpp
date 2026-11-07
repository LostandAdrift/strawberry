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

#include "gtest_include.h"

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QPalette>
#include <QString>
#include <QTemporaryDir>

#include "core/themepalette.h"

using namespace ThemePalette;

namespace {

const QByteArray kOmarchyToml =
    "# Example theme\n"
    "accent = \"#7aa2f7\"\n"
    "cursor = \"#c0caf5\"\n"
    "foreground = \"#a9b1d6\"  # trailing comment\n"
    "background = \"#1a1b26\"\n"
    "color0 = \"#32344a\"\n"
    "color1 = '#f7768e'\n"
    "color9 = 0xff7a93\n"
    "not a pair\n"
    "bad = \"#12\"\n"
    "[section]\n"
    "ignored = \"#ffffff\"\n";

}  // namespace

TEST(ThemePaletteTest, ThemeStringRoundTrip) {

  EXPECT_EQ(Theme::System, ThemeFromString(ThemeToString(Theme::System)));
  EXPECT_EQ(Theme::AdriftDark, ThemeFromString(ThemeToString(Theme::AdriftDark)));
  EXPECT_EQ(Theme::Omarchy, ThemeFromString(ThemeToString(Theme::Omarchy)));
  EXPECT_EQ(Theme::System, ThemeFromString(QStringLiteral("garbage")));
  EXPECT_EQ(Theme::System, ThemeFromString(QString()));

}

TEST(ThemePaletteTest, ParseColorsToml) {

  const QMap<QString, QColor> colors = ParseColorsToml(kOmarchyToml);
  EXPECT_EQ(QColor(0x1a, 0x1b, 0x26), colors.value(QStringLiteral("background")));
  EXPECT_EQ(QColor(0xa9, 0xb1, 0xd6), colors.value(QStringLiteral("foreground")));
  EXPECT_EQ(QColor(0x7a, 0xa2, 0xf7), colors.value(QStringLiteral("accent")));
  EXPECT_EQ(QColor(0xf7, 0x76, 0x8e), colors.value(QStringLiteral("color1")));
  EXPECT_EQ(QColor(0xff, 0x7a, 0x93), colors.value(QStringLiteral("color9")));
  EXPECT_FALSE(colors.contains(QStringLiteral("bad")));
  EXPECT_FALSE(colors.contains(QStringLiteral("not a pair")));

}

TEST(ThemePaletteTest, ParseColorsTomlEmptyAndGarbage) {

  EXPECT_TRUE(ParseColorsToml(QByteArray()).isEmpty());
  EXPECT_TRUE(ParseColorsToml(QByteArray("\x00\xff\xfe=\n=\n= =", 10)).isEmpty());
  EXPECT_TRUE(ParseColorsToml(QByteArray("background = \"#zzzzzz\"\nforeground = \"unterminated\n")).isEmpty());

}

TEST(ThemePaletteTest, ContrastRatio) {

  EXPECT_NEAR(21.0, ContrastRatio(Qt::black, Qt::white), 0.01);
  EXPECT_NEAR(1.0, ContrastRatio(Qt::red, Qt::red), 0.001);
  EXPECT_DOUBLE_EQ(ContrastRatio(Qt::black, Qt::white), ContrastRatio(Qt::white, Qt::black));

}

TEST(ThemePaletteTest, EnsureContrast) {

  const QColor bg(0x10, 0x10, 0x10);

  // Already fine: unchanged.
  EXPECT_EQ(QColor(Qt::white), EnsureContrast(Qt::white, bg, 4.5));

  // Poor contrast gets fixed on a dark background by lightening.
  const QColor fixed = EnsureContrast(QColor(0x20, 0x20, 0x20), bg, 4.5);
  EXPECT_GE(ContrastRatio(fixed, bg), 4.5);
  EXPECT_GT(fixed.lightness(), 0x20);

  // And darkening on a light background.
  const QColor light_bg(0xf0, 0xf0, 0xf0);
  const QColor fixed_light = EnsureContrast(QColor(0xe0, 0xe0, 0xe0), light_bg, 4.5);
  EXPECT_GE(ContrastRatio(fixed_light, light_bg), 4.5);

}

TEST(ThemePaletteTest, AdriftDarkMeetsContrastTargets) {

  const QMap<QPalette::ColorRole, QColor> &c = AdriftDarkColors();
  EXPECT_GE(ContrastRatio(c.value(QPalette::WindowText), c.value(QPalette::Window)), 7.0);
  EXPECT_GE(ContrastRatio(c.value(QPalette::Text), c.value(QPalette::Base)), 7.0);
  EXPECT_GE(ContrastRatio(c.value(QPalette::ButtonText), c.value(QPalette::Button)), 7.0);
  EXPECT_GE(ContrastRatio(c.value(QPalette::ToolTipText), c.value(QPalette::ToolTipBase)), 7.0);
  EXPECT_GE(ContrastRatio(c.value(QPalette::HighlightedText), c.value(QPalette::Highlight)), 7.0);
  EXPECT_GE(ContrastRatio(c.value(QPalette::Link), c.value(QPalette::Window)), 4.5);
  EXPECT_GE(ContrastRatio(c.value(QPalette::BrightText), c.value(QPalette::Window)), 4.5);
  EXPECT_GE(ContrastRatio(c.value(QPalette::PlaceholderText), c.value(QPalette::Base)), 3.0);

}

TEST(ThemePaletteTest, PaletteFromOmarchyColors) {

  const QMap<QPalette::ColorRole, QColor> c = PaletteFromOmarchyColors(ParseColorsToml(kOmarchyToml));
  ASSERT_FALSE(c.isEmpty());
  EXPECT_EQ(QColor(0x1a, 0x1b, 0x26), c.value(QPalette::Window));
  EXPECT_EQ(QColor(0x7a, 0xa2, 0xf7), c.value(QPalette::Highlight));
  EXPECT_GE(ContrastRatio(c.value(QPalette::WindowText), c.value(QPalette::Window)), 4.5);
  EXPECT_GE(ContrastRatio(c.value(QPalette::Text), c.value(QPalette::Base)), 4.5);
  EXPECT_GE(ContrastRatio(c.value(QPalette::ButtonText), c.value(QPalette::Button)), 4.5);
  EXPECT_GE(ContrastRatio(c.value(QPalette::HighlightedText), c.value(QPalette::Highlight)), 4.5);
  EXPECT_GE(ContrastRatio(c.value(QPalette::BrightText), c.value(QPalette::Window)), 4.5);

}

TEST(ThemePaletteTest, PaletteFromOmarchyFixesLowContrastTheme) {

  // Foreground nearly equal to background: must still end up readable.
  const QMap<QPalette::ColorRole, QColor> c = PaletteFromOmarchyColors(ParseColorsToml("background = \"#202020\"\nforeground = \"#252525\"\n"));
  ASSERT_FALSE(c.isEmpty());
  EXPECT_GE(ContrastRatio(c.value(QPalette::WindowText), c.value(QPalette::Window)), 4.5);
  EXPECT_GE(ContrastRatio(c.value(QPalette::Text), c.value(QPalette::Base)), 4.5);

}

TEST(ThemePaletteTest, PaletteFromOmarchyLightTheme) {

  const QMap<QPalette::ColorRole, QColor> c = PaletteFromOmarchyColors(ParseColorsToml("background = \"#fafafa\"\nforeground = \"#444444\"\naccent = \"#2a6fdb\"\n"));
  ASSERT_FALSE(c.isEmpty());
  EXPECT_GE(ContrastRatio(c.value(QPalette::Text), c.value(QPalette::Base)), 4.5);
  EXPECT_GE(ContrastRatio(c.value(QPalette::HighlightedText), c.value(QPalette::Highlight)), 4.5);

}

TEST(ThemePaletteTest, PaletteFromOmarchyRequiresBackgroundAndForeground) {

  EXPECT_TRUE(PaletteFromOmarchyColors(QMap<QString, QColor>()).isEmpty());
  EXPECT_TRUE(PaletteFromOmarchyColors(ParseColorsToml("background = \"#000000\"\n")).isEmpty());

}

TEST(ThemePaletteTest, LoadOmarchyColorsFromFile) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.path() + QStringLiteral("/colors.toml");
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(kOmarchyToml);
  file.close();

  // Missing candidates are skipped, the first readable one wins.
  const QMap<QString, QColor> colors = LoadOmarchyColors({ dir.path() + QStringLiteral("/missing.toml"), path });
  EXPECT_EQ(QColor(0x1a, 0x1b, 0x26), colors.value(QStringLiteral("background")));

  EXPECT_TRUE(LoadOmarchyColors({ dir.path() + QStringLiteral("/missing.toml") }).isEmpty());
  EXPECT_TRUE(LoadOmarchyColors(QStringList()).isEmpty());

}

TEST(ThemePaletteTest, LoadOmarchyColorsIgnoresOversizedFile) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.path() + QStringLiteral("/colors.toml");
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(kOmarchyToml);
  file.write(QByteArray(200 * 1024, '#'));
  file.close();

  EXPECT_TRUE(LoadOmarchyColors({ path }).isEmpty());

}

TEST(ThemePaletteTest, OmarchyCandidatesAreFixedPaths) {

  const QStringList candidates = OmarchyColorsCandidates(QStringLiteral("/home/example"));
  ASSERT_EQ(1, candidates.size());
  EXPECT_EQ(QStringLiteral("/home/example/.config/omarchy/current/theme/colors.toml"), candidates.first());

}

TEST(ThemePaletteTest, ColorsForSystemThemeIsEmpty) {

  EXPECT_TRUE(ColorsForTheme(Theme::System).isEmpty());
  EXPECT_FALSE(ColorsForTheme(Theme::AdriftDark).isEmpty());

}
