#!/usr/bin/python3
"""Switch staged runtime directories only before the Linux VR session starts."""
from pathlib import Path
import shutil

def activate(cache, runtime, pending, previous):
    activated = cache / 'activated.json'
    if activated.exists():
        # A previous launch failed before dashboard textures appeared. Recover
        # the last working runtime rather than entering a permanent boot loop.
        if previous.exists():
            if runtime.exists():
                shutil.rmtree(runtime)
            previous.rename(runtime)
        activated.unlink()
        (cache / 'pending.json').unlink(missing_ok=True)
        print('Recovered the previous SteamVR runtime after an incomplete startup')
    if pending.exists() and (cache / 'pending.json').exists():
        if previous.exists():
            shutil.rmtree(previous)
        (cache / 'pending.json').rename(activated)
        runtime.rename(previous)
        pending.rename(runtime)
        print('Activated staged SteamOS runtime; validating SteamVR startup')


if __name__ == '__main__':
    activate(Path('/home/stearlight/.cache/stearlight/os-update'),
             *map(Path, ('/opt/steamvr', '/opt/steamvr.next', '/opt/steamvr.previous')))
