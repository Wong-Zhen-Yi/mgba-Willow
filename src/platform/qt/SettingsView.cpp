/* Copyright (c) 2013-2021 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "SettingsView.h"
#include "moc_SettingsView.cpp"

#include "AudioProcessor.h"
#include "AutoPlay.h"
#include "CheckBoxDelegate.h"
#include "ConfigController.h"
#include "Display.h"
#include "GBAApp.h"
#include "GameLibrary.h"
#include "GBAKeyEditor.h"
#include "InputController.h"
#include "RotatedHeaderView.h"
#include "ShaderSelector.h"
#include "ShortcutView.h"

#ifdef M_CORE_GB
#include "GameBoy.h"
#include <mgba/gb/interface.h>
#endif

#include <QApplication>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QProcess>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <mgba/core/serialize.h>
#include <mgba/core/version.h>
#include <mgba/internal/gba/gba.h>

#ifdef BUILD_SDL
#define SDL_MAIN_HANDLED
#include "platform/sdl/sdl-events.h"
#endif

using namespace QGBA;

SettingsView::SettingsView(ConfigController* controller, InputController* inputController, ShortcutController* shortcutController, LogController* logController, QWidget* parent)
	: QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint)
	, m_controller(controller)
	, m_logModel(logController)
{
	m_ui.setupUi(this);
	setupSinglePage();

#ifdef M_CORE_GB
	for (auto& model : GameBoy::modelList()) {
		m_ui.gbModel->addItem(GameBoy::modelName(model), model);
		m_ui.sgbModel->addItem(GameBoy::modelName(model), model);
		m_ui.cgbModel->addItem(GameBoy::modelName(model), model);
		m_ui.cgbHybridModel->addItem(GameBoy::modelName(model), model);
		m_ui.cgbSgbModel->addItem(GameBoy::modelName(model), model);
	}

	m_ui.gbModel->setCurrentIndex(m_ui.gbModel->findData(GB_MODEL_DMG));
	m_ui.sgbModel->setCurrentIndex(m_ui.gbModel->findData(GB_MODEL_SGB));
	m_ui.cgbModel->setCurrentIndex(m_ui.gbModel->findData(GB_MODEL_CGB));
	m_ui.cgbHybridModel->setCurrentIndex(m_ui.gbModel->findData(GB_MODEL_CGB));
	m_ui.cgbSgbModel->setCurrentIndex(m_ui.gbModel->findData(GB_MODEL_CGB));
#endif

	reloadConfig();

	connect(m_ui.autorunScripts, &QAbstractButton::pressed, this, &SettingsView::openAutorunScripts);

	connect(m_ui.volume, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), [this](int v) {
		if (v < m_ui.volumeFf->value()) {
			m_ui.volumeFf->setValue(v);
		}
	});

	connect(m_ui.mute, &QAbstractButton::toggled, [this](bool e) {
		if (e) {
			m_ui.muteFf->setChecked(e);
		}
	});

	connect(m_ui.nativeGB, &QAbstractButton::pressed, [this]() {
		m_ui.fpsTarget->setValue(double(GBA_ARM7TDMI_FREQUENCY) / double(VIDEO_TOTAL_LENGTH));
	});

	if (m_ui.savegamePath->text().isEmpty()) {
		m_ui.savegameSameDir->setChecked(true);
	}
	connect(m_ui.savegameSameDir, &QAbstractButton::toggled, [this] (bool e) {
		if (e) {
			m_ui.savegamePath->clear();
		}
	});
	connect(m_ui.savegameBrowse, &QAbstractButton::pressed, [this] () {
		selectPath(m_ui.savegamePath, m_ui.savegameSameDir);
	});

	if (m_ui.savestatePath->text().isEmpty()) {
		m_ui.savestateSameDir->setChecked(true);
	}
	connect(m_ui.savestateSameDir, &QAbstractButton::toggled, [this] (bool e) {
		if (e) {
			m_ui.savestatePath->clear();
		}
	});
	connect(m_ui.savestateBrowse, &QAbstractButton::pressed, [this] () {
		selectPath(m_ui.savestatePath, m_ui.savestateSameDir);
	});

	if (m_ui.screenshotPath->text().isEmpty()) {
		m_ui.screenshotSameDir->setChecked(true);
	}
	connect(m_ui.screenshotSameDir, &QAbstractButton::toggled, [this] (bool e) {
		if (e) {
			m_ui.screenshotPath->clear();
		}
	});
	connect(m_ui.screenshotBrowse, &QAbstractButton::pressed, [this] () {
		selectPath(m_ui.screenshotPath, m_ui.screenshotSameDir);
	});

	if (m_ui.patchPath->text().isEmpty()) {
		m_ui.patchSameDir->setChecked(true);
	}
	connect(m_ui.patchSameDir, &QAbstractButton::toggled, [this] (bool e) {
		if (e) {
			m_ui.patchPath->clear();
		}
	});
	connect(m_ui.patchBrowse, &QAbstractButton::pressed, [this] () {
		selectPath(m_ui.patchPath, m_ui.patchSameDir);
	});

	if (m_ui.cheatsPath->text().isEmpty()) {
		m_ui.cheatsSameDir->setChecked(true);
	}
	connect(m_ui.cheatsSameDir, &QAbstractButton::toggled, [this] (bool e) {
		if (e) {
			m_ui.cheatsPath->clear();
		}
	});
	connect(m_ui.cheatsBrowse, &QAbstractButton::pressed, [this] () {
		selectPath(m_ui.cheatsPath, m_ui.cheatsSameDir);
	});
	connect(m_ui.bgImageBrowse, &QAbstractButton::pressed, [this] () {
		selectImage(m_ui.bgImage);
	});
	connect(m_ui.clearCache, &QAbstractButton::pressed, this, &SettingsView::libraryCleared);

	// TODO: Move to reloadConfig()
	QVariant audioDriver = m_controller->getQtOption("audioDriver");
#ifdef BUILD_QT_MULTIMEDIA
	m_ui.audioDriver->addItem(tr("Qt Multimedia"), static_cast<int>(AudioProcessor::Driver::QT_MULTIMEDIA));
	if (!audioDriver.isNull() && audioDriver.toInt() == static_cast<int>(AudioProcessor::Driver::QT_MULTIMEDIA)) {
		m_ui.audioDriver->setCurrentIndex(m_ui.audioDriver->count() - 1);
	}
#endif

#ifdef BUILD_SDL
	m_ui.audioDriver->addItem(tr("SDL"), static_cast<int>(AudioProcessor::Driver::SDL));
	if (audioDriver.isNull() || audioDriver.toInt() == static_cast<int>(AudioProcessor::Driver::SDL)) {
		m_ui.audioDriver->setCurrentIndex(m_ui.audioDriver->count() - 1);
	}
#endif

	// TODO: Move to reloadConfig()
	QVariant displayDriver = m_controller->getQtOption("displayDriver");
	m_ui.displayDriver->addItem(tr("Software (Qt)"), static_cast<int>(Display::Driver::QT));
	if (!displayDriver.isNull() && displayDriver.toInt() == static_cast<int>(Display::Driver::QT)) {
		m_ui.displayDriver->setCurrentIndex(m_ui.displayDriver->count() - 1);
	}

#if defined(BUILD_GL) || defined(BUILD_GLES2) || defined(BUILD_GLES3) || defined(USE_EPOXY)
	m_ui.displayDriver->addItem(tr("OpenGL"), static_cast<int>(Display::Driver::OPENGL));
	if (displayDriver.isNull() || displayDriver.toInt() == static_cast<int>(Display::Driver::OPENGL)) {
		m_ui.displayDriver->setCurrentIndex(m_ui.displayDriver->count() - 1);
	}
#endif

#ifdef BUILD_GL
	m_ui.displayDriver->addItem(tr("OpenGL (force version 1.x)"), static_cast<int>(Display::Driver::OPENGL1));
	if (!displayDriver.isNull() && displayDriver.toInt() == static_cast<int>(Display::Driver::OPENGL1)) {
		m_ui.displayDriver->setCurrentIndex(m_ui.displayDriver->count() - 1);
	}
#endif

	ApplicationUpdater* updater = GBAApp::app()->updater();
	m_ui.currentChannel->setText(ApplicationUpdater::readableChannel());
	m_ui.currentVersion->setText(ApplicationUpdater::currentVersion());
	QDateTime lastCheck = updater->lastCheck();
	if (!lastCheck.isNull()) {
		m_ui.lastChecked->setText(lastCheck.toLocalTime().toString());
	}
	connect(m_ui.checkUpdate, &QAbstractButton::pressed, updater, &ApplicationUpdater::checkUpdate);
	connect(updater, &ApplicationUpdater::updateAvailable, this, [this, updater](bool hasUpdate) {
		updateChecked();
		if (hasUpdate) {
			m_ui.availVersion->setText(updater->updateInfo());
		}
	});
	for (const QString& channel : ApplicationUpdater::listChannels()) {
		m_ui.updateChannel->addItem(ApplicationUpdater::readableChannel(channel), channel);
		if (channel == ApplicationUpdater::currentChannel()) {
			m_ui.updateChannel->setCurrentIndex(m_ui.updateChannel->count() - 1);
		}
	}
	connect(m_ui.updateChannel, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, updater](int) {
		QString channel = m_ui.updateChannel->currentData().toString();
		updater->setChannel(channel);
		auto updates = updater->listUpdates();
		if (updates.contains(channel)) {
			m_ui.availVersion->setText(updates[channel]);
		} else {
			m_ui.availVersion->setText(tr("None"));
		}
	});
	m_ui.availVersion->setText(updater->updateInfo());

	// TODO: Move to reloadConfig()
	QVariant cameraDriver = m_controller->getQtOption("cameraDriver");
	m_ui.cameraDriver->addItem(tr("None (Still Image)"), static_cast<int>(InputController::CameraDriver::NONE));
	if (cameraDriver.isNull() || cameraDriver.toInt() == static_cast<int>(InputController::CameraDriver::NONE)) {
		m_ui.cameraDriver->setCurrentIndex(m_ui.cameraDriver->count() - 1);
		m_ui.camera->setEnabled(false);
	}

#ifdef BUILD_QT_MULTIMEDIA
	m_ui.cameraDriver->addItem(tr("Qt Multimedia"), static_cast<int>(InputController::CameraDriver::QT_MULTIMEDIA));
	if (!cameraDriver.isNull() && cameraDriver.toInt() == static_cast<int>(InputController::CameraDriver::QT_MULTIMEDIA)) {
		m_ui.cameraDriver->setCurrentIndex(m_ui.cameraDriver->count() - 1);
		m_ui.camera->setEnabled(true);
	}
	QList<QPair<QByteArray, QString>> cameras = inputController->listCameras();
	QByteArray currentCamera = m_controller->getQtOption("camera").toByteArray();
	for (const auto& camera : cameras) {
		m_ui.camera->addItem(camera.second, camera.first);
		if (camera.first == currentCamera) {
			m_ui.camera->setCurrentIndex(m_ui.camera->count() - 1);
		}
	}
#endif

#ifdef M_CORE_GBA
	connect(m_ui.gbaBiosBrowse, &QPushButton::clicked, [this]() {
		selectBios(m_ui.gbaBios);
	});
#else
	m_ui.gbaBiosBrowse->hide();
#endif

#ifdef M_CORE_GB
	connect(m_ui.gbBiosBrowse, &QPushButton::clicked, [this]() {
		selectBios(m_ui.gbBios);
	});
	connect(m_ui.gbcBiosBrowse, &QPushButton::clicked, [this]() {
		selectBios(m_ui.gbcBios);
	});
	connect(m_ui.sgbBiosBrowse, &QPushButton::clicked, [this]() {
		selectBios(m_ui.sgbBios);
	});

	QList<QColor> defaultColors;
	defaultColors.append(QColor(0xF8, 0xF8, 0xF8));
	defaultColors.append(QColor(0xA8, 0xA8, 0xA8));
	defaultColors.append(QColor(0x50, 0x50, 0x50));
	defaultColors.append(QColor(0x00, 0x00, 0x00));
	defaultColors.append(QColor(0xF8, 0xF8, 0xF8));
	defaultColors.append(QColor(0xA8, 0xA8, 0xA8));
	defaultColors.append(QColor(0x50, 0x50, 0x50));
	defaultColors.append(QColor(0x00, 0x00, 0x00));
	defaultColors.append(QColor(0xF8, 0xF8, 0xF8));
	defaultColors.append(QColor(0xA8, 0xA8, 0xA8));
	defaultColors.append(QColor(0x50, 0x50, 0x50));
	defaultColors.append(QColor(0x00, 0x00, 0x00));
	QList<QWidget*> colors{
		m_ui.color0,
		m_ui.color1,
		m_ui.color2,
		m_ui.color3,
		m_ui.color4,
		m_ui.color5,
		m_ui.color6,
		m_ui.color7,
		m_ui.color8,
		m_ui.color9,
		m_ui.color10,
		m_ui.color11
	};
	for (int colorId = 0; colorId < 12; ++colorId) {
		bool ok;
		uint color = m_controller->getOption(QString("gb.pal[%0]").arg(colorId)).toUInt(&ok);
		if (ok) {
			defaultColors[colorId] = QColor::fromRgb(color);
			m_gbColors[colorId] = color | 0xFF000000;
		} else {
			m_gbColors[colorId] = defaultColors[colorId].rgb() & ~0xFF000000;
		}
		m_colorPickers[colorId] = ColorPicker(colors[colorId], defaultColors[colorId]);
		connect(&m_colorPickers[colorId], &ColorPicker::colorChanged, this, [this, colorId](const QColor& color) {
			m_gbColors[colorId] = color.rgb();
		});
	}

	const GBColorPreset* colorPresets;
	QString usedPreset = m_controller->getQtOption("gb.pal").toString();
	size_t nPresets = GBColorPresetList(&colorPresets);
	for (size_t i = 0; i < nPresets; ++i) {
		QString presetName(colorPresets[i].name);
		m_ui.colorPreset->addItem(presetName);
		if (usedPreset == presetName) {
			m_ui.colorPreset->setCurrentIndex(i);
		}
	}
	connect(m_ui.colorPreset, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, colorPresets](int n) {
		const GBColorPreset* preset = &colorPresets[n];
		for (int colorId = 0; colorId < 12; ++colorId) {
			uint32_t color = preset->colors[colorId] | 0xFF000000;
			m_colorPickers[colorId].setColor(color);
			m_gbColors[colorId] = color;
		}
	});
#else
	m_ui.gbBiosBrowse->hide();
	m_ui.gbcBiosBrowse->hide();
	m_ui.sgbBiosBrowse->hide();
#endif

	GBAKeyEditor* editor = new GBAKeyEditor(inputController, InputController::KEYBOARD, QString(), this);
	addPage(tr("Keyboard"), editor, Page::KEYBOARD);
	connect(m_ui.buttonBox, &QDialogButtonBox::accepted, editor, &GBAKeyEditor::save);

	GBAKeyEditor* buttonEditor = nullptr;
#ifdef BUILD_SDL
#if SDL_VERSION_ATLEAST(2, 0, 0)
	QString profile = inputController->profileForType(SDL_BINDING_CONTROLLER);
	buttonEditor = new GBAKeyEditor(inputController, SDL_BINDING_CONTROLLER, profile);
#else
	QString profile = inputController->profileForType(SDL_BINDING_BUTTON);
	buttonEditor = new GBAKeyEditor(inputController, SDL_BINDING_BUTTON, profile);
#endif
	addPage(tr("Controllers"), buttonEditor, Page::CONTROLLERS);
	connect(m_ui.buttonBox, &QDialogButtonBox::accepted, buttonEditor, &GBAKeyEditor::save);
#endif

	connect(m_ui.buttonBox, &QDialogButtonBox::accepted, this, &SettingsView::updateConfig);
	connect(m_ui.buttonBox, &QDialogButtonBox::clicked, [this, editor, buttonEditor](QAbstractButton* button) {
		if (m_ui.buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole) {
			updateConfig();
			editor->save();
			if (buttonEditor) {
				buttonEditor->save();
			}
		}
	});

	QLocale englishLocale("en");
	m_ui.languages->addItem("English", englishLocale);
	QDir ts(":/translations/");
	for (auto& name : ts.entryList()) {
		if (!name.endsWith(".qm") || !name.startsWith(binaryName)) {
			continue;
		}
		QLocale locale(name.remove(QString("%0-").arg(binaryName)).remove(".qm"));
		if (locale.language() == QLocale::English || locale.language() == QLocale::C) {
			continue;
		}
		QString endonym = locale.nativeLanguageName();
		// Manualy handle some cases that Qt can't seem to do properly
		if (locale.language() == QLocale::Spanish) {
			// Qt insists this is called español de España, regardless of if we set the country
			endonym = u8"Español";
		} else if (locale.language() == QLocale::Portuguese && locale.country() == QLocale::Brazil) {
			// Qt insists that Brazilian Portuguese is just called Português
			endonym = u8"Português brasileiro";
		}
		endonym[0] = endonym[0].toUpper();
		m_ui.languages->addItem(endonym, locale);
		if (locale.bcp47Name() == QLocale().bcp47Name()) {
			m_ui.languages->setCurrentIndex(m_ui.languages->count() - 1);
		}
	}

	m_ui.loggingView->setModel(&m_logModel);
	m_ui.loggingView->setItemDelegate(new CheckBoxDelegate(m_ui.loggingView));
	m_ui.loggingView->setHorizontalHeader(new RotatedHeaderView(Qt::Horizontal));
	m_ui.loggingView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_ui.loggingView->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	connect(m_ui.loggingView, SIGNAL(clicked(QModelIndex)), m_ui.loggingView, SLOT(setCurrentIndex(QModelIndex)));

	connect(m_ui.logFileBrowse, &QAbstractButton::pressed, [this] () {
		QString path = GBAApp::app()->getSaveFileName(this, "Select log file");
		if (!path.isNull()) {
			m_ui.logFile->setText(path);
		}
	});

	m_checkTimer.setInterval(60);
	m_checkTimer.setSingleShot(false);
	connect(&m_checkTimer, &QTimer::timeout, this, &SettingsView::updateChecked);
	m_checkTimer.start();
	updateChecked();

	ShortcutView* shortcutView = new ShortcutView();
	shortcutView->setController(shortcutController);
	shortcutView->setInputController(inputController);
	addPage(tr("Shortcuts"), shortcutView, Page::SHORTCUTS);

#if defined(BUILD_GLES2) || defined(BUILD_GLES3) || defined(USE_EPOXY)
	m_dummyShader = new QLabel(tr("Shaders are not supported when the display driver is not OpenGL.\n\n"
		"If it is set to OpenGL and you still see this, your graphics card or drivers may be too old."));
	m_dummyShader->setWordWrap(true);
	m_dummyShader->setAlignment(Qt::AlignCenter);
	addPage(tr("Shaders"), m_dummyShader, Page::SHADERS);
#endif
}

SettingsView::~SettingsView() {
#if defined(BUILD_GLES2) || defined(BUILD_GLES3) || defined(USE_EPOXY)
	if (m_shader) {
		m_shader->setParent(nullptr);
	}
#endif
}

void SettingsView::setShaderSelector(ShaderSelector* shaderSelector) {
#if  defined(BUILD_GLES2) || defined(BUILD_GLES3) || defined(USE_EPOXY)
	removePage(Page::SHADERS);
	if (m_shader) {
		QObject::disconnect(this, nullptr, m_shader, nullptr);
		QObject::disconnect(m_ui.buttonBox, nullptr, m_shader, nullptr);
	}
	m_shader = shaderSelector;
	if (shaderSelector) {
		QObject::connect(this, &SettingsView::saveSettingsRequested, m_shader, &ShaderSelector::saveSettings);
		QObject::connect(m_ui.buttonBox, &QDialogButtonBox::rejected, m_shader, &ShaderSelector::revert);
		addPage(tr("Shaders"), m_shader, Page::SHADERS);
	} else {
		addPage(tr("Shaders"), m_dummyShader, Page::SHADERS);
	}
#endif
}

void SettingsView::selectPage(SettingsView::Page page) {
	const int row = sectionRow(page);
	if (row < 0) {
		return;
	}
	// Sections have their final positions once the dialog is shown, so jump on the next pass.
	QTimer::singleShot(0, this, [this, row]() { scrollToSection(row, false); });
}

void SettingsView::setMobileStatus(const QString& status) {
	if (m_mobileStatus) {
		m_mobileStatus->setText(status.isEmpty() ? tr("Off.") : status);
	}
}

namespace {

QString startupShortcutPath() {
	return QDir(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation))
	    .filePath("Startup/mGBA Willow.lnk");
}

// Willow's launcher scripts sit beside the MCP folder that Launch mGBA.cmd points Willow at.
QString willowToolsScript(const QString& name) {
	for (const char* variable : { "WILLOW_MOBILE_SCRIPT", "WILLOW_AUTOPLAY_SCRIPT" }) {
		const QString script = qEnvironmentVariable(variable);
		if (script.isEmpty()) {
			continue;
		}
		QDir tools = QFileInfo(script).dir();
		if (tools.cdUp() && QFileInfo(tools.filePath(name)).isFile()) {
			return tools.filePath(name);
		}
	}
	return {};
}

QLabel* makeNote(const QString& text, QWidget* parent) {
	QLabel* note = new QLabel(text, parent);
	note->setWordWrap(true);
	note->setForegroundRole(QPalette::PlaceholderText);
	return note;
}

QString searchableText(const QWidget* root) {
	QStringList text;
	for (const QWidget* widget : root->findChildren<QWidget*>()) {
		if (auto* label = qobject_cast<const QLabel*>(widget)) {
			text << label->text();
		} else if (auto* button = qobject_cast<const QAbstractButton*>(widget)) {
			text << button->text();
		} else if (auto* group = qobject_cast<const QGroupBox*>(widget)) {
			text << group->title();
		} else if (auto* combo = qobject_cast<const QComboBox*>(widget)) {
			for (int i = 0; i < combo->count(); ++i) {
				text << combo->itemText(i);
			}
		}
		text << widget->toolTip();
	}
	return text.join('\n').remove('&');
}

}

void SettingsView::setupSinglePage() {
	// The .ui pages become sections, in the order of their sidebar entries.
	static const Page uiPages[] = {
		Page::AV, Page::GAMEPLAY, Page::INTERFACE, Page::UPDATE, Page::EMULATION,
		Page::ENHANCEMENTS, Page::BIOS, Page::PATHS, Page::LOGGING, Page::GB,
	};
	QStringList titles;
	for (int i = 0; i < m_ui.tabs->count(); ++i) {
		titles << m_ui.tabs->item(i)->text();
	}
	QList<QWidget*> pages;
	while (m_ui.stackedWidget->count()) {
		QWidget* page = m_ui.stackedWidget->widget(0);
		m_ui.stackedWidget->removeWidget(page);
		// removeWidget keeps the parent, and the stacked widget is deleted below.
		page->setParent(this);
		pages << page;
	}
	m_ui.tabs->clear();

	m_content = new QWidget;
	m_sectionLayout = new QVBoxLayout(m_content);
	m_sectionLayout->setContentsMargins(20, 16, 20, 24);
	m_sectionLayout->setSpacing(0);
	m_noResults = makeNote(QString(), m_content);
	m_noResults->setAlignment(Qt::AlignCenter);
	m_noResults->hide();
	m_sectionLayout->addWidget(m_noResults);
	m_sectionLayout->addStretch(1);

	m_scroll = new QScrollArea(this);
	m_scroll->setWidget(m_content);
	m_scroll->setWidgetResizable(true);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll->setFrameShape(QFrame::NoFrame);
	delete m_ui.gridLayout->replaceWidget(m_ui.stackedWidget, m_scroll);
	delete m_ui.stackedWidget;
	m_ui.stackedWidget = nullptr;

	m_scrollAnimation = new QPropertyAnimation(m_scroll->verticalScrollBar(), "value", this);
	m_scrollAnimation->setDuration(220);
	m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
	connect(m_scrollAnimation, &QPropertyAnimation::finished, this, [this]() { m_navigating = false; });
	connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, &SettingsView::syncSectionToScroll);
	connect(m_ui.tabs, &QListWidget::currentRowChanged, this, [this](int row) { scrollToSection(row, true); });

	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(tr("Search settings"));
	m_search->setAccessibleName(tr("Search settings"));
	m_search->setClearButtonEnabled(true);
	m_search->setMaximumWidth(m_ui.tabs->maximumWidth());
	m_ui.gridLayout->addWidget(m_search, 0, 0);
	connect(m_search, &QLineEdit::textChanged, this, &SettingsView::filterSections);
	auto* find = new QShortcut(QKeySequence::Find, this);
	connect(find, &QShortcut::activated, m_search, [this]() {
		m_search->setFocus(Qt::ShortcutFocusReason);
		m_search->selectAll();
	});

	m_ui.tabs->setMinimumWidth(170);
	m_ui.tabs->setAccessibleName(tr("Settings sections"));

	// One page scrolls, so the dialog can follow the screen size instead of the tallest page.
	m_ui.gridLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	setMinimumSize(720, 460);
	QSize size(1000, 780);
	if (QScreen* display = screen()) {
		size = size.boundedTo(display->availableGeometry().size() * 0.9);
	}
	resize(size);

	addPage(tr("Willow"), createWillowPage(), Page::WILLOW);
	for (int i = 0; i < pages.count(); ++i) {
		const Page page = i < int(std::size(uiPages)) ? uiPages[i] : Page::AV;
#ifndef M_CORE_GB
		if (page == Page::GB) {
			pages[i]->setParent(this);
			pages[i]->hide();
			continue;
		}
#endif
		addPage(i < titles.count() ? titles[i] : QString(), pages[i], page);
	}
	m_ui.loggingView->setMinimumHeight(360);
}

QWidget* SettingsView::createWillowPage() {
	QWidget* page = new QWidget;
	auto* layout = new QVBoxLayout(page);

	auto* library = new QGroupBox(tr("Game library"), page);
	auto* libraryForm = new QFormLayout(library);
	auto* folderRow = new QHBoxLayout;
	m_romFolder = new QLineEdit(library);
	m_romFolder->setPlaceholderText(tr("No folder chosen"));
	auto* browse = new QPushButton(tr("Browse"), library);
	folderRow->addWidget(m_romFolder, 1);
	folderRow->addWidget(browse);
	libraryForm->addRow(tr("ROM folder:"), folderRow);
	libraryForm->addRow(makeNote(tr("Willow's game library lists the games in this folder. Your iPhone shows the same list."), library));
	connect(browse, &QAbstractButton::clicked, this, [this]() {
		const QString folder = GBAApp::app()->getOpenDirectoryName(this, tr("Choose your ROM folder"), m_romFolder->text());
		if (!folder.isEmpty()) {
			m_romFolder->setText(QDir::toNativeSeparators(folder));
		}
	});
	layout->addWidget(library);

	auto* autoPlay = new QGroupBox(tr("Auto Play"), page);
	auto* autoPlayForm = new QFormLayout(autoPlay);
	m_autoPlayModel = new QComboBox(autoPlay);
	for (const auto& [id, label] : AutoPlay::modelList()) {
		m_autoPlayModel->addItem(label, id);
	}
	autoPlayForm->addRow(tr("Model:"), m_autoPlayModel);
	// Moved here from Audio: it only applies while an AI plays.
	m_ui.normalSpeedMusic->parentWidget()->layout()->removeWidget(m_ui.normalSpeedMusic);
	m_ui.normalSpeedMusic->setParent(autoPlay);
	autoPlayForm->addRow(m_ui.normalSpeedMusic);
	autoPlayForm->addRow(makeNote(tr("Click ▶ Auto Play in the status bar, or tap Auto Play on your iPhone, to let Codex "
	                                 "play the open game. A new model applies the next time Auto Play starts."),
	                              autoPlay));
	layout->addWidget(autoPlay);

#ifdef Q_OS_WIN
	auto* mobile = new QGroupBox(tr("iPhone"), page);
	auto* mobileForm = new QFormLayout(mobile);
	m_mobileStatus = new QLabel(tr("Off."), mobile);
	m_mobileStatus->setWordWrap(true);
	m_mobileStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
	mobileForm->addRow(tr("Willow Mobile:"), m_mobileStatus);
	auto* pair = new QPushButton(tr("Pair iPhone…"), mobile);
	connect(pair, &QAbstractButton::clicked, this, &SettingsView::mobilePairingRequested);
	auto* pairRow = new QHBoxLayout;
	pairRow->addWidget(pair);
	pairRow->addStretch(1);
	mobileForm->addRow(QString(), pairRow);
	m_startAtSignIn = new QCheckBox(tr("Open Willow minimized when you sign in to Windows"), mobile);
	mobileForm->addRow(m_startAtSignIn);
	QLabel* signInNote = makeNote(
	    tr("Keeps the iPhone link available whenever this PC is on. Sign-in launches use the last build."), mobile);
	mobileForm->addRow(signInNote);
	if (willowToolsScript("install-startup.ps1").isEmpty()) {
		m_startAtSignIn->setEnabled(false);
		signInNote->setText(tr("Open Willow using Launch mGBA.cmd to change this."));
	}
	layout->addWidget(mobile);
#endif

	layout->addStretch(1);
	return page;
}

int SettingsView::sectionRow(Page index) const {
	for (int row = 0; row < m_sections.count(); ++row) {
		if (m_sections[row].page == index) {
			return row;
		}
	}
	return -1;
}

void SettingsView::scrollToSection(int row, bool animate) {
	if (row < 0 || row >= m_sections.count() || m_sections[row].widget->isHidden()) {
		return;
	}
	if (m_ui.tabs->currentRow() != row) {
		const QSignalBlocker blocker(m_ui.tabs);
		m_ui.tabs->setCurrentRow(row);
	}
	QScrollBar* bar = m_scroll->verticalScrollBar();
	// Land with the section title near the top, past its divider.
	const Section& section = m_sections[row];
	const int title = section.widget->y() + (section.divider->isHidden() ? 0 : section.divider->height());
	const int target = qBound(0, title - m_sectionLayout->contentsMargins().top(), bar->maximum());
	m_scrollAnimation->stop();
	if (!animate || !isVisible() || target == bar->value()) {
		// A short last section cannot reach the top; navigating keeps its row highlighted anyway.
		m_navigating = true;
		bar->setValue(target);
		m_navigating = false;
		return;
	}
	m_navigating = true;
	m_scrollAnimation->setStartValue(bar->value());
	m_scrollAnimation->setEndValue(target);
	m_scrollAnimation->start();
}

void SettingsView::syncSectionToScroll() {
	if (m_navigating) {
		return;
	}
	const QScrollBar* bar = m_scroll->verticalScrollBar();
	const bool atEnd = bar->maximum() > 0 && bar->value() >= bar->maximum();
	const int line = bar->value() + m_scroll->viewport()->height() / 4;
	int current = -1;
	for (int row = 0; row < m_sections.count(); ++row) {
		const QWidget* widget = m_sections[row].widget;
		if (widget->isHidden()) {
			continue;
		}
		if (current < 0 || atEnd || widget->y() <= line) {
			current = row;
		}
	}
	if (current >= 0 && current != m_ui.tabs->currentRow()) {
		const QSignalBlocker blocker(m_ui.tabs);
		m_ui.tabs->setCurrentRow(current);
	}
}

void SettingsView::filterSections(const QString& query) {
	const QString needle = query.trimmed();
	int shown = 0;
	for (int row = 0; row < m_sections.count(); ++row) {
		const Section& section = m_sections[row];
		const bool match = needle.isEmpty() || m_ui.tabs->item(row)->text().contains(needle, Qt::CaseInsensitive) ||
		    searchableText(section.widget).contains(needle, Qt::CaseInsensitive);
		section.widget->setVisible(match);
		m_ui.tabs->item(row)->setHidden(!match);
		shown += match;
	}
	m_noResults->setText(tr("No settings match “%1”.").arg(needle));
	m_noResults->setVisible(!shown);
	updateDividers();
	m_scrollAnimation->stop();
	m_navigating = false;
	m_scroll->verticalScrollBar()->setValue(0);
	// Positions update after the layout runs.
	QTimer::singleShot(0, this, &SettingsView::syncSectionToScroll);
}

bool SettingsView::eventFilter(QObject* object, QEvent* event) {
	// Scrolling the page must not change a control that happens to be under the pointer.
	if (event->type() == QEvent::Wheel) {
		auto* widget = qobject_cast<QWidget*>(object);
		if (widget && !widget->hasFocus()) {
			QCoreApplication::sendEvent(m_scroll->verticalScrollBar(), event);
			return true;
		}
	}
	return QDialog::eventFilter(object, event);
}

void SettingsView::applyStartAtSignIn() {
	if (!m_startAtSignIn || m_startAtSignIn->isChecked() == m_loadedStartAtSignIn) {
		return;
	}
	const QString script = willowToolsScript("install-startup.ps1");
	if (script.isEmpty()) {
		return;
	}
	QStringList arguments{ "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", QDir::toNativeSeparators(script) };
	if (!m_startAtSignIn->isChecked()) {
		arguments << "-Remove";
	}
	QProcess process;
	process.setStandardOutputFile(QProcess::nullDevice());
	process.setStandardErrorFile(QProcess::nullDevice());
	QApplication::setOverrideCursor(Qt::WaitCursor);
	process.start(QDir(qEnvironmentVariable("SystemRoot")).filePath("System32/WindowsPowerShell/v1.0/powershell.exe"),
	              arguments);
	process.waitForFinished(20000);
	QApplication::restoreOverrideCursor();
	m_loadedStartAtSignIn = QFileInfo::exists(startupShortcutPath());
	m_startAtSignIn->setChecked(m_loadedStartAtSignIn);
}

QString SettingsView::makePortablePath(const QString& path) {
	if (m_controller->isPortable()) {
		QDir configDir(m_controller->configDir());
		QFileInfo pathInfo(path);
		if (pathInfo.canonicalPath() == configDir.canonicalPath()) {
			return configDir.relativeFilePath(pathInfo.canonicalFilePath());
		}
	}
	return path;
}

void SettingsView::selectBios(QLineEdit* bios) {
	QString filename = GBAApp::app()->getOpenFileName(this, tr("Select BIOS"));
	if (!filename.isEmpty()) {
		bios->setText(makePortablePath(filename));
	}
}

void SettingsView::selectPath(QLineEdit* field, QCheckBox* sameDir) {
	QString path = GBAApp::app()->getOpenDirectoryName(this, tr("Select directory"));
	if (!path.isNull()) {
		sameDir->setChecked(false);
		field->setText(makePortablePath(path));
	}
}

void SettingsView::selectImage(QLineEdit* field) {
	QString path = GBAApp::app()->getOpenFileName(this, tr("Select image"), tr("Image file (*.png *.jpg *.jpeg)"));
	if (!path.isNull()) {
		field->setText(makePortablePath(path));
	}
}

void SettingsView::updateConfig() {
	saveSetting("gba.bios", m_ui.gbaBios);
	saveSetting("gb.bios", m_ui.gbBios);
	saveSetting("gbc.bios", m_ui.gbcBios);
	saveSetting("sgb.bios", m_ui.sgbBios);
	saveSetting("sgb.borders", m_ui.sgbBorders);
	saveSetting("useBios", m_ui.useBios);
	saveSetting("skipBios", m_ui.skipBios);
	saveSetting("sampleRate", m_ui.sampleRate);
	saveSetting("videoSync", m_ui.videoSync);
	saveSetting("audioSync", m_ui.audioSync);
	saveSetting("frameskip", m_ui.frameskip);
	saveSetting("autofireThreshold", m_ui.autofireThreshold);
	saveSetting("lockAspectRatio", m_ui.lockAspectRatio);
	saveSetting("lockIntegerScaling", m_ui.lockIntegerScaling);
	saveSetting("interframeBlending", m_ui.interframeBlending);
	saveSetting("showOSD", m_ui.showOSD);
	saveSetting("showFrameCounter", m_ui.showFrameCounter);
	saveSetting("showResetInfo", m_ui.showResetInfo);
	saveSetting("volume", m_ui.volume);
	saveSetting("mute", m_ui.mute);
	saveSetting("fastForwardVolume", m_ui.volumeFf);
	saveSetting("fastForwardMute", m_ui.muteFf);
	saveSetting("normalSpeedMusic", m_ui.normalSpeedMusic);
	saveSetting("rewindEnable", m_ui.rewind);
	saveSetting("rewindBufferCapacity", m_ui.rewindCapacity);
	saveSetting("rewindBufferInterval", m_ui.rewindBufferInterval);
	saveSetting("resampleVideo", m_ui.resampleVideo);
	saveSetting("allowOpposingDirections", m_ui.allowOpposingDirections);
	saveSetting("suspendScreensaver", m_ui.suspendScreensaver);
	saveSetting("pauseOnFocusLost", m_ui.pauseOnFocusLost);
	saveSetting("pauseOnMinimize", m_ui.pauseOnMinimize);
	saveSetting("muteOnFocusLost", m_ui.muteOnFocusLost);
	saveSetting("muteOnMinimize", m_ui.muteOnMinimize);
	saveSetting("savegamePath", m_ui.savegamePath);
	saveSetting("savestatePath", m_ui.savestatePath);
	saveSetting("screenshotPath", m_ui.screenshotPath);
	saveSetting("patchPath", m_ui.patchPath);
	saveSetting("cheatsPath", m_ui.cheatsPath);
	saveSetting("libraryStyle", m_ui.libraryStyle->currentIndex());
	saveSetting("preload", m_ui.preload);
	saveSetting("showFps", m_ui.showFps);
	saveSetting("cheatAutoload", m_ui.cheatAutoload);
	saveSetting("cheatAutosave", m_ui.cheatAutosave);
	saveSetting("showFilename", m_ui.showFilename);
	saveSetting("autoload", m_ui.autoload);
	saveSetting("autosave", m_ui.autosave);
	saveSetting("logToFile", m_ui.logToFile);
	saveSetting("logToStdout", m_ui.logToStdout);
	saveSetting("logFile", m_ui.logFile);
	saveSetting("useDiscordPresence", m_ui.useDiscordPresence);
	saveSetting("dynamicTitle", m_ui.dynamicTitle);
	saveSetting("videoScale", m_ui.videoScale);
	saveSetting("gba.forceGbp", m_ui.forceGbp);
	saveSetting("vbaBugCompat", m_ui.vbaBugCompat);
	saveSetting("updateAutoCheck", m_ui.updateAutoCheck);
	saveSetting("showFilenameInLibrary", m_ui.showFilenameInLibrary);
	saveSetting("backgroundImage", m_ui.bgImage);

	if (m_ui.audioBufferSize->currentText().toInt() > 8192) {
		m_ui.audioBufferSize->setCurrentText("8192");
	}
	saveSetting("audioBuffers", m_ui.audioBufferSize);

	if (m_ui.fastForwardUnbounded->isChecked()) {
		saveSetting("fastForwardRatio", "-1");
	} else {
		saveSetting("fastForwardRatio", m_ui.fastForwardRatio);
	}

	double nativeFps = double(GBA_ARM7TDMI_FREQUENCY) / double(VIDEO_TOTAL_LENGTH);
	if (fabs(nativeFps - m_ui.fpsTarget->value()) < 0.0001) {
		m_controller->setOption("fpsTarget", QVariant(nativeFps));
	} else {
		saveSetting("fpsTarget", m_ui.fpsTarget);
	}

	if (m_ui.fastForwardHeldUnbounded->isChecked()) {
		saveSetting("fastForwardHeldRatio", "-1");
	} else {
		saveSetting("fastForwardHeldRatio", m_ui.fastForwardHeldRatio);
	}

	switch (m_ui.idleOptimization->currentIndex() + IDLE_LOOP_IGNORE) {
	case IDLE_LOOP_IGNORE:
		saveSetting("idleOptimization", "ignore");
		break;
	case IDLE_LOOP_REMOVE:
		saveSetting("idleOptimization", "remove");
		break;
	case IDLE_LOOP_DETECT:
		saveSetting("idleOptimization", "detect");
		break;
	}

	int loadState = SAVESTATE_RTC;
	loadState |= m_ui.loadStateScreenshot->isChecked() ? SAVESTATE_SCREENSHOT : 0;
	loadState |= m_ui.loadStateSave->isChecked() ? SAVESTATE_SAVEDATA : 0;
	loadState |= m_ui.loadStateCheats->isChecked() ? SAVESTATE_CHEATS : 0;
	saveSetting("loadStateExtdata", loadState);

	int saveState = SAVESTATE_RTC | SAVESTATE_METADATA;
	saveState |= m_ui.saveStateScreenshot->isChecked() ? SAVESTATE_SCREENSHOT : 0;
	saveState |= m_ui.saveStateSave->isChecked() ? SAVESTATE_SAVEDATA : 0;
	saveState |= m_ui.saveStateCheats->isChecked() ? SAVESTATE_CHEATS : 0;
	saveSetting("saveStateExtdata", saveState);

	QVariant audioDriver = m_ui.audioDriver->itemData(m_ui.audioDriver->currentIndex());
	if (audioDriver != m_controller->getQtOption("audioDriver")) {
		m_controller->setQtOption("audioDriver", audioDriver);
		AudioProcessor::setDriver(static_cast<AudioProcessor::Driver>(audioDriver.toInt()));
		emit audioDriverChanged();
	}

	QVariant displayDriver = m_ui.displayDriver->itemData(m_ui.displayDriver->currentIndex());
	if (displayDriver != m_controller->getQtOption("displayDriver")) {
		m_controller->setQtOption("displayDriver", displayDriver);
		Display::setDriver(static_cast<Display::Driver>(displayDriver.toInt()));
		emit displayDriverChanged();
	}

	QVariant cameraDriver = m_ui.cameraDriver->itemData(m_ui.cameraDriver->currentIndex());
	QVariant oldCameraDriver = m_controller->getQtOption("cameraDriver");
	if (cameraDriver != oldCameraDriver) {
		m_controller->setQtOption("cameraDriver", cameraDriver);
		if (cameraDriver.toInt() != static_cast<int>(InputController::CameraDriver::NONE) || !oldCameraDriver.isNull()) {
			emit cameraDriverChanged();
		}
	}

	QVariant camera = m_ui.camera->itemData(m_ui.camera->currentIndex());
	QVariant oldCamera = m_controller->getQtOption("camera");
	if (camera != oldCamera) {
		m_controller->setQtOption("camera", camera);
		if (!oldCamera.isNull()) {
			emit cameraChanged(camera.toByteArray());
		}
	}

	QLocale language = m_ui.languages->itemData(m_ui.languages->currentIndex()).toLocale();
	if (language != m_controller->getQtOption("language").toLocale() && !(language.bcp47Name() == QLocale::system().bcp47Name() && m_controller->getQtOption("language").isNull())) {
		m_controller->setQtOption("language", language.bcp47Name());
		emit languageChanged();
	}

	if (m_ui.multiplayerAudioAll->isChecked()) {
		m_controller->setQtOption("multiplayerAudio", "all");
	} else if (m_ui.multiplayerAudio1->isChecked()) {
		m_controller->setQtOption("multiplayerAudio", "p1");
	} else if (m_ui.multiplayerAudioActive->isChecked()) {
		m_controller->setQtOption("multiplayerAudio", "active");
	}

	int hwaccelVideo = m_controller->getOption("hwaccelVideo").toInt();
	saveSetting("hwaccelVideo", m_ui.hwaccelVideo->currentIndex());
	if (hwaccelVideo != m_ui.hwaccelVideo->currentIndex()) {
		emit videoRendererChanged();
	}

	m_logModel.save(m_controller);
	m_logModel.logger()->setLogFile(m_ui.logFile->text());
	m_logModel.logger()->logToFile(m_ui.logToFile->isChecked());
	m_logModel.logger()->logToStdout(m_ui.logToStdout->isChecked());

#ifdef M_CORE_GB
	QVariant modelGB = m_ui.gbModel->currentData();
	if (modelGB.isValid()) {
		m_controller->setOption("gb.model", GBModelToName(static_cast<GBModel>(modelGB.toInt())));
	}

	QVariant modelSGB = m_ui.sgbModel->currentData();
	if (modelSGB.isValid()) {
		m_controller->setOption("sgb.model", GBModelToName(static_cast<GBModel>(modelSGB.toInt())));
	}

	QVariant modelCGB = m_ui.cgbModel->currentData();
	if (modelCGB.isValid()) {
		m_controller->setOption("cgb.model", GBModelToName(static_cast<GBModel>(modelCGB.toInt())));
	}

	QVariant modelCGBHybrid = m_ui.cgbHybridModel->currentData();
	if (modelCGBHybrid.isValid()) {
		m_controller->setOption("cgb.hybridModel", GBModelToName(static_cast<GBModel>(modelCGBHybrid.toInt())));
	}

	QVariant modelCGBSGB = m_ui.cgbSgbModel->currentData();
	if (modelCGBSGB.isValid()) {
		m_controller->setOption("cgb.sgbModel", GBModelToName(static_cast<GBModel>(modelCGBSGB.toInt())));
	}

	for (int colorId = 0; colorId < 12; ++colorId) {
		if (!(m_gbColors[colorId] & 0xFF000000)) {
			continue;
		}
		QString color = QString("gb.pal[%0]").arg(colorId);
		m_controller->setOption(color.toUtf8().constData(), m_gbColors[colorId] & ~0xFF000000);

	}
	m_controller->setQtOption("gb.pal", m_ui.colorPreset->currentText());

	int gbColors = GB_COLORS_CGB;
	if (m_ui.gbColor->isChecked()) {
		gbColors = GB_COLORS_NONE;
	} else if (m_ui.cgbColor->isChecked()) {
		gbColors = GB_COLORS_CGB;
	} else if (m_ui.sgbColor->isChecked()) {
		gbColors = GB_COLORS_SGB;
	} else if (m_ui.scgbColor->isChecked()) {
		gbColors = GB_COLORS_SGB_CGB_FALLBACK;
	}
	saveSetting("gb.colors", gbColors);
#endif

	// Only real changes, so Apply does not repeat Auto Play's "new model" notice or rescan the library.
	const QString autoPlayModel = m_autoPlayModel->currentData().toString();
	if (autoPlayModel != m_controller->getOption("autoPlayModel", AutoPlay::defaultModel())) {
		m_controller->setOption("autoPlayModel", autoPlayModel);
	}
	const QString romFolder = QDir::fromNativeSeparators(m_romFolder->text().trimmed());
	if (romFolder != m_loadedRomFolder) {
		m_loadedRomFolder = romFolder;
		emit romFolderChanged(romFolder);
	}
	applyStartAtSignIn();

	emit saveSettingsRequested();

	m_controller->write();

	emit pathsChanged();
	emit biosLoaded(mPLATFORM_GBA, m_ui.gbaBios->text());
}

void SettingsView::reloadConfig() {
	m_loadedRomFolder = m_controller->getQtOption(GameLibrary::folderSettingKey()).toString();
	m_romFolder->setText(QDir::toNativeSeparators(m_loadedRomFolder));
	const int model = m_autoPlayModel->findData(m_controller->getOption("autoPlayModel", AutoPlay::defaultModel()));
	m_autoPlayModel->setCurrentIndex(qMax(model, 0));
	if (m_startAtSignIn) {
		m_loadedStartAtSignIn = QFileInfo::exists(startupShortcutPath());
		m_startAtSignIn->setChecked(m_loadedStartAtSignIn);
	}

	loadSetting("bios", m_ui.gbaBios);
	loadSetting("gba.bios", m_ui.gbaBios);
	loadSetting("gb.bios", m_ui.gbBios);
	loadSetting("gbc.bios", m_ui.gbcBios);
	loadSetting("sgb.bios", m_ui.sgbBios);
	loadSetting("sgb.borders", m_ui.sgbBorders, true);
	loadSetting("useBios", m_ui.useBios);
	loadSetting("skipBios", m_ui.skipBios);
	loadSetting("audioBuffers", m_ui.audioBufferSize);
	loadSetting("sampleRate", m_ui.sampleRate);
	loadSetting("videoSync", m_ui.videoSync);
	loadSetting("audioSync", m_ui.audioSync);
	loadSetting("frameskip", m_ui.frameskip);
	loadSetting("fpsTarget", m_ui.fpsTarget);
	loadSetting("autofireThreshold", m_ui.autofireThreshold);
	loadSetting("lockAspectRatio", m_ui.lockAspectRatio);
	loadSetting("lockIntegerScaling", m_ui.lockIntegerScaling);
	loadSetting("interframeBlending", m_ui.interframeBlending);
	loadSetting("showOSD", m_ui.showOSD, true);
	loadSetting("showFrameCounter", m_ui.showFrameCounter);
	loadSetting("showResetInfo", m_ui.showResetInfo);
	loadSetting("volume", m_ui.volume, 0x100);
	loadSetting("mute", m_ui.mute, false);
	loadSetting("fastForwardVolume", m_ui.volumeFf, m_ui.volume->value());
	loadSetting("fastForwardMute", m_ui.muteFf, m_ui.mute->isChecked());
	loadSetting("normalSpeedMusic", m_ui.normalSpeedMusic, true);
	loadSetting("rewindEnable", m_ui.rewind);
	loadSetting("rewindBufferCapacity", m_ui.rewindCapacity);
	loadSetting("rewindBufferInterval", m_ui.rewindBufferInterval);
	loadSetting("resampleVideo", m_ui.resampleVideo);
	loadSetting("allowOpposingDirections", m_ui.allowOpposingDirections);
	loadSetting("suspendScreensaver", m_ui.suspendScreensaver);
	loadSetting("pauseOnFocusLost", m_ui.pauseOnFocusLost);
	loadSetting("pauseOnMinimize", m_ui.pauseOnMinimize);
	loadSetting("muteOnFocusLost", m_ui.muteOnFocusLost);
	loadSetting("muteOnMinimize", m_ui.muteOnMinimize);
	loadSetting("savegamePath", m_ui.savegamePath);
	loadSetting("savestatePath", m_ui.savestatePath);
	loadSetting("screenshotPath", m_ui.screenshotPath);
	loadSetting("patchPath", m_ui.patchPath);
	loadSetting("cheatsPath", m_ui.cheatsPath);
	loadSetting("preload", m_ui.preload, true);
	loadSetting("showFps", m_ui.showFps, true);
	loadSetting("cheatAutoload", m_ui.cheatAutoload, true);
	loadSetting("cheatAutosave", m_ui.cheatAutosave, true);
	loadSetting("showFilename", m_ui.showFilename, false);
	loadSetting("autoload", m_ui.autoload, true);
	loadSetting("autosave", m_ui.autosave, false);
	loadSetting("logToFile", m_ui.logToFile);
	loadSetting("logToStdout", m_ui.logToStdout);
	loadSetting("logFile", m_ui.logFile);
	loadSetting("useDiscordPresence", m_ui.useDiscordPresence);
	loadSetting("dynamicTitle", m_ui.dynamicTitle, true);
	loadSetting("gba.forceGbp", m_ui.forceGbp);
	loadSetting("vbaBugCompat", m_ui.vbaBugCompat, true);
	loadSetting("updateAutoCheck", m_ui.updateAutoCheck);
	loadSetting("showFilenameInLibrary", m_ui.showFilenameInLibrary);
	loadSetting("backgroundImage", m_ui.bgImage);

	m_ui.libraryStyle->setCurrentIndex(loadSetting("libraryStyle").toInt());

	double fastForwardRatio = loadSetting("fastForwardRatio").toDouble();
	if (fastForwardRatio <= 0) {
		m_ui.fastForwardUnbounded->setChecked(true);
		m_ui.fastForwardRatio->setEnabled(false);
	} else {
		m_ui.fastForwardUnbounded->setChecked(false);
		m_ui.fastForwardRatio->setEnabled(true);
		m_ui.fastForwardRatio->setValue(fastForwardRatio);
	}

	double fastForwardHeldRatio = loadSetting("fastForwardHeldRatio").toDouble();
	if (fastForwardHeldRatio <= 0) {
		m_ui.fastForwardHeldUnbounded->setChecked(true);
		m_ui.fastForwardHeldRatio->setEnabled(false);
	} else {
		m_ui.fastForwardHeldUnbounded->setChecked(false);
		m_ui.fastForwardHeldRatio->setEnabled(true);
		m_ui.fastForwardHeldRatio->setValue(fastForwardHeldRatio);
	}

	QString idleOptimization = loadSetting("idleOptimization");
	if (idleOptimization == "ignore") {
		m_ui.idleOptimization->setCurrentIndex(0);
	} else if (idleOptimization == "remove") {
		m_ui.idleOptimization->setCurrentIndex(1);
	} else if (idleOptimization == "detect") {
		m_ui.idleOptimization->setCurrentIndex(2);
	}

	bool ok;
	int loadState = loadSetting("loadStateExtdata").toInt(&ok);
	if (!ok) {
		loadState = SAVESTATE_SCREENSHOT | SAVESTATE_RTC;
	}
	m_ui.loadStateScreenshot->setChecked(loadState & SAVESTATE_SCREENSHOT);
	m_ui.loadStateSave->setChecked(loadState & SAVESTATE_SAVEDATA);
	m_ui.loadStateCheats->setChecked(loadState & SAVESTATE_CHEATS);

	int saveState = loadSetting("saveStateExtdata").toInt(&ok);
	if (!ok) {
		saveState = SAVESTATE_SCREENSHOT | SAVESTATE_SAVEDATA | SAVESTATE_CHEATS | SAVESTATE_RTC | SAVESTATE_METADATA;
	}
	m_ui.saveStateScreenshot->setChecked(saveState & SAVESTATE_SCREENSHOT);
	m_ui.saveStateSave->setChecked(saveState & SAVESTATE_SAVEDATA);
	m_ui.saveStateCheats->setChecked(saveState & SAVESTATE_CHEATS);

	m_logModel.reset();

#ifdef M_CORE_GB
	QString modelGB = m_controller->getOption("gb.model");
	if (!modelGB.isNull()) {
		GBModel model = GBNameToModel(modelGB.toUtf8().constData());
		int index = m_ui.gbModel->findData(model);
		m_ui.gbModel->setCurrentIndex(index >= 0 ? index : 0);
	}

	QString modelSGB = m_controller->getOption("sgb.model");
	if (!modelSGB.isNull()) {
		GBModel model = GBNameToModel(modelSGB.toUtf8().constData());
		int index = m_ui.sgbModel->findData(model);
		m_ui.sgbModel->setCurrentIndex(index >= 0 ? index : 0);
	}

	QString modelCGB = m_controller->getOption("cgb.model");
	if (!modelCGB.isNull()) {
		GBModel model = GBNameToModel(modelCGB.toUtf8().constData());
		int index = m_ui.cgbModel->findData(model);
		m_ui.cgbModel->setCurrentIndex(index >= 0 ? index : 0);
	}

	QString modelCGBHybrid = m_controller->getOption("cgb.hybridModel");
	if (!modelCGBHybrid.isNull()) {
		GBModel model = GBNameToModel(modelCGBHybrid.toUtf8().constData());
		int index = m_ui.cgbHybridModel->findData(model);
		m_ui.cgbHybridModel->setCurrentIndex(index >= 0 ? index : 0);
	}

	QString modelCGBSGB = m_controller->getOption("cgb.sgbModel");
	if (!modelCGBSGB.isNull()) {
		GBModel model = GBNameToModel(modelCGBSGB.toUtf8().constData());
		int index = m_ui.cgbSgbModel->findData(model);
		m_ui.cgbSgbModel->setCurrentIndex(index >= 0 ? index : 0);
	}

	switch (m_controller->getOption("gb.colors", m_controller->getOption("useCgbColors", true).toInt()).toInt()) {
	case GB_COLORS_NONE:
		m_ui.gbColor->setChecked(true);
		break;
	default:
	case GB_COLORS_CGB:
		m_ui.cgbColor->setChecked(true);
		break;
	case GB_COLORS_SGB:
		m_ui.sgbColor->setChecked(true);
		break;
	case GB_COLORS_SGB_CGB_FALLBACK:
		m_ui.scgbColor->setChecked(true);
		break;
	}
#endif

	int hwaccelVideo = m_controller->getOption("hwaccelVideo", 1).toInt();
	m_ui.hwaccelVideo->setCurrentIndex(hwaccelVideo);

	connect(m_ui.videoScale, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this](int value) {
		m_ui.videoScaleSize->setText(tr("(%1×%2)").arg(GBA_VIDEO_HORIZONTAL_PIXELS * value).arg(GBA_VIDEO_VERTICAL_PIXELS * value));
	});
	loadSetting("videoScale", m_ui.videoScale, 1);

	QString multiplayerAudio = m_controller->getQtOption("multiplayerAudio").toString();
	if (multiplayerAudio == QLatin1String("p1")) {
		m_ui.multiplayerAudio1->setChecked(true);
	} else if (multiplayerAudio == QLatin1String("active")) {
		m_ui.multiplayerAudioActive->setChecked(true);
	} else {
		m_ui.multiplayerAudioAll->setChecked(true);
	}
}

void SettingsView::updateChecked() {
	QDateTime now(QDateTime::currentDateTimeUtc());
	QDateTime lastCheck(GBAApp::app()->updater()->lastCheck());
	if (!lastCheck.isValid()) {
		m_ui.lastChecked->setText(tr("Never"));
		return;
	}
	qint64 ago = GBAApp::app()->updater()->lastCheck().secsTo(now);
	if (ago < 60) {
		m_ui.lastChecked->setText(tr("Just now"));
		return;
	}
	if (ago < 3600) {
		m_ui.lastChecked->setText(tr("Less than an hour ago"));
		return;
	}
	ago /= 3600;
	if (ago < 24) {
		m_ui.lastChecked->setText(tr("%n hour(s) ago", nullptr, ago));
		return;
	}
	ago /= 24;
	m_ui.lastChecked->setText(tr("%n day(s) ago", nullptr, ago));
}

void SettingsView::addPage(const QString& name, QWidget* view, Page index) {
	static const QMap<Page, const char*> descriptions{
		{ Page::WILLOW, QT_TR_NOOP("Your game library, Auto Play, and playing from your iPhone.") },
		{ Page::AV, QT_TR_NOOP("Sound output, volume, and how the picture is drawn.") },
		{ Page::GAMEPLAY, QT_TR_NOOP("Frame rate and sync, what loads with a game, and save state contents.") },
		{ Page::INTERFACE, QT_TR_NOOP("Language, the classic library, focus behaviour, and on-screen messages.") },
		{ Page::UPDATE, QT_TR_NOOP("Update channel and version checks.") },
		{ Page::EMULATION, QT_TR_NOOP("Fast forward, rewind, autofire, and compatibility.") },
		{ Page::ENHANCEMENTS, QT_TR_NOOP("Video renderer and high-resolution scaling.") },
		{ Page::BIOS, QT_TR_NOOP("BIOS files for each system.") },
		{ Page::PATHS, QT_TR_NOOP("Where save games, save states, screenshots, patches, and cheats are kept.") },
		{ Page::LOGGING, QT_TR_NOOP("Which messages are logged, and where.") },
		{ Page::GB, QT_TR_NOOP("Game Boy models, palettes, and the Game Boy Camera.") },
		{ Page::KEYBOARD, QT_TR_NOOP("Keyboard keys for each button.") },
		{ Page::CONTROLLERS, QT_TR_NOOP("Gamepad buttons for each button.") },
		{ Page::SHORTCUTS, QT_TR_NOOP("Keyboard and gamepad shortcuts (hotkeys) for menu actions.") },
		{ Page::SHADERS, QT_TR_NOOP("Post-processing shaders for the OpenGL display.") },
	};

	auto* section = new QWidget(m_content);
	auto* layout = new QVBoxLayout(section);
	const int indent = view->layout() ? view->layout()->contentsMargins().left() : 0;
	layout->setContentsMargins(0, 0, 0, 4);
	layout->setSpacing(2);
	// The first shown section drops its divider; see updateDividers().
	auto* divider = new QWidget(section);
	auto* dividerLayout = new QVBoxLayout(divider);
	dividerLayout->setContentsMargins(0, 20, 0, 12);
	auto* line = new QFrame(divider);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	dividerLayout->addWidget(line);
	layout->addWidget(divider);
	auto* title = new QLabel(name, section);
	QFont font = title->font();
	font.setBold(true);
	font.setPointSizeF(font.pointSizeF() * 1.4);
	title->setFont(font);
	title->setContentsMargins(indent, 0, indent, 0);
	layout->addWidget(title);
	if (descriptions.contains(index)) {
		QLabel* description = makeNote(tr(descriptions[index]), section);
		description->setContentsMargins(indent, 0, indent, 0);
		layout->addWidget(description);
	}
	view->setParent(section);
	view->show();
	layout->addWidget(view);
	if (index == Page::SHORTCUTS || index == Page::SHADERS) {
		view->setMinimumHeight(460);
	}

	for (QWidget* control : view->findChildren<QWidget*>()) {
		if (qobject_cast<QComboBox*>(control) || qobject_cast<QAbstractSpinBox*>(control) ||
		    qobject_cast<QAbstractSlider*>(control)) {
			if (control->focusPolicy() == Qt::WheelFocus) {
				control->setFocusPolicy(Qt::StrongFocus);
			}
			control->installEventFilter(this);
		}
	}

	m_sectionLayout->insertWidget(m_sections.count(), section);
	m_sections.append({ index, section, view, divider });
	m_ui.tabs->addItem(name);
	if (!m_search->text().isEmpty()) {
		filterSections(m_search->text());
	}
	updateDividers();
}

void SettingsView::updateDividers() {
	bool first = true;
	for (const Section& section : m_sections) {
		section.divider->setVisible(!first);
		first = first && section.widget->isHidden();
	}
}

void SettingsView::removePage(Page index) {
	const int row = sectionRow(index);
	if (row < 0) {
		return;
	}
	const Section section = m_sections.takeAt(row);
	// The window owns the shader selector, so hand it back instead of deleting it.
	if (section.view == m_shader.data()) {
		section.view->setParent(nullptr);
	} else {
		section.view->setParent(this);
		section.view->hide();
	}
	{
		const QSignalBlocker blocker(m_ui.tabs);
		delete m_ui.tabs->takeItem(row);
	}
	delete section.widget;
	updateDividers();
}

void SettingsView::saveSetting(const char* key, const QAbstractButton* field) {
	m_controller->setOption(key, field->isChecked());
	m_controller->updateOption(key);
}

void SettingsView::saveSetting(const char* key, const QComboBox* field) {
	saveSetting(key, field->lineEdit());
}

void SettingsView::saveSetting(const char* key, const QDoubleSpinBox* field) {
	saveSetting(key, field->value());
}

void SettingsView::saveSetting(const char* key, const QLineEdit* field) {
	saveSetting(key, field->text());
}

void SettingsView::saveSetting(const char* key, const QSlider* field) {
	saveSetting(key, field->value());
}

void SettingsView::saveSetting(const char* key, const QSpinBox* field) {
	saveSetting(key, field->value());
}

void SettingsView::saveSetting(const char* key, const QVariant& field) {
	m_controller->setOption(key, field);
	m_controller->updateOption(key);
}

void SettingsView::loadSetting(const char* key, QAbstractButton* field, bool defaultVal) {
	QString option = loadSetting(key);
	field->setChecked(option.isNull() ? defaultVal : option != "0");
}

void SettingsView::loadSetting(const char* key, QComboBox* field) {
	loadSetting(key, field->lineEdit());
}

void SettingsView::loadSetting(const char* key, QDoubleSpinBox* field) {
	QString option = loadSetting(key);
	field->setValue(option.toDouble());
}

void SettingsView::loadSetting(const char* key, QLineEdit* field) {
	QString option = loadSetting(key);
	field->setText(option);
}

void SettingsView::loadSetting(const char* key, QSlider* field, int defaultVal) {
	QString option = loadSetting(key);
	field->setValue(option.isNull() ? defaultVal : option.toInt());
}

void SettingsView::loadSetting(const char* key, QSpinBox* field, int defaultVal) {
	QString option = loadSetting(key);
	field->setValue(option.isNull() ? defaultVal : option.toInt());
}

QString SettingsView::loadSetting(const char* key) {
	return m_controller->getOption(key);
}
