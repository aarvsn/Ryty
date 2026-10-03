#include <MainWindow.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

QGroupBox* Group(const QString& title, QLayout* layout) {
    auto* group = new QGroupBox(title);
    group->setLayout(layout);
    return group;
}

}

namespace Ryty {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Ryty - PS5 executable porter");
    resize(960, 700);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* ioGrid = new QGridLayout;
    auto addIoRow = [this, ioGrid](const QString& label, QLineEdit*& edit, const QString& placeholder) {
        edit = new QLineEdit;
        edit->setPlaceholderText(placeholder);
        auto* browse = new QPushButton("Browse...");
        const int row = ioGrid->rowCount();
        ioGrid->addWidget(new QLabel(label), row, 0);
        ioGrid->addWidget(edit, row, 1);
        ioGrid->addWidget(browse, row, 2);
        return browse;
    };

    auto* inputBrowse = addIoRow("Input ELF:", _inputPath, "eboot.bin");
    auto* outputBrowse = addIoRow("Output file:", _outputPath, "ported eboot");
    auto* reportBrowse = addIoRow("JSON report (optional):", _reportPath, "port-report.json");
    layout->addWidget(Group("Input / output", ioGrid));

    auto* optionsGrid = new QGridLayout;
    optionsGrid->addWidget(new QLabel("Target system:"), 0, 0);
    _targetSystem = new QComboBox;
    _targetSystem->addItem("Linux (native ELF)");
    _targetSystem->addItem("Windows (PE)");
    _targetSystem->addItem("macOS (Mach-O / Metal, experimental)");
    optionsGrid->addWidget(_targetSystem, 0, 1);

    optionsGrid->addWidget(new QLabel("Console platform:"), 0, 2);
    _consolePlatform = new QComboBox;
    _consolePlatform->addItem("Auto-detect");
    _consolePlatform->addItem("PlayStation 5");
    _consolePlatform->addItem("PlayStation 4");
    optionsGrid->addWidget(_consolePlatform, 0, 3);

    optionsGrid->addWidget(new QLabel("Unused NID filter:"), 1, 0);
    _unusedFilter = new QComboBox;
    _unusedFilter->addItem("0 - disabled");
    _unusedFilter->addItem("1 - CFG/GOT (resilient)");
    _unusedFilter->addItem("2 - strict reachability");
    _unusedFilter->setCurrentIndex(1);
    optionsGrid->addWidget(_unusedFilter, 1, 1);

    optionsGrid->addWidget(new QLabel("Library rpath:"), 1, 2);
    _runPath = new QLineEdit("$ORIGIN/libs");
    optionsGrid->addWidget(_runPath, 1, 3);

    auto makeCheck = [this, optionsGrid](const QString& label, bool checked, int row, int column) {
        auto* box = new QCheckBox(label);
        box->setChecked(checked);
        optionsGrid->addWidget(box, row, column);
        return box;
    };

    _toIntel = makeCheck("Lower AMD-only instructions", true, 2, 0);
    _skipSceModule = makeCheck("Skip sce_module", false, 2, 1);
    _skipSyscallCheck = makeCheck("Skip syscall check", false, 2, 2);
    _lazyBinding = makeCheck("Lazy PLT binding", false, 2, 3);
    _registry = makeCheck("Write call registry JSON", false, 3, 0);
    _autorun = makeCheck("Launch after porting", false, 3, 1);
    layout->addWidget(Group("Porting options", optionsGrid));

    auto* logLayout = new QVBoxLayout;
    _log = new QTextEdit;
    _log->setReadOnly(true);
    _log->setLineWrapMode(QTextEdit::NoWrap);
    _log->setFontFamily("Monospace");
    _log->setMinimumHeight(220);
    logLayout->addWidget(_log);

    auto* statusRow = new QHBoxLayout;
    _status = new QLabel("Ready.");
    _runButton = new QPushButton("Run porting");
    _runButton->setMinimumWidth(180);
    statusRow->addWidget(_status, 1);
    statusRow->addWidget(_runButton);
    logLayout->addLayout(statusRow);
    layout->addWidget(Group("Porting log", logLayout), 1);

    setCentralWidget(central);

    connect(inputBrowse, &QPushButton::clicked, this, &MainWindow::BrowseInput);
    connect(outputBrowse, &QPushButton::clicked, this, &MainWindow::BrowseOutput);
    connect(reportBrowse, &QPushButton::clicked, this, &MainWindow::BrowseReport);
    connect(_runButton, &QPushButton::clicked, this, &MainWindow::RunPort);
    connect(&_process, &QProcess::readyReadStandardOutput, this, &MainWindow::ReadStandardOutput);
    connect(&_process, &QProcess::readyReadStandardError, this, &MainWindow::ReadStandardError);
    connect(&_process, &QProcess::finished, this, &MainWindow::ProcessFinished);
}

void MainWindow::BrowseInput() {
    const QString path = QFileDialog::getOpenFileName(this, "Select the PS5 ELF executable", QString(),
        "PS5 executables (eboot.bin *.bin *.elf);;All files (*)");
    if (path.isEmpty()) return;
    _inputPath->setText(path);
    if (!_outputPath->text().isEmpty()) return;
    const QFileInfo info(path);
    _outputPath->setText(info.dir().filePath("ported_" + info.completeBaseName()));
}

void MainWindow::BrowseOutput() {
    const QString path = QFileDialog::getSaveFileName(this, "Select the output file", _outputPath->text(),
        "Executables (*.elf *.exe *.bin);;All files (*)");
    if (!path.isEmpty()) _outputPath->setText(path);
}

void MainWindow::BrowseReport() {
    const QString path = QFileDialog::getSaveFileName(this, "Select the report file", _reportPath->text(),
        "JSON reports (*.json);;All files (*)");
    if (!path.isEmpty()) _reportPath->setText(path);
}

QString MainWindow::CliPath() const {
    const QDir appDir = QFileInfo(QCoreApplication::applicationFilePath()).absoluteDir();
#ifdef Q_OS_WIN
    const QString candidate = appDir.filePath("ryty.exe");
#else
    const QString candidate = appDir.filePath("ryty");
#endif
    if (QFileInfo::exists(candidate)) return candidate;
    return QStandardPaths::findExecutable("ryty");
}

QStringList MainWindow::BuildArguments() const {
    QStringList arguments;
    if (_targetSystem->currentIndex() == 1) arguments << "--windows";
    else if (_targetSystem->currentIndex() == 2) arguments << "--macos";
    if (_consolePlatform->currentIndex() == 1) arguments << "--ps5";
    else if (_consolePlatform->currentIndex() == 2) arguments << "--ps4";
    if (_toIntel->isChecked()) arguments << "--to-intel";
    if (_skipSceModule->isChecked()) arguments << "--skip-sce-module";
    if (_skipSyscallCheck->isChecked()) arguments << "--skip-syscall-check";
    if (_lazyBinding->isChecked()) arguments << "--lazy-binding";
    if (_registry->isChecked()) arguments << "--registry";
    if (!_reportPath->text().trimmed().isEmpty())
        arguments << "--report" << _reportPath->text().trimmed();
    arguments << "unused-filter=" + QString::number(_unusedFilter->currentIndex());
    if (!_runPath->text().trimmed().isEmpty() && _runPath->text() != "$ORIGIN/libs")
        arguments << "--rpath" << _runPath->text().trimmed();
    arguments << _inputPath->text().trimmed();
    arguments << _outputPath->text().trimmed();
    return arguments;
}

void MainWindow::AppendLog(const QString& text, const QColor& color) {
    _log->setTextColor(color);
    _log->append(text);
}

void MainWindow::RunPort() {
    if (_process.state() != QProcess::NotRunning) {
        QMessageBox::information(this, "Ryty", "A porting run is already in progress.");
        return;
    }
    if (_inputPath->text().trimmed().isEmpty() || _outputPath->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Ryty", "Choose an input executable and an output path first.");
        return;
    }
    const QString cli = CliPath();
    if (cli.isEmpty()) {
        QMessageBox::critical(this, "Ryty",
            "The ryty command line tool was not found next to the GUI.\n"
            "Place ryty-gui in the same directory as the ryty binary.");
        return;
    }

    _log->clear();
    AppendLog("$ " + cli + " " + BuildArguments().join(' '), QColor(140, 170, 255));
    _runButton->setEnabled(false);
    _status->setText("Porting...");
    _process.start(cli, BuildArguments());
}

void MainWindow::ReadStandardOutput() {
    const QString text = QString::fromUtf8(_process.readAllStandardOutput());
    for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
        QColor color(220, 224, 232);
        if (line.startsWith("Intel substitution:") || line.startsWith("Guest module:") || line.startsWith("Output file:") || line.startsWith("Porting report:"))
            color = QColor(120, 220, 160);
        else if (line.startsWith("FAIL:"))
            color = QColor(255, 110, 110);
        else if (line.startsWith("WARNING:"))
            color = QColor(255, 200, 100);
        AppendLog(line, color);
    }
}

void MainWindow::ReadStandardError() {
    const QString text = QString::fromUtf8(_process.readAllStandardError());
    for (const QString& line : text.split('\n', Qt::SkipEmptyParts))
        AppendLog(line, QColor(255, 130, 130));
}

void MainWindow::ProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    _runButton->setEnabled(true);
    if (exitStatus == QProcess::CrashExit) {
        _status->setText("The porting process crashed.");
        AppendLog("Process crashed.", QColor(255, 110, 110));
    } else if (exitCode == 0) {
        _status->setText("Done.");
        AppendLog("Porting finished successfully.", QColor(120, 220, 160));
    } else {
        _status->setText(QString("Failed with exit code %1.").arg(exitCode));
        AppendLog(QString("Porting failed with exit code %1.").arg(exitCode), QColor(255, 110, 110));
    }
}

}
