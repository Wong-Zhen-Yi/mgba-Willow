/* Copyright (c) 2013-2014 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QTimer>

#include "ColorPicker.h"
#include "LogConfigModel.h"
#include "ShaderSelector.h"

#include <mgba/core/core.h>

#ifdef M_CORE_GB
#include <mgba/gb/interface.h>
#endif

#include "ui_SettingsView.h"

class QPropertyAnimation;
class QScrollArea;
class QVBoxLayout;

namespace QGBA {

class ConfigController;
class InputController;
class ShortcutController;

class SettingsView : public QDialog {
Q_OBJECT

public:
	enum class Page {
		AV,
		INTERFACE,
		GAMEPLAY,
		UPDATE,
		EMULATION,
		ENHANCEMENTS,
		BIOS,
		PATHS,
		LOGGING,
		GB,
		KEYBOARD,
		CONTROLLERS,
		SHORTCUTS,
		SHADERS,
		WILLOW,
	};

	SettingsView(ConfigController* controller, InputController* inputController, ShortcutController* shortcutController, LogController* logController, QWidget* parent = nullptr);
	~SettingsView();

signals:
	void biosLoaded(int platform, const QString&);
	void audioDriverChanged();
	void displayDriverChanged();
	void cameraDriverChanged();
	void cameraChanged(const QByteArray&);
	void videoRendererChanged();
	void pathsChanged();
	void languageChanged();
	void libraryCleared();
	void saveSettingsRequested();
	void openAutorunScripts();
	void romFolderChanged(const QString& folder);
	void mobilePairingRequested();

public slots:
	void selectPage(Page);
	void setShaderSelector(ShaderSelector* shaderSelector);
	void setMobileStatus(const QString& status);

private slots:
	void selectBios(QLineEdit*);
	void selectPath(QLineEdit*, QCheckBox*);
	void selectImage(QLineEdit*);
	void updateConfig();
	void reloadConfig();
	void updateChecked();

protected:
	bool eventFilter(QObject*, QEvent*) override;

private:
	// Every page is a section of one scrolling page; the sidebar jumps between them.
	struct Section {
		Page page;
		QWidget* widget;
		QWidget* view;
		QWidget* divider;
	};

	Ui::SettingsView m_ui;

	ConfigController* m_controller;
	InputController* m_input;
	QPointer<ShaderSelector> m_shader;
	QLabel* m_dummyShader;
	LogConfigModel m_logModel;
	QTimer m_checkTimer;

#ifdef M_CORE_GB
	uint32_t m_gbColors[12]{};
	ColorPicker m_colorPickers[12];
#endif

	QList<Section> m_sections;
	QScrollArea* m_scroll;
	QWidget* m_content;
	QVBoxLayout* m_sectionLayout;
	QLineEdit* m_search;
	QLabel* m_noResults;
	QPropertyAnimation* m_scrollAnimation;
	bool m_navigating = false;

	QLineEdit* m_romFolder;
	QComboBox* m_autoPlayModel;
	QLabel* m_mobileStatus = nullptr;
	QCheckBox* m_startAtSignIn = nullptr;
	QString m_loadedRomFolder;
	bool m_loadedStartAtSignIn = false;

	QString makePortablePath(const QString& path);

	void setupSinglePage();
	QWidget* createWillowPage();
	void addPage(const QString& name, QWidget* view, Page index);
	void removePage(Page index);
	int sectionRow(Page index) const;
	void scrollToSection(int row, bool animate);
	void syncSectionToScroll();
	void filterSections(const QString& query);
	void updateDividers();
	void applyStartAtSignIn();

	void saveSetting(const char* key, const QAbstractButton*);
	void saveSetting(const char* key, const QComboBox*);
	void saveSetting(const char* key, const QDoubleSpinBox*);
	void saveSetting(const char* key, const QLineEdit*);
	void saveSetting(const char* key, const QSlider*);
	void saveSetting(const char* key, const QSpinBox*);
	void saveSetting(const char* key, const QVariant&);

	void loadSetting(const char* key, QAbstractButton*, bool defaultVal = false);
	void loadSetting(const char* key, QComboBox*);
	void loadSetting(const char* key, QDoubleSpinBox*);
	void loadSetting(const char* key, QLineEdit*);
	void loadSetting(const char* key, QSlider*, int defaultVal = 0);
	void loadSetting(const char* key, QSpinBox*, int defaultVal = 0);
	QString loadSetting(const char* key);
};

}
