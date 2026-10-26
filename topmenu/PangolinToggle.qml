// Top Menu quick toggle for harbour-pangolin-ui.
//
// Runs inside the home screen (lipstick), so it is pure QML: no process
// spawning, no C++ plugin. It starts/stops the systemd *user* unit
// harbour-pangolin.service over the session D-Bus; the unit runs
// `pangolin-helper up --attach ...` (see /usr/lib/systemd/user/).
// State comes from the unit's ActiveState property (signal driven), so
// nothing polls the pangolin binary.
//
// Patterns taken from nephros/i2pd (EnableSwitch.qml, systemd Unit over
// Nemo.DBus) and poetaster/screenrecorder (ScreenrecorderToggle.qml).

import QtQuick 2.1
import Sailfish.Silica 1.0
import Nemo.DBus 2.0
import com.jolla.settings 1.0
import org.nemomobile.lipstick 0.1 as Lipstick

SettingsToggle {
    id: root

    property string unitState: unit.activeState
    readonly property bool unitRunning: unitState === "active"
    readonly property bool transitioning: unitState === "activating"
                                          || unitState === "deactivating"
                                          || unitState === "reloading"
    readonly property bool failed: unitState === "failed"

    // Marked for translation; lipstick only shows a translation if the
    // app's catalog is loaded there, otherwise the English text.
    name: failed ? qsTr("Pangolin (failed)") : "Pangolin"
    activeText: "Pangolin"
    icon.source: "/usr/share/harbour-pangolin-ui/icons/icon-m-pangolin.png"

    active: unitRunning
    checked: unitRunning
    busy: transitioning

    onToggled: {
        if (transitioning)
            return
        unit.call(unitRunning ? "Stop" : "Start", ["replace"])
        // Fallback in case no PropertiesChanged arrives.
        pollTimer.restart()
    }

    menu: ContextMenu {
        SettingsMenuItem {
            onClicked: appLauncher.launchApplication()
        }
    }

    Lipstick.LauncherItem {
        id: appLauncher
        filePath: "/usr/share/applications/harbour-pangolin-ui.desktop"
    }

    DBusInterface {
        id: unit
        bus: DBus.SessionBus
        service: "org.freedesktop.systemd1"
        // "harbour-pangolin.service" with systemd's bus-path escaping
        path: "/org/freedesktop/systemd1/unit/harbour_2dpangolin_2eservice"
        iface: "org.freedesktop.systemd1.Unit"

        property string activeState

        signalsEnabled: true
        propertiesEnabled: true
    }

    // systemd only emits change signals once some client called Subscribe().
    // Subscribing is idempotent and is never undone here (Unsubscribe would
    // affect lipstick's shared bus connection).
    DBusInterface {
        id: manager
        bus: DBus.SessionBus
        service: "org.freedesktop.systemd1"
        path: "/org/freedesktop/systemd1"
        iface: "org.freedesktop.systemd1.Manager"
        Component.onCompleted: call("Subscribe", [])
    }

    // Async Properties.Get (never blocks the home screen).
    DBusInterface {
        id: props
        bus: DBus.SessionBus
        service: "org.freedesktop.systemd1"
        path: "/org/freedesktop/systemd1/unit/harbour_2dpangolin_2eservice"
        iface: "org.freedesktop.DBus.Properties"

        function refresh() {
            typedCall("Get",
                      [{ "type": "s", "value": "org.freedesktop.systemd1.Unit" },
                       { "type": "s", "value": "ActiveState" }],
                      function(state) { if (typeof state === "string") unit.activeState = state })
        }
    }

    // Short, bounded poll only after a tap or while systemd reports a
    // transitional state; idle cost is zero.
    Timer {
        id: pollTimer
        property int ticks: 0
        interval: 1000
        repeat: true
        onRunningChanged: if (running) ticks = 0
        onTriggered: {
            props.refresh()
            ticks++
            if (ticks >= 25 || (ticks >= 2 && !root.transitioning))
                stop()
        }
    }

    onTransitioningChanged: if (transitioning && !pollTimer.running) pollTimer.start()
}
