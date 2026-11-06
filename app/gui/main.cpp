#include <MainWindow.hpp>
#include <QApplication>
#include <QCommandLineParser>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

namespace {

// Dark theme: the classic Ryty deep-blue palette (default since 1.x).
void ApplyDarkPalette() {
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
    palette.setColor(QPalette::PlaceholderText, QColor(120, 126, 138));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 126, 138));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 126, 138));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 126, 138));
    QApplication::setPalette(palette);
}

// Light theme (new in 2.0.0): a neutral bright palette for daylight use.
void ApplyLightPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(240, 242, 246));
    palette.setColor(QPalette::WindowText, QColor(32, 36, 44));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(244, 246, 250));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipText, QColor(32, 36, 44));
    palette.setColor(QPalette::Text, QColor(32, 36, 44));
    palette.setColor(QPalette::Button, QColor(228, 232, 238));
    palette.setColor(QPalette::ButtonText, QColor(32, 36, 44));
    palette.setColor(QPalette::BrightText, QColor(200, 30, 30));
    palette.setColor(QPalette::Link, QColor(20, 110, 180));
    palette.setColor(QPalette::Highlight, QColor(70, 140, 200));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(140, 146, 158));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(150, 154, 162));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(150, 154, 162));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(150, 154, 162));
    QApplication::setPalette(palette);
}

}

namespace Ryty {

void ApplyApplicationTheme(const bool dark) {
    if (dark) ApplyDarkPalette();
    else ApplyLightPalette();
}

}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Ryty");
    QApplication::setApplicationVersion("2.0.0");
    QApplication::setOrganizationName("Ryty");

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Ryty 2.0.0 - PS4/PS5 executable porting tool (GUI)");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption lightOption(QStringList() << "l" << "light", "Start with the light theme.");
    QCommandLineOption darkOption(QStringList() << "d" << "dark", "Start with the dark theme (default).");
    parser.addOptions({lightOption, darkOption});
    parser.process(app);

    QSettings settings;
    const bool dark = parser.isSet(lightOption) ? false
        : parser.isSet(darkOption) ? true
        : settings.value("ryty-gui/2.0/darkTheme", true).toBool();

    Ryty::ApplyApplicationTheme(dark);

    Ryty::MainWindow window;
    window.show();
    return QApplication::exec();
}
