# Sailfish OS GUI for the Pangolin CLI (https://github.com/fosrl/cli)
TARGET = harbour-pangolin-ui

CONFIG += sailfishapp c++11

SOURCES += \
    src/main.cpp \
    src/pangolincontroller.cpp

HEADERS += \
    src/pangolincontroller.h

DISTFILES += \
    qml/harbour-pangolin-ui.qml \
    qml/cover/CoverPage.qml \
    qml/pages/MainPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/LoginPage.qml \
    qml/pages/ClientLogPage.qml \
    rpm/harbour-pangolin-ui.spec \
    pangolin-cli/build.sh \
    pangolin-cli/olm-systemd238.patch \
    helper/pangolin-helper.c \
    harbour-pangolin-ui.desktop \
    README.md \
    topmenu/harbour-pangolin-ui.json \
    topmenu/PangolinToggle.qml \
    systemd/harbour-pangolin.service

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# Translations: lupdate/lrelease run at build time; SailfishApp::application()
# loads /usr/share/harbour-pangolin-ui/translations/harbour-pangolin-ui-<lang>.qm.
# Add a language by listing translations/harbour-pangolin-ui-<lang>.ts here.
CONFIG += sailfishapp_i18n
TRANSLATION_SOURCES += $$PWD/src $$PWD/qml $$PWD/topmenu
TRANSLATIONS += translations/harbour-pangolin-ui-uk.ts

# ---- Top Menu quick toggle (pure QML, loaded by the home screen) ----
# Pattern: poetaster/screenrecorder settings/settings.pro
toggle_entry.files = topmenu/harbour-pangolin-ui.json
toggle_entry.path = /usr/share/jolla-settings/entries
toggle_qml.files = topmenu/PangolinToggle.qml
toggle_qml.path = /usr/share/jolla-settings/pages/harbour-pangolin-ui
toggle_icon.files = topmenu/icon-m-pangolin.png
toggle_icon.path = /usr/share/$${TARGET}/icons
# systemd user unit started/stopped by the toggle and by the app
user_unit.files = systemd/harbour-pangolin.service
user_unit.path = /usr/lib/systemd/user
INSTALLS += toggle_entry toggle_qml toggle_icon user_unit
