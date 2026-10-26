#include <QtQuick>
#include <sailfishapp.h>

#include "pangolincontroller.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setOrganizationName(QStringLiteral("harbour-pangolin-ui"));
    app->setApplicationName(QStringLiteral("harbour-pangolin-ui"));

    PangolinController controller;

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("pangolin"), &controller);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    return app->exec();
}
