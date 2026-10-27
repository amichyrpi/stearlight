#!/usr/bin/python3
"""Apply Gamescope's public X11 controls in an isolated, short-lived client.

An X server can exit at any time. Keeping Xlib out of the desktop manager
prevents its fatal I/O handler from taking the manager down with it.
"""
import ctypes as C
import json
import os
from pathlib import Path
import sys


def apply(settings):
    x = C.CDLL('libX11.so.6')
    display, atom, window = C.c_void_p, C.c_ulong, C.c_ulong
    x.XOpenDisplay.argtypes, x.XOpenDisplay.restype = [C.c_char_p], display
    x.XDefaultRootWindow.argtypes, x.XDefaultRootWindow.restype = [display], window
    x.XInternAtom.argtypes, x.XInternAtom.restype = [display, C.c_char_p, C.c_int], atom
    x.XGetWindowProperty.argtypes = [display, window, atom, C.c_long, C.c_long, C.c_int, atom,
                                     C.POINTER(atom), C.POINTER(C.c_int), C.POINTER(C.c_ulong),
                                     C.POINTER(C.c_ulong), C.POINTER(C.c_void_p)]
    x.XChangeProperty.argtypes = [display, window, atom, atom, C.c_int, C.c_int, C.c_void_p, C.c_int]
    x.XFlush.argtypes = x.XCloseDisplay.argtypes = [display]
    x.XFree.argtypes = [C.c_void_p]
    x.XQueryTree.argtypes = [display, window, C.POINTER(window), C.POINTER(window),
                             C.POINTER(C.POINTER(window)), C.POINTER(C.c_uint)]
    result = []
    tagged = set()
    managed = settings.get('windows', {})
    for socket in sorted(Path('/tmp/.X11-unix').glob('X*')):
        if not socket.name[1:].isdigit():
            continue
        name = ':' + socket.name[1:]
        connection = x.XOpenDisplay(name.encode())
        if not connection:
            continue
        try:
            root = x.XDefaultRootWindow(connection)
            marker = x.XInternAtom(connection, b'GAMESCOPE_XWAYLAND_SERVER_ID', True)
            actual, fmt, count, remaining, data = atom(), C.c_int(), C.c_ulong(), C.c_ulong(), C.c_void_p()
            if not marker:
                continue
            x.XGetWindowProperty(connection, root, marker, 0, 1, False, 0,
                                 C.byref(actual), C.byref(fmt), C.byref(count), C.byref(remaining), C.byref(data))
            if data:
                x.XFree(data)
            if not count.value:
                continue
            if managed:
                parent, root_return, children, number = window(), window(), C.POINTER(window)(), C.c_uint()
                if x.XQueryTree(connection, root, C.byref(root_return), C.byref(parent), C.byref(children), C.byref(number)):
                    try:
                        pid_atom = x.XInternAtom(connection, b'_NET_WM_PID', False)
                        for index in range(number.value):
                            client = children[index]
                            data = C.c_void_p()
                            x.XGetWindowProperty(connection, client, pid_atom, 0, 1, False, 6,
                                C.byref(actual), C.byref(fmt), C.byref(count), C.byref(remaining), C.byref(data))
                            try:
                                if fmt.value != 32 or count.value != 1 or not data:
                                    continue
                                pid = C.cast(data, C.POINTER(C.c_ulong))[0]
                            finally:
                                if data:
                                    x.XFree(data)
                            try:
                                process = Path('/proc', str(pid))
                                if process.stat().st_uid != os.getuid():
                                    continue
                                environment = (process / 'environ').read_bytes().split(b'\0')
                            except OSError:
                                continue
                            for instance, app_id in managed.items():
                                if ('STEARLIGHT_DESKTOP_INSTANCE=' + instance).encode() not in environment:
                                    continue
                                value = C.c_ulong(int(app_id))
                                x.XChangeProperty(connection, client, x.XInternAtom(connection, b'STEAM_GAME', False),
                                                  6, 32, 0, C.byref(value), 1)
                                tagged.add(instance)
                    finally:
                        if children:
                            x.XFree(children)
            properties = {
                'GAMESCOPE_NEW_SCALING_FILTER': {1: 0, 2: 1, 3: 2}[settings['filter']],
                'GAMESCOPE_NEW_SCALING_SCALER': settings['scaler'] - 1,
                'GAMESCOPE_SHARPNESS': settings['sharpness'],
            }
            for key, value in properties.items():
                value = C.c_ulong(value)
                x.XChangeProperty(connection, root, x.XInternAtom(connection, key.encode(), False),
                                  6, 32, 0, C.byref(value), 1)  # XA_CARDINAL, PropModeReplace
            x.XFlush(connection)
            result.append(name)
        finally:
            x.XCloseDisplay(connection)
    return {'displays': result, 'tagged': sorted(tagged)}


if __name__ == '__main__':
    print(json.dumps(apply(json.loads(sys.argv[1]))))
