#!/usr/bin/env python3
"""Apply Quest KGSL, A6xx depth rendering and GL memory-import fixes to Mesa 26.1.6."""
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

# Allocation helpers own the DMA-heap/ION fd. The importer duplicates it for
# the BO, so retaining the original pins GPU memory after vkFreeMemory.
source = target.read_text()
for function, fd in (("bo_init_new_dmaheap", "alloc.fd"), ("bo_init_new_ion", "alloc.fd"),
                     ("bo_init_new_ion_legacy", "share.fd")):
    start = source.index(function + "(")
    end = source.index("\nstatic ", start)
    block = source[start:end]
    before = f"return tu_bo_init_dmabuf(dev, out_bo, -1, TU_BO_ALLOC_NO_FLAGS, {fd});"
    after = (f"VkResult result = tu_bo_init_dmabuf(dev, out_bo, -1, TU_BO_ALLOC_NO_FLAGS, {fd});\n"
             f"   close({fd});\n   return result;")
    if before in block:
        if block.count(before) != 1:
            raise SystemExit(f"Unsupported Mesa allocation helper: {function}")
        source = source[:start] + block.replace(before, after) + source[end:]
    elif f"close({fd});" not in block or "return result;" not in block:
        raise SystemExit(f"Unsupported Mesa allocation ownership: {function}")
target.write_text(source)

# Mesa e984ef29: KGSL interprets a zero ioctl timeout as an infinite wait.
# Vulkan zero-timeout probes must instead read the retired GPU timestamp.
# Keep 26.1's fd-based helper signature while backporting the poll behavior.
source = target.read_text()
start = source.index("static VkResult\nwait_timestamp_safe(")
end = source.index("\nVkResult\nkgsl_queue_wait_fence", start)
if "kgsl_poll_timestamp(" not in source:
    block = source[start:end]
    before = "      .timeout = get_relative_ms(abs_timeout_ns),\n"
    retry = """      if (ret == -1 && (errno == EINTR || errno == EAGAIN)) {
         int timeout_ms = get_relative_ms(abs_timeout_ns);

         /* update timeout to consider time that has passed since the start */
         if (timeout_ms == 0)
            return VK_TIMEOUT;

         wait.timeout = timeout_ms;
      } else if (ret == -1) {"""
    if block.count(before) != 1 or block.count(retry) != 1:
        raise SystemExit("Unsupported Mesa KGSL timestamp wait")
    block = block.replace(before, "").replace(retry, """      if (ret == -1 && (errno == EINTR || errno == EAGAIN)) {
         continue;
      } else if (ret == -1) {""")
    block = block.replace("   while (true) {\n", """   while (true) {
      int timeout_ms = get_relative_ms(abs_timeout_ns);
      if (timeout_ms == 0)
         return kgsl_poll_timestamp(fd, context_id, timestamp);
      wait.timeout = timeout_ms;
""")
    poll = """static VkResult
kgsl_poll_timestamp(int fd, unsigned int context_id, unsigned int timestamp)
{
   struct kgsl_cmdstream_readtimestamp_ctxtid read = {
      .context_id = context_id,
      .type = KGSL_TIMESTAMP_RETIRED,
   };
   if (safe_ioctl(fd, IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID, &read))
      return VK_ERROR_UNKNOWN;
   return timestamp_cmp(read.timestamp, timestamp) ? VK_SUCCESS : VK_TIMEOUT;
}

"""
    target.write_text(source[:start] + poll + block + source[end:])

# Quest's app policy rejects READTIMESTAMP_CTXTID with EACCES. Keep the
# zero-timeout contract using an exportable timestamp fence instead; do not
# turn the probe back into a KGSL wait (timeout 0 means infinite there).
source = target.read_text()
before = """   if (safe_ioctl(fd, IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID, &read))
      return VK_ERROR_UNKNOWN;
   return timestamp_cmp(read.timestamp, timestamp) ? VK_SUCCESS : VK_TIMEOUT;"""
after = """   static bool timestamp_read_denied;
   if (!__atomic_load_n(&timestamp_read_denied, __ATOMIC_RELAXED)) {
      if (!safe_ioctl(fd, IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID, &read))
         return timestamp_cmp(read.timestamp, timestamp) ? VK_SUCCESS : VK_TIMEOUT;
      if (errno != EACCES && errno != EPERM && errno != ENOTTY)
         return VK_ERROR_UNKNOWN;
      __atomic_store_n(&timestamp_read_denied, true, __ATOMIC_RELAXED);
   }

   int sync_fd = -1;
   struct kgsl_timestamp_event event = {
      .type = KGSL_TIMESTAMP_EVENT_FENCE,
      .timestamp = timestamp,
      .context_id = context_id,
      .priv = &sync_fd,
      .len = sizeof(sync_fd),
   };
   if (safe_ioctl(fd, IOCTL_KGSL_TIMESTAMP_EVENT, &event))
      return VK_ERROR_UNKNOWN;
   struct pollfd fence = { .fd = sync_fd, .events = POLLIN };
   int result;
   do {
      result = poll(&fence, 1, 0);
   } while (result < 0 && errno == EINTR);
   close(sync_fd);
   if (result < 0 || (fence.revents & (POLLERR | POLLNVAL)))
      return VK_ERROR_UNKNOWN;
   return result > 0 ? VK_SUCCESS : VK_TIMEOUT;"""
if after not in source:
    if source.count(before) != 1:
        raise SystemExit("Unsupported Mesa KGSL timestamp poll fallback")
    source = source.replace(before, after)
target.write_text(source)

# Mesa 9dea49d1: poll returns a positive ready count, zero on timeout, or -1
# on error. Preserve errno before closing the temporary exported sync FDs.
source = target.read_text()
before = """      for (uint32_t i = 0; i < fds_count - num_fds; i++)
         close(fds[i].fd);

      if (ret != 0) {
         assert(errno == ETIME);
         result = VK_TIMEOUT;
      } else {
         result = VK_SUCCESS;
      }"""
after = """      const int error = errno;
      for (uint32_t i = 0; i < fds_count - num_fds; i++)
         close(fds[i].fd);

      if (ret > 0) {
         result = VK_SUCCESS;
      } else if (ret == 0) {
         result = VK_TIMEOUT;
      } else {
         result = vk_errorf(device, VK_ERROR_UNKNOWN,
                            "KGSL sync FD poll failed: errno %d (%s)",
                            error, strerror(error));
      }"""
if after not in source:
    if source.count(before) != 1:
        raise SystemExit("Unsupported Mesa KGSL wait-any poll result")
    source = source.replace(before, after)
target.write_text(source)

# Mesa 1da50a1b: convert the aggregate timestamp before merging an FD or a
# timestamp from another queue. Do not interpret a queue pointer as an FD,
# or convert an incoming FD as though it were a timestamp.
source = target.read_text()
replacements = (
    ("""               ret.state = KGSL_SYNCOBJ_STATE_FD;
               int sync_fd = kgsl_syncobj_ts_to_fd(sync);
               ret.fd = sync_merge_close("tu_sync", ret.fd, sync_fd, true);""",
     """               int ret_fd = kgsl_syncobj_ts_to_fd(&ret);
               int sync_fd = kgsl_syncobj_ts_to_fd(sync);
               ret.state = KGSL_SYNCOBJ_STATE_FD;
               ret.fd = sync_merge_close("tu_sync", ret_fd, sync_fd, true);"""),
    ("""            ret.state = KGSL_SYNCOBJ_STATE_FD;
            int sync_fd = kgsl_syncobj_ts_to_fd(sync);
            ret.fd = sync_merge_close("tu_sync", ret.fd, sync_fd, true);""",
     """            int ret_fd = kgsl_syncobj_ts_to_fd(&ret);
            ret.state = KGSL_SYNCOBJ_STATE_FD;
            ret.fd = sync_merge_close("tu_sync", ret_fd, sync->fd, false);"""),
)
for before, after in replacements:
    if after not in source:
        if source.count(before) != 1:
            raise SystemExit("Unsupported Mesa KGSL timestamp/FD merge")
        source = source.replace(before, after)
target.write_text(source)

# Some Horizon KGSL kernels reject the legacy allocation ioctl while exposing
# the GPUOBJ interface used by Android's driver. Preserve the exact size and
# cache/access flags; never reinterpret a genuine ENOMEM as API incompatibility.
source = target.read_text()
helper = """static int
quest_kgsl_free(int fd, unsigned int id)
{
   struct kgsl_gpumem_free_id legacy = { .id = id };
   int ret = safe_ioctl(fd, IOCTL_KGSL_GPUMEM_FREE_ID, &legacy);
   if (!ret || (errno != EACCES && errno != EPERM &&
                errno != ENOTTY))
      return ret;
   struct kgsl_gpuobj_free modern = { .id = id };
   return safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_FREE, &modern);
}

static int
quest_kgsl_allocate(int fd, struct kgsl_gpumem_alloc_id *request)
{
   int ret = safe_ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, request);
   if (!ret || (errno != EACCES && errno != EPERM &&
                errno != ENOTTY))
      return ret;

   struct kgsl_gpuobj_alloc allocation = {
      .size = request->size,
      .flags = request->flags,
   };
   ret = safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_ALLOC, &allocation);
   if (ret)
      return ret;

   struct kgsl_gpuobj_info info = { .id = allocation.id };
   ret = safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_INFO, &info);
   /* Cache modes are a bitfield, not independent capability bits. */
   if (!ret && (((info.flags & request->flags) != request->flags) ||
                ((info.flags ^ request->flags) & KGSL_CACHEMODE_MASK))) {
      errno = EINVAL;
      ret = -1;
   }
   if (ret) {
      const int error = errno;
      struct kgsl_gpuobj_free release = { .id = allocation.id };
      safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_FREE, &release);
      errno = error;
      return ret;
   }
   request->id = allocation.id;
   request->gpuaddr = info.gpuaddr;
   request->mmapsize = allocation.mmapsize;
   request->size = info.size;
   request->flags = info.flags;
   return 0;
}

"""
if helper not in source:
    anchor = "static VkResult\nkgsl_bo_init("
    if source.count(anchor) != 1:
        raise SystemExit("Unsupported Mesa KGSL BO allocation")
    source = source.replace(anchor, helper + anchor)
    before = """   ret = safe_ioctl(dev->physical_device->local_fd,
                    IOCTL_KGSL_GPUMEM_ALLOC_ID, &req);"""
    if source.count(before) != 1:
        raise SystemExit("Unsupported Mesa KGSL legacy BO ioctl")
    source = source.replace(before, "   ret = quest_kgsl_allocate(dev->physical_device->local_fd, &req);")
    before_free = "   safe_ioctl(dev->physical_device->local_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);"
    if source.count(before_free) != 1:
        raise SystemExit("Unsupported Mesa KGSL BO release")
    source = source.replace(before_free, "   quest_kgsl_free(dev->physical_device->local_fd, req.id);")
    source = source.replace('"GPUMEM_ALLOC_ID failed (%s)", strerror(errno)',
                            '"KGSL allocation failed: size=%llu flags=%x errno=%d (%s)",\n'
                            '                       (unsigned long long) size, req.flags, errno, strerror(errno)')
target.write_text(source)

# Allocation alone is insufficient: denied legacy capability probes hide the
# cached/coherent memory types required by SteamVR. Use the same strict fallback
# for probes, retaining the kernel's actual flags and releasing every test BO.
# Probe fallback identified by leplayitisplayiting-bot/stearlight3patch:
# https://github.com/leplayitisplayiting-bot/stearlight3patch/commit/7117fc451c7b2e1174822306b340bb17a3aee5af
source = target.read_text()
before_probe = "   int ret = safe_ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &req_alloc);"
after_probe = "   int ret = quest_kgsl_allocate(fd, &req_alloc);"
if after_probe not in source:
    if source.count(before_probe) != 1:
        raise SystemExit("Unsupported Mesa KGSL memory capability probe")
    source = source.replace(before_probe, after_probe)
    before_release = """   struct kgsl_gpumem_free_id req_free = { .id = req_alloc.id };

   safe_ioctl(fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req_free);"""
    if source.count(before_release) != 1:
        raise SystemExit("Unsupported Mesa KGSL memory probe release")
    source = source.replace(before_release, "   quest_kgsl_free(fd, req_alloc.id);")
    # Cache modes are encoded values: zero-valued write-combine must not accept
    # a silently substituted write-back mode just because no bits were requested.
    source = source.replace("   bool supported = (req_alloc.flags & flags) == flags;",
                            "   bool supported = (req_alloc.flags & flags) == flags &&\n"
                            "                    !((req_alloc.flags ^ flags) & KGSL_CACHEMODE_MASK);")
    target.write_text(source)

# Mesa a70d2af590db192f87b3af01f83a68b450edb4c3 (26.2): Adreno 630/650
# can hang with D32S8, fragment discard and combined early/late depth tests.
# Keep the upstream workaround while this payload uses Mesa 26.1.6.
command_buffer = args.mesa / "src/freedreno/vulkan/tu_cmd_buffer.cc"
source = command_buffer.read_text()
before = "   if ((stencil_test_enable && depth_format == VK_FORMAT_S8_UINT) ||"
workaround = """   /* A630/A650 hangs with this combination of states. */
   if (CHIP == A6XX && zmode == A6XX_EARLY_Z_LATE_Z && depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT &&
       fs_kill_fragments)
      zmode = A6XX_LATE_Z;

"""
if workaround not in source:
    if source.count(before) != 1:
        raise SystemExit("Unsupported Mesa A6xx depth/stencil state")
    command_buffer.write_text(source.replace(before, workaround + before))

# XPutImage software transfers do not produce the Present completion events
# used by x11_wait_for_present(). Advertising PresentWait2 makes DXVK's frame
# latency thread wait forever after the first frames. Let it use GPU fences.
wsi = args.mesa / "src/vulkan/wsi/wsi_common_x11.c"
source = wsi.read_text()
before = "         pwait2->presentWait2Supported = VK_TRUE;"
after = "         pwait2->presentWait2Supported = !wsi_device->sw;"
if after not in source:
    if source.count(before) != 1:
        raise SystemExit("Unsupported Mesa software X11 presentation capabilities")
    wsi.write_text(source.replace(before, after))

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
