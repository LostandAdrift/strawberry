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

// Renders the real Adrift widgets, dialogs and palettes with synthetic data to PNG files, so the look can be reviewed without a desktop session.
// The output directory is set at configure time with -DADRIFT_SCREENSHOT_DIR=<dir>. Without it the scenes are still rendered and checked, but not saved.
// Run with QT_QPA_PLATFORM=offscreen (the CTest registration sets it) so no window ever appears on screen.

#include "gtest_include.h"

#include <functional>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QSlider>
#include <QString>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QWidget>

#include "core/themepalette.h"
#include "dialogs/directalsadialog.h"
#include "dialogs/signalpathdialog.h"
#include "engine/directalsa.h"
#include "engine/signalpath.h"
#include "streaming/playlistimport.h"
#include "widgets/signalpathwidget.h"

#ifndef ADRIFT_SCREENSHOT_DIR
#  define ADRIFT_SCREENSHOT_DIR ""
#endif

using namespace SignalPath;

namespace {

struct Theme {
  QString name;
  QString file;
  QMap<QPalette::ColorRole, QColor> colors;  // empty = system palette
};

// A sample with the same key layout as a real Omarchy theme.
const char kOmarchySample[] =
    "mode = \"dark\"\naccent = \"#faa968\"\nselection = \"#134e5a\"\nmuted = \"#2a6b78\"\nbackground = \"#05182e\"\ndark_background = \"#031222\"\n"
    "darker_background = \"#020c17\"\nlighter_background = \"#0a2540\"\nforeground = \"#f6dcac\"\ndark_foreground = \"#3f8f8a\"\nlight_foreground = \"#a7c9c6\"\n";

QList<Theme> Themes() {
  return {
    { QStringLiteral("System (Fusion)"), QStringLiteral("system"), {} },
    { QStringLiteral("Adrift Dark"), QStringLiteral("adrift-dark"), ThemePalette::AdriftDarkColors() },
    { QStringLiteral("Omarchy sample"), QStringLiteral("omarchy"), ThemePalette::PaletteFromOmarchyColors(ThemePalette::ParseColorsToml(QByteArray(kOmarchySample))) },
  };
}

void Save(const QImage &image, const QString &name) {

  ASSERT_FALSE(image.isNull());
  const QString dir = QString::fromUtf8(ADRIFT_SCREENSHOT_DIR);
  if (dir.isEmpty()) return;
  QDir().mkpath(dir);
  ASSERT_TRUE(image.save(QDir(dir).filePath(name + QStringLiteral(".png")))) << name.toStdString();

}

// Renders a widget that has not been shown: polish, lay out, grab.
QImage Render(QWidget *widget, const QSize &size = QSize()) {

  widget->ensurePolished();
  if (widget->layout()) widget->layout()->activate();
  widget->resize(size.isValid() ? size : widget->sizeHint().expandedTo(widget->minimumSizeHint()));
  if (widget->layout()) widget->layout()->activate();
  return widget->grab().toImage();

}

QPalette PaletteFor(const Theme &theme) {
  return ThemePalette::BuildPalette(QApplication::style()->standardPalette(), theme.colors);
}

Report Flac24Direct() {
  Source source;
  source.format = QStringLiteral("FLAC");
  source.lossiness = Lossiness::Lossless;
  source.bitdepth = 24;
  source.samplerate = 96000;
  Config config;
  config.output = QStringLiteral("alsasink");
  config.device = QStringLiteral("hw:CARD=Example,DEV=0");
  config.volume_percent = 100;
  return Analyze(source, config);
}

Report Flac16Mixer() {
  Source source;
  source.format = QStringLiteral("FLAC");
  source.lossiness = Lossiness::Lossless;
  source.bitdepth = 16;
  source.samplerate = 44100;
  source.origin = QStringLiteral("Tidal");
  Config config;
  config.output = QStringLiteral("pulsesink");
  config.volume_percent = 100;
  return Analyze(source, config);
}

Report Mp3WithDsp() {
  Source source;
  source.format = QStringLiteral("MP3");
  source.lossiness = Lossiness::Lossy;
  source.bitrate = 320;
  Config config;
  config.output = QStringLiteral("alsasink");
  config.device = QStringLiteral("plughw:CARD=Example,DEV=0");
  config.volume_percent = 72;
  config.replaygain = true;
  config.equalizer = true;
  return Analyze(source, config);
}

Report StreamPending() {
  Source source;
  source.format = QStringLiteral("Stream");
  Config config;
  config.output = QStringLiteral("autoaudiosink");
  config.volume_percent = 100;
  return Analyze(source, config);
}

Report LongTagged() {
  Source source;
  source.format = QStringLiteral("An extremely long codec description from a damaged tag that keeps going");
  source.lossiness = Lossiness::Lossless;
  source.bitdepth = 24;
  source.samplerate = 192000;
  Config config;
  config.output = QStringLiteral("pipewiresink");
  config.volume_percent = 100;
  return Analyze(source, config);
}

// A strip that looks like the main window's status row: a stretchy summary label plus the badge.
QWidget *StatusStrip(const Report &report, const int width) {

  QWidget *strip = new QWidget;
  QHBoxLayout *layout = new QHBoxLayout(strip);
  layout->setContentsMargins(8, 4, 8, 4);
  layout->addWidget(new QLabel(QStringLiteral("12 tracks · 54:12")), 1);
  SignalPathWidget *badge = new SignalPathWidget;
  badge->SetReport(report);
  layout->addWidget(badge);
  strip->setFixedWidth(width);
  return strip;

}

// Builds a themed sheet of the controls Adrift's windows are made of, so each palette can be judged on real widgets.
QWidget *ControlSheet(const Theme &theme) {

  QWidget *sheet = new QWidget;
  sheet->setPalette(PaletteFor(theme));
  sheet->setAutoFillBackground(true);
  QVBoxLayout *layout = new QVBoxLayout(sheet);

  QLabel *title = new QLabel(theme.name);
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 4);
  title_font.setBold(true);
  title->setFont(title_font);
  layout->addWidget(title);

  QHBoxLayout *row = new QHBoxLayout;
  row->addWidget(new QPushButton(QStringLiteral("Play")));
  QPushButton *disabled = new QPushButton(QStringLiteral("Disabled"));
  disabled->setEnabled(false);
  row->addWidget(disabled);
  QCheckBox *check = new QCheckBox(QStringLiteral("Checked option"));
  check->setChecked(true);
  row->addWidget(check);
  row->addWidget(new QCheckBox(QStringLiteral("Unchecked")));
  layout->addLayout(row);

  QLineEdit *edit = new QLineEdit;
  edit->setPlaceholderText(QStringLiteral("Search your collection"));
  layout->addWidget(edit);
  QLineEdit *filled = new QLineEdit(QStringLiteral("Aphex Twin"));
  layout->addWidget(filled);

  QComboBox *combo = new QComboBox;
  combo->addItems({ QStringLiteral("Direct ALSA"), QStringLiteral("System mixer") });
  layout->addWidget(combo);

  QListWidget *list = new QListWidget;
  list->addItems({ QStringLiteral("01  Windowlicker"), QStringLiteral("02  Xtal"), QStringLiteral("03  Selected Ambient Works"), QStringLiteral("04  Flim") });
  list->setCurrentRow(2);
  list->setAlternatingRowColors(true);
  list->setMaximumHeight(120);
  layout->addWidget(list);

  QSlider *slider = new QSlider(Qt::Horizontal);
  slider->setValue(40);
  layout->addWidget(slider);
  QProgressBar *progress = new QProgressBar;
  progress->setValue(62);
  layout->addWidget(progress);

  SignalPathWidget *badge = new SignalPathWidget;
  badge->SetReport(Flac24Direct());
  layout->addWidget(badge, 0, Qt::AlignLeft);
  SignalPathWidget *badge_dsp = new SignalPathWidget;
  badge_dsp->SetReport(Mp3WithDsp());
  layout->addWidget(badge_dsp, 0, Qt::AlignLeft);

  sheet->setFixedWidth(500);
  return sheet;

}

class RenderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    saved_style_ = QApplication::style()->objectName();
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  }
  void TearDown() override { QApplication::setStyle(saved_style_); }

  QString saved_style_;
};

}  // namespace

TEST_F(RenderTest, ThemedControlSheets) {

  for (const Theme &theme : Themes()) {
    QWidget *sheet = ControlSheet(theme);
    const QImage image = Render(sheet);
    EXPECT_GT(image.width(), 300);
    EXPECT_GT(image.height(), 300);
    // The background of a themed sheet is the theme's window color.
    const QColor expected = PaletteFor(theme).color(QPalette::Window);
    EXPECT_EQ(expected.rgb(), image.pixelColor(2, 2).rgb()) << theme.name.toStdString();
    Save(image, QStringLiteral("theme_") + theme.file);
    delete sheet;
  }

}

TEST_F(RenderTest, SignalPathBadgesAtSeveralWidthsAndThemes) {

  struct Scene {
    QString label;
    Report report;
  };
  const QList<Scene> scenes = {
    { QStringLiteral("Hi-Res over direct ALSA, app DSP off"), Flac24Direct() },
    { QStringLiteral("Tidal CD quality through the system mixer"), Flac16Mixer() },
    { QStringLiteral("Lossy source, plug layer, app DSP on"), Mp3WithDsp() },
    { QStringLiteral("Stream, metadata not arrived yet"), StreamPending() },
    { QStringLiteral("Very long tag text"), LongTagged() },
  };

  for (const Theme &theme : Themes()) {
    QWidget sheet;
    sheet.setPalette(PaletteFor(theme));
    sheet.setAutoFillBackground(true);
    QVBoxLayout *layout = new QVBoxLayout(&sheet);
    QLabel *title = new QLabel(QStringLiteral("Signal path badge: %1").arg(theme.name));
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    layout->addWidget(title);

    for (const Scene &scene : scenes) {
      layout->addWidget(new QLabel(scene.label));
      // Normal width (a wide window) and compact width (a narrow window), as in the main window status row.
      layout->addWidget(StatusStrip(scene.report, 560));
      layout->addWidget(StatusStrip(scene.report, 420));
      layout->addWidget(StatusStrip(scene.report, 300));
    }
    sheet.setFixedWidth(600);
    sheet.setMinimumHeight(0);
    const QImage image = Render(&sheet);
    EXPECT_GT(image.height(), 400);
    Save(image, QStringLiteral("signalpath_badges_") + theme.file);
  }

}

TEST_F(RenderTest, SignalPathDialog) {

  for (const Theme &theme : { Themes()[1], Themes()[0] }) {
    SignalPathDialog dialog;
    dialog.setPalette(PaletteFor(theme));
    dialog.SetReport(Flac16Mixer());
    const QImage image = Render(&dialog);
    EXPECT_LE(image.height() / static_cast<int>(image.devicePixelRatio()), SignalPathDialog::kMaximumPreferredHeight);
    Save(image, QStringLiteral("signalpath_dialog_") + theme.file);
  }

  SignalPathDialog dsp;
  dsp.setPalette(PaletteFor(Themes()[1]));
  dsp.SetReport(Mp3WithDsp());
  Save(Render(&dsp), QStringLiteral("signalpath_dialog_dsp_on"));

  // The advanced text expanded, still within the same modest height.
  SignalPathDialog expanded;
  expanded.setPalette(PaletteFor(Themes()[1]));
  expanded.SetReport(Mp3WithDsp());
  expanded.details_toggle()->setChecked(true);
  Save(Render(&expanded), QStringLiteral("signalpath_dialog_details_expanded"));

}

TEST_F(RenderTest, DirectAlsaDialog) {

  DirectAlsa::Settings current;
  current.output = QStringLiteral("pulsesink");
  current.replaygain = true;
  current.bs2b = true;
  current.crossfade = true;
  const QList<QPair<QString, QString>> devices = {
    { QStringLiteral("Example USB DAC (hw)"), QStringLiteral("hw:CARD=Example,DEV=0") },
    { QStringLiteral("Onboard analog (hw)"), QStringLiteral("hw:CARD=PCH,DEV=0") },
    { QStringLiteral("Default"), QStringLiteral("default") },
  };

  {
    DirectAlsaDialog dialog(devices, current, DirectAlsa::Warnings(true, 72, true, false), false);
    dialog.setPalette(PaletteFor(Themes()[1]));
    Save(Render(&dialog, QSize(560, 640)), QStringLiteral("direct_alsa_initial"));
  }
  {
    DirectAlsaDialog dialog(devices, current, DirectAlsa::Warnings(true, 72, true, false), true);
    dialog.setPalette(PaletteFor(Themes()[1]));
    dialog.device_combo()->setCurrentIndex(1);
    EXPECT_TRUE(dialog.apply_button()->isEnabled());
    Save(Render(&dialog, QSize(560, 640)), QStringLiteral("direct_alsa_chosen"));
  }

}
