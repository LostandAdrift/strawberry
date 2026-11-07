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

#include <algorithm>

#include <QClipboard>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
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

  // The minimum is capped, so it does not grow with the length of the tags: two different very long reports need the same minimum width.
  SignalPathWidget other_long;
  Source source;
  source.format = QStringLiteral("Another absurdly long codec description that must not make the minimum width any larger than the cap allows");
  source.lossiness = Lossiness::Lossless;
  source.bitdepth = 16;
  source.samplerate = 44100;
  Config config;
  config.output = QStringLiteral("pulsesink");
  config.volume_percent = 100;
  other_long.SetReport(Analyze(source, config));
  EXPECT_EQ(other_long.minimumSizeHint().width(), inner->minimumSizeHint().width());
}

TEST(SignalPathWidgetTest, ShortTextIsNotElided) {

  SignalPathWidget widget;
  widget.SetReport(FlacDirect());
  widget.resize(widget.sizeHint());
  EXPECT_EQ(widget.report().badge, widget.ElidedMainText());

}

TEST(SignalPathWidgetTest, SourceSurvivesNarrowWidthsAndSecondaryPartsGoFirst) {

  // Codec, bit depth and sample rate matter most: at 200-280 logical pixels they must always be shown in full.
  SignalPathWidget widget;
  widget.SetReport(FlacDirect());
  const QString source = widget.report().source_text;
  ASSERT_EQ(QStringLiteral("FLAC 24/96"), source);

  bool chip_dropped_before_output = false;
  for (int width = 280; width >= 200; width -= 10) {
    const SignalPathWidget::Layout layout = widget.ComputeLayout(width);
    EXPECT_EQ(source, layout.source) << width;
    // The chip is never shown without the output: the output is dropped after the chip, not before it.
    if (layout.show_chip) EXPECT_FALSE(layout.output.isEmpty()) << width;
    if (!layout.show_chip && !layout.output.isEmpty()) chip_dropped_before_output = true;
  }
  EXPECT_TRUE(chip_dropped_before_output);

  // Wide: everything is shown.
  const SignalPathWidget::Layout wide = widget.ComputeLayout(widget.sizeHint().width());
  EXPECT_TRUE(wide.show_chip);
  EXPECT_EQ(widget.report().output_text, wide.output);

  // At the minimum size the whole source is still there, with nothing else.
  const SignalPathWidget::Layout minimal = widget.ComputeLayout(widget.minimumSizeHint().width());
  EXPECT_EQ(source, minimal.source);
  EXPECT_TRUE(minimal.output.isEmpty());
  EXPECT_FALSE(minimal.show_chip);

  // Only below the minimum does the source itself get elided, and it keeps its beginning.
  const SignalPathWidget::Layout tiny = widget.ComputeLayout(90);
  EXPECT_TRUE(tiny.source.startsWith(QStringLiteral("F")));
  EXPECT_LT(tiny.source.size(), source.size());

}

TEST(SignalPathWidgetTest, TheThreeSegmentsHaveVisibleSpacing) {

  SignalPathWidget widget;
  widget.SetReport(FlacDirect());
  widget.resize(widget.sizeHint());

  // The natural width leaves a gap of at least the text height between source and output, and between output and chip.
  const QFontMetrics fm = widget.fontMetrics();
  const int text_widths = fm.horizontalAdvance(widget.report().source_text) + fm.horizontalAdvance(widget.report().output_text);
  const int chip_text = fm.horizontalAdvance(widget.report().dsp_text);
  const int extra = widget.sizeHint().width() - text_widths - chip_text;
  EXPECT_GE(extra, fm.height() * 3);  // two gaps, left padding with marker and the chip padding

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

namespace {

Report ManyAlterations() {
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
  config.bs2b = true;
  config.fade_or_crossfade = true;
  return Analyze(source, config);
}

QString AllLabelText(const QWidget &widget) {
  QStringList parts;
  for (const QLabel *label : widget.findChildren<QLabel*>()) parts << label->text();
  return parts.join(QLatin1Char('\n'));
}

}  // namespace

TEST(SignalPathDialogTest, LeadsWithSourceAndOutputAndSummarizesWhatIsOff) {

  SignalPathDialog dialog;
  dialog.SetReport(FlacDirect());
  const QString text = AllLabelText(dialog);

  EXPECT_TRUE(text.contains(QStringLiteral("FLAC 24/96")));
  EXPECT_TRUE(text.contains(QStringLiteral("Hi-Res")));
  EXPECT_TRUE(text.contains(QStringLiteral("Direct ALSA")));
  EXPECT_TRUE(text.contains(QStringLiteral("App DSP off")));

  // The disabled processing options are one summary line, not one row each.
  EXPECT_EQ(1, text.count(QStringLiteral("Not active:")));
  EXPECT_TRUE(text.contains(QStringLiteral("ReplayGain")));
  EXPECT_EQ(1, text.count(QStringLiteral("ReplayGain")));

}

TEST(SignalPathDialogTest, ListsOnlyTheActiveAlterationsInDetail) {

  SignalPathDialog dialog;
  dialog.SetReport(ManyAlterations());
  const QString text = AllLabelText(dialog);

  EXPECT_TRUE(text.contains(QStringLiteral("App DSP on")));
  EXPECT_TRUE(text.contains(QStringLiteral("• Volume: Attenuated to 72%")));
  EXPECT_TRUE(text.contains(QStringLiteral("• Equalizer")));
  EXPECT_TRUE(text.contains(QStringLiteral("• ReplayGain")));
  // Things that are off are in the summary line only, never as bullets.
  EXPECT_FALSE(text.contains(QStringLiteral("• Stereo balance")));
  EXPECT_TRUE(text.contains(QStringLiteral("Not active:")));

}

TEST(SignalPathDialogTest, TechnicalDetailsAreCollapsedButAlwaysCopyable) {

  SignalPathDialog dialog;
  const Report report = FlacDirect();
  dialog.SetReport(report);

  EXPECT_TRUE(dialog.technical_view()->isHidden());
  EXPECT_FALSE(dialog.details_toggle()->isChecked());

  dialog.copy_button()->click();
  EXPECT_EQ(report.technical_text, QGuiApplication::clipboard()->text());  // copied while collapsed
  EXPECT_TRUE(dialog.technical_text().contains(QStringLiteral("Source")));
  EXPECT_TRUE(dialog.technical_text().contains(QStringLiteral("Processing")));
  EXPECT_TRUE(dialog.technical_text().contains(QStringLiteral("Output")));

  dialog.details_toggle()->click();
  EXPECT_FALSE(dialog.technical_view()->isHidden());
  dialog.details_toggle()->click();
  EXPECT_TRUE(dialog.technical_view()->isHidden());

  // A new report replaces the text.
  dialog.SetReport(LongNames());
  EXPECT_EQ(LongNames().technical_text, dialog.technical_text());

}

TEST(SignalPathDialogTest, FitsASixHundredPixelHighViewportWithCloseVisible) {

  for (const Report &report : { FlacDirect(), ManyAlterations(), LongNames() }) {
    SignalPathDialog dialog;
    dialog.SetReport(report);
    dialog.details_toggle()->setChecked(true);  // even with the technical text expanded

    // The preferred size is modest (it leaves room for a title bar and panels on a 600 pixel screen area).
    EXPECT_LE(dialog.sizeHint().height(), SignalPathDialog::kMaximumPreferredHeight);

    dialog.resize(dialog.sizeHint());
    dialog.ensurePolished();
    dialog.layout()->activate();
    EXPECT_LE(dialog.height(), 600);

    // The Close and Copy buttons are inside the dialog, below the scrolling content, not pushed out of view.
    const QRect close_rect(dialog.close_button()->mapTo(&dialog, QPoint(0, 0)), dialog.close_button()->size());
    EXPECT_TRUE(QRect(QPoint(0, 0), dialog.size()).contains(close_rect)) << close_rect.y();
    EXPECT_GT(dialog.scroll_area()->height(), 120);
  }

}

TEST(SignalPathDialogTest, TagTextIsNeverInterpretedAsMarkup) {

  Source source;
  source.format = QStringLiteral("<b>FLAC</b>");
  source.lossiness = Lossiness::Lossless;
  source.bitdepth = 16;
  source.samplerate = 44100;
  Config config;
  config.output = QStringLiteral("<i>sink</i>");
  config.volume_percent = 100;

  SignalPathDialog dialog;
  dialog.SetReport(Analyze(source, config));
  for (const QLabel *label : dialog.findChildren<QLabel*>()) {
    EXPECT_EQ(Qt::PlainText, label->textFormat()) << label->text().toStdString();
  }

}

TEST(SignalPathDialogTest, OutputSettingsButtonRequestsTheBackendPage) {

  SignalPathDialog dialog;
  dialog.SetReport(FlacDirect());
  QSignalSpy requested(&dialog, &SignalPathDialog::OutputSettingsRequested);
  dialog.output_settings_button()->click();
  EXPECT_EQ(1, requested.count());

}
