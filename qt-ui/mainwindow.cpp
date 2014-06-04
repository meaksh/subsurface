/*
 * mainwindow.cpp
 *
 * classes for the main UI window in Subsurface
 */
#include "mainwindow.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QtDebug>
#include <QDateTime>
#include <QSettings>
#include <QCloseEvent>
#include <QApplication>
#include <QFontMetrics>
#include <QTableView>
#include <QDesktopWidget>
#include <QDesktopServices>
#include <QStringList>
#include <QSettings>
#include <QShortcut>
#include <QPrintDialog>
#include <fcntl.h>
#include "divelistview.h"
#include "starwidget.h"

#include "dive.h"
#include "display.h"
#include "divelist.h"
#include "pref.h"
#include "helpers.h"
#include "modeldelegates.h"
#include "models.h"
#include "downloadfromdivecomputer.h"
#include "preferences.h"
#include "subsurfacewebservices.h"
#include "divecomputermanagementdialog.h"
#include "simplewidgets.h"
#include "diveplanner.h"
#include "about.h"
#include "worldmap-save.h"
#include "updatemanager.h"
#ifndef NO_PRINTING
#include "printdialog.h"
#endif
#include "divelogimportdialog.h"
#include "divelogexportdialog.h"
#ifndef NO_USERMANUAL
#include "usermanual.h"
#endif

MainWindow *MainWindow::m_Instance = NULL;

MainWindow::MainWindow() : QMainWindow(),
	actionNextDive(0),
	actionPreviousDive(0),
	helpView(0),
	yearlyStats(0),
	yearlyStatsModel(0),
	state(VIEWALL),
	updateManager(0),
	fakeDiveId(0)
{
	Q_ASSERT_X(m_Instance == NULL, "MainWindow", "MainWindow recreated!");
	m_Instance = this;
	ui.setupUi(this);
	setWindowIcon(QIcon(":subsurface-icon"));
	connect(ui.ListWidget, SIGNAL(currentDiveChanged(int)), this, SLOT(current_dive_changed(int)));
	connect(PreferencesDialog::instance(), SIGNAL(settingsChanged()), this, SLOT(readSettings()));
	connect(PreferencesDialog::instance(), SIGNAL(settingsChanged()), ui.ListWidget, SLOT(update()));
	connect(PreferencesDialog::instance(), SIGNAL(settingsChanged()), ui.ListWidget, SLOT(reloadHeaderActions()));
	connect(PreferencesDialog::instance(), SIGNAL(settingsChanged()), ui.InfoWidget, SLOT(updateDiveInfo()));
	connect(PreferencesDialog::instance(), SIGNAL(settingsChanged()), ui.divePlannerWidget, SLOT(settingsChanged()));
	connect(PreferencesDialog::instance(), SIGNAL(settingsChanged()), TankInfoModel::instance(), SLOT(update()));
	connect(ui.actionRecent1, SIGNAL(triggered(bool)), this, SLOT(recentFileTriggered(bool)));
	connect(ui.actionRecent2, SIGNAL(triggered(bool)), this, SLOT(recentFileTriggered(bool)));
	connect(ui.actionRecent3, SIGNAL(triggered(bool)), this, SLOT(recentFileTriggered(bool)));
	connect(ui.actionRecent4, SIGNAL(triggered(bool)), this, SLOT(recentFileTriggered(bool)));
	connect(information(), SIGNAL(addDiveFinished()), ui.newProfile, SLOT(setProfileState()));
	connect(DivePlannerPointsModel::instance(), SIGNAL(planCreated()), MainWindow::instance(), SLOT(planCreated()));
	connect(DivePlannerPointsModel::instance(), SIGNAL(planCanceled()), MainWindow::instance(), SLOT(planCanceled()));
	ui.mainErrorMessage->hide();
	initialUiSetup();
	readSettings();
	ui.ListWidget->reload(DiveTripModel::TREE);
	ui.ListWidget->reloadHeaderActions();
	ui.ListWidget->setFocus();
	ui.globe->reload();
	ui.ListWidget->expand(ui.ListWidget->model()->index(0, 0));
	ui.ListWidget->scrollTo(ui.ListWidget->model()->index(0, 0), QAbstractItemView::PositionAtCenter);
	ui.divePlannerWidget->settingsChanged();
#ifdef NO_MARBLE
	ui.layoutWidget->hide();
	ui.menuView->removeAction(ui.actionViewGlobe);
#endif
#ifdef NO_USERMANUAL
	ui.menuHelp->removeAction(ui.actionUserManual);
#endif
#ifdef NO_PRINTING
	ui.menuFile->removeAction(ui.actionPrint);
#endif
}

MainWindow::~MainWindow()
{
	m_Instance = NULL;
}

void MainWindow::setLoadedWithFiles(bool f)
{
	filesAsArguments = f;
}

bool MainWindow::filesFromCommandLine() const
{
	return filesAsArguments;
}

MainWindow *MainWindow::instance()
{
	return m_Instance;
}

// this gets called after we download dives from a divecomputer
void MainWindow::refreshDisplay(bool doRecreateDiveList)
{
	showError(get_error_string());
	ui.InfoWidget->reload();
	TankInfoModel::instance()->update();
	ui.globe->reload();
	if (doRecreateDiveList)
		recreateDiveList();
	ui.diveListPane->setCurrentIndex(0); // switch to the dive list
	ui.ListWidget->setEnabled(true);
	ui.ListWidget->setFocus();
	WSInfoModel::instance()->updateInfo();
	// refresh the yearly stats if the window has an instance
	if (yearlyStats) {
		if (yearlyStatsModel)
			delete yearlyStatsModel;
		yearlyStatsModel = new YearlyStatisticsModel();
		yearlyStats->setModel(yearlyStatsModel);
	}
	if (amount_selected == 0)
		cleanUpEmpty();
}

void MainWindow::recreateDiveList()
{
	ui.ListWidget->reload(DiveTripModel::CURRENT);
}

void MainWindow::current_dive_changed(int divenr)
{
	if (divenr >= 0) {
		select_dive(divenr);
		ui.globe->centerOnCurrentDive();
	}

	/* It looks like it's a bit too cumberstone to send *one* dive using a QList,
	 * but this is just futureproofness, it's the best way in the future to show more than
	 * a single profile plot on the canvas. I know that we are using only one right now,
	 * but let's keep like this so it's easy to change when we need? :)
	 */
	ui.newProfile->plotDives(QList<dive *>() << (current_dive));
	ui.InfoWidget->updateDiveInfo(divenr);
}

void MainWindow::on_actionNew_triggered()
{
	on_actionClose_triggered();
}

void MainWindow::on_actionOpen_triggered()
{
	if (DivePlannerPointsModel::instance()->currentMode() != DivePlannerPointsModel::NOTHING ||
	    ui.InfoWidget->isEditing()) {
		QMessageBox::warning(this, tr("Warning"), tr("Please save or cancel the current dive edit before opening a new file."));
		return;
	}
	QString filename = QFileDialog::getOpenFileName(this, tr("Open File"), lastUsedDir(), filter());
	if (filename.isEmpty())
		return;
	updateLastUsedDir(QFileInfo(filename).dir().path());
	on_actionClose_triggered();
	loadFiles(QStringList() << filename);
}

void MainWindow::on_actionSave_triggered()
{
	file_save();
}

void MainWindow::on_actionSaveAs_triggered()
{
	file_save_as();
}

ProfileWidget2 *MainWindow::graphics() const
{
	return ui.newProfile;
}

void MainWindow::cleanUpEmpty()
{
	ui.InfoWidget->clearStats();
	ui.InfoWidget->clearInfo();
	ui.InfoWidget->clearEquipment();
	ui.InfoWidget->updateDiveInfo(-1);
	ui.newProfile->setEmptyState();
	ui.ListWidget->reload(DiveTripModel::TREE);
	ui.globe->reload();
	if (!existing_filename)
		setTitle(MWTF_DEFAULT);
}

void MainWindow::setToolButtonsEnabled(bool enabled)
{
	ui.profPO2->setEnabled(enabled);
	ui.profPn2->setEnabled(enabled);
	ui.profPhe->setEnabled(enabled);
	ui.profDcCeiling->setEnabled(enabled);
	ui.profCalcCeiling->setEnabled(enabled);
	ui.profCalcAllTissues->setEnabled(enabled);
	ui.profIncrement3m->setEnabled(enabled);
	ui.profMod->setEnabled(enabled);
	ui.profEad->setEnabled(enabled);
	ui.profNdl_tts->setEnabled(enabled);
	ui.profSAC->setEnabled(enabled);
	ui.profRuler->setEnabled(enabled);
	ui.profScaled->setEnabled(enabled);
	ui.profHR->setEnabled(enabled);
}

void MainWindow::on_actionClose_triggered()
{
	if (DivePlannerPointsModel::instance()->currentMode() != DivePlannerPointsModel::NOTHING ||
	    ui.InfoWidget->isEditing()) {
		QMessageBox::warning(this, tr("Warning"), tr("Please save or cancel the current dive edit before closing the file."));
		return;
	}
	if (unsaved_changes() && (askSaveChanges() == false))
		return;

	ui.newProfile->setEmptyState();
	/* free the dives and trips */
	clear_git_id();
	while (dive_table.nr)
		delete_single_dive(0);

	ui.ListWidget->clearSelection();
	/* clear the selection and the statistics */
	selected_dive = -1;

	free((void *)existing_filename);
	existing_filename = NULL;

	cleanUpEmpty();
	mark_divelist_changed(false);

	clear_events();
}

QString MainWindow::lastUsedDir()
{
	QSettings settings;
	QString lastDir = QDir::homePath();

	settings.beginGroup("FileDialog");
	if (settings.contains("LastDir"))
		if (QDir::setCurrent(settings.value("LastDir").toString()))
			lastDir = settings.value("LastDir").toString();
	return lastDir;
}

void MainWindow::updateLastUsedDir(const QString &dir)
{
	QSettings s;
	s.beginGroup("FileDialog");
	s.setValue("LastDir", dir);
}

void MainWindow::on_actionPrint_triggered()
{
#ifndef NO_PRINTING
	PrintDialog dlg(this);

	dlg.exec();
#endif
}

void MainWindow::disableDcShortcuts()
{
	ui.actionPreviousDC->setShortcut(QKeySequence());
	ui.actionNextDC->setShortcut(QKeySequence());
}

void MainWindow::enableDcShortcuts()
{
	ui.actionPreviousDC->setShortcut(Qt::Key_Left);
	ui.actionNextDC->setShortcut(Qt::Key_Right);
}

void MainWindow::showProfile()
{
	enableDcShortcuts();
	ui.newProfile->setProfileState();
	ui.infoPane->setCurrentIndex(MAINTAB);
}

void MainWindow::on_actionPreferences_triggered()
{
	PreferencesDialog::instance()->show();
}

void MainWindow::on_actionQuit_triggered()
{
	if (ui.InfoWidget->isEditing()) {
		ui.InfoWidget->rejectChanges();
		if (ui.InfoWidget->isEditing())
			// didn't discard the edits
			return;
	}
	if (DivePlannerPointsModel::instance()->currentMode() != DivePlannerPointsModel::NOTHING) {
		DivePlannerPointsModel::instance()->cancelPlan();
		if (DivePlannerPointsModel::instance()->currentMode() != DivePlannerPointsModel::NOTHING)
			// The planned dive was not discarded
			return;
	}

	if (unsaved_changes() && (askSaveChanges() == false))
		return;
	writeSettings();
	QApplication::quit();
}

void MainWindow::on_actionDownloadDC_triggered()
{
	DownloadFromDCWidget dlg(this);

	dlg.exec();
}

void MainWindow::on_actionDownloadWeb_triggered()
{
	SubsurfaceWebServices dlg(this);

	dlg.exec();
}

void MainWindow::on_actionDivelogs_de_triggered()
{
	DivelogsDeWebServices::instance()->downloadDives();
}

void MainWindow::on_actionEditDeviceNames_triggered()
{
	DiveComputerManagementDialog::instance()->init();
	DiveComputerManagementDialog::instance()->update();
	DiveComputerManagementDialog::instance()->show();
}

bool MainWindow::plannerStateClean()
{
	if (DivePlannerPointsModel::instance()->currentMode() != DivePlannerPointsModel::NOTHING ||
	    ui.InfoWidget->isEditing()) {
		QMessageBox::warning(this, tr("Warning"), tr("Please save or cancel the current dive edit before trying to add a dive."));
		return false;
	}
	return true;
}

void MainWindow::createFakeDiveForAddAndPlan()
{
	// now cheat - create one dive that we use to store the info tab data in
	//TODO: C-function create_temporary_dive ?
	struct dive *dive = alloc_dive();
	fakeDiveId = dive->id;
	dive->when = QDateTime::currentMSecsSinceEpoch() / 1000L + gettimezoneoffset();
	dive->dc.model = "manually added dive"; // don't translate! this is stored in the XML file

	dive->latitude.udeg = 0;
	dive->longitude.udeg = 0;
	record_dive(dive);
	// select this new dive (but remember the old selection
	ui.ListWidget->rememberSelection();
	ui.ListWidget->unselectDives();
	ui.ListWidget->reload(DiveTripModel::CURRENT);
	ui.ListWidget->selectDives(QList<int>() << dive_table.nr - 1);
	ui.InfoWidget->updateDiveInfo(selected_dive);
}

void MainWindow::removeFakeDiveForAddAndPlan()
{
	int idx;

	if (!fakeDiveId ||
	    (idx = get_idx_by_uniq_id(fakeDiveId)) == dive_table.nr)
		return;
	delete_single_dive(idx);
}

void MainWindow::planCanceled()
{
	removeFakeDiveForAddAndPlan();
	showProfile();
	ui.ListWidget->reload(DiveTripModel::CURRENT);
	ui.ListWidget->restoreSelection();
	refreshDisplay();
}

void MainWindow::planCreated()
{
	removeFakeDiveForAddAndPlan();
	showProfile();
	refreshDisplay();
}

void MainWindow::setPlanNotes(const char *notes)
{
	ui.divePlanOutput->setHtml(notes);
}

void MainWindow::printPlan()
{
	QPrinter printer;
	QPrintDialog *dialog = new QPrintDialog(&printer, this);
	dialog->setWindowTitle(tr("Print runtime table"));
	if (dialog->exec() != QDialog::Accepted)
		return;

	ui.divePlanOutput->print(&printer);
}

void MainWindow::on_actionDivePlanner_triggered()
{
	if(!plannerStateClean())
		return;

	// put us in PLAN mode
	DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::PLAN);
	ui.newProfile->setPlanState();
	ui.infoPane->setCurrentIndex(PLANNERWIDGET);

	// set up the staging dive and clean up the widgets
	DivePlannerPointsModel::instance()->clear();

	// setup the staging dive cylinders from the selected dive
	DivePlannerPointsModel::instance()->setupCylinders();

	// create a simple starting dive, using the first gas from the just copied cylidners
	createFakeDiveForAddAndPlan();
	DivePlannerPointsModel::instance()->createSimpleDive();

	// reload and then disable the dive list
	ui.ListWidget->reload(DiveTripModel::CURRENT);
	ui.ListWidget->setEnabled(false);
	ui.diveListPane->setCurrentIndex(1); // switch to the plan output
}

void MainWindow::on_actionAddDive_triggered()
{
	if(!plannerStateClean())
		return;

	DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::ADD);

	createFakeDiveForAddAndPlan();

	ui.InfoWidget->setCurrentIndex(0);
	ui.InfoWidget->addDiveStarted();
	ui.infoPane->setCurrentIndex(MAINTAB);

	ui.newProfile->setAddState();
	DivePlannerPointsModel::instance()->clear();
	DivePlannerPointsModel::instance()->createSimpleDive();
	ui.ListWidget->reload(DiveTripModel::CURRENT);
}

void MainWindow::on_actionRenumber_triggered()
{
	RenumberDialog::instance()->renumberOnlySelected(false);
	RenumberDialog::instance()->show();
}

void MainWindow::on_actionAutoGroup_triggered()
{
	autogroup = ui.actionAutoGroup->isChecked();
	if (autogroup)
		autogroup_dives();
	else
		remove_autogen_trips();
	refreshDisplay();
	mark_divelist_changed(true);
}

void MainWindow::on_actionYearlyStatistics_triggered()
{
	// create the widget only once
	if (!yearlyStats) {
		yearlyStats = new QTreeView();
		yearlyStats->setWindowModality(Qt::NonModal);
		yearlyStats->setMinimumWidth(600);
		yearlyStats->setWindowTitle(tr("Yearly Statistics"));
		yearlyStats->setWindowIcon(QIcon(":subsurface-icon"));
		QShortcut *closeKey = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), yearlyStats, 0, 0, Qt::WidgetShortcut);
		connect(closeKey, SIGNAL(activated()), yearlyStats, SLOT(close()));
		closeKey = new QShortcut(QKeySequence(Qt::Key_Escape), yearlyStats, 0, 0, Qt::WidgetShortcut);
		connect(closeKey, SIGNAL(activated()), yearlyStats, SLOT(close()));
		QShortcut *quitKey = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), yearlyStats, 0, 0, Qt::WidgetShortcut);
		connect(quitKey, SIGNAL(activated()), this, SLOT(close()));
	}
	/* problem here is that without more MainWindow variables or a separate YearlyStatistics
	 * class the user needs to close the window/widget and re-open it for it to update.
	 */
	if (yearlyStatsModel)
		delete yearlyStatsModel;
	yearlyStatsModel = new YearlyStatisticsModel();
	yearlyStats->setModel(yearlyStatsModel);
	yearlyStats->raise();
	yearlyStats->show();
}

#define BEHAVIOR QList<int>()
void MainWindow::on_actionViewList_triggered()
{
	beginChangeState(LIST_MAXIMIZED);
	ui.listGlobeSplitter->setSizes(BEHAVIOR << EXPANDED << COLLAPSED);
	ui.mainSplitter->setSizes(BEHAVIOR << COLLAPSED << EXPANDED);
}

void MainWindow::on_actionViewProfile_triggered()
{
	beginChangeState(PROFILE_MAXIMIZED);
	ui.infoProfileSplitter->setSizes(BEHAVIOR << COLLAPSED << EXPANDED);
	ui.mainSplitter->setSizes(BEHAVIOR << EXPANDED << COLLAPSED);
}

void MainWindow::on_actionViewInfo_triggered()
{
	beginChangeState(INFO_MAXIMIZED);
	ui.infoProfileSplitter->setSizes(BEHAVIOR << EXPANDED << COLLAPSED);
	ui.mainSplitter->setSizes(BEHAVIOR << EXPANDED << COLLAPSED);
}

void MainWindow::on_actionViewGlobe_triggered()
{
	beginChangeState(GLOBE_MAXIMIZED);
	ui.mainSplitter->setSizes(BEHAVIOR << COLLAPSED << EXPANDED);
	ui.listGlobeSplitter->setSizes(BEHAVIOR << COLLAPSED << EXPANDED);
}
#undef BEHAVIOR

void MainWindow::on_actionViewAll_triggered()
{
	beginChangeState(VIEWALL);
	static QList<int> mainSizes;
	const int appH = qApp->desktop()->size().height();
	const int appW = qApp->desktop()->size().width();
	if (mainSizes.empty()) {
		mainSizes.append(appH * 0.7);
		mainSizes.append(appH * 0.3);
	}
	static QList<int> infoProfileSizes;
	if (infoProfileSizes.empty()) {
		infoProfileSizes.append(appW * 0.3);
		infoProfileSizes.append(appW * 0.7);
	}

	static QList<int> listGlobeSizes;
	if (listGlobeSizes.empty()) {
		listGlobeSizes.append(appW * 0.7);
		listGlobeSizes.append(appW * 0.3);
	}

	QSettings settings;
	settings.beginGroup("MainWindow");
	if (settings.value("mainSplitter").isValid()) {
		ui.mainSplitter->restoreState(settings.value("mainSplitter").toByteArray());
		ui.infoProfileSplitter->restoreState(settings.value("infoProfileSplitter").toByteArray());
		ui.listGlobeSplitter->restoreState(settings.value("listGlobeSplitter").toByteArray());
		if (ui.mainSplitter->sizes().first() == 0 || ui.mainSplitter->sizes().last() == 0)
			ui.mainSplitter->setSizes(mainSizes);
		if (ui.infoProfileSplitter->sizes().first() == 0 || ui.infoProfileSplitter->sizes().last() == 0)
			ui.infoProfileSplitter->setSizes(infoProfileSizes);
		if (ui.listGlobeSplitter->sizes().first() == 0 || ui.listGlobeSplitter->sizes().last() == 0)
			ui.listGlobeSplitter->setSizes(listGlobeSizes);

	} else {
		ui.mainSplitter->setSizes(mainSizes);
		ui.infoProfileSplitter->setSizes(infoProfileSizes);
		ui.listGlobeSplitter->setSizes(listGlobeSizes);
	}
}

void MainWindow::beginChangeState(CurrentState s)
{
	if (state == VIEWALL && state != s) {
		saveSplitterSizes();
	}
	state = s;
}

void MainWindow::saveSplitterSizes()
{
	QSettings settings;
	settings.beginGroup("MainWindow");
	settings.setValue("mainSplitter", ui.mainSplitter->saveState());
	settings.setValue("infoProfileSplitter", ui.infoProfileSplitter->saveState());
	settings.setValue("listGlobeSplitter", ui.listGlobeSplitter->saveState());
}

void MainWindow::on_actionPreviousDC_triggered()
{
	unsigned nrdc = number_of_computers(current_dive);
	dc_number = (dc_number + nrdc - 1) % nrdc;
	ui.InfoWidget->updateDiveInfo(selected_dive);
	ui.newProfile->plotDives(QList<struct dive *>() << (current_dive));
}

void MainWindow::on_actionNextDC_triggered()
{
	unsigned nrdc = number_of_computers(current_dive);
	dc_number = (dc_number + 1) % nrdc;
	ui.InfoWidget->updateDiveInfo(selected_dive);
	ui.newProfile->plotDives(QList<struct dive *>() << (current_dive));
}

void MainWindow::on_actionFullScreen_triggered(bool checked)
{
	if (checked) {
		setWindowState(windowState() | Qt::WindowFullScreen);
	} else {
		setWindowState(windowState() & ~Qt::WindowFullScreen);
	}
}

void MainWindow::on_actionSelectEvents_triggered()
{
	qDebug("actionSelectEvents");
}

void MainWindow::on_actionInputPlan_triggered()
{
	qDebug("actionInputPlan");
}

void MainWindow::on_actionAboutSubsurface_triggered()
{
	SubsurfaceAbout dlg(this);

	dlg.exec();
}

void MainWindow::on_action_Check_for_Updates_triggered()
{
	if (!updateManager)
		updateManager = new UpdateManager(this);

	updateManager->checkForUpdates();
}

void MainWindow::on_actionUserManual_triggered()
{
#ifndef NO_USERMANUAL
	if (!helpView) {
		helpView = new UserManual(this);
	}
	helpView->show();
#endif
}

QString MainWindow::filter()
{
	QString f;
	f += "ALL ( *.ssrf *.xml *.XML *.uddf *.udcf *.UDFC *.jlb *.JLB ";
	f += "*.sde *.SDE *.dld *.DLD ";
	f += "*.db";
	f += ");;";

	f += "Subsurface (*.ssrf);;";
	f += "XML (*.xml *.XML);;";
	f += "UDDF (*.uddf);;";
	f += "UDCF (*.udcf *.UDCF);;";
	f += "JLB  (*.jlb *.JLB);;";

	f += "SDE (*.sde *.SDE);;";
	f += "DLD (*.dld *.DLD);;";
	f += "DB (*.db)";

	return f;
}

bool MainWindow::askSaveChanges()
{
	QString message;
	QMessageBox response(MainWindow::instance());

	if (existing_filename)
		message = tr("Do you want to save the changes you made in the file %1?").arg(existing_filename);
	else
		message = tr("Do you want to save the changes you made in the datafile?");

	response.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
	response.setDefaultButton(QMessageBox::Save);
	response.setText(message);
	response.setWindowTitle(tr("Save Changes?")); // Not displayed on MacOSX as described in Qt API
	response.setInformativeText(tr("Changes will be lost if you don't save them."));
	response.setIcon(QMessageBox::Warning);
	response.setWindowModality(Qt::WindowModal);
	int ret = response.exec();

	switch (ret) {
	case QMessageBox::Save:
		file_save();
		return true;
	case QMessageBox::Discard:
		return true;
	}
	return false;
}

void MainWindow::initialUiSetup()
{
	QSettings settings;
	settings.beginGroup("MainWindow");
	QSize sz = settings.value("size", qApp->desktop()->size()).value<QSize>();
	if (settings.value("maximized", isMaximized()).value<bool>())
		showMaximized();
	else
		resize(sz);

	state = (CurrentState)settings.value("lastState", 0).toInt();
	switch (state) {
	case VIEWALL:
		on_actionViewAll_triggered();
		break;
	case GLOBE_MAXIMIZED:
		on_actionViewGlobe_triggered();
		break;
	case INFO_MAXIMIZED:
		on_actionViewInfo_triggered();
		break;
	case LIST_MAXIMIZED:
		on_actionViewList_triggered();
		break;
	case PROFILE_MAXIMIZED:
		on_actionViewProfile_triggered();
		break;
	}
	settings.endGroup();
}

#define TOOLBOX_PREF_BUTTON(pref, setting, button) \
	prefs.pref = s.value(#setting).toBool();   \
	ui.button->setChecked(prefs.pref);

void MainWindow::readSettings()
{
	QSettings s;
	s.beginGroup("Display");
	QFont defaultFont = QFont(default_prefs.divelist_font);
	defaultFont = s.value("divelist_font", defaultFont).value<QFont>();
	defaultFont.setPointSizeF(s.value("font_size", default_prefs.font_size).toFloat());
	qApp->setFont(defaultFont);
	s.endGroup();

	s.beginGroup("TecDetails");
	TOOLBOX_PREF_BUTTON(calcalltissues, calcalltissues, profCalcAllTissues);
	TOOLBOX_PREF_BUTTON(calcceiling, calcceiling, profCalcCeiling);
	TOOLBOX_PREF_BUTTON(dcceiling, dcceiling, profDcCeiling);
	TOOLBOX_PREF_BUTTON(ead, ead, profEad);
	TOOLBOX_PREF_BUTTON(calcceiling3m, calcceiling3m, profIncrement3m);
	TOOLBOX_PREF_BUTTON(mod, mod, profMod);
	TOOLBOX_PREF_BUTTON(calcndltts, calcndltts, profNdl_tts);
	TOOLBOX_PREF_BUTTON(pp_graphs.phe, phegraph, profPhe);
	TOOLBOX_PREF_BUTTON(pp_graphs.pn2, pn2graph, profPn2);
	TOOLBOX_PREF_BUTTON(pp_graphs.po2, po2graph, profPO2);
	TOOLBOX_PREF_BUTTON(hrgraph, hrgraph, profHR);
	TOOLBOX_PREF_BUTTON(rulergraph, rulergraph, profRuler);
	TOOLBOX_PREF_BUTTON(show_sac, show_sac, profSAC);
}

#undef TOOLBOX_PREF_BUTTON

void MainWindow::writeSettings()
{
	QSettings settings;

	settings.beginGroup("MainWindow");
	settings.setValue("lastState", (int)state);
	settings.setValue("maximized", isMaximized());
	if (!isMaximized())
		settings.setValue("size", size());
	if (state == VIEWALL)
		saveSplitterSizes();
	settings.endGroup();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (DivePlannerPointsModel::instance()->currentMode() != DivePlannerPointsModel::NOTHING ||
	    ui.InfoWidget->isEditing()) {
		on_actionQuit_triggered();
		event->ignore();
		return;
	}

	if (helpView && helpView->isVisible()) {
		helpView->close();
		helpView->deleteLater();
	}

	if (yearlyStats && yearlyStats->isVisible()) {
		yearlyStats->close();
		yearlyStats->deleteLater();
		yearlyStatsModel->deleteLater();
	}

	if (unsaved_changes() && (askSaveChanges() == false)) {
		event->ignore();
		return;
	}
	event->accept();
	writeSettings();
	QApplication::closeAllWindows();
}

DiveListView *MainWindow::dive_list()
{
	return ui.ListWidget;
}

GlobeGPS *MainWindow::globe()
{
	return ui.globe;
}

MainTab *MainWindow::information()
{
	return ui.InfoWidget;
}

void MainWindow::loadRecentFiles(QSettings *s)
{
	QStringList files;
	bool modified = false;

	s->beginGroup("Recent_Files");
	for (int c = 1; c <= 4; c++) {
		QString key = QString("File_%1").arg(c);
		if (s->contains(key)) {
			QString file = s->value(key).toString();

			if (QFile::exists(file)) {
				files.append(file);
			} else {
				modified = true;
			}
		} else {
			break;
		}
	}

	if (modified) {
		for (int c = 0; c < 4; c++) {
			QString key = QString("File_%1").arg(c + 1);

			if (files.count() > c) {
				s->setValue(key, files.at(c));
			} else {
				if (s->contains(key)) {
					s->remove(key);
				}
			}
		}

		s->sync();
	}
	s->endGroup();

	for (int c = 0; c < 4; c++) {
		QAction *action = this->findChild<QAction *>(QString("actionRecent%1").arg(c + 1));

		if (files.count() > c) {
			QFileInfo fi(files.at(c));
			action->setText(fi.fileName());
			action->setToolTip(fi.absoluteFilePath());
			action->setVisible(true);
		} else {
			action->setVisible(false);
		}
	}
}

void MainWindow::addRecentFile(const QStringList &newFiles)
{
	QStringList files;
	QSettings s;

	if (newFiles.isEmpty())
		return;

	s.beginGroup("Recent_Files");

	for (int c = 1; c <= 4; c++) {
		QString key = QString("File_%1").arg(c);
		if (s.contains(key)) {
			QString file = s.value(key).toString();

			files.append(file);
		} else {
			break;
		}
	}

	foreach (const QString &file, newFiles) {
		int index = files.indexOf(file);

		if (index >= 0) {
			files.removeAt(index);
		}
	}

	foreach (const QString &file, newFiles) {
		if (QFile::exists(file)) {
			files.prepend(file);
		}
	}

	while (files.count() > 4) {
		files.removeLast();
	}

	for (int c = 1; c <= 4; c++) {
		QString key = QString("File_%1").arg(c);

		if (files.count() >= c) {
			s.setValue(key, files.at(c - 1));
		} else {
			if (s.contains(key)) {
				s.remove(key);
			}
		}
	}
	s.endGroup();
	s.sync();

	loadRecentFiles(&s);
}

void MainWindow::removeRecentFile(QStringList failedFiles)
{
	QStringList files;
	QSettings s;

	if (failedFiles.isEmpty())
		return;

	s.beginGroup("Recent_Files");

	for (int c = 1; c <= 4; c++) {
		QString key = QString("File_%1").arg(c);

		if (s.contains(key)) {
			QString file = s.value(key).toString();
			files.append(file);
		} else {
			break;
		}
	}

	foreach (QString file, failedFiles)
		files.removeAll(file);

	for (int c = 1; c <= 4; c++) {
		QString key = QString("File_%1").arg(c);

		if (files.count() >= c) {
			s.setValue(key, files.at(c - 1));
		} else {
			if (s.contains(key))
				s.remove(key);
		}
	}

	s.endGroup();
	s.sync();

	loadRecentFiles(&s);
}

void MainWindow::recentFileTriggered(bool checked)
{
	Q_UNUSED(checked);

	QAction *actionRecent = (QAction *)sender();

	const QString &filename = actionRecent->toolTip();

	updateLastUsedDir(QFileInfo(filename).dir().path());
	on_actionClose_triggered();
	loadFiles(QStringList() << filename);
}

int MainWindow::file_save_as(void)
{
	QString filename;
	const char *default_filename = existing_filename;
	filename = QFileDialog::getSaveFileName(this, tr("Save File as"), default_filename,
						tr("Subsurface XML files (*.ssrf *.xml *.XML)"));
	if (filename.isNull() || filename.isEmpty())
		return report_error("No filename to save into");

	if (ui.InfoWidget->isEditing())
		ui.InfoWidget->acceptChanges();

	if (save_dives(filename.toUtf8().data())) {
		showError(get_error_string());
		return -1;
	}

	showError(get_error_string());
	set_filename(filename.toUtf8().data(), true);
	setTitle(MWTF_FILENAME);
	mark_divelist_changed(false);
	addRecentFile(QStringList() << filename);
	return 0;
}

int MainWindow::file_save(void)
{
	const char *current_default;

	if (!existing_filename)
		return file_save_as();

	if (ui.InfoWidget->isEditing())
		ui.InfoWidget->acceptChanges();

	current_default = prefs.default_filename;
	if (strcmp(existing_filename, current_default) == 0) {
		/* if we are using the default filename the directory
		 * that we are creating the file in may not exist */
		QDir current_def_dir = QFileInfo(current_default).absoluteDir();
		if (!current_def_dir.exists())
			current_def_dir.mkpath(current_def_dir.absolutePath());
	}
	if (save_dives(existing_filename)) {
		showError(get_error_string());
		return -1;
	}
	showError(get_error_string());
	mark_divelist_changed(false);
	addRecentFile(QStringList() << QString(existing_filename));
	return 0;
}

void MainWindow::showError(QString message)
{
	if (message.isEmpty())
		return;
	ui.mainErrorMessage->setText(message);
	ui.mainErrorMessage->setCloseButtonVisible(true);
	ui.mainErrorMessage->setMessageType(KMessageWidget::Error);
	ui.mainErrorMessage->animatedShow();
}

void MainWindow::setTitle(enum MainWindowTitleFormat format)
{
	switch (format) {
	case MWTF_DEFAULT:
		setWindowTitle("Subsurface");
		break;
	case MWTF_FILENAME:
		if (!existing_filename) {
			setTitle(MWTF_DEFAULT);
			return;
		}
		QFile f(existing_filename);
		QFileInfo fileInfo(f);
		QString fileName(fileInfo.fileName());
		setWindowTitle("Subsurface: " + fileName);
		break;
	}
}

void MainWindow::importFiles(const QStringList fileNames)
{
	if (fileNames.isEmpty())
		return;

	QByteArray fileNamePtr;

	for (int i = 0; i < fileNames.size(); ++i) {
		fileNamePtr = QFile::encodeName(fileNames.at(i));
		parse_file(fileNamePtr.data());
	}
	process_dives(true, false);
	refreshDisplay();
}

void MainWindow::loadFiles(const QStringList fileNames)
{
	if (fileNames.isEmpty())
		return;

	QByteArray fileNamePtr;
	QStringList failedParses;

	for (int i = 0; i < fileNames.size(); ++i) {
		int error;

		fileNamePtr = QFile::encodeName(fileNames.at(i));
		error = parse_file(fileNamePtr.data());
		if (!error) {
			set_filename(fileNamePtr.data(), true);
			setTitle(MWTF_FILENAME);
		} else {
			failedParses.append(fileNames.at(i));
		}
	}

	process_dives(false, false);
	addRecentFile(fileNames);
	removeRecentFile(failedParses);

	refreshDisplay();
	ui.actionAutoGroup->setChecked(autogroup);
}

void MainWindow::on_actionImportDiveLog_triggered()
{
	QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Open Dive Log File"), lastUsedDir(), tr("Dive Log Files (*.xml *.uddf *.udcf *.csv *.jlb *.dld *.sde *.db);;XML Files (*.xml);;UDDF/UDCF Files(*.uddf *.udcf);;JDiveLog Files(*.jlb);;Suunto Files(*.sde *.db);;CSV Files(*.csv);;All Files(*)"));

	if (fileNames.isEmpty())
		return;
	updateLastUsedDir(QFileInfo(fileNames[0]).dir().path());

	QStringList logFiles = fileNames.filter(QRegExp("^.*\\.(?!csv)", Qt::CaseInsensitive));
	QStringList csvFiles = fileNames.filter(".csv", Qt::CaseInsensitive);
	if (logFiles.size()) {
		importFiles(logFiles);
	}

	if (csvFiles.size()) {
		DiveLogImportDialog *diveLogImport = new DiveLogImportDialog(&csvFiles, this);
		diveLogImport->show();
		process_dives(true, false);
		refreshDisplay();
	}
}

void MainWindow::editCurrentDive()
{
	if (information()->isEditing() || DivePlannerPointsModel::instance()->currentMode() != DivePlannerPointsModel::NOTHING) {
		QMessageBox::warning(this, tr("Warning"), tr("Please, first finish the current edition before trying to do another."));
		return;
	}

	struct dive *d = current_dive;
	QString defaultDC(d->dc.model);
	DivePlannerPointsModel::instance()->clear();
	if (defaultDC == "manually added dive") {
		disableDcShortcuts();
		DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::ADD);
		//TODO: I BROKE THIS BY COMMENTING THE LINE BELOW
		// and I'm sleepy now, so I think I should not try to fix right away.
		// we don't setCurrentIndex anymore, we ->setPlanState() or ->setAddState() on the ProfileView.
		//ui.stackedWidget->setCurrentIndex(PLANNERPROFILE); // Planner.
		ui.infoPane->setCurrentIndex(MAINTAB);
		DivePlannerPointsModel::instance()->loadFromDive(d);
		ui.InfoWidget->enableEdition(MainTab::MANUALLY_ADDED_DIVE);
	} else if (defaultDC == "planned dive") {
		disableDcShortcuts();
		DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::PLAN);
		//TODO: I BROKE THIS BY COMMENTING THE LINE BELOW
		// and I'm sleepy now, so I think I should not try to fix right away.
		// we don't setCurrentIndex anymore, we ->setPlanState() or ->setAddState() on the ProfileView.
		//ui.stackedWidget->setCurrentIndex(PLANNERPROFILE); // Planner.
		ui.infoPane->setCurrentIndex(PLANNERWIDGET);
		DivePlannerPointsModel::instance()->loadFromDive(d);
		ui.InfoWidget->enableEdition(MainTab::MANUALLY_ADDED_DIVE);
	}
}

#define TOOLBOX_PREF_PROFILE(PREFS)    \
	QSettings s;                   \
	s.beginGroup("TecDetails");    \
	s.setValue(#PREFS, triggered); \
	PreferencesDialog::instance()->emitSettingsChanged();

void MainWindow::on_profCalcAllTissues_clicked(bool triggered)
{
	prefs.calcalltissues = triggered;
	TOOLBOX_PREF_PROFILE(calcalltissues);
}
void MainWindow::on_profCalcCeiling_clicked(bool triggered)
{
	prefs.calcceiling = triggered;
	TOOLBOX_PREF_PROFILE(calcceiling);
}
void MainWindow::on_profDcCeiling_clicked(bool triggered)
{
	prefs.dcceiling = triggered;
	TOOLBOX_PREF_PROFILE(dcceiling);
}
void MainWindow::on_profEad_clicked(bool triggered)
{
	prefs.ead = triggered;
	TOOLBOX_PREF_PROFILE(ead);
}
void MainWindow::on_profIncrement3m_clicked(bool triggered)
{
	prefs.calcceiling3m = triggered;
	TOOLBOX_PREF_PROFILE(calcceiling3m);
}
void MainWindow::on_profMod_clicked(bool triggered)
{
	prefs.mod = triggered;
	TOOLBOX_PREF_PROFILE(mod);
}
void MainWindow::on_profNdl_tts_clicked(bool triggered)
{
	prefs.calcndltts = triggered;
	TOOLBOX_PREF_PROFILE(calcndltts);
}
void MainWindow::on_profPhe_clicked(bool triggered)
{
	prefs.pp_graphs.phe = triggered;
	TOOLBOX_PREF_PROFILE(phegraph);
}
void MainWindow::on_profPn2_clicked(bool triggered)
{
	prefs.pp_graphs.pn2 = triggered;
	TOOLBOX_PREF_PROFILE(pn2graph);
}
void MainWindow::on_profPO2_clicked(bool triggered)
{
	prefs.pp_graphs.po2 = triggered;
	TOOLBOX_PREF_PROFILE(po2graph);
}
void MainWindow::on_profHR_clicked(bool triggered)
{
	prefs.hrgraph = triggered;
	TOOLBOX_PREF_PROFILE(hrgraph);
}
void MainWindow::on_profRuler_clicked(bool triggered)
{
	prefs.rulergraph = triggered;
	TOOLBOX_PREF_PROFILE(rulergraph);
}
void MainWindow::on_profSAC_clicked(bool triggered)
{
	prefs.show_sac = triggered;
	TOOLBOX_PREF_PROFILE(show_sac);
}

void MainWindow::on_profScaled_clicked(bool triggered)
{
	prefs.zoomed_plot = triggered;
	TOOLBOX_PREF_PROFILE(zoomed_plot);
}

#undef TOOLBOX_PREF_PROFILE

void MainWindow::on_actionExport_triggered()
{
	DiveLogExportDialog *diveLogExport = new DiveLogExportDialog(this);
	diveLogExport->show();
}
