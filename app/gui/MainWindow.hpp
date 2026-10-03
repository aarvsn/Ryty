#ifndef RYTY_APP_GUI_MAINWINDOW_HPP
#define RYTY_APP_GUI_MAINWINDOW_HPP

#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QProcess>
#include <QPushButton>
#include <QTextEdit>

class QLabel;

namespace Ryty {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void BrowseInput();
    void BrowseOutput();
    void BrowseReport();
    void RunPort();
    void ReadStandardOutput();
    void ReadStandardError();
    void ProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QWidget* CreateInputGroup();
    QWidget* CreateOptionsGroup();
    QWidget* CreateLogGroup();
    [[nodiscard]] QStringList BuildArguments() const;
    void AppendLog(const QString& text, const QColor& color);
    [[nodiscard]] QString CliPath() const;

    QLineEdit* _inputPath = nullptr;
    QLineEdit* _outputPath = nullptr;
    QLineEdit* _reportPath = nullptr;
    QComboBox* _targetSystem = nullptr;
    QComboBox* _unusedFilter = nullptr;
    QLineEdit* _runPath = nullptr;
    QCheckBox* _toIntel = nullptr;
    QCheckBox* _skipSceModule = nullptr;
    QCheckBox* _skipSyscallCheck = nullptr;
    QCheckBox* _lazyBinding = nullptr;
    QCheckBox* _registry = nullptr;
    QCheckBox* _writeReport = nullptr;
    QCheckBox* _autorun = nullptr;
    QPushButton* _runButton = nullptr;
    QTextEdit* _log = nullptr;
    QLabel* _status = nullptr;
    QProcess _process;
};

}

#endif
