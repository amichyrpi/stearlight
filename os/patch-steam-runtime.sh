#!/bin/sh
# Keep Valve's replaceable Steam Runtime usable on the musl host. The stock
# run.sh puts both i386 and x86_64 libraries in one search path; that is unsafe
# for Alpine helpers and for Mesa's ABI-sensitive DRI loader. This patch keeps
# the Valve script intact apart from one deterministic ABI-selection block.

set -eu

run_script=${1:?path to Steam runtime run.sh is required}
tmp_script="${run_script}.stearlight"
runtime_root=${run_script%/run.sh}

# Steam's old libacl/libattr copies can make musl host tools (rm, cp, tar)
# resolve a glibc library from the runtime. They are not needed by Steam's
# client, so remove only those conflicting names from the multiarch trees.
for runtime_dir in \
    "$runtime_root/lib/i386-linux-gnu" \
    "$runtime_root/lib/x86_64-linux-gnu" \
    "$runtime_root/usr/lib/i386-linux-gnu" \
    "$runtime_root/usr/lib/x86_64-linux-gnu"; do
    rm -f "$runtime_dir"/libacl.so* "$runtime_dir"/libattr.so*
done

# Mesa's software DRI modules need newer GCC/C++ and XCB symbols than some
# SteamRT snapshots carry. Copy the ABI-matched files beside the runtime
# libraries before rewriting run.sh. The seed is absent during image build
# and supplied by the VM/first-boot launcher when repairing a live tree.
# During the image build the ABI seed is already unpacked beside run.sh, but
# no runtime environment variable exists yet.  Infer that in-tree seed so the
# patched bootstrap archive carries the compatible loader dependencies from
# the first boot (the live repair hook still overrides this with /usr/lib/steam).
seed_root=${SVRT_STEAM_RUNTIME_SEED-}
if [ -z "$seed_root" ] && [ -d "$runtime_root/stearlight_libs_32" ]; then
    seed_root="$runtime_root"
fi
if [ -n "$seed_root" ]; then
    for abi in 32 64; do
        if [ "$abi" = 32 ]; then
            multiarch=i386-linux-gnu
        else
            multiarch=x86_64-linux-gnu
        fi
        for runtime_dir in \
            "$runtime_root/lib/$multiarch" \
            "$runtime_root/usr/lib/$multiarch"; do
            [ -d "$runtime_dir" ] || continue
            for name in libgcc_s.so.1 libstdc++.so.6 libxcb.so.1 \
                       libxcb-dri3.so.0 libxcb-dri2.so.0; do
                source="$seed_root/stearlight_libs_$abi/$name"
                [ -f "$source" ] || continue
                if [ ! -f "$runtime_dir/$name" ] ||
                   ! cmp -s "$source" "$runtime_dir/$name"; then
                    rm -f "$runtime_dir/$name"
                    cp -L "$source" "$runtime_dir/$name"
                fi
            done
        done
    done
fi

awk '
# Remove both the marked block emitted by this version and the unmarked
# target_elf block emitted by older revisions. The latter starts at the
# unique target_elf_class assignment and ends immediately before the debugger
# case in Valve\047s run.sh. Re-emitting one block makes repair idempotent.
/^# STEARLIGHT_ABI_BOUNDARY_BEGIN$/ { skip=1; legacy=0; next }
/^# STEARLIGHT_ABI_BOUNDARY_END$/ { skip=0; next }
legacy && /^case "\$\{DEBUGGER-\}" in$/ {
    skip=0
    legacy=0
    emit_block()
    print
    next
}
skip { next }
/^target_elf_class=0$/ { skip=1; legacy=1; next }
/^case "\$\{DEBUGGER-\}" in$/ {
    emit_block()
    print
    next
}
/^steam_runtime_library_paths=/ {
    print "# An explicit platform path is used only by the VM\047s --run bootstrap."
    print "# Valve\047s normal steam.sh branch prepends the platform directory itself;"
    print "# --run enters run.sh directly and otherwise cannot resolve steamui\047s"
    print "# platform libraries (for example libvpx.so.6)."
    print "steam_platform_library_path=\"\${SVRT_STEAM_PLATFORM_LIBRARY_PATH-}\""
    print "steam_runtime_library_paths=\"\${steam_platform_library_path:+\$steam_platform_library_path:}\$host_library_paths\$STEAM_RUNTIME/lib/\$target_elf_multiarch:\$STEAM_RUNTIME/usr/lib/\$target_elf_multiarch:\$STEAM_RUNTIME/lib:\$STEAM_RUNTIME/usr/lib\""
    next
}
{ print }

function emit_block() {
    print "# STEARLIGHT_ABI_BOUNDARY_BEGIN"
    print "target_elf_class=0"
    # The bootstrap asks for paths with this literal option before launching
    # the 32-bit client. Normal calls probe the ELF command itself.
    print "if [ \"\${1-}\" = \"--print-steam-runtime-library-paths\" ]; then"
    print "    target_elf_class=1"
    print "else"
    print "    target_elf_probe=\"\${1-}\""
    print "    if [ ! -r \"\$target_elf_probe\" ]; then target_elf_probe=\$(command -v -- \"\$target_elf_probe\" 2>/dev/null || true); fi"
    print "    if [ -r \"\$target_elf_probe\" ]; then"
    print "        target_elf_class=\$(dd if=\"\$target_elf_probe\" bs=1 skip=4 count=1 2>/dev/null | od -An -tu1 | tr -d \"[:space:]\")"
    print "    fi"
    print "fi"
    print "if [ \"\$target_elf_class\" = 0 ]; then"
    print "    case \"\${STEAM_RUNTIME_ARCH-}\" in"
    print "        i386|386|32) target_elf_class=1 ;;"
    print "        amd64|x86_64|64) target_elf_class=2 ;;"
    print "        *) target_elf_class=2 ;;"
    print "    esac"
    print "fi"
    print "seed_runtime_root=\"\${SVRT_STEAM_RUNTIME_SEED-}\""
    print "if [ \"\$target_elf_class\" = 1 ]; then"
    print "    target_elf_multiarch=i386-linux-gnu"
    print "    target_elf_host=\"\$STEAM_RUNTIME/stearlight_libs_32\""
    print "    target_elf_seed=\"\$seed_runtime_root/stearlight_libs_32\""
    print "else"
    print "    target_elf_multiarch=x86_64-linux-gnu"
    print "    target_elf_host=\"\$STEAM_RUNTIME/stearlight_libs_64\""
    print "    target_elf_seed=\"\$seed_runtime_root/stearlight_libs_64\""
    print "fi"
    print "if [ -n \"\$seed_runtime_root\" ] && [ -d \"\$target_elf_seed\" ]; then"
    print "    target_elf_host=\"\$target_elf_seed\""
    print "fi"
    print "target_dri=\"\$target_elf_host/dri\""
    print "# Keep Mesa DRI search ABI-neutral for mixed-architecture Steam children."
    print "# The VM/host session seeds /usr/lib/<multiarch>/dri; callers can opt into"
    print "# a private seed only for a single-ABI diagnostic invocation."
    print "if [ \"\${SVRT_STEAM_USE_SEED_DRI:-0}\" = 1 ] && [ -d \"\$target_dri\" ]; then"
    print "    export LIBGL_DRIVERS_PATH=\"\$target_dri\""
    print "else"
    print "    unset LIBGL_DRIVERS_PATH"
    print "fi"
    print "# Vulkan ICD manifests are ABI-specific too. Steam launches both"
    print "# i386 and x86_64 children from this one wrapper; selecting the ICD"
    print "# from the executable class prevents a 64-bit child inheriting the"
    print "# 32-bit lavapipe library (and vice versa)."
    print "if [ \"\${SVRT_STEAM_USE_SEED_DRI:-0}\" = 1 ] && [ -r \"\$target_elf_seed/lvp_icd.json\" ]; then"
    print "    export VK_ICD_FILENAMES=\"\$target_elf_seed/lvp_icd.json\""
    print "else"
    print "    unset VK_ICD_FILENAMES"
    print "fi"
    print "host_library_paths=\"\$target_elf_host:\""
    print "# STEARLIGHT_ABI_BOUNDARY_END"
}
' "$run_script" > "$tmp_script"

chmod 0755 "$tmp_script"
mv "$tmp_script" "$run_script"
