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

#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/song.h"
#include "listeningroom/roomstyle.h"
#include "listeningroom/roomwidgets.h"
#include "playlistimport.h"
#include "streamingaccount.h"
#include "streamingimportview.h"

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr int kPlaylistRole = Qt::UserRole + 1;
constexpr int kImportedRole = Qt::UserRole + 2;
constexpr int kCompactWidth = 640;
constexpr int kMaxRetryBlockSecs = 120;
constexpr int kMaxAutoRateLimitSecs = 3600;

QString Tracks(const int n) {
  return n == 1 ? QObject::tr("1 track") : QObject::tr("%1 tracks").arg(n);
}

QString Playlists(const int n) {
  return n == 1 ? QObject::tr("1 playlist") : QObject::tr("%1 playlists").arg(n);
}

bool DarkPalette(const QPalette &palette) {
  return palette.color(QPalette::Window).lightness() < 128;
}

// A warm alert color that keeps its contrast on light and on dark surfaces. It is never the only signal: the banner always says "Problem".
QColor AlertColor(const QPalette &palette) {
  return DarkPalette(palette) ? QColor(255, 140, 120) : QColor(176, 42, 28);
}

// A surface that reads as a card on the window color without hard-coding a theme.
class Card : public QFrame {
 public:
  explicit Card(QWidget *parent = nullptr) : QFrame(parent) {}

  void SetAccent(const bool accent, const bool alert) {
    accent_ = accent;
    alert_ = alert;
    update();
  }

 protected:
  void paintEvent(QPaintEvent *e) override {
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 12, 12);
    p.fillPath(path, RoomStyle::Surface(palette(), 0.05));
    p.setPen(QPen(RoomStyle::Hairline(palette()), 1));
    p.drawPath(path);
    if (accent_) {
      p.setClipPath(path);
      p.fillRect(QRectF(r.left(), r.top(), 4, r.height()), alert_ ? AlertColor(palette()) : RoomStyle::Accent(palette()));
    }
  }

 private:
  bool accent_ = false;
  bool alert_ = false;
};

// Round monogram of the service, a calm stand-in for a logo.
class Monogram : public QWidget {
 public:
  explicit Monogram(QWidget *parent = nullptr) : QWidget(parent) {
    setFixedSize(44, 44);
    setAccessibleName(QObject::tr("Service"));
  }

  void SetLetter(const QString &name, const bool active) {
    letter_ = name.left(1).toUpper();
    active_ = active;
    update();
  }

 protected:
  void paintEvent(QPaintEvent *e) override {
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setBrush(active_ ? RoomStyle::Mix(RoomStyle::Window(palette()), RoomStyle::Accent(palette()), 0.55) : RoomStyle::Surface(palette(), 0.12));
    p.setPen(Qt::NoPen);
    p.drawEllipse(r);
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.35);
    p.setFont(f);
    p.setPen(RoomStyle::Text(palette()));
    p.drawText(rect(), Qt::AlignCenter, letter_);
  }

 private:
  QString letter_;
  bool active_ = false;
};

// Playlist card: name, then "N tracks" and whether it is already a local snapshot, in words.
class PlaylistDelegate : public QStyledItemDelegate {
 public:
  explicit PlaylistDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    Q_UNUSED(index)
    const QFontMetrics fm(option.font);
    return QSize(200, std::max(58, fm.height() * 2 + 26));
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    const PlaylistImport::PlaylistInfo info = index.data(kPlaylistRole).value<PlaylistImport::PlaylistInfo>();
    const bool imported = index.data(kImportedRole).toBool();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const QPalette &pal = option.palette;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(option.rect).adjusted(0, 3, 0, -3);
    // The same cards as the Playlists page: a quiet surface, an accent edge for the chosen one.
    painter->setPen(QPen(RoomStyle::Hairline(pal), 1));
    painter->setBrush(selected ? RoomStyle::Surface(pal, 0.12) : (hovered ? RoomStyle::Surface(pal, 0.08) : RoomStyle::Surface(pal, 0.045)));
    painter->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    if (selected) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(RoomStyle::Accent(pal));
      painter->drawRoundedRect(QRectF(card.left() + 1, card.top() + 10, 3, card.height() - 20), 1.5, 1.5);
    }
    if (option.state & QStyle::State_HasFocus) {
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.8), 1.5));
      painter->drawRoundedRect(card.adjusted(1.5, 1.5, -1.5, -1.5), 9, 9);
    }

    const QColor main_color = RoomStyle::Text(pal);
    const QColor sub_color = RoomStyle::Muted(pal);

    const QRectF inner = card.adjusted(18, 6, -16, -6);
    QFont title_font = option.font;
    title_font.setWeight(QFont::DemiBold);
    const QFontMetrics tfm(title_font);
    painter->setFont(title_font);
    painter->setPen(main_color);
    const QRectF title_rect(inner.left(), inner.top(), inner.width(), tfm.height());
    painter->drawText(title_rect, Qt::AlignVCenter | Qt::AlignLeft, tfm.elidedText(info.name, Qt::ElideRight, static_cast<int>(inner.width())));

    painter->setFont(option.font);
    painter->setPen(sub_color);
    const QFontMetrics sfm(option.font);
    const QString sub = QStringLiteral("%1  ·  %2").arg(Tracks(info.track_count), imported ? QObject::tr("Imported as a local playlist") : QObject::tr("Not imported yet"));
    const QRectF sub_rect(inner.left(), inner.top() + tfm.height() + 2, inner.width(), sfm.height());
    painter->drawText(sub_rect, Qt::AlignVCenter | Qt::AlignLeft, sfm.elidedText(sub, Qt::ElideRight, static_cast<int>(inner.width())));
    painter->restore();
  }
};

}  // namespace

StreamingImportView::StreamingImportView(QWidget *parent)
    : QWidget(parent),
      header_(nullptr), header_layout_(nullptr), monogram_(nullptr), label_account_title_(nullptr), label_account_status_(nullptr), service_switch_(nullptr),
      service_group_(nullptr), button_account_(nullptr), button_signout_(nullptr),
      banner_(nullptr), banner_title_(nullptr), banner_message_(nullptr), banner_primary_(nullptr), banner_retry_(nullptr), banner_dismiss_(nullptr),
      banner_details_toggle_(nullptr), banner_details_(nullptr),
      pages_(nullptr), label_setup_help_(nullptr), label_setup_error_(nullptr), setup_form_(nullptr), button_save_setup_(nullptr), button_advanced_(nullptr),
      label_signedout_title_(nullptr), label_signedout_help_(nullptr), button_signin_page_(nullptr), label_waiting_help_(nullptr), link_edit_(nullptr),
      link_row_(nullptr), button_copy_link_(nullptr), button_cancel_signin_(nullptr), label_loading_(nullptr), label_unloaded_(nullptr), label_empty_(nullptr), button_load_(nullptr),
      search_(nullptr), label_list_summary_(nullptr), button_reload_(nullptr), list_(nullptr), button_empty_reload_(nullptr),
      label_status_(nullptr), progress_(nullptr), button_cancel_(nullptr), button_import_(nullptr), button_browse_(nullptr), button_open_(nullptr) {

  setObjectName(u"StreamingImportView"_s);
  setAccessibleName(tr("Streaming accounts and playlists"));
  setMinimumWidth(340);
  Build();
  auto_timer_.setInterval(auto_interval_ms_);
  QObject::connect(&auto_timer_, &QTimer::timeout, this, &StreamingImportView::RunAutoRefreshIfDue);
  auto_timer_.start();
  UpdateAll();

}

StreamingImportView::~StreamingImportView() {

  // Stop requests that are still running before the services (and the objects they point to) go away.
  auto_timer_.stop();
  for (Svc *svc : std::as_const(services_)) {
    svc->run = Svc::AutoRun();
    svc->auto_enabled = false;
    if (svc->importer && svc->importer->busy()) svc->importer->Cancel();
    if (svc->session && svc->session->state() == AccountSession::State::SigningIn) svc->session->Cancel();
  }
  qDeleteAll(services_);
  services_.clear();

}

QString StreamingImportView::Identity(const QString &url_scheme, const QString &playlist_id) {
  return url_scheme + u':' + playlist_id;
}

void StreamingImportView::Build() {

  QVBoxLayout *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 8);
  root->setSpacing(12);

  // Header card
  Card *header = new Card;
  header_ = header;
  header_->setObjectName(u"AccountHeader"_s);
  header_->setAccessibleName(tr("Account"));
  header_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, header_);
  header_layout_->setContentsMargins(16, 12, 16, 12);
  header_layout_->setSpacing(14);

  Monogram *monogram = new Monogram;
  monogram_ = monogram;

  QWidget *texts = new QWidget;
  QVBoxLayout *texts_layout = new QVBoxLayout(texts);
  texts_layout->setContentsMargins(0, 0, 0, 0);
  texts_layout->setSpacing(2);
  label_account_title_ = MakeLabel(QString(), false);
  StyleLabel(label_account_title_, RoomStyle::Scaled(font(), 1.3, true), ElidedLabel::Tone::Normal);
  label_account_title_->setTextFormat(Qt::PlainText);
  label_account_status_ = MakeLabel(QString(), false);
  label_account_status_->setTextFormat(Qt::PlainText);
  label_account_status_->setWordWrap(true);
  label_account_status_->setAccessibleName(tr("Account status"));
  StyleLabel(label_account_status_, font(), ElidedLabel::Tone::Muted);
  texts_layout->addWidget(label_account_title_);
  texts_layout->addWidget(label_account_status_);

  service_switch_ = new QWidget;
  QHBoxLayout *switch_layout = new QHBoxLayout(service_switch_);
  switch_layout->setContentsMargins(0, 0, 0, 0);
  switch_layout->setSpacing(6);
  service_group_ = new QButtonGroup(this);
  service_group_->setExclusive(true);

  button_account_ = new RoomButton(QString());
  button_signout_ = new RoomButton(tr("Sign out"));
  button_signout_->setAccessibleDescription(tr("Removes the stored sign-in of this service from Adrift. Imported playlists stay."));

  QHBoxLayout *identity = new QHBoxLayout;
  identity->setSpacing(14);
  identity->addWidget(monogram_);
  identity->addWidget(texts, 1);
  header_actions_ = new QBoxLayout(QBoxLayout::LeftToRight);
  header_actions_->setSpacing(10);
  QHBoxLayout *account_buttons = new QHBoxLayout;
  account_buttons->setSpacing(8);
  account_buttons->addWidget(button_account_);
  account_buttons->addWidget(button_signout_);
  header_actions_->addWidget(service_switch_);
  header_actions_->addLayout(account_buttons);
  header_layout_->addLayout(identity, 1);
  header_layout_->addLayout(header_actions_);
  root->addWidget(header_);

  BuildBanner();
  root->addWidget(banner_);

  BuildPages();
  root->addWidget(pages_, 1);

  // Footer
  label_status_ = MakeLabel(QString(), false);
  label_status_->setTextFormat(Qt::PlainText);
  label_status_->setWordWrap(true);
  label_status_->setAccessibleName(tr("Import status"));
  StyleLabel(label_status_, font(), ElidedLabel::Tone::Muted);
  label_status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  progress_ = new QProgressBar;
  progress_->setTextVisible(true);
  progress_->setAccessibleName(tr("Progress"));
  progress_->hide();
  button_cancel_ = new RoomButton(tr("Cancel"));
  button_cancel_->setAccessibleDescription(tr("Stops reading. Nothing is imported, and an automatic refresh stops before the next playlist."));
  button_import_ = new RoomButton(tr("Import"), true);
  button_browse_ = new RoomButton(QString());
  button_open_ = new RoomButton(tr("Open imported playlist"));
  button_open_->hide();

  QHBoxLayout *progress_row = new QHBoxLayout;
  progress_row->addWidget(progress_, 1);
  progress_row->addWidget(button_cancel_);
  QBoxLayout *buttons = new QBoxLayout(QBoxLayout::LeftToRight);
  footer_buttons_ = buttons;
  buttons->setSpacing(8);
  buttons->addWidget(button_browse_);
  buttons->addWidget(button_open_);
  buttons->addStretch(1);
  buttons->addWidget(button_import_);

  // Automatic refresh of the snapshots that already exist. Off until the user turns it on.
  auto_check_ = new QCheckBox;
  auto_check_->setAccessibleName(tr("Keep imported playlists up to date automatically"));
  auto_status_ = MakeLabel(QString(), false);
  auto_status_->setTextFormat(Qt::PlainText);
  auto_status_->setWordWrap(true);
  auto_status_->setAccessibleName(tr("Automatic refresh status"));
  StyleLabel(auto_status_, font(), ElidedLabel::Tone::Muted);
  auto_resume_ = new RoomButton(tr("Resume automatic refresh"));
  auto_resume_->setAccessibleDescription(tr("Allows the next automatic refresh to read your imported playlists again."));
  auto_resume_->hide();
  QVBoxLayout *auto_layout = new QVBoxLayout;
  auto_layout->setSpacing(4);
  auto_layout->addWidget(auto_check_);
  auto_layout->addWidget(auto_status_);
  auto_layout->addWidget(auto_resume_, 0, Qt::AlignLeft);

  root->addLayout(auto_layout);
  root->addWidget(label_status_);
  root->addLayout(progress_row);
  root->addLayout(buttons);

  QObject::connect(button_account_, &QPushButton::clicked, this, &StreamingImportView::PrimaryAccountAction);
  QObject::connect(button_signout_, &QPushButton::clicked, this, [this]() {
    Svc *svc = current();
    if (!svc) return;
    CancelAuto(svc);
    svc->session->SignOut();
    svc->force_setup = false;
    svc->status = tr("Signed out of %1. Imported playlists stay in Adrift.").arg(svc->entry.name);
    UpdateAll();
  });
  QObject::connect(button_cancel_, &QPushButton::clicked, this, [this]() {
    if (Svc *running = RunningSvc()) CancelAuto(running);
    else if (Svc *svc = current(); svc && svc->importer->busy()) svc->importer->Cancel();
  });
  QObject::connect(auto_check_, &QCheckBox::clicked, this, [this](const bool on) {
    Svc *svc = current();
    if (svc) SetAutoEnabled(svc, on);
  });
  QObject::connect(auto_resume_, &QPushButton::clicked, this, [this]() {
    Svc *svc = current();
    if (!svc || !svc->auto_enabled) return;
    svc->auto_paused = false;
    svc->auto_due = QDateTime();
    svc->auto_result.clear();
    UpdateAll();
    RunAutoRefreshIfDue();
  });
  QObject::connect(button_import_, &QPushButton::clicked, this, [this]() {
    Svc *svc = current();
    if (!svc) return;
    const PlaylistImport::PlaylistInfo info = SelectedPlaylist();
    if (!info.id.isEmpty()) StartImport(svc, info);
  });
  QObject::connect(button_browse_, &QPushButton::clicked, this, [this]() {
    if (const Svc *svc = current()) Q_EMIT BrowseServiceRequested(svc->entry.account->key());
  });
  QObject::connect(button_open_, &QPushButton::clicked, this, [this]() {
    if (const Svc *svc = current()) {
      if (!svc->opened_identity.isEmpty()) Q_EMIT OpenSnapshotRequested(svc->opened_identity);
    }
  });

  setTabOrder(button_account_, button_signout_);
  ApplyCompact();

}

void StreamingImportView::BuildBanner() {

  Card *banner = new Card;
  banner_ = banner;
  banner_->setObjectName(u"ImportBanner"_s);
  banner_->hide();
  banner_->setFocusPolicy(Qt::NoFocus);

  QVBoxLayout *layout = new QVBoxLayout(banner_);
  layout->setContentsMargins(18, 10, 14, 10);
  layout->setSpacing(6);

  banner_title_ = MakeLabel(QString(), false);
  banner_title_->setTextFormat(Qt::PlainText);
  StyleLabel(banner_title_, RoomStyle::Scaled(font(), 1.05, true), ElidedLabel::Tone::Normal);
  banner_message_ = MakeLabel(QString(), false);
  banner_message_->setTextFormat(Qt::PlainText);
  banner_message_->setWordWrap(true);
  banner_message_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  banner_primary_ = new RoomButton(QString());
  banner_retry_ = new RoomButton(tr("Retry"));
  banner_retry_->setAccessibleDescription(tr("Reads again. Only reads from the service, nothing is written to it."));
  banner_dismiss_ = new RoomButton(tr("Dismiss"));
  banner_details_toggle_ = new QToolButton;
  banner_details_toggle_->setText(tr("Technical details"));
  banner_details_toggle_->setCheckable(true);
  banner_details_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  banner_details_toggle_->setArrowType(Qt::RightArrow);
  banner_details_toggle_->setAutoRaise(true);
  banner_details_ = new QPlainTextEdit;
  banner_details_->setReadOnly(true);
  banner_details_->setMaximumHeight(96);
  banner_details_->setAccessibleName(tr("Technical details"));
  banner_details_->hide();

  QHBoxLayout *buttons = new QHBoxLayout;
  buttons->setSpacing(8);
  buttons->addWidget(banner_primary_);
  buttons->addWidget(banner_retry_);
  buttons->addWidget(banner_dismiss_);
  buttons->addStretch(1);
  buttons->addWidget(banner_details_toggle_);

  layout->addWidget(banner_title_);
  layout->addWidget(banner_message_);
  layout->addLayout(buttons);
  layout->addWidget(banner_details_);

  QObject::connect(banner_details_toggle_, &QToolButton::toggled, this, [this](const bool on) {
    banner_details_->setVisible(on);
    banner_details_toggle_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
  });
  QObject::connect(banner_dismiss_, &QPushButton::clicked, this, [this]() {
    Svc *svc = current();
    if (!svc) return;
    svc->has_failure = false;
    svc->notice.clear();
    UpdateAll();
  });
  QObject::connect(banner_retry_, &QPushButton::clicked, this, &StreamingImportView::Retry);
  QObject::connect(banner_primary_, &QPushButton::clicked, this, &StreamingImportView::PrimaryAccountAction);

}

void StreamingImportView::BuildPages() {

  pages_ = new QStackedWidget;
  pages_->setObjectName(u"ImportPages"_s);

  // PageSetup
  {
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 8);
    label_setup_help_ = MakeLabel(QString(), false);
    label_setup_help_->setWordWrap(true);
    label_setup_help_->setTextFormat(Qt::PlainText);
    label_setup_help_->setAccessibleName(tr("Setup needed"));
    StyleLabel(label_setup_help_, font(), ElidedLabel::Tone::Muted);
    setup_form_ = new QWidget;
    new QFormLayout(setup_form_);
    label_setup_error_ = MakeLabel(QString(), false);
    label_setup_error_->setWordWrap(true);
    label_setup_error_->setTextFormat(Qt::PlainText);
    label_setup_error_->hide();
    button_save_setup_ = new RoomButton(tr("Save and continue"));
    button_advanced_ = new RoomButton(tr("Advanced settings…"));
    QHBoxLayout *row = new QHBoxLayout;
    row->addWidget(button_save_setup_);
    row->addStretch(1);
    row->addWidget(button_advanced_);
    layout->addWidget(label_setup_help_);
    layout->addWidget(setup_form_);
    layout->addWidget(label_setup_error_);
    layout->addLayout(row);
    layout->addStretch(1);
    pages_->addWidget(page);
    QObject::connect(button_save_setup_, &QPushButton::clicked, this, &StreamingImportView::SaveSetup);
    QObject::connect(button_advanced_, &QPushButton::clicked, this, [this]() {
      if (const Svc *svc = current()) Q_EMIT ServiceSettingsRequested(svc->entry.account->key());
    });
  }

  // PageSignedOut
  {
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 8);
    layout->addStretch(1);
    label_signedout_title_ = MakeLabel(QString(), false);
    StyleLabel(label_signedout_title_, RoomStyle::Scaled(font(), 1.4, true), ElidedLabel::Tone::Normal);
    label_signedout_title_->setAlignment(Qt::AlignHCenter);
    label_signedout_title_->setWordWrap(true);
    label_signedout_title_->setTextFormat(Qt::PlainText);
    label_signedout_help_ = MakeLabel(QString(), false);
    label_signedout_help_->setAlignment(Qt::AlignHCenter);
    StyleLabel(label_signedout_help_, RoomStyle::Scaled(font(), 1.05), ElidedLabel::Tone::Muted);
    label_signedout_help_->setWordWrap(true);
    label_signedout_help_->setTextFormat(Qt::PlainText);
    button_signin_page_ = new RoomButton(QString());
    layout->addWidget(label_signedout_title_);
    layout->addWidget(label_signedout_help_);
    layout->addSpacing(6);
    layout->addWidget(button_signin_page_, 0, Qt::AlignHCenter);
    layout->addStretch(2);
    pages_->addWidget(page);
    QObject::connect(button_signin_page_, &QPushButton::clicked, this, &StreamingImportView::PrimaryAccountAction);
  }

  // PageWaiting
  {
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 8);
    layout->addStretch(1);
    QLabel *title = MakeLabel(tr("Waiting for your browser…"), false);
    StyleLabel(title, RoomStyle::Scaled(font(), 1.4, true), ElidedLabel::Tone::Normal);
    title->setAlignment(Qt::AlignHCenter);
    label_waiting_help_ = MakeLabel(QString(), false);
    label_waiting_help_->setAlignment(Qt::AlignHCenter);
    label_waiting_help_->setWordWrap(true);
    label_waiting_help_->setTextFormat(Qt::PlainText);
    label_waiting_help_->setAccessibleName(tr("Sign-in steps"));
    StyleLabel(label_waiting_help_, RoomStyle::Scaled(font(), 1.05), ElidedLabel::Tone::Muted);
    QProgressBar *busy = new QProgressBar;
    busy->setRange(0, 0);
    busy->setTextVisible(false);
    busy->setMaximumWidth(320);
    busy->setAccessibleName(tr("Waiting for sign-in"));
    link_row_ = new QWidget;
    QHBoxLayout *link_layout = new QHBoxLayout(link_row_);
    link_layout->setContentsMargins(0, 0, 0, 0);
    link_edit_ = new QLineEdit;
    link_edit_->setReadOnly(true);
    link_edit_->setAccessibleName(tr("Sign-in address"));
    button_copy_link_ = new RoomButton(tr("Copy address"));
    link_layout->addWidget(link_edit_, 1);
    link_layout->addWidget(button_copy_link_);
    link_row_->hide();
    button_cancel_signin_ = new RoomButton(tr("Cancel sign-in"));
    layout->addWidget(title);
    layout->addWidget(label_waiting_help_);
    layout->addWidget(busy, 0, Qt::AlignHCenter);
    layout->addWidget(link_row_);
    layout->addSpacing(6);
    layout->addWidget(button_cancel_signin_, 0, Qt::AlignHCenter);
    layout->addStretch(2);
    pages_->addWidget(page);
    QObject::connect(button_cancel_signin_, &QPushButton::clicked, this, [this]() {
      if (Svc *svc = current()) svc->session->Cancel();
    });
    QObject::connect(button_copy_link_, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(link_edit_->text()); });
  }

  // PageLoading
  {
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addStretch(1);
    label_loading_ = MakeLabel(QString(), false);
    label_loading_->setAlignment(Qt::AlignHCenter);
    StyleLabel(label_loading_, RoomStyle::Scaled(font(), 1.1), ElidedLabel::Tone::Muted);
    layout->addWidget(label_loading_);
    layout->addStretch(2);
    pages_->addWidget(page);
  }

  // PageList
  {
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    RoomSearchField *search_field = new RoomSearchField;
    search_field->SetPlaceholder(tr("Search playlists"));
    search_ = search_field->edit();
    search_->setAccessibleName(tr("Search playlists"));
    label_list_summary_ = MakeLabel(QString(), false);
    label_list_summary_->setTextFormat(Qt::PlainText);
    label_list_summary_->setWordWrap(true);
    label_list_summary_->setAccessibleName(tr("List summary"));
    StyleLabel(label_list_summary_, font(), ElidedLabel::Tone::Faint);
    button_reload_ = new RoomButton(tr("Refresh list"));
    button_reload_->setAccessibleDescription(tr("Reads your playlists from the service again."));
    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(search_field, 1);
    top->addWidget(button_reload_);
    list_ = new QListWidget;
    list_->setAccessibleName(tr("Remote playlists"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setItemDelegate(new PlaylistDelegate(list_));
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setUniformItemSizes(true);
    // The cards sit on the page, not on a darker panel of their own.
    list_->setAutoFillBackground(false);
    list_->viewport()->setAutoFillBackground(false);
    list_->setMouseTracking(true);
    layout->addLayout(top);
    layout->addWidget(label_list_summary_);
    layout->addWidget(list_, 1);
    pages_->addWidget(page);
    QObject::connect(search_, &QLineEdit::textChanged, this, &StreamingImportView::ApplyFilter);
    QObject::connect(button_reload_, &QPushButton::clicked, this, [this]() {
      if (Svc *svc = current()) StartList(svc);
    });
    QObject::connect(list_, &QListWidget::itemSelectionChanged, this, [this]() { UpdateFooter(current()); });
    QObject::connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *) {
      if (button_import_->isEnabled()) button_import_->click();
    });
  }

  // PageEmpty
  {
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addStretch(1);
    label_empty_ = MakeLabel(QString(), false);
    label_empty_->setAlignment(Qt::AlignHCenter);
    label_empty_->setWordWrap(true);
    StyleLabel(label_empty_, RoomStyle::Scaled(font(), 1.1), ElidedLabel::Tone::Muted);
    button_empty_reload_ = new RoomButton(tr("Check again"));
    layout->addWidget(label_empty_);
    layout->addWidget(button_empty_reload_, 0, Qt::AlignHCenter);
    layout->addStretch(2);
    pages_->addWidget(page);
    QObject::connect(button_empty_reload_, &QPushButton::clicked, this, [this]() {
      if (Svc *svc = current()) StartList(svc);
    });
  }

  // PageUnloaded
  {
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addStretch(1);
    label_unloaded_ = MakeLabel(QString(), false);
    label_unloaded_->setAlignment(Qt::AlignHCenter);
    label_unloaded_->setWordWrap(true);
    StyleLabel(label_unloaded_, RoomStyle::Scaled(font(), 1.1), ElidedLabel::Tone::Muted);
    button_load_ = new RoomButton(tr("Load playlists"));
    layout->addWidget(label_unloaded_);
    layout->addWidget(button_load_, 0, Qt::AlignHCenter);
    layout->addStretch(2);
    pages_->addWidget(page);
    QObject::connect(button_load_, &QPushButton::clicked, this, [this]() {
      if (Svc *svc = current()) StartList(svc);
    });
  }

}

// ---- services ---------------------------------------------------------------------------------------------------------------------------------------------

void StreamingImportView::AddService(const ServiceEntry &entry) {

  if (!entry.page_source || !entry.account) return;

  Svc *svc = new Svc;
  svc->entry = entry;
  if (!entry.page_source->parent()) entry.page_source->setParent(this);
  if (!entry.account->parent()) entry.account->setParent(this);
  svc->session = new AccountSession(entry.account, this);
  svc->importer = new PlaylistImport::Importer(entry.page_source, this);
  svc->account_key = entry.account->account_key();
  services_ << svc;

  ConnectService(svc);

  RoomButton *button = new RoomButton(entry.name);
  button->setCheckable(true);
  button->setAccessibleName(tr("Show %1").arg(entry.name));
  button->setObjectName(u"service_"_s + entry.account->key());
  service_switch_->layout()->addWidget(button);
  const int index = static_cast<int>(services_.count()) - 1;
  service_group_->addButton(button, index);
  QObject::connect(button, &QPushButton::clicked, this, [this, index]() {
    if (busy()) {
      // Switching is allowed, but reading goes on in the background. Keep it simple and predictable: no switching while reading.
      if (QAbstractButton *b = service_group_->button(current_)) b->setChecked(true);
      return;
    }
    SetService(index);
  });

  if (current_ < 0) SetService(index);
  service_switch_->setVisible(services_.count() > 1);
  UpdateAll();

}

void StreamingImportView::ConnectService(Svc *svc) {

  PlaylistImport::Importer *importer = svc->importer;

  QObject::connect(importer, &PlaylistImport::Importer::StateChanged, this, [this]() { UpdateAll(); });
  QObject::connect(importer, &PlaylistImport::Importer::Progress, this, [this, svc](const int loaded, const int total) {
    if (svc != current()) return;
    if (total > 0) {
      progress_->setRange(0, total);
      progress_->setValue(std::min(loaded, total));
      svc->status = tr("Reading… %1 of %2").arg(loaded).arg(total);
    }
    else {
      progress_->setRange(0, 0);
      svc->status = loaded > 0 ? tr("Reading… %1 received").arg(loaded) : tr("Reading…");
    }
    label_status_->setText(svc->status);
    label_loading_->setText(svc->status);
  });

  QObject::connect(importer, &PlaylistImport::Importer::PlaylistsLoaded, this, [this, svc](const QList<PlaylistImport::PlaylistInfo> &playlists) {
    svc->playlists = playlists;
    svc->loaded = true;
    svc->loaded_at = QDateTime::currentDateTime();
    svc->refresh_failed = false;
    svc->account_key = svc->entry.account->account_key();
    ClearFailure(svc);
    if (svc->run.active && svc->run.reading_list) {
      // Only playlists that are already local snapshots are refreshed. Nothing new is imported.
      svc->run.queue.clear();
      for (const PlaylistImport::PlaylistInfo &info : playlists) {
        if (SnapshotExistsNow(svc, Identity(svc->entry.url_scheme, info.id))) svc->run.queue << info;
      }
      svc->run.total = static_cast<int>(svc->run.queue.count());
      svc->run.reading_list = false;
      UpdateAll();
      QTimer::singleShot(0, this, [this, svc]() { AutoNext(svc); });
      return;
    }
    // After a manual list read an enabled automatic refresh gets its turn.
    if (svc->auto_enabled) QTimer::singleShot(0, this, &StreamingImportView::RunAutoRefreshIfDue);
    svc->status = playlists.isEmpty() ? tr("No playlists found on this account.") : tr("%1 found. Select one to import.").arg(Playlists(static_cast<int>(playlists.count())));
    UpdateAll();
  });

  QObject::connect(importer, &PlaylistImport::Importer::PlaylistImported, this, [this, svc](const PlaylistImport::PlaylistInfo &playlist, const QList<PlaylistImport::Track> &tracks, const int skipped) {
    if (svc->run.active) {
      if (!svc->run.reading_list && playlist.id == svc->run.current.id) {
        AutoCommit(svc, playlist, tracks, skipped);
        if (svc->run.active) QTimer::singleShot(0, this, [this, svc]() { AutoNext(svc); });
      }
      return;
    }
    SongList songs;
    songs.reserve(tracks.count());
    for (const PlaylistImport::Track &track : tracks) {
      songs << PlaylistImport::TrackToSong(track, svc->entry.song_source, svc->entry.url_scheme);
    }
    // Neutral until the caller confirms with ShowImported(): reading is not importing.
    svc->status = tr("Read %1 from “%2”. Creating the local playlist…").arg(Tracks(static_cast<int>(songs.count())), playlist.name);
    ClearFailure(svc);
    Q_EMIT SnapshotReady(svc->entry.name, Identity(svc->entry.url_scheme, playlist.id), QStringLiteral("%1: %2").arg(svc->entry.name, playlist.name), songs, skipped);
    UpdateAll();
  });

  QObject::connect(importer, &PlaylistImport::Importer::FailureOccurred, this, [this, svc](const PlaylistImport::Failure &failure) {
    if (svc->run.active) {
      EndAutoRun(svc, AutoEnd::Failed, QString());
      PlaylistImport::Failure shown = failure;
      shown.retryable = false;  // the banner's Retry repeats a manual read; automatic refresh has its own Resume
      if (failure.kind != PlaylistImport::FailureKind::RateLimited) shown.message += u' ' + tr("Automatic refresh is paused. Your local playlists keep their last snapshot.");
      SetFailure(svc, shown);
      if (failure.kind == PlaylistImport::FailureKind::RateLimited) {
        const int wait = std::clamp(failure.retry_after_sec > 0 ? failure.retry_after_sec : 30, 1, kMaxAutoRateLimitSecs);
        svc->auto_waiting_rate_limit = true;
        // Retry-After is a floor: never earlier than the wait, and never sooner than the normal interval.
        svc->auto_due = QDateTime::currentDateTime().addSecs(std::max<qint64>(wait, auto_interval_ms_ / 1000));
        svc->auto_paused = false;
      }
      else {
        svc->auto_paused = true;
      }
      UpdateAll();
      return;
    }
    if (svc->last_op.kind == Op::Kind::List && svc->loaded) svc->refresh_failed = true;
    svc->status = tr("Nothing was imported.");
    if (svc->last_op.kind == Op::Kind::List) svc->status = svc->loaded ? tr("The list could not be refreshed. The earlier list is still shown.") : tr("Your playlists could not be read.");
    SetFailure(svc, failure);
    UpdateAll();
  });

  QObject::connect(importer, &PlaylistImport::Importer::Cancelled, this, [this, svc]() {
    if (svc->run.active) {
      EndAutoRun(svc, AutoEnd::Cancelled);
      UpdateAll();
      return;
    }
    svc->status = tr("Cancelled. Nothing was imported.");
    UpdateAll();
  });

  // Account
  AccountSession *session = svc->session;
  QObject::connect(session, &AccountSession::StateChanged, this, [this, svc](const AccountSession::State state) {
    if (state != AccountSession::State::SignedIn && svc->run.active) CancelAuto(svc);
    if (state == AccountSession::State::SignedOut || state == AccountSession::State::NeedsSetup) {
      // Signed out behind our back (the session was refused or cleared). Keep what was loaded and say so.
      if (svc->loaded && svc->notice.isEmpty() && !svc->force_setup) {
        svc->notice = tr("You are signed out of %1. The list below is from %2. Sign in again to refresh it or to import.").arg(svc->entry.name, QLocale().toString(svc->loaded_at.time(), QLocale::ShortFormat));
      }
    }
    UpdateAll();
  });
  QObject::connect(session, &AccountSession::SignInFinished, this, [this, svc](const bool success, const QString &message, const bool cancelled) {
    SignInFinishedFor(svc, success, message, cancelled);
  });
  QObject::connect(session, &AccountSession::LinkChanged, this, [this]() { UpdatePage(current()); });

}

void StreamingImportView::SetService(const int index) {

  if (index < 0 || index >= services_.count() || index == current_) return;
  current_ = index;
  if (QAbstractButton *button = service_group_->button(index)) button->setChecked(true);
  search_->clear();
  UpdateAll();
  Q_EMIT ServiceSelected(services_[index]->entry.account->key());
  MaybeAutoLoad(services_[index]);

}

void StreamingImportView::SelectService(const QString &key_or_name) {

  if (busy()) return;
  for (int i = 0; i < services_.count(); ++i) {
    const Svc *svc = services_[i];
    if (svc->entry.account->key().compare(key_or_name, Qt::CaseInsensitive) == 0 || svc->entry.name.compare(key_or_name, Qt::CaseInsensitive) == 0) {
      SetService(i);
      return;
    }
  }

}

QString StreamingImportView::current_service_key() const {
  const Svc *svc = current();
  return svc ? svc->entry.account->key() : QString();
}

StreamingImportView::Svc *StreamingImportView::current() {
  return current_ >= 0 && current_ < services_.count() ? services_[current_] : nullptr;
}

const StreamingImportView::Svc *StreamingImportView::current() const {
  return current_ >= 0 && current_ < services_.count() ? services_[current_] : nullptr;
}

StreamingImportView::Svc *StreamingImportView::find(const QString &key) const {
  for (Svc *svc : services_) {
    if (svc->entry.account->key() == key) return svc;
  }
  return nullptr;
}

AccountSession *StreamingImportView::session(const QString &key) const {
  const Svc *svc = find(key);
  return svc ? svc->session : nullptr;
}

PlaylistImport::Importer *StreamingImportView::importer(const QString &key) const {
  const Svc *svc = find(key);
  return svc ? svc->importer : nullptr;
}

QPushButton *StreamingImportView::service_button(const QString &key) const {
  return findChild<QPushButton*>(u"service_"_s + key);
}

bool StreamingImportView::busy() const {
  for (const Svc *svc : services_) {
    if (svc->importer->busy()) return true;
  }
  return false;
}

StreamingImportView::Page StreamingImportView::current_page() const {
  return static_cast<Page>(pages_->currentIndex());
}

bool StreamingImportView::SnapshotExists(const Svc *svc, const QString &playlist_id) const {

  const QString identity = Identity(svc->entry.url_scheme, playlist_id);
  return svc->imported.contains(identity) || (snapshot_exists_ && snapshot_exists_(identity));

}

PlaylistImport::PlaylistInfo StreamingImportView::SelectedPlaylist() const {

  const QListWidgetItem *item = list_->currentItem();
  if (!item || !item->isSelected() || item->isHidden()) return PlaylistImport::PlaylistInfo();
  return item->data(kPlaylistRole).value<PlaylistImport::PlaylistInfo>();

}

// ---- actions ----------------------------------------------------------------------------------------------------------------------------------------------

void StreamingImportView::Activate() {

  Svc *svc = current();
  if (!svc) return;
  svc->session->Refresh();
  DiscardIfAccountChanged(svc);
  MaybeAutoLoad(svc);
  UpdateAll();
  RunAutoRefreshIfDue();

}

void StreamingImportView::MaybeAutoLoad(Svc *svc) {

  // One automatic read per sign-in. After a failure the user decides (Retry), so there is no loop.
  if (!svc || svc->auto_tried || svc->loaded || svc->has_failure) return;
  if (svc->session->state() != AccountSession::State::SignedIn || svc->importer->busy()) return;
  svc->auto_tried = true;
  StartList(svc);

}

void StreamingImportView::DiscardIfAccountChanged(Svc *svc) {

  const QString key = svc->entry.account->account_key();
  if (key.isEmpty() || key == svc->account_key) return;

  // A different account is signed in: the earlier list belongs to someone else.
  const bool had_list = svc->loaded;
  svc->account_key = key;
  svc->loaded = false;
  svc->playlists.clear();
  svc->imported.clear();
  svc->refresh_failed = false;
  svc->auto_tried = false;
  svc->opened_identity.clear();
  if (had_list) svc->notice = tr("You signed in with a different %1 account. The earlier list was removed.").arg(svc->entry.name);
  AutoAccountMatches(svc);  // an opt-in of the earlier account does not carry over

}

void StreamingImportView::StartList(Svc *svc) {

  if (!svc || svc->importer->busy() || AnyRun()) return;
  if (svc->session->state() != AccountSession::State::SignedIn) {
    PrimaryAccountAction();
    return;
  }
  svc->last_op = Op{Op::Kind::List, PlaylistImport::PlaylistInfo()};
  svc->status.clear();
  svc->notice.clear();
  svc->importer->LoadPlaylists();

}

void StreamingImportView::StartImport(Svc *svc, const PlaylistImport::PlaylistInfo &playlist) {

  if (!svc || svc->importer->busy() || AnyRun() || playlist.id.isEmpty()) return;
  if (svc->session->state() != AccountSession::State::SignedIn) return;
  svc->last_op = Op{Op::Kind::Import, playlist};
  svc->status.clear();
  svc->notice.clear();
  svc->importer->ImportPlaylist(playlist);

}

void StreamingImportView::Retry() {

  Svc *svc = current();
  if (!svc || svc->importer->busy() || AnyRun()) return;
  if (QDateTime::currentDateTime() < svc->retry_blocked_until) return;
  const Op op = svc->last_op;
  // Retry only repeats the read that failed. Both are reads of the service.
  if (op.kind == Op::Kind::Import) StartImport(svc, op.playlist);
  else StartList(svc);

}

void StreamingImportView::PrimaryAccountAction() {

  Svc *svc = current();
  if (!svc) return;

  switch (svc->session->state()) {
    case AccountSession::State::NeedsSetup:
      svc->force_setup = true;
      UpdateAll();
      if (!setup_edits_.isEmpty()) setup_edits_.first()->setFocus();
      break;
    case AccountSession::State::SignedOut:
    case AccountSession::State::SignedIn:
      svc->force_setup = false;
      svc->has_failure = false;
      svc->session->SignIn();
      break;
    case AccountSession::State::SigningIn:
      svc->session->Cancel();
      break;
  }

}

PlaylistImport::Failure StreamingImportView::SignInFailure(const QString &service, const QString &message, const bool timeout) {

  PlaylistImport::Failure failure;
  failure.kind = PlaylistImport::FailureKind::NotSignedIn;
  failure.needs_signin = true;
  failure.retryable = false;
  failure.details = message;
  const QString lower = message.toLower();
  if (timeout) {
    failure.title = tr("Sign-in timed out");
    failure.message = tr("The sign-in with %1 was not completed in time. If the browser did not open or you closed it, start again.").arg(service);
  }
  else if (lower.contains(u"access_denied"_s) || lower.contains(u"denied"_s)) {
    failure.title = tr("Access was declined");
    failure.message = tr("%1 reported that access was declined, in the browser or by the service. Adrift can only read your playlists once you allow it.").arg(service);
  }
  else if (lower.contains(u"client"_s) || lower.contains(u"redirect"_s) || lower.contains(u"invalid_request"_s)) {
    failure.title = tr("The service refused this app");
    failure.message = tr("%1 did not accept the app credentials or the return address that Adrift used. Check the developer app settings you entered.").arg(service);
  }
  else {
    failure.title = tr("Sign-in did not finish");
    failure.message = tr("The sign-in with %1 did not complete. Nothing was changed.").arg(service);
  }
  return failure;

}

void StreamingImportView::SignInFinishedFor(Svc *svc, const bool success, const QString &message, const bool cancelled) {

  if (success) {
    ClearFailure(svc);
    svc->force_setup = false;
    svc->notice.clear();
    DiscardIfAccountChanged(svc);
    svc->account_key = svc->entry.account->account_key();
    svc->auto_tried = false;
    svc->status = tr("Signed in to %1.").arg(svc->entry.name);
    // Signing in again is the recovery the pause asked for, for the account that turned the refresh on.
    if (svc->auto_enabled && AutoAccountMatches(svc)) svc->auto_paused = false;
    UpdateAll();
    MaybeAutoLoad(svc);
    RunAutoRefreshIfDue();
  }
  else if (cancelled) {
    svc->status = tr("Sign-in cancelled.");
    UpdateAll();
  }
  else {
    const bool timeout = svc->session->last_attempt_timed_out();
    SetFailure(svc, SignInFailure(svc->entry.name, message, timeout));
    svc->status = tr("Not signed in.");
    UpdateAll();
  }

  Q_EMIT SignInFinished(svc->entry.account->key(), success);

}

void StreamingImportView::SetFailure(Svc *svc, const PlaylistImport::Failure &failure) {

  svc->has_failure = true;
  svc->failure = failure;
  svc->notice.clear();
  svc->retry_blocked_until = QDateTime();
  if (failure.kind == PlaylistImport::FailureKind::RateLimited) {
    const int wait = std::clamp(failure.retry_after_sec > 0 ? failure.retry_after_sec : 30, 1, kMaxRetryBlockSecs);
    svc->retry_blocked_until = QDateTime::currentDateTime().addSecs(wait);
    // Re-enable the Retry button when the wait is over.
    QTimer::singleShot(wait * 1000 + 50, this, [this]() { if (current()) UpdateBanner(current()); });
  }
  // The authentication state may have changed because of what this failure means (an expired session).
  svc->session->Refresh();

}

void StreamingImportView::ClearFailure(Svc *svc) {
  svc->has_failure = false;
}

void StreamingImportView::SaveSetup() {

  Svc *svc = current();
  if (!svc) return;
  QMap<QString, QString> values;
  for (auto it = setup_edits_.constBegin(); it != setup_edits_.constEnd(); ++it) {
    values.insert(it.key(), it.value()->text());
  }
  QString error;
  if (!svc->entry.account->SaveSetup(values, &error)) {
    svc->setup_error = error;
    UpdatePage(svc);
    return;
  }
  svc->setup_error.clear();
  svc->force_setup = false;
  for (QLineEdit *edit : std::as_const(setup_edits_)) edit->clear();  // never keep secrets in the form
  svc->session->Refresh();
  svc->status = tr("Saved. You can sign in now.");
  UpdateAll();

}

void StreamingImportView::ShowImported(const QString &playlist_name, const bool replaced, const int track_count, const int skipped) {

  Svc *committing = nullptr;
  for (Svc *candidate : std::as_const(services_)) {
    if (candidate->auto_committing) committing = candidate;
  }
  if (committing) {
    // The answer to an automatic refresh: no navigation, no "Open" button, no replacement of the user's own status line.
    committing->auto_replied = true;
    ++committing->run.refreshed;
    committing->auto_deferred_ids.remove(Identity(committing->entry.url_scheme, committing->run.current.id));
    committing->auto_deferred.removeAll(committing->run.current.name);
    Q_UNUSED(playlist_name)
    Q_UNUSED(replaced)
    Q_UNUSED(track_count)
    Q_UNUSED(skipped)
    return;
  }

  Svc *svc = current();
  if (!svc) return;
  // The playlist name is "<Service>: <name>"; the identity of the snapshot is the last imported op.
  if (svc->last_op.kind == Op::Kind::Import) {
    const QString identity = Identity(svc->entry.url_scheme, svc->last_op.playlist.id);
    svc->imported.insert(identity);
    svc->opened_identity = identity;
  }
  QString text = replaced ? tr("Refreshed “%1” with %2.").arg(playlist_name, Tracks(track_count)) : tr("Imported “%1” with %2.").arg(playlist_name, Tracks(track_count));
  if (skipped > 0) text += u' ' + tr("Unavailable and left out: %1.").arg(Tracks(skipped));
  svc->status = text;
  ClearFailure(svc);
  UpdateAll();

}

void StreamingImportView::ShowImportFailed(const QString &playlist_name, const QString &reason) {

  Svc *committing = nullptr;
  for (Svc *candidate : std::as_const(services_)) {
    if (candidate->auto_committing) committing = candidate;
  }
  if (committing) {
    committing->auto_replied = true;
    PlaylistImport::Failure failure;
    failure.kind = PlaylistImport::FailureKind::BadResponse;
    failure.title = tr("The local playlist was not updated");
    failure.message = tr("“%1” was read from %2 but Adrift could not store it. Automatic refresh is paused. The playlist keeps its last snapshot.").arg(playlist_name, committing->entry.name);
    failure.details = reason;
    EndAutoRun(committing, AutoEnd::Failed);
    committing->auto_paused = true;
    SetFailure(committing, failure);
    UpdateAll();
    return;
  }

  Svc *svc = current();
  if (!svc) return;
  PlaylistImport::Failure failure;
  failure.kind = PlaylistImport::FailureKind::BadResponse;
  failure.title = tr("The local playlist was not created");
  failure.message = tr("“%1” was read from %2 but Adrift could not store it. Nothing was imported.").arg(playlist_name, svc->entry.name);
  failure.details = reason;
  failure.retryable = true;
  SetFailure(svc, failure);
  svc->status = tr("Nothing was imported.");
  UpdateAll();

}

// ---- display ----------------------------------------------------------------------------------------------------------------------------------------------

void StreamingImportView::UpdateAll() {

  const Svc *svc = current();
  UpdateHeader(svc);
  UpdateBanner(svc);
  UpdatePage(svc);
  UpdateList(svc);
  UpdateFooter(svc);

}

void StreamingImportView::UpdateHeader(const Svc *svc) {

  Monogram *monogram = static_cast<Monogram*>(monogram_);
  if (!svc) {
    label_account_title_->setText(tr("Streaming"));
    label_account_status_->setText(tr("No streaming service is available in this build."));
    monogram->SetLetter(u"?"_s, false);
    button_account_->hide();
    button_signout_->hide();
    return;
  }

  const AccountSession::State state = svc->session->state();
  label_account_title_->setText(svc->entry.name);
  monogram->SetLetter(svc->entry.name, state == AccountSession::State::SignedIn);
  button_account_->show();

  QString status;
  QString action;
  switch (state) {
    case AccountSession::State::SignedIn:
      status = tr("Signed in");
      action = tr("Reconnect");
      break;
    case AccountSession::State::SignedOut:
      status = tr("Not signed in");
      action = tr("Sign in");
      break;
    case AccountSession::State::NeedsSetup:
      status = tr("Sign-in needs setup");
      action = tr("Set up sign-in");
      break;
    case AccountSession::State::SigningIn:
      status = tr("Waiting for the browser…");
      action = tr("Cancel sign-in");
      break;
  }
  label_account_status_->setText(status);
  button_account_->setText(action);
  static_cast<RoomButton*>(button_account_)->SetPrimary(state == AccountSession::State::SignedOut || state == AccountSession::State::NeedsSetup);
  button_account_->setAccessibleName(tr("%1: %2").arg(action, svc->entry.name));
  button_account_->setEnabled(!svc->importer->busy());
  button_signout_->setVisible(state == AccountSession::State::SignedIn);
  button_signout_->setEnabled(!svc->importer->busy());
  button_signout_->setAccessibleName(tr("Sign out of %1").arg(svc->entry.name));
  service_switch_->setEnabled(!busy());

}

void StreamingImportView::UpdateBanner(const Svc *svc) {

  const bool show = svc && (svc->has_failure || !svc->notice.isEmpty());
  Card *card = static_cast<Card*>(banner_);
  if (!show) {
    banner_->hide();
    return;
  }

  const bool was_hidden = banner_->isHidden();
  const AccountSession::State state = svc->session->state();

  if (svc->has_failure) {
    const PlaylistImport::Failure &f = svc->failure;
    // The word "Problem" carries the meaning, the accent color only supports it.
    banner_title_->setText(tr("Problem: %1").arg(f.title));
    banner_message_->setText(f.message);
    banner_details_->setPlainText(f.details.isEmpty() ? tr("No further details.") : f.details);
    banner_details_toggle_->setVisible(true);
    card->SetAccent(true, true);

    // Primary action fits the cause
    QString primary;
    if (f.needs_signin) {
      if (state == AccountSession::State::NeedsSetup) primary = tr("Set up sign-in");
      else if (state == AccountSession::State::SigningIn) primary = tr("Cancel sign-in");
      else primary = f.kind == PlaylistImport::FailureKind::NotSignedIn && state != AccountSession::State::SignedIn ? tr("Sign in") : tr("Reconnect");
    }
    banner_primary_->setVisible(!primary.isEmpty());
    banner_primary_->setText(primary);
    static_cast<RoomButton*>(banner_primary_)->SetPrimary(true);
    banner_primary_->setAccessibleName(tr("%1 %2").arg(primary, svc->entry.name));

    const bool can_retry = f.retryable && svc->last_op.kind != Op::Kind::None;
    banner_retry_->setVisible(can_retry);
    banner_retry_->setEnabled(can_retry && !svc->importer->busy() && QDateTime::currentDateTime() >= svc->retry_blocked_until);
    banner_dismiss_->setText(tr("Dismiss"));
    banner_->setAccessibleName(tr("Problem: %1").arg(f.title));
    banner_->setAccessibleDescription(f.message);
  }
  else {
    banner_title_->setText(tr("Note"));
    banner_message_->setText(svc->notice);
    banner_details_toggle_->setVisible(false);
    banner_details_->hide();
    card->SetAccent(true, false);
    banner_primary_->hide();
    banner_retry_->hide();
    banner_->setAccessibleName(tr("Note"));
    banner_->setAccessibleDescription(svc->notice);
  }

  banner_->show();
  // Word-wrapped text changes the height of the card: make the surrounding layout measure it again.
  FitBannerText();
  if (was_hidden) {
    // Announce new problems to assistive technology.
    QAccessibleEvent event(banner_, QAccessible::Alert);
    QAccessible::updateAccessibility(&event);
  }

}

void StreamingImportView::UpdatePage(const Svc *svc) {

  Page page = PageSignedOut;
  if (!svc) {
    page = PageEmpty;
    label_empty_->setText(tr("Tidal and Qobuz are not available in this build of Adrift."));
    button_empty_reload_->hide();
    pages_->setCurrentIndex(page);
    return;
  }
  button_empty_reload_->show();

  const AccountSession::State state = svc->session->state();
  const bool busy_list = svc->importer->state() == PlaylistImport::Importer::State::LoadingPlaylists;

  if (state == AccountSession::State::SigningIn) {
    page = PageWaiting;
  }
  else if (svc->force_setup && state == AccountSession::State::NeedsSetup) {
    page = PageSetup;
  }
  else if (svc->loaded) {
    page = svc->playlists.isEmpty() ? PageEmpty : PageList;
  }
  else if (state == AccountSession::State::NeedsSetup) {
    page = PageSetup;
  }
  else if (state == AccountSession::State::SignedOut) {
    page = PageSignedOut;
  }
  else if (busy_list || svc->importer->busy()) {
    page = PageLoading;
  }
  else {
    page = PageUnloaded;
  }

  // Page contents
  if (page == PageSetup) {
    label_setup_help_->setText(svc->entry.account->setup_help());
    // Rebuild the form for the current service
    QFormLayout *form = static_cast<QFormLayout*>(setup_form_->layout());
    const QList<AccountBackend::SetupField> fields = svc->entry.account->setup_fields();
    bool same = setup_edits_.count() == fields.count();
    if (same) {
      for (const AccountBackend::SetupField &field : fields) same = same && setup_edits_.contains(field.key);
    }
    if (!same || setup_form_->property("service").toString() != svc->entry.account->key()) {
      while (form->rowCount() > 0) form->removeRow(0);
      setup_edits_.clear();
      for (const AccountBackend::SetupField &field : fields) {
        QLineEdit *edit = new QLineEdit;
        edit->setPlaceholderText(field.placeholder);
        edit->setAccessibleName(field.label);
        if (field.secret) edit->setEchoMode(QLineEdit::Password);
        form->addRow(field.label + u':', edit);
        setup_edits_.insert(field.key, edit);
      }
      setup_form_->setProperty("service", svc->entry.account->key());
    }
    label_setup_error_->setVisible(!svc->setup_error.isEmpty());
    label_setup_error_->setText(tr("Problem: %1").arg(svc->setup_error));
  }
  else if (page == PageSignedOut) {
    label_signedout_title_->setText(tr("Sign in to %1").arg(svc->entry.name));
    label_signedout_help_->setText(tr("Sign in here to read your %1 playlists and import them. %2").arg(svc->entry.name, svc->entry.account->signin_help()));
    button_signin_page_->setText(tr("Sign in to %1").arg(svc->entry.name));
  }
  else if (page == PageWaiting) {
    label_waiting_help_->setText(tr("1. Finish signing in to %1 in the browser window that opened.\n2. Allow Adrift to read your playlists.\n3. The browser returns you here.\n\nYou can keep using the rest of Adrift meanwhile.").arg(svc->entry.name));
    const QUrl link = svc->session->link();
    const bool show_link = !link.isEmpty() && !svc->session->link_opened();
    link_row_->setVisible(show_link);
    if (show_link) {
      link_edit_->setText(link.toString());
      link_edit_->setCursorPosition(0);
      label_waiting_help_->setText(tr("Adrift could not open a browser. Copy this address into a browser to continue:"));
    }
  }
  else if (page == PageLoading) {
    label_loading_->setText(svc->status.isEmpty() ? tr("Reading your playlists…") : svc->status);
  }
  else if (page == PageUnloaded) {
    label_unloaded_->setText(svc->has_failure ? tr("Your playlists could not be loaded. The problem is described above. Nothing was changed on %1.").arg(svc->entry.name)
                                              : tr("Your playlists have not been read yet. Adrift only reads them when you ask, and never changes anything on the service."));
    button_load_->setText(svc->has_failure ? tr("Try loading again") : tr("Load playlists"));
  }
  else if (page == PageEmpty) {
    label_empty_->setText(tr("No playlists found on this account.\n%1").arg(svc->entry.name == u"Tidal"_s ? tr("Adrift lists the playlists you created on Tidal, not ones you only follow.") : tr("Adrift lists the playlists this account owns or saved on Qobuz.")));
  }

  pages_->setCurrentIndex(page);

}

void StreamingImportView::UpdateList(const Svc *svc) {

  // Rebuilt only when the content changed: progress updates must not reset the scroll position or the selection.
  QString signature = svc ? svc->entry.account->key() : QString();
  if (svc) {
    for (const PlaylistImport::PlaylistInfo &info : svc->playlists) {
      signature += u'|' + info.id + u'/' + info.name + u'/' + QString::number(info.track_count) + (SnapshotExists(svc, info.id) ? u"+"_s : u"-"_s);
    }
  }

  if (signature != list_signature_) {
    const QString selected_id = SelectedPlaylist().id;
    const int scroll = list_->verticalScrollBar()->value();
    QSignalBlocker blocker(list_);
    list_->clear();
    list_signature_ = signature;
    if (svc) {
      for (const PlaylistImport::PlaylistInfo &info : svc->playlists) {
        QListWidgetItem *item = new QListWidgetItem(list_);
        const bool imported = SnapshotExists(svc, info.id);
        item->setData(kPlaylistRole, QVariant::fromValue(info));
        item->setData(kImportedRole, imported);
        item->setData(Qt::AccessibleTextRole, tr("%1, %2, %3").arg(info.name, Tracks(info.track_count), imported ? tr("imported") : tr("not imported yet")));
        item->setToolTip(info.name);
        if (!selected_id.isEmpty() && info.id == selected_id) {
          list_->setCurrentItem(item, QItemSelectionModel::NoUpdate);
          item->setSelected(true);
        }
      }
    }
    list_->verticalScrollBar()->setValue(scroll);
  }
  if (!svc) return;
  ApplyFilter();

  QString summary = Playlists(static_cast<int>(svc->playlists.count()));
  if (svc->loaded) summary += tr("  ·  read at %1").arg(QLocale().toString(svc->loaded_at.time(), QLocale::ShortFormat));
  if (svc->refresh_failed) summary += tr("  ·  the last refresh failed, this list may be out of date");
  label_list_summary_->setText(summary);

}

void StreamingImportView::ApplyFilter() {

  const QString needle = search_->text().trimmed();
  int visible = 0;
  for (int i = 0; i < list_->count(); ++i) {
    QListWidgetItem *item = list_->item(i);
    const QString name = item->data(kPlaylistRole).value<PlaylistImport::PlaylistInfo>().name;
    const bool match = needle.isEmpty() || name.contains(needle, Qt::CaseInsensitive);
    item->setHidden(!match);
    if (!match && item->isSelected()) item->setSelected(false);
    if (match) ++visible;
  }
  if (!needle.isEmpty()) {
    const Svc *svc = current();
    if (svc) label_list_summary_->setText(tr("%1 of %2 match “%3”").arg(visible).arg(svc->playlists.count()).arg(needle));
  }
  UpdateFooter(current());

}

void StreamingImportView::UpdateFooter(const Svc *svc) {

  if (!svc) {
    label_status_->clear();
    progress_->hide();
    button_cancel_->hide();
    button_import_->setEnabled(false);
    button_browse_->hide();
    button_open_->hide();
    return;
  }

  const bool busy_now = svc->importer->busy() || AnyRun();
  const bool signed_in = svc->session->state() == AccountSession::State::SignedIn;
  UpdateAutoRow(svc);
  progress_->setVisible(busy_now);
  if (busy_now && progress_->maximum() == progress_->minimum() && progress_->maximum() != 0) progress_->setRange(0, 0);
  button_cancel_->setVisible(busy_now);
  button_cancel_->setAccessibleName(tr("Cancel reading from %1").arg(AnyRun() && RunningSvc() ? RunningSvc()->entry.name : svc->entry.name));

  const PlaylistImport::PlaylistInfo info = SelectedPlaylist();
  QString import_text = tr("Import");
  if (!info.id.isEmpty() && SnapshotExists(svc, info.id)) import_text = tr("Refresh snapshot");
  button_import_->setText(import_text);
  button_import_->setEnabled(!busy_now && signed_in && !info.id.isEmpty() && svc->loaded);
  button_import_->setToolTip(signed_in ? QString() : tr("Sign in to import."));
  button_import_->setAccessibleDescription(tr("Reads the whole playlist and creates or replaces a local copy. Nothing is changed on %1.").arg(svc->entry.name));
  button_reload_->setEnabled(!busy_now && signed_in);
  button_empty_reload_->setEnabled(!busy_now && signed_in);
  button_load_->setEnabled(!busy_now && signed_in);

  button_browse_->show();
  button_browse_->setText(tr("Browse %1 favorites").arg(svc->entry.name));
  button_open_->setVisible(!svc->opened_identity.isEmpty() && !busy_now);

  if (!busy_now) {
    label_status_->setText(svc->status);
  }
  else if (AnyRun() && !svc->importer->busy()) {
    label_status_->setText(tr("Updating imported playlists. Cancel stops before the next playlist."));
  }
  label_status_->setVisible(!label_status_->text().isEmpty());

}

// ---- layout -----------------------------------------------------------------------------------------------------------------------------------------------

void StreamingImportView::ApplyCompact() {

  const bool compact = width() < kCompactWidth;
  compact_ = compact;
  header_layout_->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
  header_actions_->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
  header_layout_->setSpacing(compact ? 8 : 14);
  // Compact: the footer buttons stack, the primary action first.
  if (footer_buttons_) footer_buttons_->setDirection(compact ? QBoxLayout::BottomToTop : QBoxLayout::LeftToRight);
  if (QBoxLayout *root = qobject_cast<QBoxLayout*>(layout())) root->setContentsMargins(0, 0, 0, 8);

}

void StreamingImportView::FitBannerText() {

  // A word-wrapped label inside a card can be given less height than its wrapped text needs. Reserve the height for the width the card really has.
  const QMargins root = layout() ? layout()->contentsMargins() : QMargins();
  const QMargins card = banner_->layout() ? banner_->layout()->contentsMargins() : QMargins();
  const int available = std::max(60, width() - root.left() - root.right() - card.left() - card.right());
  banner_message_->setMinimumHeight(banner_message_->heightForWidth(available));
  banner_->layout()->invalidate();
  banner_->updateGeometry();

}

void StreamingImportView::resizeEvent(QResizeEvent *e) {

  QWidget::resizeEvent(e);
  if ((width() < kCompactWidth) != compact_) ApplyCompact();
  if (banner_ && banner_->isVisibleTo(this)) FitBannerText();

}

void StreamingImportView::showEvent(QShowEvent *e) {

  QWidget::showEvent(e);
  ApplyCompact();
  Activate();

}

// ---- automatic refresh of existing snapshots -----------------------------------------------------------------------------------------------------------------

void StreamingImportView::SetAutoRefreshInterval(const int msecs) {

  auto_interval_ms_ = std::max(1, msecs);
  auto_timer_.setInterval(auto_interval_ms_);
  auto_timer_.start();
  // A shorter interval also shortens a wait that is already scheduled, except a wait the service asked for.
  const QDateTime soonest = QDateTime::currentDateTime().addMSecs(auto_interval_ms_);
  for (Svc *svc : std::as_const(services_)) {
    if (svc->auto_due.isValid() && !svc->auto_waiting_rate_limit && svc->auto_due > soonest) svc->auto_due = soonest;
  }

}

bool StreamingImportView::AnyRun() const {
  return RunningSvc() != nullptr;
}

StreamingImportView::Svc *StreamingImportView::RunningSvc() const {

  for (Svc *svc : services_) {
    if (svc->run.active) return svc;
  }
  return nullptr;

}

bool StreamingImportView::auto_refresh_enabled(const QString &service_key) const {
  const Svc *svc = find(service_key);
  return svc && svc->auto_enabled;
}

bool StreamingImportView::auto_refresh_running() const {
  return AnyRun();
}

bool StreamingImportView::auto_refresh_paused(const QString &service_key) const {
  const Svc *svc = find(service_key);
  return svc && svc->auto_enabled && svc->auto_paused;
}

void StreamingImportView::RestoreAutoRefresh(const QString &service_key, const QString &account_key) {

  Svc *svc = find(service_key);
  if (!svc || account_key.isEmpty()) return;
  svc->auto_enabled = true;
  svc->auto_account = account_key;
  svc->auto_paused = false;
  svc->auto_due = QDateTime();
  UpdateAll();

}

bool StreamingImportView::SnapshotExistsNow(const Svc *svc, const QString &identity) const {
  // The host is the truth: a snapshot that was deleted while a read was running must not be created again, that would open and select a new playlist.
  return snapshot_exists_ ? snapshot_exists_(identity) : svc->imported.contains(identity);
}

bool StreamingImportView::SnapshotPlaying(const Svc *svc, const QString &playlist_id) const {
  return snapshot_playing_ && snapshot_playing_(Identity(svc->entry.url_scheme, playlist_id));
}

// The opt-in belongs to the account that gave it. Another account that is signed in now drops it, loudly.
bool StreamingImportView::AutoAccountMatches(Svc *svc) {

  if (!svc->auto_enabled) return false;
  const QString account = svc->entry.account->account_key();
  if (svc->session->state() != AccountSession::State::SignedIn || account.isEmpty()) return false;
  if (account == svc->auto_account) return true;
  DropAutoOptIn(svc, tr("Automatic refresh of %1 playlists was turned off because a different account is signed in. Turn it on again if you want it for this account.").arg(svc->entry.name));
  return false;

}

void StreamingImportView::DropAutoOptIn(Svc *svc, const QString &reason) {

  CancelAuto(svc);
  svc->auto_enabled = false;
  svc->auto_account.clear();
  svc->auto_paused = false;
  svc->auto_waiting_rate_limit = false;
  svc->auto_due = QDateTime();
  svc->auto_deferred.clear();
  svc->auto_deferred_ids.clear();
  svc->auto_result = reason;  // shown by the automatic refresh row, which a later read of the list does not clear
  if (!reason.isEmpty()) svc->notice = reason;
  Q_EMIT AutoRefreshChanged(svc->entry.account->key(), false, QString());
  UpdateAll();

}

void StreamingImportView::SetAutoEnabled(Svc *svc, const bool enabled) {

  if (enabled) {
    const QString account = svc->entry.account->account_key();
    if (svc->session->state() != AccountSession::State::SignedIn || account.isEmpty()) {
      UpdateAutoRow(svc);
      return;
    }
    svc->auto_enabled = true;
    svc->auto_account = account;
    svc->auto_paused = false;
    svc->auto_waiting_rate_limit = false;
    svc->auto_due = QDateTime();  // due at the next eligible activation or timer tick, not in the middle of the click
    svc->auto_result.clear();
    Q_EMIT AutoRefreshChanged(svc->entry.account->key(), true, account);
  }
  else {
    DropAutoOptIn(svc, QString());
    svc->auto_result.clear();
  }
  UpdateAll();

}

bool StreamingImportView::AutoDue(Svc *svc) {

  if (!svc->auto_enabled || svc->auto_paused || svc->run.active) return false;
  if (!AutoAccountMatches(svc)) return false;
  const QDateTime now = QDateTime::currentDateTime();
  if (svc->auto_due.isValid() && now < svc->auto_due) return false;
  if (now < svc->retry_blocked_until) return false;
  return true;

}

void StreamingImportView::RunAutoRefreshIfDue() {

  if (AnyRun()) return;
  for (const Svc *svc : std::as_const(services_)) {
    if (svc->importer->busy()) return;  // a manual read is going on: never overlap
  }
  for (Svc *svc : std::as_const(services_)) {
    if (AutoDue(svc)) {
      StartAutoRun(svc);
      return;
    }
  }

}

void StreamingImportView::StartAutoRun(Svc *svc) {

  svc->run = Svc::AutoRun();
  svc->run.active = true;
  svc->run.reading_list = true;
  svc->auto_waiting_rate_limit = false;
  svc->auto_result.clear();
  svc->importer->LoadPlaylists();
  if (!svc->importer->busy()) {
    // The importer refused to start: do not stay in a run that nothing drives.
    svc->run = Svc::AutoRun();
  }
  UpdateAll();

}

void StreamingImportView::AutoNext(Svc *svc) {

  if (!svc->run.active || svc->run.reading_list || svc->importer->busy()) return;
  if (!svc->auto_enabled || svc->session->state() != AccountSession::State::SignedIn || !AutoAccountMatches(svc)) {
    EndAutoRun(svc, AutoEnd::Ineligible);
    UpdateAll();
    return;
  }

  while (!svc->run.queue.isEmpty()) {
    const PlaylistImport::PlaylistInfo info = svc->run.queue.takeFirst();
    if (!SnapshotExistsNow(svc, Identity(svc->entry.url_scheme, info.id))) continue;  // deleted meanwhile
    if (SnapshotPlaying(svc, info.id)) {
      AutoDefer(svc, info);  // not even read: playback is never disturbed
      continue;
    }
    svc->run.current = info;
    svc->importer->ImportPlaylist(info);
    UpdateAll();
    return;
  }
  EndAutoRun(svc, AutoEnd::Done);
  UpdateAll();

}

void StreamingImportView::AutoDefer(Svc *svc, const PlaylistImport::PlaylistInfo &playlist) {

  const QString identity = Identity(svc->entry.url_scheme, playlist.id);
  if (!svc->auto_deferred_ids.contains(identity)) {
    svc->auto_deferred_ids.insert(identity);
    svc->auto_deferred << playlist.name;
  }

}

void StreamingImportView::AutoCommit(Svc *svc, const PlaylistImport::PlaylistInfo &playlist, const QList<PlaylistImport::Track> &tracks, const int skipped) {

  const QString identity = Identity(svc->entry.url_scheme, playlist.id);
  // Checked again at the moment of replacement: it may have started playing while it was being read. Then the read is dropped.
  if (SnapshotPlaying(svc, playlist.id)) {
    AutoDefer(svc, playlist);
    return;
  }
  if (!SnapshotExistsNow(svc, identity)) return;

  SongList songs;
  songs.reserve(tracks.count());
  for (const PlaylistImport::Track &track : tracks) {
    songs << PlaylistImport::TrackToSong(track, svc->entry.song_source, svc->entry.url_scheme);
  }
  svc->auto_committing = true;
  svc->auto_replied = false;
  Q_EMIT SnapshotReady(svc->entry.name, identity, QStringLiteral("%1: %2").arg(svc->entry.name, playlist.name), songs, skipped);
  svc->auto_committing = false;

}

void StreamingImportView::CancelAuto(Svc *svc) {

  if (!svc->run.active) return;
  svc->run.queue.clear();
  if (svc->importer->busy()) svc->importer->Cancel();  // ends the run through Cancelled
  if (svc->run.active) EndAutoRun(svc, AutoEnd::Cancelled);

}

void StreamingImportView::EndAutoRun(Svc *svc, const AutoEnd how, const QString &detail) {

  const int refreshed = svc->run.refreshed;
  svc->run = Svc::AutoRun();
  const QDateTime now = QDateTime::currentDateTime();
  const QString time = QLocale().toString(now.time(), QLocale::ShortFormat);
  switch (how) {
    case AutoEnd::Done:
      svc->auto_last_ok = now;
      svc->auto_due = now.addMSecs(auto_interval_ms_);
      svc->auto_result = refreshed == 1 ? tr("Updated 1 imported playlist at %1.").arg(time) : tr("Updated %1 imported playlists at %2.").arg(refreshed).arg(time);
      break;
    case AutoEnd::Cancelled:
      svc->auto_due = now.addMSecs(auto_interval_ms_);
      svc->auto_result = tr("Cancelled at %1. Playlists already updated stay updated, the others were not changed.").arg(time);
      break;
    case AutoEnd::Failed:
      svc->auto_due = now.addMSecs(auto_interval_ms_);
      svc->auto_result = detail;
      break;
    case AutoEnd::Ineligible:
      svc->auto_due = QDateTime();
      svc->auto_result = tr("Stopped at %1 because the account is no longer signed in.").arg(time);
      break;
  }
  // The next service that is due, strictly after this run.
  QTimer::singleShot(0, this, &StreamingImportView::RunAutoRefreshIfDue);

}

QString StreamingImportView::AutoStatusText(const Svc *svc) const {

  const int minutes = std::max(1, auto_interval_ms_ / 60000);
  if (!svc->auto_enabled) {
    return (svc->auto_result.isEmpty() ? QString() : svc->auto_result + u' ') + tr("Off. Adrift only reads %1 again when you ask. Turn it on to update the playlists you already imported, about every %2 minutes, read-only.").arg(svc->entry.name).arg(minutes);
  }
  QStringList parts;
  const QString name = svc->entry.name;
  if (svc->run.active) {
    parts << (svc->run.reading_list ? tr("Updating imported playlists: reading the list from %1…").arg(name)
                                     : tr("Updating imported playlists: %1 of %2 done.").arg(svc->run.refreshed).arg(svc->run.total));
  }
  else if (svc->auto_paused) {
    parts << tr("Paused because of the problem above. Imported playlists keep their last snapshot. Resume when it is solved.");
  }
  else if (svc->session->state() != AccountSession::State::SignedIn) {
    parts << tr("Waiting for you to sign in to %1.").arg(name);
  }
  else if (svc->auto_waiting_rate_limit && svc->auto_due.isValid()) {
    parts << tr("%1 asked Adrift to wait. Next automatic try at %2.").arg(name, QLocale().toString(svc->auto_due.time(), QLocale::ShortFormat));
  }
  else if (svc->auto_due.isValid()) {
    parts << tr("On. Only playlists already imported are updated, read-only. Next about %1.").arg(QLocale().toString(svc->auto_due.time(), QLocale::ShortFormat));
  }
  else {
    parts << tr("On, read-only. The first update runs the next time this page opens or within %1 minutes.").arg(minutes);
  }
  if (!svc->auto_result.isEmpty() && !svc->run.active) parts << svc->auto_result;
  if (!svc->auto_deferred.isEmpty()) {
    parts << tr("Playing now, so not changed: %1. It is updated by a later run when it is not playing.").arg(svc->auto_deferred.join(u", "_s));
  }
  return parts.join(u' ');

}

void StreamingImportView::UpdateAutoRow(const Svc *svc) {

  if (!svc) {
    auto_check_->hide();
    auto_status_->hide();
    auto_resume_->hide();
    return;
  }
  const bool signed_in = svc->session->state() == AccountSession::State::SignedIn;
  const int minutes = std::max(1, auto_interval_ms_ / 60000);
  auto_check_->show();
  auto_status_->show();
  {
    QSignalBlocker blocker(auto_check_);
    auto_check_->setChecked(svc->auto_enabled);
  }
  auto_check_->setText(tr("Keep imported %1 playlists up to date (read-only, about every %2 minutes)").arg(svc->entry.name).arg(minutes));
  auto_check_->setEnabled(svc->auto_enabled || signed_in);
  auto_check_->setToolTip(svc->auto_enabled || signed_in ? QString() : tr("Sign in to turn this on."));
  auto_status_->setText(AutoStatusText(svc));
  auto_resume_->setVisible(svc->auto_enabled && svc->auto_paused && !svc->run.active);
  auto_resume_->setEnabled(!svc->importer->busy() && !AnyRun());

}
