# Quest setup and troubleshooting

Quest 2 is the hardware-tested target. Quest 3 compatibility is not yet verified.

## Stream from a PC

On the PC, open **Steam → Settings → Remote Play** and enable Remote Play.
Keep Steam running, sign in to the same Steam account on both devices, and
start with the PC and Quest on the same local network. Install the game on the
PC, then select that PC as the launch source in the headset.

For VR streaming, install SteamVR on the PC and launch the game in VR mode.
A direct USB cable also needs the USB networking setup described in the
[runtime documentation](../packages/quest-runtime/README.md); connecting an
ADB cable alone does not establish a streaming network.

Valve provides [Remote Play setup and troubleshooting](https://help.steampowered.com/en/faqs/view/0689-74B8-92AC-10F2)
and [network troubleshooting](https://help.steampowered.com/en/faqs/view/3E3D-BE6B-787D-A5D2).

If Steam says all apps are filtered out, clear the library search and review
the active library filters. Check that the selected source is the PC for
streamed games. Please report the exact message and take a screenshot that
includes the filters and source selector. This message alone does not identify
a decoder or network failure, and clearing filters does not make every game
compatible with local Quest execution.

## Use Touch controllers as a gamepad

Launch a game and select **gamepad input** on its window. Both Touch controllers
must be awake. Gamepad mode and laser interaction are separate input modes.

| Touch input | Gamepad input |
| --- | --- |
| Right A / B | A / B |
| Left X / Y | X / Y |
| Left / right stick | Left / right stick |
| Left / right trigger | LT / RT |
| Left / right grip | LB / RB |
| Left grip + left stick click | View |
| Right grip + right stick click | Menu |

Streamed games use Steam Input's configuration. The local fallback supports
SDL2 games, including Celeste; it does not cover SDL3 or every input API.
See the [runtime documentation](../packages/quest-runtime/README.md) for details.

## Make text easier to read

Use Steam's move and resize controls to bring the window closer or make it
larger. Chromium also supports **Ctrl + +** to zoom a page when a keyboard is
available. The current APK renders at 1374 × 1440 per eye, requests 72 Hz, and
has no user-facing native-resolution switch. Increasing window size can help
readability but does not change that rendering limit.

## Report a Chromium failure

State whether only Chromium closes, the desktop closes, or the entire Quest
app exits. Include the APK release, Quest model, Horizon OS version, page or
action that triggers it, and whether other desktop apps were open.

To capture a launch error, close Chromium and open Konsole in KDE, then run:

```sh
/opt/quest/chromium-launcher.sh https://example.org > "$HOME/chromium-launch.log" 2>&1
```

Reproduce the problem, then inspect `chromium-launch.log` in your home folder.
For a whole-app exit, a developer can also capture Android's crash log with
`adb logcat -b crash -d` after reconnecting the Quest. Review logs before
sharing them: they can include browsing addresses and device information.
Keep the browser profile in place; deleting it removes useful reproduction
state and may remove saved browser data.
