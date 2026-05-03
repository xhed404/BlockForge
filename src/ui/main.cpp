#include "ui/MainWindow.h"

#include <QApplication>
#include "core/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QResource>
#include <QTextStream>
#include <QStringConverter>

#include <mutex>

static QFile* g_logFile = nullptr;
static std::mutex g_logMutex;

static void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    const char* t = "INFO";
    if (type == QtWarningMsg) t = "WARN";
    else if (type == QtCriticalMsg) t = "ERROR";
    else if (type == QtFatalMsg) t = "FATAL";

    const auto line = QString("[%1] [%2] %3\n")
                          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
                          .arg(t)
                          .arg(msg);

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_logFile && g_logFile->isOpen())
        {
            QTextStream ts(g_logFile);
            ts.setEncoding(QStringConverter::Utf8);
            ts << line;
            g_logFile->flush();
        }
    }

    fprintf(stderr, "%s", line.toUtf8().constData());
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(resources);
    const auto logDir = QDir(QString::fromStdString(blockforge::AppPaths::dataDir().string()));
    logDir.mkpath(".");
    auto* f = new QFile(logDir.filePath("launcher.log"));
    if (f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        g_logFile = f;
        qInstallMessageHandler(messageHandler);
    }
    app.setStyle("Fusion");
    app.setWindowIcon(QIcon(":/assets/icons/logo.svg"));
    blockforge::MainWindow w;
    w.resize(1200, 720);
    w.show();
    return app.exec();
}
