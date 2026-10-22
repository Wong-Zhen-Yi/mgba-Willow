/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "GameLibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <mgba-util/vfs.h>

#include "moc_GameLibrary.cpp"

using namespace QGBA;

namespace {

enum GameRole { PathRole = Qt::UserRole, GamePlatformRole, LocationRole };

QString romPlatform(const QString& suffix) {
	if (suffix == QStringLiteral("gba") || suffix == QStringLiteral("agb")) {
		return QStringLiteral("GBA");
	}
	if (suffix == QStringLiteral("gbc")) {
		return QStringLiteral("GBC");
	}
	if (suffix == QStringLiteral("gb") || suffix == QStringLiteral("sgb")) {
		return QStringLiteral("GB");
	}
	return { };
}

bool isMetadata(const QString& path) {
	for (const QString& part : QDir::fromNativeSeparators(path).split('/')) {
		if (part == QStringLiteral("__MACOSX") || part == QStringLiteral(".DS_Store") || part.startsWith("._")) {
			return true;
		}
	}
	return false;
}

QString archivePlatform(const QFileInfo& file) {
	QString format;
	QString suffix = file.suffix().toLower();
#if defined(USE_LIBZIP) || defined(USE_MINIZIP)
	if (suffix == QStringLiteral("zip")) {
		format = QStringLiteral("ZIP");
	}
#endif
#ifdef USE_LZMA
	if (suffix == QStringLiteral("7z")) {
		format = QStringLiteral("7Z");
	}
#endif
	if (format.isEmpty()) {
		return { };
	}
	// Inspect names only: don't extract/decompress ROMs while browsing the library.
	VDir* archive = VDirOpenArchive(file.absoluteFilePath().toUtf8().constData());
	if (!archive) {
		return { };
	}
	bool hasRom = false;
	while (VDirEntry* entry = archive->listNext(archive)) {
		QString name = QString::fromUtf8(entry->name(entry));
		if (entry->type(entry) == VFS_DIRECTORY || isMetadata(name)) {
			continue;
		}
		QString extension = QFileInfo(name).suffix().toLower();
		if (!romPlatform(extension).isEmpty() || extension == QStringLiteral("rom") ||
		    extension == QStringLiteral("bin") || extension == QStringLiteral("mb")) {
			hasRom = true;
			break;
		}
	}
	archive->close(archive);
	return hasRom ? format : QString();
}

// Paint cards rather than creating a widget and loading artwork for every ROM.
class GameCardDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(208, 232); }

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		QRect card = option.rect.adjusted(5, 5, -5, -5);
		bool selected = option.state & QStyle::State_Selected;
		bool hovered = option.state & QStyle::State_MouseOver;
		painter->setPen(QPen(selected ? QColor("#a7c6ff") : QColor(hovered ? "#667da0" : "#2c374b"), selected ? 2 : 1));
		painter->setBrush(QColor(hovered ? "#222e43" : "#1b2537"));
		painter->drawRoundedRect(card, 13, 13);

		QRect art = card.adjusted(10, 10, -10, -80);
		QString platform = index.data(GamePlatformRole).toString();
		QColor accent(platform == QStringLiteral("GBA")     ? "#7996ff" :
		                  platform == QStringLiteral("GBC") ? "#5bcbb1" :
		                  platform == QStringLiteral("GB")  ? "#d2ad73" :
		                                                      "#c496ef");
		QLinearGradient gradient(art.topLeft(), art.bottomRight());
		gradient.setColorAt(0, accent.darker(170));
		gradient.setColorAt(1, QColor("#25314b"));
		painter->setPen(Qt::NoPen);
		painter->setBrush(gradient);
		painter->drawRoundedRect(art, 8, 8);

		// A stylized cartridge provides intentional artwork without downloading covers.
		QRect cartridge(art.center().x() - 30, art.center().y() - 33, 60, 66);
		painter->setPen(QPen(accent.lighter(120), 2));
		painter->setBrush(QColor(20, 28, 46, 130));
		painter->drawRoundedRect(cartridge, 7, 7);
		painter->drawRoundedRect(cartridge.adjusted(10, 12, -10, -20), 3, 3);
		painter->drawLine(cartridge.left() + 17, cartridge.bottom() - 9, cartridge.right() - 17,
		                  cartridge.bottom() - 9);

		QFont font = option.font;
		font.setPointSize(9);
		font.setBold(true);
		painter->setFont(font);
		painter->setPen(accent.lighter(150));
		painter->drawText(art.adjusted(9, 6, -9, -6), Qt::AlignTop | Qt::AlignLeft, platform);

		QRect title(card.left() + 12, art.bottom() + 12, card.width() - 24, 24);
		font.setPointSize(11);
		painter->setFont(font);
		painter->setPen(QColor("#f2f5fc"));
		painter->drawText(title, Qt::AlignVCenter,
		                  painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, title.width()));
		font.setPointSize(9);
		font.setBold(false);
		painter->setFont(font);
		painter->setPen(QColor("#a6b4ca"));
		painter->drawText(
		    title.translated(0, 25), Qt::AlignVCenter,
		    painter->fontMetrics().elidedText(index.data(LocationRole).toString(), Qt::ElideMiddle, title.width()));
		painter->restore();
	}
};

QPushButton* makeButton(const QString& text, const QString& name, QWidget* parent) {
	auto* button = new QPushButton(text, parent);
	button->setObjectName(name);
	button->setCursor(Qt::PointingHandCursor);
	return button;
}

QLabel* makeLabel(const QString& text, const QString& name, QWidget* parent) {
	auto* label = new QLabel(text, parent);
	label->setObjectName(name);
	label->setTextFormat(Qt::PlainText);
	return label;
}

}

GameLibrary::GameLibrary(QWidget* parent)
    : QWidget(parent) {
	setObjectName("gameLibrary");
	setAttribute(Qt::WA_StyledBackground);
	setStyleSheet(QStringLiteral(
	    "#gameLibrary { background: #111827; color: #edf2fb; }"
	    "#gameLibrary QWidget { color: #edf2fb; font-family: 'Segoe UI'; font-size: 13px; }"
	    "#gameLibrary QLabel { background: transparent; }"
	    "#gameLibrary #sidebar { background: #151f30; border-right: 1px solid #2b364a; }"
	    "#gameLibrary #brand { color: #b4caff; font-size: 23px; font-weight: 700; letter-spacing: 3px; }"
	    "#gameLibrary #heading { font-size: 30px; font-weight: 700; }"
	    "#gameLibrary #emptyTitle { font-size: 22px; font-weight: 600; }"
	    "#gameLibrary #subtitle, #gameLibrary #count, #gameLibrary #folderPath,"
	    "#gameLibrary #emptyText, #gameLibrary #hint { color: #a6b4ca; }"
	    "#gameLibrary QPushButton { background: #26344b; border: 1px solid #3b4b65;"
	    " border-radius: 8px; padding: 10px 14px; text-align: left; }"
	    "#gameLibrary QPushButton:hover { background: #344764; border-color: #7f9cc9; }"
	    "#gameLibrary QPushButton:focus, #gameLibrary QLineEdit:focus { border: 2px solid #b4caff; }"
	    "#gameLibrary QPushButton:disabled { color: #718099; background: #1b2537; border-color: #283349; }"
	    "#gameLibrary #play, #gameLibrary #emptyChoose { background: #a9c5ff; color: #111c32;"
	    " font-weight: 600; border-color: #a9c5ff; }"
	    "#gameLibrary #play:hover, #gameLibrary #emptyChoose:hover { background: #c5d7ff; }"
	    "#gameLibrary #play:disabled { color: #718099; background: #1b2537; border-color: #283349; }"
	    "#gameLibrary QLineEdit { background: #1b2537; border: 1px solid #3b4b65;"
	    " border-radius: 9px; padding: 11px; selection-background-color: #435c88; }"
	    "#gameLibrary QListWidget { background: transparent; border: none; outline: none; }"
	    "#gameLibrary QScrollBar:vertical { background: #151f30; width: 10px; margin: 0; }"
	    "#gameLibrary QScrollBar::handle:vertical { background: #43516c; border-radius: 5px; min-height: 30px; }"
	    "#gameLibrary QScrollBar::add-line:vertical, #gameLibrary QScrollBar::sub-line:vertical { height: 0; }"));

	auto* root = new QHBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);
	auto* sidebar = new QWidget(this);
	sidebar->setObjectName("sidebar");
	sidebar->setFixedWidth(178);
	auto* nav = new QVBoxLayout(sidebar);
	nav->setContentsMargins(20, 28, 20, 24);
	nav->setSpacing(12);
	nav->addWidget(makeLabel(tr("WILLOW"), "brand", sidebar));
	nav->addWidget(makeLabel(tr("mGBA / Game library"), "subtitle", sidebar));
	nav->addSpacing(26);
	auto* library = makeButton(tr("Game library"), "libraryNav", sidebar);
	nav->addWidget(library);
	auto* open = makeButton(tr("Open ROM…"), "openROM", sidebar);
	nav->addWidget(open);
	nav->addStretch();
	auto* settings = makeButton(tr("Settings"), "settings", sidebar);
	nav->addWidget(settings);
	root->addWidget(sidebar);

	auto* content = new QVBoxLayout;
	content->setContentsMargins(28, 26, 28, 20);
	content->setSpacing(16);
	root->addLayout(content, 1);
	auto* header = new QHBoxLayout;
	auto* heading = new QVBoxLayout;
	heading->addWidget(makeLabel(tr("Your games"), "heading", this));
	heading->addWidget(makeLabel(tr("A little nostalgia. Ready to play."), "subtitle", this));
	header->addLayout(heading, 1);
	auto* folder = makeButton(tr("Choose folder…"), "chooseFolder", this);
	header->addWidget(folder);
	content->addLayout(header);

	auto* tools = new QHBoxLayout;
	m_search = new QLineEdit(this);
	m_search->setObjectName("gameSearch");
	m_search->setPlaceholderText(tr("Search your games"));
	m_search->setAccessibleName(tr("Search your games"));
	m_search->setClearButtonEnabled(true);
	tools->addWidget(m_search, 1);
	auto* refreshButton = makeButton(tr("Refresh"), "refresh", this);
	tools->addWidget(refreshButton);
	content->addLayout(tools);
	m_count = makeLabel(QString(), "count", this);
	content->addWidget(m_count);

	m_pages = new QStackedWidget(this);
	m_games = new QListWidget(m_pages);
	m_games->setObjectName("gameCards");
	m_games->setAccessibleName(tr("Game cards. Select a game and press Play, or double-click to start."));
	m_games->setViewMode(QListView::IconMode);
	m_games->setResizeMode(QListView::Adjust);
	m_games->setMovement(QListView::Static);
	m_games->setSelectionMode(QAbstractItemView::SingleSelection);
	m_games->setUniformItemSizes(true);
	m_games->setWordWrap(false);
	m_games->setMouseTracking(true);
	m_games->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_games->setItemDelegate(new GameCardDelegate(m_games));
	m_pages->addWidget(m_games);

	auto* empty = new QWidget(m_pages);
	auto* emptyLayout = new QVBoxLayout(empty);
	emptyLayout->addStretch();
	m_emptyTitle = makeLabel(QString(), "emptyTitle", empty);
	m_emptyTitle->setAlignment(Qt::AlignCenter);
	emptyLayout->addWidget(m_emptyTitle);
	m_emptyText = makeLabel(QString(), "emptyText", empty);
	m_emptyText->setWordWrap(true);
	m_emptyText->setAlignment(Qt::AlignCenter);
	emptyLayout->addWidget(m_emptyText);
	m_emptyChoose = makeButton(tr("Choose your ROM folder"), "emptyChoose", empty);
	emptyLayout->addWidget(m_emptyChoose, 0, Qt::AlignHCenter);
	emptyLayout->addStretch();
	m_pages->addWidget(empty);
	content->addWidget(m_pages, 1);

	auto* bottom = new QHBoxLayout;
	bottom->addWidget(makeLabel(tr("Select a card to play · Double-click or press Enter to start"), "hint", this), 1);
	m_play = makeButton(tr("Play game"), "play", this);
	m_play->setEnabled(false);
	bottom->addWidget(m_play);
	content->addLayout(bottom);
	m_folderLabel = makeLabel(QString(), "folderPath", this);
	m_folderLabel->setWordWrap(true);
	m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	content->addWidget(m_folderLabel);

	connect(folder, &QPushButton::clicked, this, &GameLibrary::chooseFolder);
	connect(m_emptyChoose, &QPushButton::clicked, this, &GameLibrary::chooseFolder);
	connect(refreshButton, &QPushButton::clicked, this, &GameLibrary::refresh);
	connect(library, &QPushButton::clicked, m_search, [this]() { m_search->setFocus(); });
	connect(open, &QPushButton::clicked, this, &GameLibrary::openRequested);
	connect(settings, &QPushButton::clicked, this, &GameLibrary::settingsRequested);
	connect(m_search, &QLineEdit::textChanged, this, &GameLibrary::filterGames);
	connect(m_games, &QListWidget::itemSelectionChanged, this, &GameLibrary::updateStatus);
	connect(m_games, &QListWidget::itemActivated, this, &GameLibrary::launchSelected);
	connect(m_play, &QPushButton::clicked, this, &GameLibrary::launchSelected);
	connect(&m_scanTimer, &QTimer::timeout, this, &GameLibrary::scanBatch);
	m_scanTimer.setInterval(1);
	updateStatus();
}

GameLibrary::~GameLibrary() = default;

void GameLibrary::chooseFolder() {
	QString folder = QFileDialog::getExistingDirectory(this, tr("Choose your ROM folder"), m_folder);
	if (!folder.isEmpty()) {
		setFolder(folder);
	}
}

void GameLibrary::setFolder(const QString& folder) {
	QString normalized = folder.isEmpty() ? QString() : QDir(folder).absolutePath();
	bool changed = m_folder != normalized;
	m_folder = normalized;
	if (changed) {
		emit folderChanged(m_folder);
	}
	refresh();
}

void GameLibrary::refresh() {
	m_scanTimer.stop();
	m_iterator.reset();
	m_games->clear();
	QFileInfo directory(m_folder);
	if (!m_folder.isEmpty() && directory.isDir() && directory.isReadable()) {
		// Do not follow directory symlinks: avoid loops and scanning outside the chosen tree.
		m_iterator = std::make_unique<QDirIterator>(m_folder, QDir::Files | QDir::Readable | QDir::NoSymLinks,
		                                            QDirIterator::Subdirectories);
		m_scanTimer.start();
	}
	m_folderLabel->setText(m_folder.isEmpty() ? tr("Your folder choice is remembered on this computer.") :
	                                            tr("ROM folder: %1").arg(QDir::toNativeSeparators(m_folder)));
	updateStatus();
	if (!m_iterator) {
		emit scanFinished();
	}
}

void GameLibrary::scanBatch() {
	// Yield between batches so search, folder changes and window events remain responsive.
	for (int i = 0; i < 128 && m_iterator->hasNext(); ++i) {
		m_iterator->next();
		QFileInfo file = m_iterator->fileInfo();
		QString relativePath = QDir(m_folder).relativeFilePath(file.absoluteFilePath());
		if (isMetadata(relativePath)) {
			continue;
		}
		QString platform = romPlatform(file.suffix().toLower());
		bool compressed = platform.isEmpty();
		if (compressed) {
			platform = archivePlatform(file);
		}
		if (platform.isEmpty()) {
			continue;
		}
		auto* item = new QListWidgetItem(file.completeBaseName(), m_games);
		item->setData(PathRole, file.absoluteFilePath());
		item->setData(GamePlatformRole, platform);
		item->setData(LocationRole, relativePath);
		item->setToolTip(compressed ? tr("%1\nCompressed ROM: plays directly without extracting files.\n"
		                                 "For multi-game archives, Play opens the first compatible ROM.")
		                                  .arg(file.absoluteFilePath()) :
		                              file.absoluteFilePath());
		item->setHidden(!item->text().contains(m_search->text().trimmed(), Qt::CaseInsensitive));
		if (compressed) {
			// Opening archives can take longer than ordinary file discovery. Yield after each one.
			break;
		}
	}
	if (!m_iterator->hasNext()) {
		m_scanTimer.stop();
		m_iterator.reset();
		m_games->sortItems(Qt::AscendingOrder);
		updateStatus();
		emit scanFinished();
	} else {
		updateStatus();
	}
}

void GameLibrary::filterGames() {
	for (int i = 0; i < m_games->count(); ++i) {
		auto* item = m_games->item(i);
		item->setHidden(!item->text().contains(m_search->text().trimmed(), Qt::CaseInsensitive));
	}
	updateStatus();
}

void GameLibrary::updateStatus() {
	int visible = 0;
	for (int i = 0; i < m_games->count(); ++i) {
		visible += !m_games->item(i)->isHidden();
	}
	m_count->setText(
	    m_iterator ?
	        tr("Finding games… %1 found").arg(m_games->count()) :
	        tr("%1 of %2 games · Game Boy / Game Boy Color / Game Boy Advance").arg(visible).arg(m_games->count()));
	m_pages->setCurrentIndex(visible ? 0 : 1);
	m_emptyChoose->setVisible(m_folder.isEmpty() || (!m_iterator && !m_games->count()));
	if (m_folder.isEmpty()) {
		m_emptyTitle->setText(tr("Your next adventure starts here"));
		m_emptyText->setText(tr("Choose the folder containing your ROMs. We’ll remember it for next time.\n"
		                        "Your files stay exactly where they are."));
	} else if (m_iterator) {
		m_emptyTitle->setText(tr("Finding your games…"));
		m_emptyText->setText(tr("Looking through your ROM folder and its subfolders."));
	} else if (!QFileInfo(m_folder).isDir() || !QFileInfo(m_folder).isReadable()) {
		m_emptyTitle->setText(tr("ROM folder unavailable"));
		m_emptyText->setText(tr("Reconnect the drive and refresh, or choose another folder.\n"
		                        "Your saved folder has not been forgotten."));
	} else if (!m_games->count()) {
		m_emptyTitle->setText(tr("No games in this folder yet"));
		QStringList formats { ".gba", ".agb", ".gb", ".gbc", ".sgb" };
#if defined(USE_LIBZIP) || defined(USE_MINIZIP)
		formats.append(".zip");
#endif
#ifdef USE_LZMA
		formats.append(".7z");
#endif
		m_emptyText->setText(tr("Add supported ROMs or compressed games (%1), then refresh.\n"
		                        "Subfolders are included. Nintendo DS games are not supported by mGBA.")
		                         .arg(formats.join(", ")));
	} else {
		m_emptyTitle->setText(tr("No matching games"));
		m_emptyText->setText(tr("Try another title or clear the search box."));
	}
	auto* item = m_games->selectedItems().value(0, nullptr);
	m_play->setEnabled(item && !item->isHidden());
}

void GameLibrary::launchSelected() {
	auto* item = m_games->selectedItems().value(0, nullptr);
	if (!item || item->isHidden()) {
		return;
	}
	QString path = item->data(PathRole).toString();
	if (!QFileInfo(path).isFile()) {
		refresh();
		return;
	}
	emit gameSelected(path);
}
