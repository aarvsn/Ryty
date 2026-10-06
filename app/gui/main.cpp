#include <QApplication>
#include <QCommandLineParser>
#include <QPalette>
#include <QStyleFactory>
#include <MainWindow.hpp>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Ryty");
    QApplication::setApplicationVersion("1.0.3");
    QApplication::setOrganizationName("Ryty");

    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(24, 30, 42));
    palette.setColor(QPalette::WindowText, QColor(226, 230, 238));
    palette.setColor(QPalette::Base, QColor(15, 19, 28));
    palette.setColor(QPalette::AlternateBase, QColor(24, 30, 42));
    palette.setColor(QPalette::ToolTipBase, QColor(15, 19, 28));
    palette.setColor(QPalette::ToolTipText, QColor(226, 230, 238));
    palette.setColor(QPalette::Text, QColor(226, 230, 238));
    palette.setColor(QPalette::Button, QColor(38, 46, 64));
    palette.setColor(QPalette::ButtonText, QColor(226, 230, 238));
    palette.setColor(QPalette::BrightText, QColor(255, 90, 90));
    palette.setColor(QPalette::Link, QColor(90, 200, 250));
    palette.setColor(QPalette::Highlight, QColor(48, 110, 160));
    palette.setColor(QPalette::HighlightedText, QColor(240, 246, 252));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 126, 138));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 126, 138));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 126, 138));
    QApplication::setPalette(palette);

    QCommandLineParser parser;
    parser.setApplicationDescription("Ryty - PS5 executable porting tool (GUI)");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    Ryty::MainWindow window;
    window.show();
    return QApplication::exec();
}
