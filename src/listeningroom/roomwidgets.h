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

#ifndef ROOMWIDGETS_H
#define ROOMWIDGETS_H

#include "config.h"

#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStackedWidget>
#include <QStringList>
#include <QWidget>

#include "elidedlabel.h"
#include "roommotion.h"

class QLineEdit;
class QPaintEvent;
class QVBoxLayout;

// The shared parts of the Adrift pages: buttons, labels, page headings, the search field and the segmented control.
// Everything is painted from the application palette, so the three palettes and runtime palette changes work, and everything that responds to the pointer
// eases through RoomTween (and jumps when motion is reduced).

// A stacked widget whose size hints are those of the page that is shown. The default takes the largest of all pages, so a hidden page with a large
// minimum would stop the other pages from fitting a small window.
class RoomPageStack : public QStackedWidget {
  Q_OBJECT

 public:
  explicit RoomPageStack(QWidget *parent = nullptr) : QStackedWidget(parent) {
    QObject::connect(this, &QStackedWidget::currentChanged, this, [this]() { updateGeometry(); });
  }

  QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(0, 0); }
  QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize(0, 0); }
};

// A pill-shaped button. The primary one is filled with the accent, the others have a quiet outline.
// The hover is a short fade, and a press makes the pill sink a little, so it feels like something you push.
class RoomButton : public QPushButton {
  Q_OBJECT

 public:
  explicit RoomButton(const QString &text, const bool primary = false, QWidget *parent = nullptr);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

  // The hover and press amounts (0 to 1), for tests.
  double hover_amount() const;
  double press_amount() const;

 protected:
  void paintEvent(QPaintEvent *e) override;
  void enterEvent(QEnterEvent *e) override;
  void leaveEvent(QEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void focusInEvent(QFocusEvent *e) override;
  void focusOutEvent(QFocusEvent *e) override;

 private:
  bool primary_;
  RoomTween *hover_;
  RoomTween *press_;
};

// A plain-text label whose color is derived from the palette every time it is painted, so runtime palette changes (a theme switch) apply at once.
class ToneLabel : public QLabel {
  Q_OBJECT

 public:
  explicit ToneLabel(const QString &text, QWidget *parent = nullptr) : QLabel(text, parent) {}

  void SetTone(const ElidedLabel::Tone tone) {
    tone_ = tone;
    update();
  }

 protected:
  void paintEvent(QPaintEvent *e) override;

 private:
  ElidedLabel::Tone tone_ = ElidedLabel::Tone::Normal;
};

QLabel *MakeLabel(const QString &text = QString(), const bool word_wrap = true);
// Sets a label font and a palette-derived tone.
void StyleLabel(QLabel *label, const QFont &font, const ElidedLabel::Tone tone);
// The heading of a page: a title over one line of description, with an optional widget at the right. Every page uses it, so the headings line up.
QLabel *AddPageHeader(QVBoxLayout *layout, const QFont &base, const QString &title, const QString &description, QWidget *trailing = nullptr);

// The search field of the Library: a pill with a magnifier, a placeholder and a clear button.
class RoomSearchField : public QWidget {
  Q_OBJECT

 public:
  explicit RoomSearchField(QWidget *parent = nullptr);

  QString text() const;
  void SetText(const QString &text);
  void SetPlaceholder(const QString &text);
  QLineEdit *edit() const { return edit_; }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 Q_SIGNALS:
  void TextChanged(const QString &text);
  void Submitted();

 protected:
  void paintEvent(QPaintEvent *e) override;
  void changeEvent(QEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;
  bool eventFilter(QObject *watched, QEvent *e) override;

 private:
  void ApplyEditPalette();
  QRect EditRect() const;

  QLineEdit *edit_;
  RoomTween *focus_;
  bool applying_palette_ = false;
};

// A few mutually exclusive choices in a pill, with a highlight that glides from one to the next.
class RoomSegmented : public QWidget {
  Q_OBJECT

 public:
  explicit RoomSegmented(const QStringList &labels, QWidget *parent = nullptr);

  int current() const { return current_; }
  void SetCurrent(const int index);
  int count() const { return static_cast<int>(labels_.count()); }
  // Where the highlight is now (0 to count-1, fractional while it glides), for tests.
  double highlight_position() const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

 Q_SIGNALS:
  void CurrentChanged(const int index);

 protected:
  void paintEvent(QPaintEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;

 private:
  int SegmentWidth() const;

  QStringList labels_;
  int current_ = 0;
  RoomTween *glide_;
};

#endif  // ROOMWIDGETS_H
