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

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QTimer>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "listeningroom.h"
#include "coverartwidget.h"
#include "libraryview.h"
#include "roommotion.h"
#include "roomwidgets.h"
#include "elidedlabel.h"
#include "playlist/playlist.h"
#include "roomqueuemodel.h"
#include "roomqueueview.h"
#include "roomstyle.h"
#include "widgets/signalpathwidget.h"

namespace {

constexpr int kRoleId = Qt::UserRole + 1;
constexpr int kRoleCount = Qt::UserRole + 2;
constexpr int kRoleKind = Qt::UserRole + 3;
constexpr int kRoleCurrent = Qt::UserRole + 4;
constexpr int kRolePlaying = Qt::UserRole + 5;

constexpr int kRailWidth = 204;
constexpr int kRailWidthCompact = 68;
constexpr int kCompactBelow = 1100;
// Below this width the queue moves under the cover. Above it, the two sit side by side even in small windows.
constexpr int kStackedBelow = 700;

QString CountText(const int n, const char *one, const char *many) {
  return n == 1 ? QCoreApplication::translate("ListeningRoom", one) : QCoreApplication::translate("ListeningRoom", many).arg(n);
}

QString Tracks(const int n) {
  return CountText(n, "1 track", "%1 tracks");
}

// The side rail background: slightly apart from the content, with a hairline on its right edge.
// It also paints the navigation indicator, one pill that glides from the previous destination to the new one (instead of each button switching its own
// highlight on and off). The glide repaints only the strip of the rail it moves through, and ends at rest.
class RailWidget : public QWidget {
 public:
  explicit RailWidget(QWidget *parent = nullptr) : QWidget(parent), glide_(new RoomTween(this, 0.0, 280, QEasingCurve::OutCubic)) {
    QObject::connect(glide_, &RoomTween::Changed, this, [this]() { update(); });
  }

  // Moves the indicator to `target` (a button of the rail), or hides it for null.
  void SetTarget(const QWidget *target, const bool animate = true) {
    const QWidget *previous = target_;
    target_ = target;
    if (!target) {
      visible_ = false;
      update();
      return;
    }
    const QRectF to = Geometry(target);
    if (visible_ && target == previous && to == to_) return;
    if (!visible_ || !animate) {
      from_ = to_ = to;
      visible_ = true;
      glide_->Jump(1.0);
      update();
      return;
    }
    from_ = Current();
    to_ = to;
    glide_->Jump(0.0);
    glide_->SetTarget(1.0);
  }

  // Where the indicator is, and where it is going (for tests).
  QRectF indicator() const { return visible_ ? Current() : QRectF(); }
  QRectF indicator_target() const { return visible_ ? to_ : QRectF(); }
  bool gliding() const { return glide_->running(); }

 protected:
  void paintEvent(QPaintEvent *e) override {
    Q_UNUSED(e)
    QPainter p(this);
    p.fillRect(rect(), RoomStyle::Rail(palette()));
    p.setPen(RoomStyle::Hairline(palette()));
    p.drawLine(width() - 1, 0, width() - 1, height());
    if (!visible_) return;
    // A moved or resized rail (compact mode) re-aims a resting indicator at its button.
    if (!glide_->running() && target_) {
      const QRectF now = Geometry(target_);
      if (now != to_) from_ = to_ = now;
    }
    const QRectF r = Current();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(RoomStyle::Mix(RoomStyle::Rail(palette()), RoomStyle::Accent(palette()), 0.16));
    p.drawRoundedRect(r, 10, 10);
    p.setBrush(RoomStyle::Accent(palette()));
    p.drawRoundedRect(QRectF(r.left(), r.top() + 9, 3, r.height() - 18), 1.5, 1.5);
  }

 private:
  QRectF Geometry(const QWidget *target) const {
    return QRectF(target->geometry()).adjusted(0, 1, 0, -1);
  }
  QRectF Current() const {
    const qreal t = glide_->value();
    return QRectF(from_.left() + (to_.left() - from_.left()) * t, from_.top() + (to_.top() - from_.top()) * t, from_.width() + (to_.width() - from_.width()) * t, from_.height() + (to_.height() - from_.height()) * t);
  }

  RoomTween *glide_;
  const QWidget *target_ = nullptr;
  QRectF from_;
  QRectF to_;
  bool visible_ = false;
};

// The Adrift wordmark: a short wave in the accent color followed by the name and a small label.
// While music plays the wave drifts slowly. Its timer exists only while playing, visible and with motion allowed, and repaints just the wave.
class Wordmark : public QWidget {
 public:
  explicit Wordmark(QWidget *parent = nullptr) : QWidget(parent), compact_(false), timer_(new QTimer(this)) {
    setAccessibleName(QCoreApplication::translate("ListeningRoom", "Adrift"));
    timer_->setInterval(45);
    QObject::connect(timer_, &QTimer::timeout, this, [this]() {
      phase_ = std::fmod(phase_ + 0.11, 6.2832);
      update(WaveRect());
    });
    QObject::connect(RoomMotion::Instance(), &RoomMotion::ReducedChanged, this, [this]() { Sync(); });
  }

  void SetCompact(const bool compact) {
    if (compact_ == compact) return;
    compact_ = compact;
    updateGeometry();
    update();
  }

  void SetPlaying(const bool playing) {
    playing_ = playing;
    Sync();
  }
  bool timer_active() const { return timer_->isActive(); }

  QSize sizeHint() const override {
    const int name_height = QFontMetrics(RoomStyle::Scaled(font(), 1.7, true)).height();
    const int label_height = QFontMetrics(RoomStyle::Label(font())).height();
    return QSize(kRailWidth, 34 + name_height + 2 + label_height + 10);
  }

 protected:
  void showEvent(QShowEvent *e) override {
    QWidget::showEvent(e);
    Sync();
  }
  void hideEvent(QHideEvent *e) override {
    QWidget::hideEvent(e);
    Sync();
  }

  void paintEvent(QPaintEvent *e) override {
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();

    // A single drifting wave line. At rest its phase is zero; while playing it travels.
    const qreal wave_width = 30;
    const QPointF start(compact_ ? (width() - wave_width) / 2.0 : 20, 24);
    QPainterPath wave;
    wave.moveTo(start);
    const qreal amplitude = 4.2 * (playing_ && !RoomMotion::Instance()->reduced() ? 1.0 + 0.18 * std::sin(phase_ * 0.5) : 1.0);
    for (int i = 0; i <= 30; ++i) {
      const qreal t = i / 30.0;
      wave.lineTo(start.x() + t * wave_width, start.y() + std::sin(t * 6.2832 * 1.5 - phase_) * amplitude * (1.0 - 0.25 * t));
    }
    p.setPen(QPen(RoomStyle::Accent(pal), 2.2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(wave);

    if (compact_) return;

    const QFont name_font = RoomStyle::Scaled(font(), 1.7, true);
    p.setFont(name_font);
    p.setPen(RoomStyle::Text(pal));
    const int name_top = 34;
    p.drawText(QRect(18, name_top, width() - 24, QFontMetrics(name_font).height()), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("ListeningRoom", "Adrift"));

    p.setFont(RoomStyle::Label(font()));
    p.setPen(RoomStyle::Faint(pal));
    p.drawText(QRect(20, name_top + QFontMetrics(name_font).height() + 1, width() - 24, QFontMetrics(font()).height() + 4), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("ListeningRoom", "Listening Room"));
  }

 private:
  QRect WaveRect() const {
    return QRect(static_cast<int>(compact_ ? (width() - 30) / 2.0 : 20) - 4, 24 - 9, 30 + 8, 18);
  }
  void Sync() {
    const bool want = playing_ && isVisible() && !RoomMotion::Instance()->reduced();
    if (want && !timer_->isActive()) timer_->start();
    else if (!want && timer_->isActive()) {
      timer_->stop();
      phase_ = 0;
      update(WaveRect());
    }
  }

  bool compact_;
  bool playing_ = false;
  qreal phase_ = 0;
  QTimer *timer_;
};

// A stacked widget whose size hints are those of the page that is shown. The default takes the largest of all pages, so a hidden page with a large
// minimum (the classic window in the Library) would stop the other views from fitting a small window.
class PageStack : public QStackedWidget {
 public:
  explicit PageStack(QWidget *parent = nullptr) : QStackedWidget(parent) {
    QObject::connect(this, &QStackedWidget::currentChanged, this, [this]() { updateGeometry(); });
  }

  QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(0, 0); }
  QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize(0, 0); }
};

// Playlist cards: a name over its details, and a badge on the right for the playlist that is playing.
class PlaylistCardDelegate : public QStyledItemDelegate {
 public:
  explicit PlaylistCardDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    Q_UNUSED(index)
    return QSize(200, std::max(58, QFontMetrics(option.font).height() * 2 + 26));
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = option.palette;
    const QRect row = option.rect.adjusted(0, 3, 0, -3);
    const bool current = index.data(kRoleCurrent).toBool();
    const bool playing = index.data(kRolePlaying).toBool();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    painter->setPen(QPen(RoomStyle::Hairline(pal), 1));
    painter->setBrush(selected ? RoomStyle::Surface(pal, 0.12) : (hovered ? RoomStyle::Surface(pal, 0.08) : RoomStyle::Surface(pal, 0.045)));
    painter->drawRoundedRect(QRectF(row).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    if (current) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(RoomStyle::Accent(pal));
      painter->drawRoundedRect(QRect(row.left() + 1, row.top() + 10, 3, row.height() - 20), 1.5, 1.5);
    }
    if ((option.state & QStyle::State_HasFocus) && selected) {
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.75), 1.5));
      painter->drawRoundedRect(QRectF(row).adjusted(1.5, 1.5, -1.5, -1.5), 9, 9);
    }

    const QFontMetrics fm(option.font);
    const int left = row.left() + 18;
    const int right = row.right() - 16;
    QString badge;
    if (playing) badge = QCoreApplication::translate("ListeningRoom", "Playing");
    else if (current) badge = QCoreApplication::translate("ListeningRoom", "Shown");
    int badge_width = 0;
    if (!badge.isEmpty()) {
      badge_width = fm.horizontalAdvance(badge) + 20;
      const QRectF pill(right - badge_width, row.center().y() - fm.height() / 2.0 - 3, badge_width, fm.height() + 6);
      painter->setPen(Qt::NoPen);
      painter->setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Accent(pal), playing ? 0.3 : 0.16));
      painter->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
      painter->setPen(RoomStyle::Text(pal));
      painter->setFont(option.font);
      painter->drawText(pill, Qt::AlignCenter, badge);
    }

    const int text_width = std::max(0, right - left - badge_width - (badge_width ? 14 : 0));
    const int top = row.top() + (row.height() - fm.height() * 2 - 3) / 2;
    QFont title_font = option.font;
    title_font.setWeight(QFont::DemiBold);
    painter->setFont(title_font);
    painter->setPen(RoomStyle::Text(pal));
    painter->drawText(QRect(left, top, text_width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(title_font).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text_width));

    QString detail = Tracks(index.data(kRoleCount).toInt());
    const QString kind = index.data(kRoleKind).toString();
    if (!kind.isEmpty()) detail += QStringLiteral(" · ") + kind;
    painter->setFont(option.font);
    painter->setPen(RoomStyle::Muted(pal));
    painter->drawText(QRect(left, top + fm.height() + 3, text_width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(detail, Qt::ElideRight, text_width));
    painter->restore();
  }
};

}  // namespace

// ------------------------------------------------------------------------------------------------------------ nav button

RoomNavButton::RoomNavButton(const QString &text, const Glyph glyph, QWidget *parent)
    : QAbstractButton(parent), glyph_(glyph), compact_(false), hover_(new RoomTween(this, 0.0, 150)), press_(new RoomTween(this, 0.0, 90)) {

  QObject::connect(hover_, &RoomTween::Changed, this, [this]() { update(); });
  QObject::connect(press_, &RoomTween::Changed, this, [this]() { update(); });

  setText(text);
  setCheckable(true);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAccessibleName(text);
  setToolTip(text);

}

void RoomNavButton::SetCompact(const bool compact) {

  if (compact_ == compact) return;
  compact_ = compact;
  updateGeometry();
  update();

}

QSize RoomNavButton::sizeHint() const {

  const int height = std::max(42, fontMetrics().height() * 2 + 6);
  return QSize(compact_ ? kRailWidthCompact - 16 : kRailWidth - 24, height);

}

void RoomNavButton::enterEvent(QEnterEvent *e) {
  QAbstractButton::enterEvent(e);
  hover_->SetTarget(1.0);
}

void RoomNavButton::leaveEvent(QEvent *e) {
  QAbstractButton::leaveEvent(e);
  hover_->SetTarget(0.0);
  press_->SetTarget(0.0);
}

void RoomNavButton::mousePressEvent(QMouseEvent *e) {
  QAbstractButton::mousePressEvent(e);
  if (isDown()) press_->SetTarget(1.0);
}

void RoomNavButton::mouseReleaseEvent(QMouseEvent *e) {
  QAbstractButton::mouseReleaseEvent(e);
  press_->SetTarget(0.0);
}

void RoomNavButton::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPalette &pal = palette();
  // A press sinks the button a little. The highlight of the current destination is the rail's gliding indicator, not painted here.
  const qreal sink = press_->value() * 1.5;
  const QRectF r = QRectF(rect()).adjusted(0, 1, 0, -1).adjusted(sink, sink, -sink, -sink);

  if (!isChecked() && hover_->value() > 0.01) {
    p.setPen(Qt::NoPen);
    p.setBrush(RoomStyle::Surface(pal, 0.07 * hover_->value() + 0.04 * press_->value()));
    p.drawRoundedRect(r, 10, 10);
  }
  if (hasFocus()) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.8), 1.5));
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 9, 9);
  }

  const QColor ink = isChecked() ? RoomStyle::Text(pal) : RoomStyle::Muted(pal);
  const QColor accent = isChecked() ? RoomStyle::Accent(pal) : ink;
  const qreal glyph_size = 18;
  const QPointF centre(compact_ ? r.center().x() : r.left() + 22, r.center().y());
  const QRectF box(centre.x() - glyph_size / 2, centre.y() - glyph_size / 2, glyph_size, glyph_size);

  p.setBrush(Qt::NoBrush);
  QPen pen(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  switch (glyph_) {
    case Glyph::Listening:
      p.drawEllipse(box);
      p.setBrush(accent);
      p.drawEllipse(box.center(), 2.6, 2.6);
      break;
    case Glyph::Library:
      for (int i = 0; i < 3; ++i) {
        const qreal h = i == 1 ? 18 : 14;
        p.drawRoundedRect(QRectF(box.left() + i * 6.4, box.bottom() - h, 4.2, h), 1.2, 1.2);
      }
      break;
    case Glyph::Playlists:
      for (int i = 0; i < 3; ++i) {
        const qreal y = box.top() + 3 + i * 6;
        p.drawLine(QPointF(box.left() + 6, y), QPointF(box.right(), y));
        p.drawPoint(QPointF(box.left() + 1, y));
      }
      break;
    case Glyph::Streaming:
      p.drawArc(QRectF(box.left() + 1, box.top() + 2, 16, 16), 35 * 16, 110 * 16);
      p.drawArc(QRectF(box.left() + 4.5, box.top() + 5.5, 9, 9), 35 * 16, 110 * 16);
      p.setBrush(accent);
      p.drawEllipse(QPointF(box.center().x(), box.bottom() - 3), 1.8, 1.8);
      break;
    case Glyph::Motion:
      // Three waves that settle into flat lines when motion is reduced (the button is checked then).
      for (int i = 0; i < 3; ++i) {
        const qreal y = box.top() + 3 + i * 6;
        QPainterPath line;
        line.moveTo(box.left(), y);
        for (int k = 0; k <= 12; ++k) {
          const qreal t = k / 12.0;
          line.lineTo(box.left() + t * box.width(), y + (isChecked() ? 0.0 : std::sin(t * 6.2832 + i) * 1.8));
        }
        p.drawPath(line);
      }
      break;
    case Glyph::Settings:
      for (int i = 0; i < 2; ++i) {
        const qreal y = box.top() + 5 + i * 8;
        p.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
        p.setBrush(RoomStyle::Rail(pal));
        p.drawEllipse(QPointF(box.left() + (i ? 12 : 6), y), 2.6, 2.6);
      }
      break;
  }

  if (!compact_) {
    p.setPen(ink);
    p.setFont(RoomStyle::Scaled(font(), 1.04, isChecked()));
    p.drawText(QRectF(r.left() + 44, r.top(), r.width() - 48, r.height()), Qt::AlignLeft | Qt::AlignVCenter, text());
  }

}

// ------------------------------------------------------------------------------------------------------------ the room

ListeningRoom::ListeningRoom(QWidget *parent)
    : QWidget(parent),
      view_(View::Listening),
      compact_(false),
      state_(TransportBar::State::Stopped),
      rail_(nullptr),
      button_listening_(nullptr),
      button_library_(nullptr),
      button_playlists_(nullptr),
      button_streaming_(nullptr),
      button_settings_(nullptr),
      button_motion_(nullptr),
      wordmark_(nullptr),
      stack_(new PageStack),
      page_listening_(new QWidget),
      library_(new LibraryView),
      classic_container_(new QWidget),
      page_playlists_(new QWidget),
      page_streaming_(new QWidget),
      transport_(new TransportBar),
      listening_layout_(nullptr),
      cover_(new CoverArtWidget),
      label_status_(nullptr),
      label_title_(new ElidedLabel),
      label_artist_(new ElidedLabel),
      label_album_(new ElidedLabel),
      label_detail_(new ElidedLabel),
      signal_path_(new SignalPathWidget),
      widget_empty_actions_(nullptr),
      button_empty_library_(nullptr),
      button_empty_import_(nullptr),
      label_empty_hint_(nullptr),
      widget_meta_(nullptr),
      label_queue_heading_(nullptr),
      label_queue_sub_(nullptr),
      label_queue_note_(nullptr),
      queue_model_(new RoomQueueModel(this)),
      queue_view_(new RoomQueueView),
      playlists_list_(new QListWidget),
      button_playlists_import_(nullptr),
      label_playlists_empty_(nullptr),
      streaming_host_(new QWidget) {

  setAccessibleName(tr("Adrift Listening Room"));

  BuildRail();
  BuildListeningPage();
  BuildPlaylistsPage();
  BuildStreamingPage();

  // The pages, in the order of View.
  QVBoxLayout *classic_layout = new QVBoxLayout(classic_container_);
  classic_layout->setContentsMargins(0, 0, 0, 0);
  classic_layout->setSpacing(0);
  // The order of View: the classic window is the last page and has no rail button of its own.
  stack_->addWidget(page_listening_);
  stack_->addWidget(library_);
  stack_->addWidget(page_playlists_);
  stack_->addWidget(page_streaming_);
  stack_->addWidget(classic_container_);

  QVBoxLayout *content = new QVBoxLayout;
  content->setContentsMargins(0, 0, 0, 0);
  content->setSpacing(0);
  content->addWidget(stack_, 1);
  content->addWidget(transport_);

  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(rail_);
  layout->addLayout(content, 1);

  // Navigation
  QButtonGroup *group = new QButtonGroup(this);
  group->setExclusive(true);
  for (RoomNavButton *button : { button_listening_, button_library_, button_playlists_, button_streaming_ }) group->addButton(button);
  QObject::connect(button_listening_, &QAbstractButton::clicked, this, [this]() { SetView(View::Listening); });
  QObject::connect(button_library_, &QAbstractButton::clicked, this, [this]() { SetView(View::Library); });
  QObject::connect(button_playlists_, &QAbstractButton::clicked, this, [this]() { SetView(View::Playlists); });
  QObject::connect(button_streaming_, &QAbstractButton::clicked, this, [this]() { SetView(View::Streaming); });
  QObject::connect(button_settings_, &QAbstractButton::clicked, this, &ListeningRoom::SettingsRequested);
  QObject::connect(button_motion_, &QAbstractButton::toggled, this, [this](const bool reduced) {
    RoomMotion::Instance()->SetReduced(reduced);
    Q_EMIT ReducedMotionChanged(reduced);
  });
  button_motion_->setChecked(RoomMotion::Instance()->reduced());
  QObject::connect(RoomMotion::Instance(), &RoomMotion::ReducedChanged, this, [this](const bool reduced) {
    if (button_motion_->isChecked() != reduced) {
      QSignalBlocker blocker(button_motion_);
      button_motion_->setChecked(reduced);
      button_motion_->update();
    }
    button_motion_->setToolTip(reduced ? tr("Motion is reduced. Click to bring the animations back.") : tr("Reduce motion: turns the animations off."));
  });
  button_motion_->setToolTip(RoomMotion::Instance()->reduced() ? tr("Motion is reduced. Click to bring the animations back.") : tr("Reduce motion: turns the animations off."));
  QObject::connect(library_, &LibraryView::ShowStreamingRequested, this, [this]() { SetView(View::Streaming); });
  QObject::connect(library_, &LibraryView::AdvancedRequested, this, [this]() { SetView(View::Classic); });

  const View views[] = { View::Listening, View::Library, View::Playlists, View::Streaming };
  const int keys[] = { Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4 };
  for (int i = 0; i < 4; ++i) {
    QShortcut *shortcut = new QShortcut(QKeySequence(Qt::ALT | keys[i]), this);
    const View target = views[i];
    QObject::connect(shortcut, &QShortcut::activated, this, [this, target]() { SetView(target); });
  }

  // Queue
  queue_view_->setModel(queue_model_);
  QObject::connect(queue_view_, &RoomQueueView::RowActivated, this, &ListeningRoom::QueueRowActivated);
  QObject::connect(queue_model_, &QAbstractItemModel::modelReset, this, [this]() { UpdateQueueHeader(); });
  QObject::connect(queue_model_, &QAbstractItemModel::rowsInserted, this, [this]() { UpdateQueueHeader(); });
  QObject::connect(queue_model_, &QAbstractItemModel::rowsRemoved, this, [this]() { UpdateQueueHeader(); });

  QObject::connect(signal_path_, &QAbstractButton::clicked, this, &ListeningRoom::SignalPathDetailsRequested);

  SetNowPlaying(RoomTrack());
  SetView(View::Listening);
  UpdateQueueHeader();

}

ListeningRoom::~ListeningRoom() {

  // The hosted Streaming widget belongs to its creator (MainWindow, a test): give it back instead of letting the scroll area delete it.
  if (streaming_scroll_ && streaming_widget_) {
    streaming_scroll_->takeWidget();
    streaming_widget_->setParent(nullptr);
    streaming_widget_ = nullptr;
  }

}

ListeningRoom::QueueChoice ListeningRoom::ChooseQueue(const QList<int> &open_ids, const bool playback_active, const int active_id, const int current_id, const int browsed_id) {

  QueueChoice choice;
  choice.playing = playback_active && open_ids.contains(active_id) ? active_id : -1;

  if (browsed_id >= 0 && open_ids.contains(browsed_id) && choice.playing >= 0 && browsed_id != choice.playing) {
    choice.shown = browsed_id;
    choice.browsed = browsed_id;
  }
  else if (choice.playing >= 0) {
    choice.shown = choice.playing;
  }
  else if (open_ids.contains(current_id)) {
    choice.shown = current_id;
  }
  return choice;

}

QString ListeningRoom::ViewToString(const View view) {

  switch (view) {
    case View::Listening: return QStringLiteral("listening");
    case View::Library:   return QStringLiteral("library");
    case View::Playlists: return QStringLiteral("playlists");
    case View::Streaming: return QStringLiteral("streaming");
    case View::Classic:   return QStringLiteral("classic");
  }
  return QStringLiteral("listening");

}

ListeningRoom::View ListeningRoom::ViewFromString(const QString &value, const View fallback) {

  const QString v = value.trimmed().toLower();
  if (v == QLatin1String("listening")) return View::Listening;
  if (v == QLatin1String("library")) return View::Library;
  if (v == QLatin1String("playlists")) return View::Playlists;
  if (v == QLatin1String("streaming")) return View::Streaming;
  return fallback;

}

RoomNavButton *ListeningRoom::nav_button(const View view) const {

  switch (view) {
    case View::Listening: return button_listening_;
    case View::Library:   return button_library_;
    case View::Playlists: return button_playlists_;
    case View::Streaming: return button_streaming_;
    case View::Classic:   return button_library_;
  }
  return nullptr;

}

QWidget *ListeningRoom::page(const View view) const {

  switch (view) {
    case View::Listening: return page_listening_;
    case View::Library:   return library_;
    case View::Classic:   return classic_container_;
    case View::Playlists: return page_playlists_;
    case View::Streaming: return page_streaming_;
  }
  return nullptr;

}

void ListeningRoom::SetView(const View view) {

  const bool changed = view != view_;
  view_ = view;
  stack_->setCurrentIndex(static_cast<int>(view));
  nav_button(view)->setChecked(true);
  RetargetRailIndicator();
  // The classic window brings its own controls, so the room's transport steps aside there. Every other page keeps the room's transport.
  transport_->setVisible(view != View::Classic);
  if (view == View::Listening) queue_view_->ScrollToCurrent();
  if (changed) Q_EMIT ViewChanged(view);

}

// ------------------------------------------------------------------------------------------------------------ building

void ListeningRoom::BuildRail() {

  rail_ = new RailWidget;
  rail_->setAccessibleName(tr("Navigation"));
  rail_->setFixedWidth(kRailWidth);

  Wordmark *wordmark = new Wordmark;
  wordmark_ = wordmark;

  button_listening_ = new RoomNavButton(tr("Listening"), RoomNavButton::Glyph::Listening);
  button_library_ = new RoomNavButton(tr("Library"), RoomNavButton::Glyph::Library);
  button_playlists_ = new RoomNavButton(tr("Playlists"), RoomNavButton::Glyph::Playlists);
  button_streaming_ = new RoomNavButton(tr("Streaming"), RoomNavButton::Glyph::Streaming);
  button_settings_ = new RoomNavButton(tr("Settings"), RoomNavButton::Glyph::Settings);
  button_settings_->setCheckable(false);
  button_motion_ = new RoomNavButton(tr("Reduce motion"), RoomNavButton::Glyph::Motion);
  button_motion_->setCheckable(true);
  button_motion_->setAccessibleDescription(tr("Turns the animations of Adrift off. Checked means motion is reduced."));

  QVBoxLayout *layout = new QVBoxLayout(rail_);
  layout->setContentsMargins(12, 8, 12, 14);
  layout->setSpacing(4);
  layout->addWidget(wordmark_);
  layout->addSpacing(14);
  layout->addWidget(button_listening_);
  layout->addWidget(button_library_);
  layout->addWidget(button_playlists_);
  layout->addWidget(button_streaming_);
  layout->addStretch(1);
  layout->addWidget(button_motion_);
  layout->addWidget(button_settings_);

}

void ListeningRoom::BuildListeningPage() {

  listening_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, page_listening_);
  listening_layout_->setContentsMargins(36, 28, 36, 20);
  listening_layout_->setSpacing(44);

  // Left: the cover and what is playing.
  QVBoxLayout *left = new QVBoxLayout;
  left->setSpacing(10);
  left->addWidget(cover_, 1);

  widget_meta_ = new QWidget;
  QVBoxLayout *meta = new QVBoxLayout(widget_meta_);
  // Starts where the sleeve starts, so the cover and its text share one left edge.
  meta->setContentsMargins(cover_->ShadowMargin(), 0, 4, 0);
  meta->setSpacing(3);

  label_status_ = MakeLabel(QString(), false);
  StyleLabel(label_status_, RoomStyle::Label(font()), ElidedLabel::Tone::Accent);
  label_status_->setAccessibleName(tr("Playback state"));

  label_title_->setFont(RoomStyle::Scaled(font(), 2.0, true));
  label_title_->setAccessibleName(tr("Title"));
  label_artist_->setFont(RoomStyle::Scaled(font(), 1.3));
  label_artist_->SetTone(ElidedLabel::Tone::Normal);
  label_artist_->setAccessibleName(tr("Artist"));
  label_album_->setFont(RoomStyle::Scaled(font(), 1.05));
  label_album_->SetTone(ElidedLabel::Tone::Muted);
  label_album_->setAccessibleName(tr("Album"));
  label_detail_->setFont(font());
  label_detail_->SetTone(ElidedLabel::Tone::Faint);

  label_empty_hint_ = MakeLabel(tr("Choose a playlist, open your Library, or import one from Tidal or Qobuz."));
  StyleLabel(label_empty_hint_, RoomStyle::Scaled(font(), 1.1), ElidedLabel::Tone::Muted);

  widget_empty_actions_ = new QWidget;
  QHBoxLayout *actions = new QHBoxLayout(widget_empty_actions_);
  actions->setContentsMargins(0, 6, 0, 0);
  actions->setSpacing(10);
  button_empty_library_ = new RoomButton(tr("Open Library"), true);
  button_empty_import_ = new RoomButton(tr("Import playlists…"));
  actions->addWidget(button_empty_library_);
  actions->addWidget(button_empty_import_);
  actions->addStretch(1);
  QObject::connect(button_empty_library_, &QPushButton::clicked, this, [this]() { SetView(View::Library); });
  QObject::connect(button_empty_import_, &QPushButton::clicked, this, [this]() { Q_EMIT ImportPlaylistsRequested(QString()); });

  meta->addWidget(label_status_);
  meta->addWidget(label_title_);
  meta->addWidget(label_artist_);
  meta->addWidget(label_album_);
  meta->addWidget(label_detail_);
  meta->addSpacing(6);
  meta->addWidget(signal_path_, 0, Qt::AlignLeft);
  meta->addWidget(label_empty_hint_);
  meta->addWidget(widget_empty_actions_);
  left->addWidget(widget_meta_);

  // Right: the queue, a view of the real playlist.
  QVBoxLayout *right = new QVBoxLayout;
  right->setSpacing(4);
  label_queue_heading_ = MakeLabel(tr("Playlist"), false);
  StyleLabel(label_queue_heading_, RoomStyle::Label(font()), ElidedLabel::Tone::Faint);
  label_queue_sub_ = MakeLabel(QString(), false);
  StyleLabel(label_queue_sub_, RoomStyle::Scaled(font(), 1.15, true), ElidedLabel::Tone::Normal);
  label_queue_note_ = MakeLabel(QString(), true);
  StyleLabel(label_queue_note_, font(), ElidedLabel::Tone::Muted);
  button_queue_back_ = new RoomButton(tr("Show what is playing"));
  button_queue_back_->setVisible(false);
  QObject::connect(button_queue_back_, &QPushButton::clicked, this, &ListeningRoom::ShowPlayingQueueRequested);
  queue_view_->SetEmptyText(tr("This playlist is empty"), tr("Open a playlist, or add music from the Library."));
  queue_view_->setMinimumWidth(300);

  right->addWidget(label_queue_heading_);
  right->addWidget(label_queue_sub_);
  right->addWidget(label_queue_note_);
  right->addWidget(button_queue_back_, 0, Qt::AlignLeft);
  right->addSpacing(6);
  right->addWidget(queue_view_, 1);

  listening_layout_->addLayout(left, 5);
  listening_layout_->addLayout(right, 4);

}

void ListeningRoom::BuildPlaylistsPage() {

  QVBoxLayout *layout = new QVBoxLayout(page_playlists_);
  layout->setContentsMargins(40, 32, 40, 20);
  layout->setSpacing(10);

  button_playlists_import_ = new RoomButton(tr("Import from Tidal or Qobuz…"), true);
  button_playlists_import_->setToolTip(tr("Makes local copies of your streaming playlists. Nothing is changed on the service."));
  QObject::connect(button_playlists_import_, &QPushButton::clicked, this, [this]() { Q_EMIT ImportPlaylistsRequested(QString()); });
  AddPageHeader(layout, font(), tr("Playlists"), tr("Your open playlists. Choose one to browse its tracks."), button_playlists_import_);

  playlists_list_->setItemDelegate(new PlaylistCardDelegate(playlists_list_));
  playlists_list_->setFrameShape(QFrame::NoFrame);
  playlists_list_->setMouseTracking(true);
  playlists_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  playlists_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  playlists_list_->setAccessibleName(tr("Playlists"));
  playlists_list_->viewport()->setAutoFillBackground(false);
  playlists_list_->setAutoFillBackground(false);
  QObject::connect(playlists_list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
    Q_EMIT PlaylistActivated(item->data(kRoleId).toInt());
    SetView(View::Listening);
  });
  layout->addWidget(playlists_list_, 1);

  label_playlists_empty_ = MakeLabel(tr("There are no open playlists yet. Import one, or build one in the Library."));
  StyleLabel(label_playlists_empty_, RoomStyle::Scaled(font(), 1.1), ElidedLabel::Tone::Muted);
  label_playlists_empty_->setAlignment(Qt::AlignCenter);
  layout->addWidget(label_playlists_empty_);
  label_playlists_empty_->setVisible(false);

}

void ListeningRoom::BuildStreamingPage() {

  // The page has two faces in the same window: the service cards (with the account and import panel), and a catalog that is being browsed.
  QVBoxLayout *outer = new QVBoxLayout(page_streaming_);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  streaming_stack_ = new QStackedLayout;
  outer->addLayout(streaming_stack_);
  streaming_main_ = new QWidget;
  streaming_stack_->addWidget(streaming_main_);

  streaming_catalog_page_ = new QWidget;
  catalog_layout_ = new QVBoxLayout(streaming_catalog_page_);
  catalog_layout_->setContentsMargins(40, 28, 40, 0);
  catalog_layout_->setSpacing(10);
  button_catalog_back_ = new RoomButton(tr("‹  Streaming"));
  button_catalog_back_->setAccessibleName(tr("Back to the streaming services"));
  label_catalog_title_ = MakeLabel(QString(), false);
  StyleLabel(label_catalog_title_, RoomStyle::Scaled(font(), 1.9, true), ElidedLabel::Tone::Normal);
  QHBoxLayout *catalog_header = new QHBoxLayout;
  catalog_header->setSpacing(16);
  catalog_header->addWidget(button_catalog_back_);
  catalog_header->addWidget(label_catalog_title_, 1);
  catalog_layout_->addLayout(catalog_header);
  streaming_stack_->addWidget(streaming_catalog_page_);
  QObject::connect(button_catalog_back_, &QPushButton::clicked, this, &ListeningRoom::CloseCatalog);

  QVBoxLayout *layout = new QVBoxLayout(streaming_main_);
  layout->setContentsMargins(40, 32, 40, 20);
  layout->setSpacing(10);

  AddPageHeader(layout, font(), tr("Streaming"), tr("Sign in, browse your favorites and import playlists. Refresh your local copies here or keep them updated automatically."));

  // One page: the account, the playlists, and every problem are the hosted widget's (StreamingImportView); this is its place.
  streaming_host_layout_ = new QVBoxLayout(streaming_host_);
  streaming_host_layout_->setContentsMargins(0, 0, 0, 0);
  streaming_host_layout_->setSpacing(0);
  label_streaming_none_ = MakeLabel(tr("No streaming service is built into this version of Adrift."));
  StyleLabel(label_streaming_none_, RoomStyle::Scaled(font(), 1.1), ElidedLabel::Tone::Muted);
  label_streaming_none_->setVisible(false);
  streaming_host_layout_->addWidget(label_streaming_none_);

  // A long state (a problem with its technical details on a small window) scrolls instead of making the window taller than the screen.
  streaming_scroll_ = new QScrollArea;
  streaming_scroll_->setFrameShape(QFrame::NoFrame);
  streaming_scroll_->setWidgetResizable(true);
  streaming_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  streaming_scroll_->setAutoFillBackground(false);
  streaming_scroll_->viewport()->setAutoFillBackground(false);
  streaming_scroll_->setAccessibleName(tr("Streaming"));
  streaming_scroll_->setMinimumHeight(0);
  streaming_host_layout_->addWidget(streaming_scroll_, 1);
  layout->addWidget(streaming_host_, 1);

}

void ListeningRoom::SetStreamingWidget(QWidget *widget) {

  if (streaming_widget_ == widget) return;
  if (streaming_widget_) {
    // The scroll area would delete what it holds; the room does not own the widget, so it is taken out first.
    streaming_scroll_->takeWidget();
    streaming_widget_->hide();
  }
  streaming_widget_ = widget;
  if (widget) {
    streaming_scroll_->setWidget(widget);
    widget->show();
  }
  label_streaming_none_->setVisible(!widget && !services_available_);

}

void ListeningRoom::ShowCatalog(QWidget *widget, const QString &title) {

  if (!widget) return;
  if (catalog_widget_ && catalog_widget_ != widget) CloseCatalog();
  catalog_widget_ = widget;
  label_catalog_title_->setText(title);
  catalog_layout_->addWidget(widget, 1);
  widget->show();
  streaming_stack_->setCurrentWidget(streaming_catalog_page_);
  SetView(View::Streaming);

}

bool ListeningRoom::catalog_visible() const {
  return catalog_widget_ && streaming_stack_->currentWidget() == streaming_catalog_page_;
}

void ListeningRoom::CloseCatalog() {

  const bool was_showing = catalog_widget_ != nullptr;
  if (catalog_widget_) {
    catalog_layout_->removeWidget(catalog_widget_);
    catalog_widget_->hide();
    catalog_widget_ = nullptr;
  }
  streaming_stack_->setCurrentWidget(streaming_main_);
  if (was_showing) Q_EMIT CatalogClosed();

}

void ListeningRoom::SetReducedMotion(const bool reduced) {
  RoomMotion::Instance()->SetReduced(reduced);
}

bool ListeningRoom::reduced_motion() const {
  return RoomMotion::Instance()->reduced();
}

// ------------------------------------------------------------------------------------------------------------ state

void ListeningRoom::SetNowPlaying(const RoomTrack &track) {

  track_ = track;
  UpdateNowPlayingTexts();
  transport_->SetNowPlaying(track.title, track.artist);
  queue_view_->ScrollToCurrent();

}

void ListeningRoom::UpdateNowPlayingTexts() {

  const bool has_track = track_.valid();

  label_title_->setText(has_track ? track_.title : tr("Nothing playing"));
  label_title_->SetTone(has_track ? ElidedLabel::Tone::Normal : ElidedLabel::Tone::Muted);
  label_artist_->setText(track_.artist);
  label_album_->setText(track_.album);
  label_detail_->setText(track_.detail);
  label_artist_->setVisible(has_track && !track_.artist.isEmpty());
  label_album_->setVisible(has_track && !track_.album.isEmpty());
  label_detail_->setVisible(has_track && !track_.detail.isEmpty());

  // The empty state explains what to do instead of leaving a hole.
  label_empty_hint_->setVisible(!has_track);
  widget_empty_actions_->setVisible(!has_track);

  QString status;
  if (has_track) {
    switch (state_) {
      case TransportBar::State::Playing: status = tr("Now playing"); break;
      case TransportBar::State::Paused:  status = tr("Paused"); break;
      case TransportBar::State::Stopped: status = tr("Ready"); break;
    }
  }
  else status = tr("Ready");
  label_status_->setText(status);

  cover_->SetIdleText(QString());
  cover_->setAccessibleDescription(has_track ? tr("Cover of %1").arg(track_.album.isEmpty() ? track_.title : track_.album) : tr("No track"));

}

void ListeningRoom::SetCover(const QImage &image) {
  cover_->SetImage(image, track_.valid());
}

void ListeningRoom::SetPlaybackState(const TransportBar::State state) {

  state_ = state;
  UpdateQueueHeader();
  transport_->SetState(state);
  // Playing (not paused, not stopped) is what makes the wave drift and the record come out.
  static_cast<Wordmark*>(wordmark_)->SetPlaying(state == TransportBar::State::Playing);
  cover_->SetPlaying(state == TransportBar::State::Playing);
  queue_model_->SetPlaybackActive(state != TransportBar::State::Stopped);
  UpdateNowPlayingTexts();
  if (state == TransportBar::State::Stopped) {
    cover_->SetImage(QImage(), false);
  }
  else {
    queue_view_->ScrollToCurrent();
  }

}

void ListeningRoom::SetPosition(const int position_ms, const int length_ms) {
  transport_->SetPosition(position_ms, length_ms);
}

void ListeningRoom::SetCanSeek(const bool can_seek) {
  transport_->SetCanSeek(can_seek);
}

void ListeningRoom::SetVolume(const int volume) {
  transport_->SetVolume(volume);
}

void ListeningRoom::SetVolumeEnabled(const bool enabled) {
  transport_->SetVolumeEnabled(enabled);
}

void ListeningRoom::SetMuted(const bool muted) {
  transport_->SetMuted(muted);
}

void ListeningRoom::SetQueuePlaylist(Playlist *playlist, const QString &name) {

  queue_model_->set_playlist_name(name);
  queue_model_->SetPlaylist(playlist);
  UpdateQueueHeader();
  queue_view_->ScrollToCurrent();

}

void ListeningRoom::SetQueueBrowsing(const QString &playing_name) {

  browsing_note_ = playing_name.isEmpty() ? QString() : tr("This is not the playlist that is playing. Playback continues from “%1”.").arg(playing_name);
  UpdateQueueHeader();

}

void ListeningRoom::SetTransportActionStates(const bool previous, const bool play_pause, const bool next, const bool stop) {
  transport_->SetActionStates(previous, play_pause, next, stop);
}

void ListeningRoom::UpdateQueueHeader() {

  const int count = queue_model_->rowCount();
  const QString name = queue_model_->playlist_name();
  label_queue_sub_->setText(queue_model_->playlist() ? (name.isEmpty() ? Tracks(count) : QStringLiteral("%1 · %2").arg(name, Tracks(count))) : tr("No playlist"));
  // The heading says where the list comes from, not what plays next: "Playing from" the playlist that plays, "Browsing" another one.
  const bool browsing = !browsing_note_.isEmpty();
  label_queue_heading_->setText(browsing ? tr("Browsing") : (state_ != TransportBar::State::Stopped && queue_model_->playlist() ? tr("Playing from") : tr("Playlist")));
  label_queue_note_->setText(browsing_note_);
  label_queue_note_->setVisible(browsing);
  button_queue_back_->setVisible(browsing);

}

void ListeningRoom::SetPlaylists(const QList<RoomPlaylistCard> &playlists) {

  const int selected_id = playlists_list_->currentItem() ? playlists_list_->currentItem()->data(kRoleId).toInt() : -1;
  playlists_list_->clear();
  for (const RoomPlaylistCard &card : playlists) {
    QListWidgetItem *item = new QListWidgetItem(card.name, playlists_list_);
    item->setData(kRoleId, card.id);
    item->setData(kRoleCount, card.track_count);
    item->setData(kRoleKind, card.kind);
    item->setData(kRoleCurrent, card.is_current);
    item->setData(kRolePlaying, card.is_playing);
    item->setToolTip(card.name);
    if (card.id == selected_id) playlists_list_->setCurrentItem(item);
  }
  if (!playlists_list_->currentItem() && playlists_list_->count() > 0) playlists_list_->setCurrentRow(0);
  label_playlists_empty_->setVisible(playlists.isEmpty());
  playlists_list_->setVisible(!playlists.isEmpty());

}

void ListeningRoom::SetStreamingServices(const QList<RoomServiceCard> &services) {

  // The room only needs to know whether there is a service at all: the accounts themselves are the hosted widget's business.
  services_available_ = !services.isEmpty();
  label_streaming_none_->setVisible(!streaming_widget_ && !services_available_);
  // The import entry points only make sense when a service is available.
  button_playlists_import_->setVisible(services_available_);
  button_empty_import_->setVisible(services_available_);
  button_streaming_->setVisible(true);

}

// ------------------------------------------------------------------------------------------------------------ layout

void ListeningRoom::setVisible(const bool visible) {

  if (visible) ApplyResponsiveLayout();
  QWidget::setVisible(visible);

}

void ListeningRoom::resizeEvent(QResizeEvent *e) {

  QWidget::resizeEvent(e);
  ApplyResponsiveLayout();

}

void ListeningRoom::ApplyResponsiveLayout() {

  // A drag-resize sends a stream of sizes. The responsive layout has only two decisions (compact rail, stacked cover and queue), so nothing is touched
  // unless one of them changes: setting a fixed width, margins or a direction invalidates every layout of the room, which used to happen on every size.
  const bool compact = width() < kCompactBelow;
  const bool stacked = width() < kStackedBelow;
  if (layout_applied_ && compact == layout_compact_ && stacked == layout_stacked_) return;
  layout_applied_ = true;
  layout_compact_ = compact;
  layout_stacked_ = stacked;
  compact_ = compact;

  rail_->setFixedWidth(compact ? kRailWidthCompact : kRailWidth);
  static_cast<Wordmark*>(wordmark_)->SetCompact(compact);
  for (RoomNavButton *button : { button_listening_, button_library_, button_playlists_, button_streaming_, button_settings_, button_motion_ }) button->SetCompact(compact);
  listening_layout_->setSpacing(compact ? 28 : 44);
  listening_layout_->setContentsMargins(compact ? 24 : 36, compact ? 20 : 28, compact ? 24 : 36, 16);

  // Below this width the queue moves under the cover instead of beside it.
  listening_layout_->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
  RetargetRailIndicator();

}

QRectF ListeningRoom::nav_indicator() const {
  return static_cast<RailWidget*>(rail_)->indicator();
}

QRectF ListeningRoom::nav_indicator_target() const {
  return static_cast<RailWidget*>(rail_)->indicator_target();
}

bool ListeningRoom::nav_indicator_gliding() const {
  return static_cast<RailWidget*>(rail_)->gliding();
}

bool ListeningRoom::wordmark_animating() const {
  return static_cast<Wordmark*>(wordmark_)->timer_active();
}

void ListeningRoom::RetargetRailIndicator() {

  RoomNavButton *button = nav_button(view_);
  // The indicator follows the button's final place, which the layout decides; this is called again once the layout has run.
  static_cast<RailWidget*>(rail_)->SetTarget(button);

}

void ListeningRoom::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.fillRect(rect(), RoomStyle::Window(palette()));

}
