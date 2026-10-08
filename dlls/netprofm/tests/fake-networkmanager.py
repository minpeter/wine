#!/usr/bin/python3

import os
import sys

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib


CONNECTIVITY = {"UNKNOWN": 0, "NONE": 1, "PORTAL": 2, "LIMITED": 3, "FULL": 4}


class NetworkManager(dbus.service.Object):
    def __init__(self, bus, command_path, ready_path, connectivity, available, enabled):
        self.command_path = command_path
        self.last_command = None
        self.connectivity = CONNECTIVITY[connectivity]
        self.available = available
        self.enabled = enabled
        super().__init__(bus, "/org/freedesktop/NetworkManager")
        open(ready_path, "w", encoding="ascii").close()
        GLib.timeout_add(50, self.read_command)

    def properties(self):
        return {
            "Connectivity": dbus.UInt32(self.connectivity),
            "ConnectivityCheckAvailable": dbus.Boolean(self.available),
            "ConnectivityCheckEnabled": dbus.Boolean(self.enabled),
        }

    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        if interface != "org.freedesktop.NetworkManager":
            return {}
        return self.properties()

    @dbus.service.signal("org.freedesktop.DBus.Properties", signature="sa{sv}as")
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    def read_command(self):
        try:
            with open(self.command_path, encoding="ascii") as command_file:
                command = command_file.read().strip()
        except FileNotFoundError:
            return True
        if not command or command == self.last_command:
            return True

        serial, connectivity, available, enabled = command.split()
        self.last_command = command
        self.connectivity = CONNECTIVITY[connectivity]
        self.available = available == "1"
        self.enabled = enabled == "1"
        self.PropertiesChanged("org.freedesktop.NetworkManager", self.properties(), [])
        open(os.path.join(os.path.dirname(self.command_path), "ack-" + serial), "w", encoding="ascii").close()
        return True


def main():
    command_path, ready_path, connectivity, available, enabled = sys.argv[1:]
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    name = dbus.service.BusName("org.freedesktop.NetworkManager", bus)
    manager = NetworkManager(bus, command_path, ready_path, connectivity, available == "1", enabled == "1")
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
