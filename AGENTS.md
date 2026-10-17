# Stearlight OS

The maintained target is the standalone Quest APK. `Containerfile` assembles
the clean ARM64 rootfs; `build_files/quest/` builds the payload and
`post_process/quest/` verifies and publishes the APK. CI publishes the default
branch to the continuous release. VM tools are archived and are not built
by CI. Keep it small and straightforward; Valve's SteamVR runtime owns its
dashboard UI and behavior.

# A note from Amory

I like VR and solutions that are simple yet functional. Keep the project lean
and make the Quest build and test path easy to run and inspect.

When responding, be direct and concise, with only the detail needed to answer
the user's request.

# The ways to hurt yourself

1. Do not loop on a feature that cannot work in the current Quest/runtime.
2. Keep tests focused and report concise results rather than dumping logs.
