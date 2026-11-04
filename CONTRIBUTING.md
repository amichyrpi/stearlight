## Contributing

When working on this project, you should know that it uses the **Vouch** trust
system to know who is regularly submitting PRs to this project. It also uses a size
system to calculate the size of a PR. If your PR is bigger than **XL** and
you're not **Vouched**, your PR may never be merged.

## Quality

To know if your PR works and is not breaking any other features, you will need
to provide a video of the feature or the bug fix in action.

## AI

AI is authorized in this project and is even recommended, but please use a good
model and indicate which model was used in your changes. If you don't indicate
the model used and that you used AI to do the work, your PR will not be merged.

# Issues

Before opening a new issue, please provide logs, videos, and/or photos of your
issue, along with your headset model (Quest 2, 3, or 3S).

### First Boot

The first boot takes a while because it extracts the OS and updates Steam's
bundled recovery client. Later launches reuse the completed installation.

If you didn't approve all the prompts at the start of the app, it can cause issues.
Make sure to approve all of them, and if your issue persists, file an issue.

### Game Compatibility

Not all games are supported. Some work out of the box, some have additional fixes
(the ones on [the list](/STANDALONE_COMPATIBILITY.md)), but others are not supported and
need additional fixes.

If your game crashes the app when it starts, doesn't want to download, or doesn't run,
it's because it needs additional fixes.

> [!WARNING]
> **Do not file an issue for unsupported games.** Instead, file a PR with your game marked as unsupported in [the list](/STANDALONE_COMPATIBILITY.md).

### First Boot Login

Choose **More Options…** on Steam's sign-in panel for QR or account/password sign-in.

Nearby-headset sign-in is also available:

1. Open the Steam mobile app.
2. Go to the **Steam Guard** tab.
3. Click on the **gear icon** in the corner.
4. Press the **"Log in to Steam Frame"** button.
5. Select the nearby headset and approve only if its picture matches the one
   shown in your headset.

If sign-in still returns to the login screen, include `steamui_login.txt` and
`bootstrap_log.txt` from `files/rootfs/opt/steam/logs` with your report, along with
your headset model and sign-in method.
