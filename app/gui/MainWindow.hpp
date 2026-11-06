#ifndef RYTY_APP_GUI_MAINWINDOW_HPP
#define RYTY_APP_GUI_MAINWINDOW_HPP

#include <QComboBox>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QTextEdit>

class QAction;

namespace Ryty {

// Defined in main.cpp: builds the dark or light Fusion palette and applies it
// to the application.
void ApplyApplicationTheme(bool dark);

// ryty-gui 2.0.0: themeable (dark/light) launcher around the `ryty` CLI with
// drag & drop, live progress, persisted options (QSettings ini), log export,
// cancellation, a batch mode that ports every ELF image of a folder and the
// Apple targets (macOS universal binaries, macOS/iOS/iOS-Simulator .app
// bundles, built-in entitlements presets).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void BrowseInput();
    void BrowseOutput();
    void BrowseReport();
    void RunPort();
    void CancelPort();
    void ExportLog();
    void ShowAbout();
    void ToggleTheme(bool dark);
    void ReadStandardOutput();
    void ReadStandardError();
    void ProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void TickElapsed();

private:
    QWidget* CreateInputGroup();
    QWidget* CreateOptionsGroup();
    QWidget* CreateLogGroup();
    void CreateMenus();
    [[nodiscard]] QStringList BuildArguments() const;
    void AppendLog(const QString& text, const QColor& color);
    [[nodiscard]] QColor LogColorFor(const QString& line) const;
    [[nodiscard]] QString CliPath() const;
    void LoadSettings();
    void SaveSettings();
    void ApplyThemeDependentColors();
    void UpdateSchemePreview();
    void SetRunning(bool running);
    void AdvanceProgress(const QString& line);

    QLineEdit* _inputPath = nullptr;
    QLineEdit* _outputPath = nullptr;
    QLineEdit* _reportPath = nullptr;
    QComboBox* _targetSystem = nullptr;
    QComboBox* _consolePlatform = nullptr;
    QComboBox* _unusedFilter = nullptr;
    QComboBox* _entitlementsTemplate = nullptr;
    QLineEdit* _runPath = nullptr;
    QLineEdit* _minOs = nullptr;
    QLineEdit* _interpPath = nullptr;
    QLabel* _schemeLabel = nullptr;
    QCheckBox* _toIntel = nullptr;
    QCheckBox* _skipSceModule = nullptr;
    QCheckBox* _skipSyscallCheck = nullptr;
    QCheckBox* _lazyBinding = nullptr;
    QCheckBox* _registry = nullptr;
    QCheckBox* _autorun = nullptr;
    QCheckBox* _rytyNet = nullptr;
    QCheckBox* _verifyOutput = nullptr;
    QCheckBox* _verboseOutput = nullptr;
    QCheckBox* _batchMode = nullptr;
    QCheckBox* _universalBinary = nullptr;
    QCheckBox* _appleBundle = nullptr;
    QCheckBox* _hardenedRuntime = nullptr;
    QPushButton* _runButton = nullptr;
    QPushButton* _cancelButton = nullptr;
    QPushButton* _exportLogButton = nullptr;
    QTextEdit* _log = nullptr;
    QLabel* _status = nullptr;
    QLabel* _elapsed = nullptr;
    QProgressBar* _progress = nullptr;
    QProcess _process;
    QSettings _settings;
    QTimer _elapsedTimer;
    QElapsedTimer _runTimer;
    QAction* _darkThemeAction = nullptr;
};

}

#endif
