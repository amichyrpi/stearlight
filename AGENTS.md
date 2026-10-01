# H.265 SVRT

The maintained OS target is the ARM64 Steam Frame test VM in `vm/`. It is an
alpha work in progress. Keep it small and straightforward; Valve's SteamVR
runtime owns its dashboard UI and behavior.

# A note from Amory

I like VR and solutions that are simple yet functional. Keep the project lean
and make the VM test path easy to run and inspect.

When responding, be direct and concise, with only the detail needed to answer
the user's request.

# The ways to hurt yourself

1. Do not loop on a feature that cannot work in the current VM/runtime.
2. Keep tests focused and report concise results rather than dumping logs.
