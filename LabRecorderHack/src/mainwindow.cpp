#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>

#include <string>
#include <vector>

// recording class
#include "recording.h"

MainWindow::MainWindow(QWidget *parent, const char *config_file)
	: QMainWindow(parent), ui(new Ui::MainWindow) {

	ui->setupUi(this);
	connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);
	connect(ui->actionLoadConfig, &QAction::triggered, [this]() {
		this->load_config(QFileDialog::getOpenFileName(
			this, "Load Configuration File", "", "Configuration Files (*.cfg)"));
	});
	connect(ui->actionSaveConfig, &QAction::triggered, [this]() {
		this->save_config(QFileDialog::getSaveFileName(
			this, "Save Configuration File", "", "Configuration Files (*.cfg)"));
	});

	// Signals for stream finding/selecting/starting/stopping
	connect(ui->refreshButton, &QPushButton::clicked, this, &MainWindow::refreshStreams);
	connect(ui->selectAllButton, &QPushButton::clicked, this, &MainWindow::selectAllStreams);
	connect(ui->selectNoneButton, &QPushButton::clicked, this, &MainWindow::selectNoStreams);
	connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::startRecording);
	connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::stopRecording);
	connect(ui->actionAbout, &QAction::triggered, [this]() {
		QString infostr = QStringLiteral("LSL library version: ") +
						  QString::number(lsl::library_version()) +
						  "\nLSL library info:" + lsl::lsl_library_info();
		QMessageBox::about(this, "About LabRecorder", infostr);
	});

	// Wheenver lineEdit_template is changed, print the final result.
	connect(
		ui->lineEdit_template, &QLineEdit::textChanged, this, &MainWindow::printReplacedFilename);
	auto spinchanged = static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged);
	connect(ui->spin_counter, spinchanged, this, &MainWindow::printReplacedFilename);

	// Signals for builder-related edits -> buildFilename
	connect(ui->rootBrowseButton, &QPushButton::clicked, [this]() {
		this->ui->rootEdit->setText(
			QDir::toNativeSeparators(
				QFileDialog::getExistingDirectory(this, "Study root folder...")));
		this->buildFilename();
	});
	connect(ui->rootEdit, &QLineEdit::editingFinished, this, &MainWindow::buildFilename);
	connect(ui->lineEdit_participant, &QLineEdit::editingFinished, this, &MainWindow::buildFilename);
	connect(ui->lineEdit_session, &QLineEdit::editingFinished, this, &MainWindow::buildFilename);
	connect(ui->lineEdit_acq, &QLineEdit::editingFinished, this, &MainWindow::buildFilename);
	connect(ui->input_blocktask, &QComboBox::currentTextChanged, this, &MainWindow::buildFilename);
	connect(ui->set_time, &QPushButton::clicked, [this](){
		QString timeFormat = "yyyy_MM_dd_HH_mm_ss";
		QDateTime dateTime = QDateTime::currentDateTime();
		QString timeString = dateTime.toString(timeFormat);
		ui->lineEdit_session->setText(timeString);
		this->buildFilename();
	});
	connect(ui->auto_time, &QCheckBox::toggled, [this](bool checked){
		doAutoTime = checked;
	});
	connect(ui->check_bids, &QCheckBox::toggled, [this](bool checked) {
		ui->lineEdit_participant->setText("SMARTASS");
		
		auto &box = *ui->lineEdit_template;
		box.setReadOnly(checked);
		if (checked) {
			legacyTemplate = box.text();
			box.setText(
				QDir::toNativeSeparators(
					QStringLiteral("sub-%p/ses-%s/eeg/sub-%p_ses-%s_task-%b[_acq-%a]_run-%r_eeg.xdf")));
			ui->label_counter->setText("Run (%r)");
		} else {
			box.setText(QDir::toNativeSeparators(legacyTemplate));
			ui->label_counter->setText("Exp num (%n)");
		}
	});

	QString cfgfilepath = find_config_file(config_file);
	load_config(cfgfilepath);

	timer = std::make_unique<QTimer>(this);
	connect(&*timer, &QTimer::timeout, this, &MainWindow::statusUpdate);
	timer->start(1000);
	// startTime = (int)lsl::local_clock();
}

MainWindow::~MainWindow() noexcept = default;

void MainWindow::statusUpdate() const {
	if (currentRecording) {
		auto elapsed = static_cast<unsigned int>(lsl::local_clock() - startTime);
		QString recFilename = replaceFilename(QDir::cleanPath(ui->lineEdit_template->text()));
		auto fileinfo = QFileInfo(QDir::cleanPath(ui->rootEdit->text()) + '/' + recFilename);
		fileinfo.refresh();
		auto size = fileinfo.size();
		QString timeString = QStringLiteral("Recording to %1 (%2; %3kb)")
				.arg(QDir::toNativeSeparators(recFilename),
					QDateTime::fromTime_t(elapsed).toUTC().toString("hh:mm:ss"),
					QString::number(size / 1000));
		statusBar()->showMessage(timeString);
	}
}

void MainWindow::closeEvent(QCloseEvent *ev) {
	if (currentRecording) ev->ignore();
}

void MainWindow::blockSelected(const QString &block) {
	if (currentRecording)
		QMessageBox::information(this, "Still recording",
			"Please stop recording before switching blocks.", QMessageBox::Ok);
	else {
		printReplacedFilename();
		// scripted action code here...
	}
}

void MainWindow::load_config(QString filename) {
	qInfo() << "loading config file " << QDir::toNativeSeparators(filename);
	try {
		// if (!QFileInfo::exists(filename)) throw std::runtime_error("Settings file doesn't
		// exist.");
		QSettings pt(QDir::cleanPath(filename), QSettings::Format::IniFormat);

		// ----------------------------
		// required streams
		// ----------------------------
		requiredStreams = pt.value("RequiredStreams").toStringList();

		// ----------------------------
		// online sync streams
		// ----------------------------
		QStringList onlineSyncStreams = pt.value("OnlineSync", QStringList()).toStringList();
		for (QString &oss : onlineSyncStreams) {
			QStringList words = oss.split(' ', QString::SkipEmptyParts);
			// The first two words ("StreamName (PC)") are the stream identifier
			if (words.length() < 2) {
				qInfo() << "Invalid sync stream config: " << oss;
				continue;
			}
			QString key = words.takeFirst() + ' ' + words.takeFirst();

			int val = 0;
			for (const auto &word : words) {
				if (word == "post_clocksync") { val |= lsl::post_clocksync; }
				if (word == "post_dejitter") { val |= lsl::post_dejitter; }
				if (word == "post_monotonize") { val |= lsl::post_monotonize; }
				if (word == "post_threadsafe") { val |= lsl::post_threadsafe; }
				if (word == "post_ALL") { val = lsl::post_ALL; }
			}
			syncOptionsByStreamName[key.toStdString()] = val;
			qInfo() << "stream sync options: " << key << ": " << val;
		}

		// ----------------------------
		// Block/Task Names
		// ----------------------------
		QStringList taskNames;
		if (pt.contains("SessionBlocks")) { taskNames = pt.value("SessionBlocks").toStringList(); }
		ui->input_blocktask->clear();
		ui->input_blocktask->insertItems(0, taskNames);

		// StorageLocation
		QString studyRoot;
		legacyTemplate.clear();
		// StudyRoot
		if (pt.contains("StudyRoot")) { studyRoot = pt.value("StudyRoot").toString(); }

		if (pt.contains("StorageLocation")) {
			if (!studyRoot.isEmpty())
				throw std::runtime_error("Both StudyRoot and StorageLocation specified");
			if (pt.contains("PathTemplate"))
				throw std::runtime_error("Both StorageLocation and PathTemplate specified");

			QString str_path = pt.value("StorageLocation").toString();
			QString path_root;
			auto index = str_path.indexOf('%');
			if (index != -1) {
				// When a % is encountered, the studyroot gets set to the
				// longest path before the placeholder, e.g.
				// foo/bar/baz%a/untitled.xdf gets split into
				// foo/bar and baz%a/untitled.xdf
				path_root = str_path.left(index);
			} else
				// Otherwise, it's split into folder and constant filename
				path_root = str_path;
			path_root = QFileInfo(path_root).path();
			legacyTemplate = str_path.remove(0, path_root.length() + 1);
			// absolute path, nothing to be done
			studyRoot = QFileInfo(path_root).absolutePath();
			ui->lineEdit_template->setText(QDir::toNativeSeparators(legacyTemplate));
		}
		if (pt.contains("PathTemplate")) legacyTemplate = pt.value("PathTemplate").toString();

		QDir curr_dir = QDir::current();
		curr_dir.cdUp();
		if (studyRoot.isEmpty())
			studyRoot = curr_dir.path() +
						QDir::separator() + "Data";
		ui->rootEdit->setText(QDir::toNativeSeparators(studyRoot));

//		if (legacyTemplate.isEmpty()) {
//			ui->check_bids->setChecked(true);
//			// Legacy takes the form path/to/study/exp%n/%b.xdf
//			legacyTemplate = "exp%n/block_%b.xdf";
//		} else {
//			ui->check_bids->setChecked(false);
//			ui->lineEdit_template->setText(QDir::toNativeSeparators(legacyTemplate));
//		}
		
		ui->check_bids->setChecked(false);
		legacyTemplate = "session_%s.xdf";
		ui->lineEdit_template->setText(QDir::toNativeSeparators(legacyTemplate));
		ui->lineEdit_session->setText("Session Name");

		buildFilename();

		// Check the wild-card-replaced filename to see if it exists already.
		// If it does then increment the exp number.
		// We only do this on settings-load because manual spin changes might indicate purposeful overwriting.
		QString recFilename = QDir::cleanPath(ui->lineEdit_template->text());
		// Spin Number
		if (recFilename.contains(counterPlaceholder())) {
			for (int i = 1; i < 1001; i++) {
				ui->spin_counter->setValue(i);
				if (!QFileInfo::exists(replaceFilename(recFilename))) break;
			}
		}

	} catch (std::exception &e) { qWarning() << "Problem parsing config file: " << e.what(); }
	// std::cout << "refreshing streams ..." <<std::endl;
	refreshStreams();
}

void MainWindow::save_config(QString filename) {
	QSettings settings(filename, QSettings::Format::IniFormat);
	settings.setValue("StudyRoot", QDir::cleanPath(ui->lineEdit_template->text()));
	if (!ui->check_bids->isChecked())
		settings.setValue("PathTemplate", QDir::cleanPath(ui->lineEdit_template->text()));
	qInfo() << requiredStreams;
	settings.setValue("RequiredStreams", requiredStreams);
	// Stub.
}

QSet<QString> MainWindow::getCheckedStreams() const {
	QSet<QString> checked;
	for (int i = 0; i < ui->streamList->count(); i++) {
		QListWidgetItem *item = ui->streamList->item(i);
		if (item->checkState() == Qt::Checked) checked.insert(item->text());
	}
	return checked;
}

/**
 * @brief MainWindow::refreshStreams Find streams, generate a list of missing streams
 * and fill the UI streamlist.
 * @return A vector of found stream_infos
 */
std::vector<lsl::stream_info> MainWindow::refreshStreams() {
	std::vector<lsl::stream_info> resolvedStreams = lsl::resolve_streams(1.0);

	QSet<QString> foundStreamNames;
	for (auto &s : resolvedStreams)
		foundStreamNames.insert(QString::fromStdString(s.name() + " (" + s.hostname() + ")"));

	const QSet<QString> previouslyChecked = getCheckedStreams();
	// Missing streams: all checked or required streams that weren't found
	missingStreams = (previouslyChecked + requiredStreams.toSet()) - foundStreamNames;

	// (Re-)Populate the UI list
	const QBrush good_brush(QColor(0, 128, 0)), bad_brush(QColor(255, 0, 0));
	ui->streamList->clear();
	for (auto &&streamName : foundStreamNames + missingStreams) {
		auto *item = new QListWidgetItem(streamName, ui->streamList);

		item->setCheckState(previouslyChecked.contains(streamName) ? Qt::Checked : Qt::Unchecked);
		item->setForeground(missingStreams.contains(streamName) ? bad_brush : good_brush);

		ui->streamList->addItem(item);
	}

	return resolvedStreams;
}

void MainWindow::startRecording() {

	if (!currentRecording) {
		if (doAutoTime) {
			QString timeFormat = "yyyy_MM_dd_HH_mm_ss";
			QDateTime dateTime = QDateTime::currentDateTime();
			QString timeString = dateTime.toString(timeFormat);
			ui->lineEdit_session->setText(timeString);
			this->buildFilename();
		}
		
		// automatically refresh streams
		const std::vector<lsl::stream_info> resolvedStreams = refreshStreams();
		const QSet<QString> checked = getCheckedStreams();

		// if a checked stream is now missing
		// change to "checked.intersects(missingStreams) as soon as Ubuntu 16.04/Qt 5.5 is EOL
		QSet<QString> missing = checked;
		if (!missing.intersect(missingStreams).isEmpty()) {
			// are you sure?
			QMessageBox msgBox(QMessageBox::Warning, "Stream not found",
				"At least one of the streams that you checked seems to be offline",
				QMessageBox::Yes | QMessageBox::No, this);
			msgBox.setInformativeText("Do you want to start recording anyway?");
			msgBox.setDefaultButton(QMessageBox::No);
			if (msgBox.exec() != QMessageBox::Yes) return;
		}

		if (checked.isEmpty()) {
			QMessageBox msgBox(QMessageBox::Warning, "No streams selected",
				"You have selected no streams", QMessageBox::Yes | QMessageBox::No, this);
			msgBox.setInformativeText("Do you want to start recording anyway?");
			msgBox.setDefaultButton(QMessageBox::No);
			if (msgBox.exec() != QMessageBox::Yes) return;
		}

		QString recFilename = replaceFilename(QDir::cleanPath(ui->lineEdit_template->text()));
		if (recFilename.isEmpty()) {
			QMessageBox::critical(this, "Filename empty", "Can not record without a file name");
			return;
		}
		recFilename.prepend(QDir::cleanPath(ui->rootEdit->text()) + '/');

		QFileInfo recFileInfo(recFilename);
		if (recFileInfo.exists()) {
			if (recFileInfo.isDir()) {
				QMessageBox::warning(
					this, "Error", "Recording path already exists and is a directory");
				return;
			}
			QString rename_to = recFileInfo.absolutePath() + '/' +
								recFileInfo.baseName() + "_old%1." + recFileInfo.suffix();
			// search for highest _oldN
			int i = 1;
			while (QFileInfo::exists(rename_to.arg(i))) i++;
			QString newname = rename_to.arg(i);
			if (!QFile::rename(recFileInfo.absoluteFilePath(), newname)) {
				QMessageBox::warning(this, "Permissions issue",
					"Cannot rename the file " + recFilename + " to " + newname);
				return;
			}
			qInfo() << "Moved existing file to " << newname;
			recFileInfo.refresh();
		}

		// regardless, we need to create the directory if it doesn't exist
		if (!recFileInfo.dir().mkpath(".")) {
			QMessageBox::warning(this, "Permissions issue",
				"Can not create the directory " + recFileInfo.dir().path() +
					". Please check your permissions.");
			return;
		}

		// go through all the listed streams
		std::vector<lsl::stream_info> checkedStreams;

		for (const lsl::stream_info &stream : resolvedStreams)
			if (checked.contains(
					QString::fromStdString(stream.name() + " (" + stream.hostname() + ')')))
				checkedStreams.push_back(stream);

		std::vector<std::string> watchfor;
		for (const QString &missing : missingStreams) watchfor.push_back(missing.toStdString());
		qInfo() << "Missing: " << missingStreams;

		currentRecording = std::make_unique<recording>(
			recFilename.toStdString(), checkedStreams, watchfor, syncOptionsByStreamName, true);
		ui->stopButton->setEnabled(true);
		ui->startButton->setEnabled(false);
		startTime = (int)lsl::local_clock();

	} else
		QMessageBox::information(
			this, "Already recording", "The recording is already running", QMessageBox::Ok);
}

void MainWindow::stopRecording() {

	if (!currentRecording)
		QMessageBox::information(
			this, "Not recording", "There is not ongoing recording", QMessageBox::Ok);
	else {
		try {
			currentRecording = nullptr;
		} catch (std::exception &e) { qWarning() << "exception on stop: " << e.what(); }
		ui->startButton->setEnabled(true);
		ui->stopButton->setEnabled(false);
		statusBar()->showMessage("Stopped");
	}
}

void MainWindow::selectAllStreams() {
	for (int i = 0; i < ui->streamList->count(); i++) {
		QListWidgetItem *item = ui->streamList->item(i);
		item->setCheckState(Qt::Checked);
	}
}

void MainWindow::selectNoStreams() {
	for (int i = 0; i < ui->streamList->count(); i++) {
		QListWidgetItem *item = ui->streamList->item(i);
		item->setCheckState(Qt::Unchecked);
	}
}

void MainWindow::buildBidsTemplate() {
	// path/to/CurrentStudy/sub-%p/ses-%s/eeg/sub-%p_ses-%s_task-%b[_acq-%a]_run-%r_eeg.xdf

	// Make sure the BIDS required fields are full.
	if (ui->lineEdit_participant->text().isEmpty()) { ui->lineEdit_participant->setText("P001"); }
	if (ui->lineEdit_session->text().isEmpty()) { ui->lineEdit_session->setText("S001"); }
	if (ui->input_blocktask->currentText().isEmpty()) {
		ui->input_blocktask->setCurrentText("Default");
	}

	// Folder hierarchy
	QStringList fileparts{"sub-%p", "ses-%s", "eeg"};

	// filename
	QString fname = "sub-%p_ses-%s_task-%b";
	if (!ui->lineEdit_acq->text().isEmpty()) { fname.append("_acq-%a"); }
	fname.append("_run-%r_eeg.xdf");
	fileparts << fname;
	ui->lineEdit_template->setText(QDir::toNativeSeparators(fileparts.join('/')));
}

void MainWindow::buildFilename() {
	// This function is only called when a widget within Location Builder is activated.

	// Build the file location in parts, starting with the root folder.
	if (ui->check_bids->isChecked()) buildBidsTemplate();
	QString tpl = QDir::cleanPath(ui->lineEdit_template->text());

	// Auto-increment Spin/Run Number if necessary.
	if (tpl.contains(counterPlaceholder())) {
		for (int i = 1; i < 1001; i++) {
			ui->spin_counter->setValue(i);
			if (!QFileInfo::exists(replaceFilename(tpl))) break;
		}
	}
	// Sometimes lineEdit_template doesn't change so printReplacedFilename isn't triggered.
	// So trigger manually.
	printReplacedFilename();
}

QString MainWindow::replaceFilename(QString fullfile) const {
	// Replace wildcards.
	// There are two different wildcard formats: legacy, BIDS

	// Legacy takes the form path/to/study/exp%n/%b.xdf
	// Where %n will be replaced by the contents of the spin_counter widget
	// and %b will be replaced by the contents of the blockList widget.
	fullfile.replace("%b", ui->input_blocktask->currentText());

	// BIDS
	// See https://docs.google.com/document/d/1ArMZ9Y_quTKXC-jNXZksnedK2VHHoKP3HCeO5HPcgLE/
	// path/to/study/sub-<participant_label>/ses-<session_label>/eeg/sub-<participant_label>_ses-<session_label>_task-<task_label>[_acq-<acq_label>]_run-<run_index>_eeg.xdf
	// path/to/study/sub-%p/ses-%s/eeg/sub-%p_ses-%s_task-%b[_acq-%a]_run-%r_eeg.xdf
	// %b already replaced above.
	fullfile.replace("%p", ui->lineEdit_participant->text());
	fullfile.replace("%s", ui->lineEdit_session->text());
	fullfile.replace("%a", ui->lineEdit_acq->text());

	// Replace either %r or %n with the counter
	QString run = QString("%1").arg(ui->spin_counter->value(), 3, 10, QChar('0'));
	fullfile.replace(counterPlaceholder(), run);

	return fullfile;
}

/**
 * Find a config file to load. This is (in descending order or preference):
 * - a file supplied on the command line
 * - [executablename].cfg in one the the following folders:
 *	- the current working directory
 *	- the default config folder, e.g. '~/Library/Preferences' on OS X
 *	- the executable folder
 * @param filename	Optional file name supplied e.g. as command line parameter
 * @return Path to a found config file
 */
QString MainWindow::find_config_file(const char *filename) {
	if (filename) {
		QString qfilename(filename);
		if (!QFileInfo::exists(qfilename))
			QMessageBox(QMessageBox::Warning, "Config file not found",
				QStringLiteral("The file '%1' doesn't exist").arg(qfilename), QMessageBox::Ok,
				this);
		else
			return qfilename;
	}
	QFileInfo exeInfo(QCoreApplication::applicationFilePath());
	QString defaultCfgFilename(exeInfo.completeBaseName() + ".cfg");
	qInfo() << defaultCfgFilename;
	QStringList cfgpaths;
	cfgpaths << QDir::currentPath()
			 << QStandardPaths::standardLocations(QStandardPaths::ConfigLocation) << exeInfo.path();
	for (auto path : cfgpaths) {
		QString cfgfilepath = path + QDir::separator() + defaultCfgFilename;
		if (QFileInfo::exists(cfgfilepath)) return cfgfilepath;
	}
	QMessageBox::warning(this, "No config file not found",
		QStringLiteral("No default config file could be found"), "Continue with default config");
	return "";
}

QString MainWindow::counterPlaceholder() const
{
	return ui->check_bids->isChecked() ? "%r" : "%n";
}

void MainWindow::printReplacedFilename() {
	ui->locationLabel->setText(
		ui->rootEdit->text() + '\n' + replaceFilename(ui->lineEdit_template->text()));
}
