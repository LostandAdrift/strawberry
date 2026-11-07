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

#include <QClipboard>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QWidget>

#include "dialogs/signalpathdialog.h"
#include "engine/signalpath.h"
#include "widgets/signalpathwidget.h"

using namespace SignalPath;

namespace {

Report FlacDirect() {
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

Report LongNames() {
  Source source;
  source.format = QStringLiteral("An extremely long codec description that a broken tag could contain, repeated over and over and over and over");
  source.lossiness = Lossiness::Lossless;
  source.bitdepth = 24;
  source.samplerate = 192000;
  Config config;
  config.output = QStringLiteral("a-very-long-custom-sink-name-that-goes-on-and-on-and-on-and-on");
  config.volume_percent = 100;
  config.equalizer = true;
  return Analyze(source, config);
}

}  // namespace

TEST(SignalPathWidgetTest, HiddenUntilAReportArrivesAndAgainAfterClear) {

  // As in the main window: a child of a visible window, hidden until there is something to show.
  QWidget parent;
  QHBoxLayout *layout = new QHBoxLayout(&parent);
  SignalPathWidget *widget = new SignalPathWidget(&parent);
  layout->addWidget(widget);
  parent.show();

  EXPECT_FALSE(widget->has_report());
  EXPECT_FALSE(widget->isVisible());

  widget->SetReport(FlacDirect());
  EXPECT_TRUE(widget->has_report());
  EXPECT_TRUE(widget->isVisible());

  widget->Clear();
  EXPECT_FALSE(widget->isVisible());
  EXPECT_FALSE(widget->has_report());

}

TEST(SignalPathWidgetTest, ExposesAccessibleNameDescriptionAndTooltip) {

  SignalPathWidget widget;
  widget.SetReport(FlacDirect());
  EXPECT_TRUE(widget.accessibleName().contains(QStringLiteral("FLAC 24/96")));
  EXPECT_TRUE(widget.accessibleName().contains(QStringLiteral("App DSP off")));
  EXPECT_FALSE(widget.accessibleDescription().isEmpty());
  EXPECT_TRUE(widget.toolTip().contains(QStringLiteral("Click for details")));
  EXPECT_EQ(Qt::StrongFocus, widget.focusPolicy());

}

TEST(SignalPathWidgetTest, ActivatesByClickAndKeyboard) {

  SignalPathWidget widget;
  widget.SetReport(FlacDirect());
  widget.show();
  widget.setFocus();
  QSignalSpy clicked(&widget, &QAbstractButton::clicked);

  QTest::mouseClick(&widget, Qt::LeftButton);
  EXPECT_EQ(1, clicked.count());

  QTest::keyClick(&widget, Qt::Key_Space);
  EXPECT_EQ(2, clicked.count());

}

TEST(SignalPathWidgetTest, LongTextIsCappedAndElidedInsteadOfWideningTheWindow) {

  SignalPathWidget widget;
  widget.SetReport(LongNames());

  // The natural size is capped, whatever the tags say.
  const int chrome = 80 + widget.fontMetrics().horizontalAdvance(widget.report().dsp_text);
  EXPECT_LE(widget.sizeHint().width(), SignalPathWidget::kMaximumTextWidth + chrome + 40);
  EXPECT_LT(widget.minimumSizeHint().width(), widget.sizeHint().width());

  // At its natural width the main text is elided, and the DSP chip text is never part of the elided string.
  widget.resize(widget.sizeHint());
  EXPECT_TRUE(widget.ElidedMainText().endsWith(QChar(0x2026)));
  EXPECT_FALSE(widget.ElidedMainText().contains(QStringLiteral("App DSP")));

  // Inside a layout the widget adds only its small minimum to the parent's minimum width, so it cannot force the window wide.
  QWidget parent;
  QHBoxLayout *layout = new QHBoxLayout(&parent);
  layout->addWidget(new QLabel(QStringLiteral("Playlist summary")), 1);
  SignalPathWidget *inner = new SignalPathWidget;
  inner->SetReport(LongNames());
  layout->addWidget(inner);
  EXPECT_LT(parent.minimumSizeHint().width(), 460);

  // The minimum does not depend on how long the tags are: a short report with the same chip needs exactly the same minimum width.
  SignalPathWidget short_one;
  Source source;
  source.format = QStringLiteral("MP3");
  source.lossiness = Lossiness::Lossy;
  Config config;
  config.output = QStringLiteral("pulsesink");
  config.volume_percent = 100;
  config.equalizer = true;  // "App DSP on", like the long report
  short_one.SetReport(Analyze(source, config));
  EXPECT_EQ(short_one.minimumSizeHint().width(), inner->minimumSizeHint().width());

}

TEST(SignalPathWidgetTest, ShortTextIsNotElided) {

  SignalPathWidget widget;
  widget.SetReport(FlacDirect());
  widget.resize(widget.sizeHint());
  EXPECT_EQ(widget.report().badge, widget.ElidedMainText());

}

TEST(SignalPathWidgetTest, ElidesProgressivelyAtCompactWidths) {

  SignalPathWidget widget;
  widget.SetReport(FlacDirect());
  const int natural = widget.sizeHint().width();
  int previous_length = widget.report().badge.size() + 1;
  for (const int width : { natural, natural - 30, natural - 60, widget.minimumSizeHint().width() }) {
    widget.resize(width, widget.sizeHint().height());
    const int length = static_cast<int>(widget.ElidedMainText().size());
    EXPECT_LE(length, previous_length) << width;
    previous_length = length;
  }
  EXPECT_LT(previous_length, widget.report().badge.size());

}

TEST(SignalPathWidgetTest, PaintsAtNormalAndCompactSizes) {

  SignalPathWidget widget;
  widget.SetReport(FlacDirect());
  for (const int width : { widget.sizeHint().width(), widget.minimumSizeHint().width() }) {
    widget.resize(width, widget.sizeHint().height());
    const QImage image = widget.grab().toImage();
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(width, image.width() / static_cast<int>(image.devicePixelRatio()));
    // Something other than a flat background was drawn.
    bool varied = false;
    const QRgb first = image.pixel(image.width() / 2, image.height() / 2);
    for (int x = 0; x < image.width() && !varied; ++x) {
      if (image.pixel(x, image.height() / 2) != first) varied = true;
    }
    EXPECT_TRUE(varied) << width;
  }

}

TEST(SignalPathDialogTest, ShowsSectionsAndCopiesTechnicalDetails) {

  SignalPathDialog dialog;
  const Report report = FlacDirect();
  dialog.SetReport(report);

  EXPECT_EQ(report.technical_text, dialog.technical_text());
  EXPECT_TRUE(dialog.technical_text().contains(QStringLiteral("Source")));
  EXPECT_TRUE(dialog.technical_text().contains(QStringLiteral("Processing")));
  EXPECT_TRUE(dialog.technical_text().contains(QStringLiteral("Output")));
  EXPECT_TRUE(dialog.output_settings_button() != nullptr);

  dialog.copy_button()->click();
  EXPECT_EQ(report.technical_text, QGuiApplication::clipboard()->text());

  // Setting a new report replaces the sections rather than stacking them.
  dialog.SetReport(LongNames());
  EXPECT_EQ(LongNames().technical_text, dialog.technical_text());
  EXPECT_EQ(3, dialog.findChildren<QGroupBox*>().size());

}

TEST(SignalPathDialogTest, OutputSettingsButtonRequestsTheBackendPage) {

  SignalPathDialog dialog;
  dialog.SetReport(FlacDirect());
  QSignalSpy requested(&dialog, &SignalPathDialog::OutputSettingsRequested);
  dialog.output_settings_button()->click();
  EXPECT_EQ(1, requested.count());

}
