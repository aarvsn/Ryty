#include <MainWindow.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr char SettingsGroup[] = "ryty-gui/2.0";

QGroupBox* Group(const QString& title, QLayout* layout) {
    auto* group = new QGroupBox(title);
    group->setLayout(layout);
    return group;
}

}

namespace Ryty {

constexpr char GuiVersion[] = "2.0.0";

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), _settings(this) {
    setWindowTitle(QString("Ryty %1 - PS4 / PS5 executable porter").arg(QLatin1String(GuiVersion)));
    setAcceptDrops(true);
    resize(1040, 780);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    layout->addWidget(CreateInputGroup());
    layout->addWidget(CreateOptionsGroup());
    layout->addWidget(CreateLogGroup(), 1);

    setCentralWidget(central);
    CreateMenus();
    LoadSettings();

    connect(_runButton, &QPushButton::clicked, this, &MainWindow::RunPort);
    connect(_cancelButton, &QPushButton::clicked, this, &MainWindow::CancelPort);
    connect(_exportLogButton, &QPushButton::clicked, this, &MainWindow::ExportLog);
    connect(&_process, &QProcess::readyReadStandardOutput, this, &MainWindow::ReadStandardOutput);
    connect(&_process, &QProcess::readyReadStandardError, this, &MainWindow::ReadStandardError);
    connect(&_process, &QProcess::finished, this, &MainWindow::ProcessFinished);
    connect(&_elapsedTimer, &QTimer::timeout, this, &MainWindow::TickElapsed);
    // 2.0.0 porting scheme: keep the one-line scheme summary live.
    connect(_targetSystem, &QComboBox::currentIndexChanged, this, &MainWindow::UpdateSchemePreview);
    connect(_consolePlatform, &QComboBox::currentIndexChanged, this, &MainWindow::UpdateSchemePreview);
    connect(_universalBinary, &QCheckBox::toggled, this, &MainWindow::UpdateSchemePreview);
    connect(_appleBundle, &QCheckBox::toggled, this, &MainWindow::UpdateSchemePreview);
    connect(_hardenedRuntime, &QCheckBox::toggled, this, &MainWindow::UpdateSchemePreview);
    connect(_lazyBinding, &QCheckBox::toggled, this, &MainWindow::UpdateSchemePreview);
    connect(_minOs, &QLineEdit::textChanged, this, &MainWindow::UpdateSchemePreview);
    connect(_interpPath, &QLineEdit::textChanged, this, &MainWindow::UpdateSchemePreview);
    connect(_runPath, &QLineEdit::textChanged, this, &MainWindow::UpdateSchemePreview);
}

void MainWindow::CreateMenus() {
    auto* view = menuBar()->addMenu("&View");
    _darkThemeAction = new QAction("Dark theme", this);
    _darkThemeAction->setCheckable(true);
    _darkThemeAction->setChecked(_settings.value(QString(SettingsGroup) + "/darkTheme", true).toBool());
    connect(_darkThemeAction, &QAction::toggled, this, &MainWindow::ToggleTheme);
    view->addAction(_darkThemeAction);

    auto* help = menuBar()->addMenu("&Help");
    auto* about = new QAction("About Ryty", this);
    connect(about, &QAction::triggered, this, &MainWindow::ShowAbout);
    help->addAction(about);
}

QWidget* MainWindow::CreateInputGroup() {
    auto* ioGrid = new QGridLayout;
    auto addIoRow = [ioGrid](const QString& label, QLineEdit*& edit, const QString& placeholder, const QString& tooltip) {
        edit = new QLineEdit;
        edit->setPlaceholderText(placeholder);
        edit->setToolTip(tooltip);
        auto* browse = new QPushButton("Browse...");
        const int row = ioGrid->rowCount();
        auto* caption = new QLabel(label);
        caption->setToolTip(tooltip);
        ioGrid->addWidget(caption, row, 0);
        ioGrid->addWidget(edit, row, 1);
        ioGrid->addWidget(browse, row, 2);
        return browse;
    };

    auto* inputBrowse = addIoRow("Input ELF / batch folder:", _inputPath, "eboot.bin (or a folder for batch mode)",
        "PS4/PS5 executable (eboot.bin, *.elf, *.prx). In batch mode this is the folder whose ELF files are all ported. You can also drag & drop it here.");
    auto* outputBrowse = addIoRow("Output file / folder:", _outputPath, "ported eboot",
        "Where the ported executable is written. In batch mode this is the output folder (default: <batch folder>/ported).");
    auto* reportBrowse = addIoRow("JSON report (optional):", _reportPath, "port-report.json",
        "Writes a ryty-port-report/2 JSON document describing every lowering site, counter and warning.");

    connect(inputBrowse, &QPushButton::clicked, this, &MainWindow::BrowseInput);
    connect(outputBrowse, &QPushButton::clicked, this, &MainWindow::BrowseOutput);
    connect(reportBrowse, &QPushButton::clicked, this, &MainWindow::BrowseReport);
    return Group("Input / output", ioGrid);
}

QWidget* MainWindow::CreateOptionsGroup() {
    auto* optionsGrid = new QGridLayout;

    auto addCaption = [optionsGrid](const QString& text, int row, int column, const QString& tooltip) {
        auto* caption = new QLabel(text);
        caption->setToolTip(tooltip);
        optionsGrid->addWidget(caption, row, column);
    };

    addCaption("Target system:", 0, 0, "Native binary format of the ported executable.");
    _targetSystem = new QComboBox;
    _targetSystem->addItem("Linux (native ELF)");
    _targetSystem->addItem("Windows (PE)");
    _targetSystem->addItem("macOS (Mach-O / Metal, experimental)");
    _targetSystem->addItem("iOS (arm64 Mach-O, experimental)");
    _targetSystem->addItem("iOS Simulator (arm64 Mach-O, experimental)");
    _targetSystem->setToolTip("Linux produces a SysV ELF64, Windows a PE executable, macOS an ad-hoc signed Mach-O with the experimental Metal bridge, "
                              "iOS a signed arm64 guest container with .app/.ipa packaging, and the iOS Simulator the same arm64 container "
                              "with LC_BUILD_VERSION platform 7 so Xcode can run it without provisioning.");
    optionsGrid->addWidget(_targetSystem, 0, 1);

    addCaption("Console platform:", 0, 2, "PS4 and PS5 executables are auto-detected by default.");
    _consolePlatform = new QComboBox;
    _consolePlatform->addItem("Auto-detect");
    _consolePlatform->addItem("PlayStation 5");
    _consolePlatform->addItem("PlayStation 4");
    optionsGrid->addWidget(_consolePlatform, 0, 3);

    addCaption("Unused NID filter:", 1, 0, "0 keeps every reference; 1 is the resilient CFG/GOT filter; 2 is strict reachability with PLT compaction.");
    _unusedFilter = new QComboBox;
    _unusedFilter->addItem("0 - disabled");
    _unusedFilter->addItem("1 - CFG/GOT (resilient)");
    _unusedFilter->addItem("2 - strict reachability");
    _unusedFilter->setCurrentIndex(1);
    optionsGrid->addWidget(_unusedFilter, 1, 1);

    addCaption("Library rpath:", 1, 2, "Runtime library search path baked into the output.");
    _runPath = new QLineEdit("$ORIGIN/libs");
    _runPath->setToolTip("Default $ORIGIN/libs; PRX modules are expected in this folder next to the executable.");
    optionsGrid->addWidget(_runPath, 1, 3);

    addCaption("Entitlements preset:", 5, 2, "Built-in entitlements templates for Apple targets; --entitlements overrides this.");
    _entitlementsTemplate = new QComboBox;
    _entitlementsTemplate->addItem("None (unsigned features)");
    _entitlementsTemplate->addItem("macos-app-sandbox");
    _entitlementsTemplate->addItem("macos-jit");
    _entitlementsTemplate->addItem("ios-device");
    _entitlementsTemplate->addItem("ios-debug");
    _entitlementsTemplate->setToolTip("macos-app-sandbox: App Sandbox + files/network grants. macos-jit: hardened-runtime JIT exceptions. "
                                      "ios-device: distribution-style (get-task-allow=false, increased memory). ios-debug: development-style (get-task-allow=true).");
    optionsGrid->addWidget(_entitlementsTemplate, 5, 3);

    // 2.0.0 porting scheme: deployment target (Apple) and ELF interpreter
    // (Linux) row, plus a live one-line summary of the resolved scheme.
    addCaption("Apple min OS:", 6, 0, "Deployment target baked into LC_BUILD_VERSION (--min-os). Applies to the selected Apple target; empty uses the per-target default (macOS 11.0, iOS/iOS Simulator 14.0).");
    _minOs = new QLineEdit;
    _minOs->setPlaceholderText("default per target");
    _minOs->setToolTip("--min-os: one deployment target for whichever Apple target is selected, e.g. 12.0. Overrides --macos-min / --ios-min.");
    optionsGrid->addWidget(_minOs, 6, 1);
    addCaption("ELF interpreter (Linux):", 6, 2, "Dynamic loader baked into PT_INTERP (--interp). Empty uses the default /lib64/ld-linux-x86-64.so.2.");
    _interpPath = new QLineEdit;
    _interpPath->setPlaceholderText("/lib64/ld-linux-x86-64.so.2");
    _interpPath->setToolTip("--interp: loader path for the Linux target. musl hosts use /lib/ld-musl-x86_64.so.1. Ignored on the other targets.");
    optionsGrid->addWidget(_interpPath, 6, 3);

    auto makeCheck = [optionsGrid](const QString& label, bool checked, int row, int column, const QString& tooltip) {
        auto* box = new QCheckBox(label);
        box->setChecked(checked);
        box->setToolTip(tooltip);
        optionsGrid->addWidget(box, row, column);
        return box;
    };

    _toIntel = makeCheck("Lower AMD-only instructions", true, 2, 0,
        "Rewrites AMD-only instructions (SHA-NI, RDPRU, MONITORX, ...) for Intel hosts. Recommended unless the host CPU is an AMD processor.");
    _skipSceModule = makeCheck("Skip sce_module", false, 2, 1,
        "Do not process the sce_module/sce_modules PRX files next to the input.");
    _skipSyscallCheck = makeCheck("Skip syscall check", false, 2, 2,
        "Skip the syscall-presence validation of the input image.");
    _lazyBinding = makeCheck("Lazy PLT binding", false, 2, 3,
        "Use lazy PLT binding (incompatible with guest modules).");
    _registry = makeCheck("Write call registry JSON", false, 3, 0,
        "Write <output>.registry.json with every external reference and resolved call site.");
    _autorun = makeCheck("Launch after porting", false, 3, 1,
        "Start the ported executable when the port finishes.");
    _rytyNet = makeCheck("RytyNet online mode (experimental)", false, 3, 2,
        "Enable the experimental RytyNet online-mode bridge. Writes a <output>.rytynet marker.");
    _verifyOutput = makeCheck("Verify output", true, 4, 0,
        "Re-read the finished image and validate its structure (ELF/PE/Mach-O header, code signature, LC_BUILD_VERSION)."
    );
    _verboseOutput = makeCheck("Verbose diagnostics", false, 4, 1,
        "Print per-site lowering details, input digest and timing information.");
    _batchMode = makeCheck("Batch mode: port every ELF in a folder", false, 4, 2,
        "Treat the input row as a folder and port every ELF image inside it into per-file folders, with a summary table and a batch-report.json.");
    _universalBinary = makeCheck("Universal binary: arm64 launcher (macOS)", false, 4, 3,
        "Wrap the macOS port as a fat binary: a native arm64 Rosetta launcher plus the x86_64 guest slice, so Apple Silicon Macs run it out of the box.");
    _appleBundle = makeCheck("Write .app bundle (macOS/iOS)", false, 5, 0,
        "Package the ported executable as an .app bundle with Info.plist (with --ipa on iOS it is zipped into a .ipa archive).");
    _hardenedRuntime = makeCheck("Hardened runtime (Apple)", false, 5, 1,
        "Set the CS_RUNTIME flag in the embedded ad-hoc code signature.");

    _schemeLabel = new QLabel;
    _schemeLabel->setWordWrap(true);
    _schemeLabel->setToolTip("The resolved porting scheme: what the ryty CLI will produce with the options above. It is printed by the CLI as 'Porting scheme:' and recorded in every JSON report.");

    auto* progressLayout = new QHBoxLayout;
    _progress = new QProgressBar;
    _progress->setRange(0, 100);
    _progress->setValue(0);
    _progress->setTextVisible(false);
    _progress->setToolTip("Live porting progress.");
    _progress->setMaximumHeight(14);
    progressLayout->addWidget(_progress, 1);
    _elapsed = new QLabel("elapsed 0.0s");
    _elapsed->setToolTip("Time since the porting run started.");
    progressLayout->addWidget(_elapsed);

    auto* runRow = new QHBoxLayout;
    _status = new QLabel("Ready.");
    _runButton = new QPushButton("Run porting");
    _runButton->setMinimumWidth(160);
    _runButton->setToolTip("Start the ryty CLI with the options above.");
    _cancelButton = new QPushButton("Cancel");
    _cancelButton->setEnabled(false);
    _cancelButton->setToolTip("Terminate the running porting process.");
    _exportLogButton = new QPushButton("Export log...");
    _exportLogButton->setToolTip("Save the porting log to a text file.");
    runRow->addWidget(_status, 1);
    runRow->addWidget(_exportLogButton);
    runRow->addWidget(_cancelButton);
    runRow->addWidget(_runButton);

    auto* outer = new QVBoxLayout;
    outer->addLayout(optionsGrid);
    outer->addWidget(_schemeLabel);
    outer->addLayout(progressLayout);
    outer->addLayout(runRow);

    _runButton->setDefault(true);
    UpdateSchemePreview();
    return Group("Porting options", outer);
}

QWidget* MainWindow::CreateLogGroup() {
    auto* logLayout = new QVBoxLayout;
    _log = new QTextEdit;
    _log->setReadOnly(true);
    _log->setLineWrapMode(QTextEdit::NoWrap);
    _log->setFontFamily("Monospace");
    _log->setMinimumHeight(200);
    _log->setPlaceholderText("The ryty CLI output appears here. Drag & drop an ELF file or a batch folder onto the window to start.");
    logLayout->addWidget(_log);
    return Group("Porting log", logLayout);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) return;
    const QFileInfo info(path);
    if (info.isDir()) {
        _batchMode->setChecked(true);
        _inputPath->setText(path);
        if (_outputPath->text().trimmed().isEmpty())
            _outputPath->setText(QDir(path).filePath("ported"));
        AppendLog("Batch folder loaded: " + path, LogColorFor(QString()));
    } else {
        _batchMode->setChecked(false);
        _inputPath->setText(path);
        if (_outputPath->text().trimmed().isEmpty())
            _outputPath->setText(info.dir().filePath("ported_" + info.completeBaseName()));
        AppendLog("Input loaded: " + path, LogColorFor(QString()));
    }
    event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    SaveSettings();
    event->accept();
}

void MainWindow::BrowseInput() {
    if (_batchMode->isChecked()) {
        const QString path = QFileDialog::getExistingDirectory(this, "Select the folder with ELF executables", _inputPath->text());
        if (!path.isEmpty()) {
            _inputPath->setText(path);
            if (_outputPath->text().trimmed().isEmpty()) _outputPath->setText(QDir(path).filePath("ported"));
        }
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, "Select the PS4/PS5 ELF executable", _inputPath->text(),
        "PS4/PS5 executables (eboot.bin *.bin *.elf *.prx *.self);;All files (*)");
    if (path.isEmpty()) return;
    _inputPath->setText(path);
    if (!_outputPath->text().isEmpty()) return;
    const QFileInfo info(path);
    _outputPath->setText(info.dir().filePath("ported_" + info.completeBaseName()));
}

void MainWindow::BrowseOutput() {
    if (_batchMode->isChecked()) {
        const QString path = QFileDialog::getExistingDirectory(this, "Select the batch output folder", _outputPath->text());
        if (!path.isEmpty()) _outputPath->setText(path);
        return;
    }
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
    // 2.0.0 porting scheme: every target goes through the same --target flag,
    // exactly like the CLI examples in the usage text.
    const QStringList targets = {"linux", "windows", "macos", "ios", "ios-sim"};
    arguments << "--target" << targets.at(_targetSystem->currentIndex());
    if (_targetSystem->currentIndex() == 2 && _universalBinary->isChecked()) arguments << "--universal";
    if (_targetSystem->currentIndex() >= 2 && _appleBundle->isChecked()) arguments << "--bundle";
    if (_targetSystem->currentIndex() == 3 && _appleBundle->isChecked()) arguments << "--ipa";
    if (_targetSystem->currentIndex() >= 2 && _hardenedRuntime->isChecked()) arguments << "--hardened-runtime";
    if (_targetSystem->currentIndex() >= 2 && _entitlementsTemplate->currentIndex() > 0)
        arguments << "--entitlements-template" << _entitlementsTemplate->currentText();
    if (_targetSystem->currentIndex() >= 2 && !_minOs->text().trimmed().isEmpty())
        arguments << "--min-os" << _minOs->text().trimmed();
    if (_targetSystem->currentIndex() == 0 && !_interpPath->text().trimmed().isEmpty()
        && _interpPath->text().trimmed() != "/lib64/ld-linux-x86-64.so.2")
        arguments << "--interp" << _interpPath->text().trimmed();
    if (_consolePlatform->currentIndex() == 1) arguments << "--ps5";
    else if (_consolePlatform->currentIndex() == 2) arguments << "--ps4";
    if (_toIntel->isChecked()) arguments << "--to-intel";
    if (_skipSceModule->isChecked()) arguments << "--skip-sce-module";
    if (_skipSyscallCheck->isChecked()) arguments << "--skip-syscall-check";
    if (_lazyBinding->isChecked()) arguments << "--lazy-binding";
    if (_registry->isChecked()) arguments << "--registry";
    if (_rytyNet->isChecked()) arguments << "--rytynet";
    if (_verifyOutput->isChecked()) arguments << "--verify";
    if (_verboseOutput->isChecked()) arguments << "--verbose";
    if (!_reportPath->text().trimmed().isEmpty() && !_batchMode->isChecked())
        arguments << "--report" << _reportPath->text().trimmed();
    arguments << "unused-filter=" + QString::number(_unusedFilter->currentIndex());
    const QString runPath = _runPath->text().trimmed();
    if (runPath != "$ORIGIN/libs")
        arguments << "--rpath" << runPath; // an empty value disables the baked-in search path
    if (_batchMode->isChecked()) {
        arguments << "--batch" << _inputPath->text().trimmed();
        if (!_outputPath->text().trimmed().isEmpty())
            arguments << "--batch-out" << _outputPath->text().trimmed();
    } else {
        arguments << _inputPath->text().trimmed();
        arguments << _outputPath->text().trimmed();
    }
    return arguments;
}

void MainWindow::AppendLog(const QString& text, const QColor& color) {
    _log->setTextColor(color);
    _log->append(text);
}

QColor MainWindow::LogColorFor(const QString& line) const {
    const bool dark = _settings.value(QString(SettingsGroup) + "/darkTheme", true).toBool();
    if (line.startsWith("Intel substitution:") || line.startsWith("Guest module:") || line.startsWith("Output file:")
        || line.startsWith("Porting report:") || line.startsWith("Verification:") || line.startsWith("Batch report:")
        || line.startsWith("    OK:") || line.startsWith("Log exported:"))
        return dark ? QColor(120, 220, 160) : QColor(24, 128, 64);
    if (line.startsWith("FAIL:") || line.startsWith("    FAILED:") || line.startsWith("Process crashed."))
        return dark ? QColor(255, 110, 110) : QColor(176, 32, 32);
    if (line.startsWith("WARNING:") || line.startsWith("      reason:"))
        return dark ? QColor(255, 200, 100) : QColor(170, 110, 0);
    if (line.startsWith("$ "))
        return dark ? QColor(140, 170, 255) : QColor(40, 80, 180);
    return dark ? QColor(220, 224, 232) : QColor(40, 44, 52);
}

void MainWindow::RunPort() {
    if (_process.state() != QProcess::NotRunning) {
        QMessageBox::information(this, "Ryty", "A porting run is already in progress.");
        return;
    }
    if (_inputPath->text().trimmed().isEmpty() || _outputPath->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Ryty", "Choose an input executable (or a batch folder) and an output path first.");
        return;
    }
    const QString cli = CliPath();
    if (cli.isEmpty()) {
        QMessageBox::critical(this, "Ryty",
            "The ryty command line tool was not found next to the GUI.\n"
            "Place ryty-gui in the same directory as the ryty binary.");
        return;
    }

    SaveSettings();
    _log->clear();
    AppendLog("$ " + QFileInfo(cli).fileName() + " " + BuildArguments().join(' '), LogColorFor("$ "));
    SetRunning(true);
    _process.start(cli, BuildArguments());
}

void MainWindow::CancelPort() {
    if (_process.state() == QProcess::NotRunning) return;
    AppendLog("Cancellation requested; terminating the porting process...", LogColorFor("WARNING:"));
    _process.kill();
}

void MainWindow::ExportLog() {
    const QString defaultName = QString("ryty-log-%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    const QString path = QFileDialog::getSaveFileName(this, "Export the porting log", defaultName, "Text files (*.txt);;All files (*)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Ryty", QString("Cannot write the log: %1").arg(file.errorString()));
        return;
    }
    QTextStream stream(&file);
    stream << _log->toPlainText();
    AppendLog("Log exported: " + path, LogColorFor("Output file:"));
}

void MainWindow::ShowAbout() {
    QMessageBox::about(this, "About Ryty",
        QString("<b>Ryty %1</b><br/><br/>"
                "Automatic porting of PlayStation 5 and PlayStation 4 executables to native "
                "Linux, Windows and experimental macOS and iOS binaries.<br/><br/>"
                "Apple targets (macOS, iOS, iOS Simulator): universal arm64+x86_64 binaries, "
                "entitlements presets, hardened runtime, configurable deployment targets, "
                ".app/.ipa bundles, provisioning profiles and signature-aware verification.<br/><br/>"
                "Also in 2.0.0: dark/light theme, drag &amp; drop, live progress, persisted options, "
                "log export, batch mode, output verification and verbose diagnostics.<br/><br/>"
                "Ryty is a fork of AnyPS5, licensed under the GNU GPL v2.")
            .arg(QLatin1String(GuiVersion)));
}

void MainWindow::ToggleTheme(bool dark) {
    _settings.setValue(QString(SettingsGroup) + "/darkTheme", dark);
    ApplyApplicationTheme(dark);
    AppendLog(dark ? "Dark theme enabled." : "Light theme enabled.", LogColorFor(QString()));
}

// 2.0.0 porting scheme: mirrors the CLI's MakePortScheme/PortScheme::Describe
// so the GUI always shows the plan the ryty run will follow.
void MainWindow::UpdateSchemePreview() {
    if (_schemeLabel == nullptr) return;
    const int target = _targetSystem->currentIndex();
    const QStringList systems = {"linux", "windows", "macos", "ios", "ios-sim"};
    const QString system = systems.at(target);

    QString format;
    QString architecture;
    QString signature = "no signature";
    QString layout = "flat";
    if (target == 0) {
        format = "SysV ELF64";
        architecture = "x86-64";
    } else if (target == 1) {
        format = "PE32+";
        architecture = "x86-64";
        signature = "unsigned";
    } else {
        format = "Mach-O 64";
        architecture = (target == 2) ? "x86_64" : "arm64";
        if (target == 2 && _universalBinary->isChecked()) architecture = "x86_64 + arm64 launcher";
        signature = "ad-hoc CodeDirectory";
        if (_hardenedRuntime->isChecked()) signature += " (hardened runtime)";
        if (_appleBundle->isChecked()) layout = (target == 3) ? ".app bundle + .ipa" : ".app bundle";
    }

    QString scheme = format + " (" + architecture + ")";
    if (target >= 2) {
        QString platform = (target == 2) ? "platform 1 (macOS)" : (target == 3) ? "platform 2 (iOS)" : "platform 7 (iOS Simulator)";
        QString minOs = _minOs->text().trimmed();
        if (minOs.isEmpty()) minOs = (target == 2) ? "11.0" : "14.0";
        scheme += ", " + platform + ", minOS " + minOs;
    }
    scheme += ", " + signature + ", " + layout;
    if (target == 0) {
        QString interpreter = _interpPath->text().trimmed();
        if (interpreter.isEmpty()) interpreter = "/lib64/ld-linux-x86-64.so.2";
        scheme += ", PT_INTERP " + interpreter + ", PT_GNU_STACK NX";
    }
    if (target != 1) {
        const QString runPath = _runPath->text().trimmed();
        if (runPath.isEmpty()) scheme += ", no baked-in library path";
        else scheme += ", library path " + runPath;
    }
    if (_lazyBinding->isChecked()) scheme += ", lazy PLT binding";
    _schemeLabel->setText(QString("Scheme: %1 \u2192 %2").arg(system, scheme));
}

void MainWindow::SetRunning(bool running) {
    _runButton->setEnabled(!running);
    _cancelButton->setEnabled(running);
    if (running) {
        _status->setText("Porting...");
        _progress->setValue(4);
        _runTimer.start();
        _elapsedTimer.start(100);
        _elapsed->setText("elapsed 0.0s");
    } else {
        _elapsedTimer.stop();
        _progress->setValue(100);
    }
}

void MainWindow::TickElapsed() {
    if (_runTimer.isValid())
        _elapsed->setText(QString("elapsed %1s").arg(_runTimer.elapsed() / 1000.0, 0, 'f', 1));
}

void MainWindow::AdvanceProgress(const QString& line) {
    int value = _progress->value();
    if (line.startsWith("Console:")) value = std::max(value, 20);
    if (line.startsWith("Relinker backend mode:")) value = std::max(value, 45);
    if (line.startsWith("NID total:")) value = std::max(value, 60);
    if (line.startsWith("Verification:")) value = std::max(value, 80);
    if (line.startsWith("Output file:")) value = std::max(value, 92);
    if (line.startsWith("    OK:")) value = std::max(value, 95);
    _progress->setValue(std::min(value, 99));
}

void MainWindow::ReadStandardOutput() {
    const QString text = QString::fromUtf8(_process.readAllStandardOutput());
    for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
        AppendLog(line, LogColorFor(line));
        AdvanceProgress(line);
    }
}

void MainWindow::ReadStandardError() {
    const QString text = QString::fromUtf8(_process.readAllStandardError());
    for (const QString& line : text.split('\n', Qt::SkipEmptyParts))
        AppendLog(line, LogColorFor(line));
}

void MainWindow::ProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    SetRunning(false);
    if (exitStatus == QProcess::CrashExit) {
        _status->setText("The porting process crashed.");
        AppendLog("Process crashed.", LogColorFor("FAIL:"));
        _progress->setValue(0);
    } else if (exitCode == 0) {
        _status->setText(_batchMode->isChecked() ? "Batch finished." : "Done.");
        AppendLog(_batchMode->isChecked() ? "Batch finished successfully." : "Porting finished successfully.",
                  LogColorFor("Output file:"));
    } else {
        _status->setText(QString("Failed with exit code %1.").arg(exitCode));
        AppendLog(QString("Porting failed with exit code %1.").arg(exitCode), LogColorFor("FAIL:"));
    }
}

void MainWindow::LoadSettings() {
    _settings.beginGroup(SettingsGroup);
    restoreGeometry(_settings.value("geometry").toByteArray());
    const QString input = _settings.value("inputPath").toString();
    if (!input.isEmpty()) _inputPath->setText(input);
    const QString output = _settings.value("outputPath").toString();
    if (!output.isEmpty()) _outputPath->setText(output);
    _reportPath->setText(_settings.value("reportPath").toString());
    _targetSystem->setCurrentIndex(_settings.value("targetSystem", 0).toInt());
    _consolePlatform->setCurrentIndex(_settings.value("consolePlatform", 0).toInt());
    _unusedFilter->setCurrentIndex(_settings.value("unusedFilter", 1).toInt());
    _entitlementsTemplate->setCurrentIndex(_settings.value("entitlementsTemplate", 0).toInt());
    const QString rpath = _settings.value("rpath", "$ORIGIN/libs").toString();
    _runPath->setText(rpath);
    _minOs->setText(_settings.value("minOs").toString());
    _interpPath->setText(_settings.value("interpPath").toString());
    _toIntel->setChecked(_settings.value("toIntel", true).toBool());
    _skipSceModule->setChecked(_settings.value("skipSceModule", false).toBool());
    _skipSyscallCheck->setChecked(_settings.value("skipSyscallCheck", false).toBool());
    _lazyBinding->setChecked(_settings.value("lazyBinding", false).toBool());
    _registry->setChecked(_settings.value("registry", false).toBool());
    _autorun->setChecked(_settings.value("autorun", false).toBool());
    _rytyNet->setChecked(_settings.value("rytyNet", false).toBool());
    _verifyOutput->setChecked(_settings.value("verify", true).toBool());
    _verboseOutput->setChecked(_settings.value("verbose", false).toBool());
    _batchMode->setChecked(_settings.value("batchMode", false).toBool());
    _universalBinary->setChecked(_settings.value("universalBinary", false).toBool());
    _appleBundle->setChecked(_settings.value("appleBundle", false).toBool());
    _hardenedRuntime->setChecked(_settings.value("hardenedRuntime", false).toBool());
    _settings.endGroup();
}

void MainWindow::SaveSettings() {
    _settings.beginGroup(SettingsGroup);
    _settings.setValue("geometry", saveGeometry());
    _settings.setValue("inputPath", _inputPath->text());
    _settings.setValue("outputPath", _outputPath->text());
    _settings.setValue("reportPath", _reportPath->text());
    _settings.setValue("targetSystem", _targetSystem->currentIndex());
    _settings.setValue("consolePlatform", _consolePlatform->currentIndex());
    _settings.setValue("unusedFilter", _unusedFilter->currentIndex());
    _settings.setValue("entitlementsTemplate", _entitlementsTemplate->currentIndex());
    _settings.setValue("rpath", _runPath->text());
    _settings.setValue("minOs", _minOs->text());
    _settings.setValue("interpPath", _interpPath->text());
    _settings.setValue("toIntel", _toIntel->isChecked());
    _settings.setValue("skipSceModule", _skipSceModule->isChecked());
    _settings.setValue("skipSyscallCheck", _skipSyscallCheck->isChecked());
    _settings.setValue("lazyBinding", _lazyBinding->isChecked());
    _settings.setValue("registry", _registry->isChecked());
    _settings.setValue("autorun", _autorun->isChecked());
    _settings.setValue("rytyNet", _rytyNet->isChecked());
    _settings.setValue("verify", _verifyOutput->isChecked());
    _settings.setValue("verbose", _verboseOutput->isChecked());
    _settings.setValue("batchMode", _batchMode->isChecked());
    _settings.setValue("universalBinary", _universalBinary->isChecked());
    _settings.setValue("appleBundle", _appleBundle->isChecked());
    _settings.setValue("hardenedRuntime", _hardenedRuntime->isChecked());
    _settings.endGroup();
}

}
