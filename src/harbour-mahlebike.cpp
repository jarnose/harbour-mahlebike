#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <sailfishapp.h>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <QTranslator>

#include "controller/appcontroller.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    QTranslator translator;
    const QString translationsDir = SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
    if (translator.load(QLocale(), QStringLiteral("harbour-mahlebike"), QStringLiteral("-"), translationsDir))
        app->installTranslator(&translator);

    // Sailjail whitelists (and persists) ~/.local/share/<OrganizationName>/<ApplicationName> from the
    // .desktop file's [X-Sailjail] values. AppDataLocation, where the ride database lives, and
    // AppConfigLocation, where the settings live, are built from these two, so they must match
    // harbour-mahlebike.desktop exactly or the app writes somewhere Sailjail never keeps.
    app->setOrganizationName(QStringLiteral("io.github.jarnose"));
    app->setApplicationName(QStringLiteral("mahlebike"));

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    AppController controller;
    view->rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);

    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    return app->exec();
}
