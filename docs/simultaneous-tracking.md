# Simultaneous hands and controller trackers

With Stearlight focused, enter **Up, Up, Down, Down, Left, Right, Left, Right,
B, A** using the **right controller's thumbstick and buttons**. Release the
stick to its center between directions and release B before pressing A. Keep
each gap below three seconds. Repeat the sequence to turn the mode off.

A short haptic pulse confirms a saved change. A longer, faint pulse means the
runtime rejected the request or the setting could not be saved. The preference
survives app and headset restarts; Stearlight reapplies it to each OpenXR session.

The runtime must advertise and accept `XR_META_simultaneous_hands_and_controllers`.
Unsupported runtimes retain their normal input behavior. While this mode is active,
hands supply the controller inputs and physical controllers supply tracker poses.
Touch buttons do not also operate the hand controllers. Pick up
the right controller to enter the sequence to exit.

Stearlight registers two OpenVR `GenericTracker` devices with stable identities,
the installed Vive Tracker 3.0 render model and HTC's tracker input profile. They
can be assigned tracker roles in SteamVR. They remain Meta-tracked devices.
Original Quest 2 Touch controllers supply poses only while Horizon tracks them;
this identity conversion cannot keep unsupported controllers tracked when released.
Detached tracking additionally requires `XR_META_detached_controllers` and
compatible controllers, such as Touch Pro or supported Quest 3/3S controllers.
Invalid, untracked and stale poses are disconnected. Repeating the code turns
tracker mode off and restores normal Touch controller input.

The local SteamVR tracker path does not establish PCVR streaming support by
itself. Extra tracker forwarding through Valve's streaming player requires a
separate hardware/host verification; this mode does not claim that it works.
It also does not make standalone VRChat compatible.

Diagnostics are private files under `files/state`:

- `multimodal-mode`: saved preference (`1` or `0`).
- `multimodal-capabilities`: runtime extension/property support.
- `multimodal-status`: `off`, `hands-and-trackers-held-only`, `hands-and-trackers`, or the
  reason activation failed.

See [Meta's native multimodal documentation](https://developers.meta.com/vr/documentation/native/android/native-multimodal/)
for runtime requirements and limitations.
