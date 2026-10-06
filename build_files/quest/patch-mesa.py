#!/usr/bin/env python3
"""Apply Quest KGSL timeline and opaque GL memory-import fixes to Mesa 26.1.6."""
import argparse
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("mesa", type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
target = args.mesa / "src/freedreno/vulkan/tu_knl_kgsl.cc"
source = target.read_text()
if '#include "mesa-shared-timeline.h"' not in source:
    replacements = {
        "static const struct tu_knl kgsl_knl_funcs = {":
            '#include "mesa-shared-timeline.h"\n\nstatic const struct tu_knl kgsl_knl_funcs = {',
        ".queue_submit = kgsl_queue_submit,": ".queue_submit = quest_kgsl_queue_submit,",
        "device->sync_types[1] = &device->timeline_type.sync;":
            "device->sync_types[1] = &quest_timeline_type;",
        "if (wait_flags & VK_SYNC_WAIT_PENDING)\n      return VK_SUCCESS;":
            "/* Conservative pending waits also wait for GPU completion. */",
    }
    for before, after in replacements.items():
        expected = 2 if before.startswith("if (wait_flags") else 1
        if source.count(before) != expected:
            raise SystemExit(f"Unsupported Mesa source: expected {expected} occurrences of {before!r}")
        source = source.replace(before, after)
    target.write_text(source)
shutil.copyfile(repo / "packages/quest-runtime/linux/mesa-shared-timeline.h",
                target.parent / "mesa-shared-timeline.h")

# An opaque GL memory fd carries no explicit DRM modifier. The state tracker
# initializes winsys_handle to zero, which otherwise looks like linear tiling
# and makes Zink reinterpret an optimal Vulkan image as a linear dma-buf.
zink = args.mesa / "src/gallium/drivers/zink/zink_resource.c"
source = zink.read_text()
before = "memobj->whandle.type = ZINK_EXTERNAL_MEMORY_HANDLE;"
after = before + "\n   memobj->whandle.modifier = DRM_FORMAT_MOD_INVALID;"
if after not in source:
    if source.count(before) != 1:
        raise SystemExit("Unsupported Mesa Zink memory-object import")
    zink.write_text(source.replace(before, after))
