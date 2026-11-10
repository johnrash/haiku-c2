<!--
Copyright 2026, haiku-odroid-c2 port authors.
Distributed under the terms of the MIT License.
-->

# M43: Mesa's lima driver on Haiku (Mali-450 MP3), userland design study

> **Status:** Design study before M43's userland. As built: the device is `/dev/misc/mali/0`, the driver is "Lima Pipe" (package `mesa_lima`), buffers are cached and cleaned around jobs, quads draw correctly without P7, and a presenter thread overlaps the readback (M55). Overlapping GP and PP jobs was tried in M61 and not adopted (`patches/haiku/meson_mali-gp-pp-overlap.patch`).

> Facts read from Mesa 22.0.5 as we build it: `deps/build/mesa-22.0.5` (the release tarball, HaikuPorts' `mesa-22.0.5.patchset` and `tools/patches/mesa-22.0.5-bitmap-stride.patch`), on 2026-10-31. Every `file:line` is in that tree unless it names `haiku/` or `docs/milestones.md`. The Linux lima kernel source is not on this machine. What the kernel must do is inferred from the uapi header and from how Mesa uses it, and marked **UNVERIFIED** where it matters. Nothing was downloaded. The Mesa tree was not changed; a trial build ran in a scratch directory (§1.6).

This is the input for the userland half of hardware OpenGL on the C2: Mesa's lima Gallium driver, a small libdrm replacement, and a second BGLRenderer add-on. The kernel driver is a separate piece of work. §1.8 is the contract it has to meet.

---

## 0. Summary

- lima talks to the kernel through 12 libdrm functions and 20 ioctl numbers; 2 of those are only in renderonly code it never reaches (§1.1, §1.2). There is no `drmCommandWrite*`: lima calls `drmIoctl()` directly.
- A minimal kernel needs 7 lima ioctls (2 of them trivial), 5 core DRM ioctls (`VERSION`, `GEM_CLOSE`, 3 syncobj ones) and a way to map buffers. Prime, flink, sync files and device enumeration can be stubbed or emulated in userland (§1.7).
- lima maps buffers with `mmap()` on the DRM fd. Haiku can't map a device fd (§1.3). That costs one small Mesa patch.
- lima exposes OpenGL 2.1, GLSL 1.20 and OpenGL ES 2.0. Legacy GL 1.x runs through Mesa's fixed-function emulation, with gaps: no `glPolygonMode` lines, no flat shading, occlusion queries that always return 0, no 32-bit float textures, 4096x4096 textures at most (§2.3, §2.4).
- The hgl frontend is not tied to software rendering. It only needs a present hook (`flush_frontbuffer`) that lima lacks, and a pipe context passed to it (§3.2).
- Recommended: a static libdrm shim (ours, MIT) found through `libdrm.pc`, `-Dgallium-drivers=swrast,lima`, six small Mesa patches and a new target `haiku-lima` that reads each finished frame back into a BBitmap (§3.3 to §3.6).
- A trial configure and compile for Haiku arm64 needed no meson change. Only 2 of lima's 42 C files failed, both with one-line causes (§1.6).
- First test without any kernel driver: the shim's built-in no-op backend runs the whole stack, shader compilers included. First test on hardware: clear a render target and read it back (§4).

---

## 1. What lima needs from the kernel and from libdrm

### 1.1 libdrm functions

lima includes `xf86drm.h` in `lima_bo.c:29`, `lima_screen.c:45` and `lima_context.c:43`. The functions it calls:

| Function | Called at | What for | Haiku v1 |
|---|---|---|---|
| `drmIoctl` | `lima_bo.c:91, 135, 302, 375, 487, 541`; `lima_context.c:89, 103`; `lima_screen.c:508, 522`; `lima_job.c:233` | every lima and GEM ioctl | pass-through to Haiku's `ioctl(fd, req, arg, _IOC_SIZE(req))`, retried on EINTR and EAGAIN as libdrm does |
| `drmGetVersion`, `drmFreeVersion` | `lima_screen.c:492, 499` | a version above 1.0 turns on the growable heap buffer (`lima_screen.c:496-497`) | `DRM_IOCTL_VERSION`; the kernel answers 1.0 |
| `drmGetDevice2`, `drmFreeDevice` | `lima_screen.c:473, 484` | only to spot `allwinner,sun50i-h5-mali` (`:480-481`). The caller ignores failure (`:527`); Mali-450 has its value set before (`:466-467`) | stub: return an error |
| `drmPrimeHandleToFD` | `lima_bo.c:398` | export a dma-buf (`WINSYS_HANDLE_TYPE_FD`) | stub; hgl never asks |
| `drmPrimeFDToHandle` | `lima_bo.c:429` | import a dma-buf; the size comes from `lseek()` on it (`:435-441`) | stub |
| `drmSyncobjCreate` | `lima_job.c:1142-1143` | 2 in and 2 out syncobjs per context, created signalled | kernel |
| `drmSyncobjDestroy` | `lima_job.c:1160, 1162` | context teardown | kernel |
| `drmSyncobjWait` | `lima_job.c:250` | only from `lima_job_wait()`, which only runs with `LIMA_DEBUG=dump` (`:950-962`, `:1031-1036`) | kernel; the shim also uses it (below) |
| `drmSyncobjExportSyncFile` | `lima_job.c:1109` | a fence for `pipe->flush(&fence)`: glFinish, glFenceSync | shim emulation (§1.4) |
| `drmSyncobjImportSyncFile` | `lima_job.c:223` | `fence_server_sync` (glWaitSync) | shim: no-op (§1.4) |

Code that is compiled but never reached from hgl:

| Function | Where | When it runs |
|---|---|---|
| `drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB / DESTROY_DUMB)`, `drmPrimeHandleToFD`, `drmPrimeFDToHandle` | `gallium/auxiliary/renderonly/renderonly.c:48, 72, 90, 101, 134` | only when the screen has a `renderonly` (a separate display device). Our screen has none. The file is built into libgallium as soon as libdrm is found (`gallium/auxiliary/meson.build:332-337`), and lima references its functions directly (`lima_resource.c:67, 78, 278, 390, 420`), so they must link. |
| `drmOpenWithType("lima", NULL, DRM_NODE_RENDER)` | `gallium/winsys/kmsro/drm/kmsro_drm_winsys.c:116` | DRI only. kmsro is compiled whenever lima is enabled (`meson.build:235`, `gallium/meson.build:110-114`), so the declaration must exist. It is not linked into our target. |

src/loader, the pipe-loader and the DRI frontend are not built for Haiku (`build_by_default : false`, `loader/meson.build`, `pipe-loader/meson.build:54, 73`). They add nothing.

### 1.2 DRM ioctls

Numbers use Linux's `_IOC` encoding on a 64-bit system (struct `drm_version` holds pointers). They were computed from Mesa's own `include/drm-uapi/drm.h` and `lima_drm.h`. If our `sys/ioccom.h` uses the same encoding (§3.5), the numbers on Haiku are these.

| Ioctl | Number | Size | Issued from | Use | v1 |
|---|---|---|---|---|---|
| `DRM_IOCTL_LIMA_GET_PARAM` | 0xC0106440 | 16 | `lima_screen.c:508, 522` | `GPU_ID` (must be `MALI450` = 2, else the screen fails, `:511-518`) and `NUM_PP` (3 on this SoC). `GP_VERSION` and `PP_VERSION` are never asked. | must |
| `DRM_IOCTL_LIMA_GEM_CREATE` | 0xC0106441 | 16 | `lima_bo.c:302` | size (aligned to 4 KiB, `:284`), flags 0 or `LIMA_BO_FLAG_HEAP` (only with version > 1.0, `lima_context.c:246-256`) | must |
| `DRM_IOCTL_LIMA_GEM_INFO` | 0xC0106442 | 16 | `lima_bo.c:135` | GPU virtual address `va` and the mmap `offset` | must |
| `DRM_IOCTL_LIMA_GEM_SUBMIT` | 0x40306443 | 48 | `lima_job.c:233` | one GP or PP task (§1.4, §1.8) | must |
| `DRM_IOCTL_LIMA_GEM_WAIT` | 0x40106444 | 16 | `lima_bo.c:541` | CPU waits on a buffer (§1.4) | must |
| `DRM_IOCTL_LIMA_CTX_CREATE` | 0x80086445 | 8 | `lima_context.c:89` | a scheduling context id | trivial |
| `DRM_IOCTL_LIMA_CTX_FREE` | 0x40086446 | 8 | `lima_context.c:103` | | trivial |
| `DRM_IOCTL_VERSION` | 0xC0406400 | 64 | `drmGetVersion` | name and version | must (or synthesise in the shim) |
| `DRM_IOCTL_GEM_CLOSE` | 0x40086409 | 8 | `lima_bo.c:91` | free a handle | must |
| `DRM_IOCTL_SYNCOBJ_CREATE` | 0xC00864BF | 8 | `drmSyncobjCreate` | flag `DRM_SYNCOBJ_CREATE_SIGNALED` (1) | must |
| `DRM_IOCTL_SYNCOBJ_DESTROY` | 0xC00864C0 | 8 | `drmSyncobjDestroy` | | must |
| `DRM_IOCTL_SYNCOBJ_WAIT` | 0xC02064C3 | 32 | `drmSyncobjWait` | one handle, flags 0, absolute timeout | must (the shim's fence emulation needs it) |
| `DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD` | 0xC01064C1 | 16 | `drmSyncobjExportSyncFile` | flag `EXPORT_SYNC_FILE` | emulated in the shim |
| `DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE` | 0xC01064C2 | 16 | `drmSyncobjImportSyncFile` | flag `IMPORT_SYNC_FILE` | emulated in the shim |
| `DRM_IOCTL_GEM_FLINK` | 0xC008640A | 8 | `lima_bo.c:375` | `WINSYS_HANDLE_TYPE_SHARED` export | stub (EINVAL) |
| `DRM_IOCTL_GEM_OPEN` | 0xC010640B | 16 | `lima_bo.c:487` | `WINSYS_HANDLE_TYPE_SHARED` import | stub |
| `DRM_IOCTL_PRIME_HANDLE_TO_FD` | 0xC00C642D | 12 | `drmPrimeHandleToFD` | dma-buf export | stub in the shim |
| `DRM_IOCTL_PRIME_FD_TO_HANDLE` | 0xC00C642E | 12 | `drmPrimeFDToHandle` | dma-buf import | stub in the shim |
| `DRM_IOCTL_MODE_CREATE_DUMB` | 0xC02064B2 | 32 | `renderonly.c:72` | scanout buffers | never reached |
| `DRM_IOCTL_MODE_DESTROY_DUMB` | 0xC00464B4 | 4 | `renderonly.c:48, 101` | | never reached |

Frame sizes passed in `GEM_SUBMIT.frame_size`: GP 24 bytes (`struct drm_lima_gp_frame`), PP 312 bytes (`struct drm_lima_m450_pp_frame`; 272 for Mali-400). The register layouts behind them are in `lima_gpu.h:34-82` (`lima_gp_frame_reg`, `lima_pp_frame_reg`, `lima_pp_wb_reg`), filled in `lima_do_job()` (`lima_job.c:896-1056`).

Mesa's own drm-shim (`src/gallium/drivers/lima/drm-shim/lima_noop.c`, `src/drm-shim/device.c:236-245`) confirms the minimal set. It runs lima on Linux with no GPU. It implements `GET_PARAM`, `GEM_CREATE` and `GEM_INFO`, makes `GEM_SUBMIT`, `GEM_WAIT` and the context ioctls no-ops, and handles `VERSION`, `GET_CAP`, `GEM_CLOSE` and `SYNCOBJ_CREATE`, with the other syncobj ioctls stubbed. It reports version 1.1 and 6 PP cores.

### 1.3 Mapping the DRM fd

There is one mapping site:

- `lima_bo.c:344`: `os_mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, bo->screen->fd, bo->offset)`. `os_mmap` is plain `mmap()` (`gallium/auxiliary/os/os_mman.h`).
- `lima_bo.c:356`: `os_munmap(bo->map, bo->size)`.
- `bo->offset` is `GEM_INFO.offset` (`lima_bo.c:138`), read for every created buffer (`:313`) and import (`:505`). `bo->size` is the size `GEM_CREATE` returned (`:306`).
- Maps are made on first use and kept for the buffer's life, also while it sits in the buffer cache. They go at `lima_bo_free()` (`:111-112`).
- Almost every buffer gets mapped: the screen's `pp_buffer` at screen creation (`lima_screen.c:704-735`), the context's PLB stream (`lima_context.c:274`), command streams, shader code and every transfer.

On Haiku, `mmap()` of a device fd can't map buffer memory. `_vm_map_file()` needs the vnode's file cache and asks for it without creating one (`haiku/src/system/kernel/vm/vm.cpp:2363-2365`, "this only works for file systems that use the file cache"). devfs nodes have none.

So `lima_bo_map()` and `lima_bo_unmap()` need a Haiku branch (patch P1, §3.5). The proposal follows the house pattern of `meson_vdec` and `meson_hdmi`: the kernel keeps each buffer in a cloneable area, `GEM_INFO.offset` carries its `area_id`, and the shim clones it. `meson_hdmi.cpp:1855-1866` notes that clones of a device area keep its memory type, which gives userland a write-combining view.

CPU caching is a kernel decision. Reads from write-combining memory bypass the caches, and the present path (§3.4) reads every frame. **UNVERIFIED**: Linux lima is said to map buffers write-combining; its source is not here to check. Stage 1 measures the readback (§4).

### 1.4 Job completion: syncobjs, sync files and buffer waits

How lima orders work:

- **Per context:** `in_sync[2]` and `out_sync[2]`, one pair per pipe (GP, PP), created signalled (`lima_job.c:1142-1143`).
- **Each submit** names `out_sync = ctx->out_sync[pipe]` (`lima_job.c:219`). The kernel attaches the task's completion to that syncobj.
- **`in_sync[0]`** is set only when `ctx->in_sync_fd >= 0` (`lima_job.c:222-231`), which only `fence_server_sync` (glWaitSync) causes. The sync file is imported into `in_sync[pipe]` and closed.
- **GP before PP** has no explicit sync. A flush submits the GP task, then the PP task (`lima_job.c:948`, `:997` or `:1028`), with `flags = 0`, so not `LIMA_SUBMIT_FLAG_EXPLICIT_FENCE`. The PP depends on the GP through implicit buffer fences: the GP task lists the PLB and tile heap as `WRITE`, the PP task lists them as `READ` (`lima_update_job_bo()`, `lima_job.c:746-769`). The kernel must make the PP wait.
- **Buffer lifetime:** right after the submit ioctl, lima drops its references to the job's buffers (`lima_job.c:235-237`). A buffer can be closed with `GEM_CLOSE` while the GPU still uses it. The kernel must keep it alive until the task ends.

How the CPU waits:

- **`GEM_WAIT`** is the main path (`lima_bo.c:523-541`). `op` READ means "I want to read": wait for GPU writers. WRITE means "I want to write": wait for all GPU users. `timeout_ns` is an absolute `CLOCK_MONOTONIC` time (`os_time_get_absolute_timeout`); `INT64_MAX` is forever and 0 is a poll. Callers:
  - `lima_transfer_map()`, after flushing jobs that touch the buffer (`lima_resource.c:650-654`). glReadPixels and our present path go through here.
  - `lima_texture_subdata()` (`lima_resource.c:911-912`).
  - The buffer cache, polling with timeout 0 to see whether a cached buffer is idle (`lima_bo.c:253`).
- **Fences** are sync file fds. `pipe->flush(&fence)` exports `out_sync[PP]` (`lima_job.c:1109`). `fence_finish` is `sync_wait()`, which is `poll()` on the fd (`lima_fence.c:112`, `util/libsync.h:102-129`). `fence_get_fd` dups it. glFinish takes this path (`state_tracker/st_cb_flush.c:68-81`). If the export fails, lima leaves `*fence` NULL and glFinish returns without waiting.
- **`drmSyncobjWait`** is only used for the debug dump (§1.1).

Emulation in the shim, so the kernel needs no sync files:

- `drmSyncobjExportSyncFile`: wait on the syncobj with `SYNCOBJ_WAIT` (forever), then return the read end of a pipe holding one byte. `poll()` finds it readable at once, so glFinish keeps its meaning. It waits at flush instead of at `fence_finish`.
- `drmSyncobjImportSyncFile`: return 0 and do nothing. Every fence the shim hands out is already signalled.
- `sync_merge` (`libsync.h:132-150`) fails on a pipe. `sync_accumulate` then keeps the first fd (`:179-200`) and lima ignores the error. That is harmless for signalled fences.

### 1.5 Other OS services lima and its helpers use

| Service | Where | Haiku |
|---|---|---|
| `fcntl(F_DUPFD_CLOEXEC)` | `os_dupfd_cloexec` (`util/os_file.c:51-54`), used by the winsys (`lima_drm_winsys.c:83`) and fences | defined (`haiku/headers/posix/fcntl.h:23`) |
| Screen sharing per fd | `util_hash_table_create_fd_keys` and `os_same_file_description` | the non-Linux version only matches identical fd numbers (`util/os_file.c:216-226`). Fine: one screen per team. |
| `poll()` on a fence fd | `util/libsync.h:113` | works on pipes |
| `clock_gettime(CLOCK_MONOTONIC)` | buffer cache (`lima_bo.c:220`), timeouts | Haiku's monotonic clock is `system_time()`. The kernel must compare `timeout_ns` with `system_time() * 1000`. |
| `os_get_total_physical_memory` | PP stream cache size, 0.1% of RAM (`lima_screen.c:671-677`) | Haiku branch exists (`util/os_misc.c`) |
| Shader disk cache | `lima_disk_cache.c` | needs patch P2 (§1.6) |
| Environment | `LIMA_DEBUG` (gp, pp, dump, nobocache, notiling, nogrowheap, singlejob, ...), `LIMA_CTX_NUM_PLB`, `LIMA_PLB_MAX_BLK`, `LIMA_PPIR_FORCE_SPILLING`, `LIMA_PLB_PP_STREAM_CACHE_SIZE` (`lima_screen.c:582-645`) | as on Linux |
| errno | `lima_context_create_drm_ctx()` returns `errno` on failure and the caller tests `< 0` (`lima_context.c:91, 212-213`) | Haiku's errno values are negative, so the test works here |

### 1.6 What a Haiku build needs at compile time (trial build)

A trial ran in a scratch directory: `meson setup` with our cross file, `-Dgallium-drivers=swrast,lima -Dllvm=disabled`, and a header-only libdrm (a `libdrm.pc` with version 2.4.109, `xf86drm.h`, `sys/ioccom.h`) on the global include path. Results:

- **Configure:** passes with no meson change. `libdrm found: YES 2.4.109`, `Gallium drivers: swrast lima`. Haiku is never refused: the only kmsro check is for X11 (`meson.build:610-611`). libdrm is optional on Haiku (`meson.build:1608-1612`); once it is found, `HAVE_LIBDRM` is defined everywhere.
- **Compiled cleanly:** liblimawinsys, libpanfrost_shared (lima uses its tiling code), libkmsrowinsys, `renderonly.c` and `mesa/main/externalobjects.c`, and 40 of lima's 42 C files.
- **Failure 1, `lima_fence.c`:** `util/libsync.h:97-98` uses `_IOWR`, which Haiku's `<sys/ioctl.h>` lacks. The file compiled once `<sys/ioccom.h>` was included first. That is patch P3.
- **Failure 2, `lima_disk_cache.c:195-198`:** it calls `build_id_find_nhdr_for_addr()`, which exists only with `HAVE_DL_ITERATE_PHDR`. Meson's check says no (Haiku has `dl_iterate_phdr` in libbsd, not libroot). Patch P2 uses `disk_cache_get_function_identifier()` instead (`util/disk_cache.h:120-136`). It falls back to `dladdr()` and the file's mtime, as llvmpipe does (`lp_screen.c:933`).
- **Global include path:** `mesa/main/externalobjects.c` includes `drm-uapi/drm_fourcc.h` under `HAVE_LIBDRM` (`:46-48`), and libmesa doesn't use the libdrm dependency. Mesa's `drm.h` includes `<sys/ioccom.h>` on every non-Linux system (`include/drm-uapi/drm.h:46-48`). So the shim's include directory must go into the cross file's `c_args` and `cpp_args`, not just into `libdrm.pc`.
- **What the shim's `xf86drm.h` must declare:** `DRM_CLOEXEC`, `DRM_NODE_RENDER`, `DRM_BUS_PLATFORM`, `drmVersion`, and `drmDevice` with `bustype` and `deviceinfo.platform->compatible`, plus the 13 functions of §1.1 (with `drmOpenWithType`). It can include `<drm-uapi/drm.h>` from Mesa, so there is only one copy of the uapi.
- **Not tried:** linking. The shim had no code and there was no target yet.

### 1.7 Must provide, can stub

| Item | Kernel v1 | Shim v1 |
|---|---|---|
| `LIMA_GET_PARAM` (GPU_ID, NUM_PP) | yes | pass-through |
| `LIMA_GEM_CREATE`, `LIMA_GEM_INFO`, `GEM_CLOSE` | yes | pass-through |
| Buffer mapping | cloneable area per buffer, id in `GEM_INFO.offset` | `drmHaikuMap()` / `drmHaikuUnmap()` (clone, delete), called from patched `lima_bo_map()` |
| `LIMA_GEM_SUBMIT` (GP, PP for Mali-450, both with and without DLBU) | yes | pass-through |
| `LIMA_GEM_WAIT` | yes | pass-through |
| `LIMA_CTX_CREATE`, `LIMA_CTX_FREE` | return an id, nothing else | pass-through |
| `VERSION` | 1.1 ("lima"): growable heaps (1.0 until patch 0188) | `drmGetVersion` in two calls, as libdrm does |
| `SYNCOBJ_CREATE`, `DESTROY`, `WAIT` | yes | pass-through |
| Sync file export and import | no | emulated (§1.4) |
| `drmGetDevice2` | no | returns an error |
| Prime, `GEM_FLINK`, `GEM_OPEN` | EINVAL | Prime functions return an error |
| `MODE_*_DUMB`, `drmOpenWithType` | no | declared; `drmOpenWithType` returns -1 |
| Growable heap (`LIMA_BO_FLAG_HEAP`, version 1.1) | yes (patch 0188): 64 KiB at first, doubled on PLBU_OUT_OF_MEM | — |

**Upstream libdrm** is not needed. It is not on this machine, not in our repository and not in the bootstrap packages. Whether HaikuPorts has a recipe can't be checked offline. Either way, libdrm's core finds devices through Linux and BSD device numbers and sysfs, so a port would still need glue to reach our driver. A 300-line shim is simpler.

### 1.8 The contract for the kernel driver (what Mesa relies on)

1. **Device node:** one node, opened `O_RDWR`. Keep it out of `/dev/graphics`, which app_server opens and probes for accelerants (`haiku/src/servers/app/drawing/interface/local/AccelerantHWInterface.cpp:273`). Proposal: `/dev/misc/lima/0`.
2. **One open file description = one DRM file.** Buffer handles, syncobj handles, context ids and the GPU address space belong to it. lima dups the fd (`lima_drm_winsys.c:83`). Dups share the cookie, as on Linux.
3. **ioctl numbers and structures** exactly as in Mesa's `include/drm-uapi/lima_drm.h` and `drm.h` (MIT), with Linux's `_IOC` encoding. The shim stays a pass-through, and Mesa's drm-shim and lima code serve as the reference. All numbers are above `B_DEVICE_OP_CODES_END`. Nested pointers (`submit.bos`, `submit.frame`, `version.name`) need `user_memcpy`.
4. **GPU addresses** (`GEM_INFO.va`) are 32-bit and stay fixed for a buffer's life. lima writes them into other buffers (`lima_screen.c:735`, `lima_context.c:276-281`). v1 may use one address space for everything; a per-file space is later work.
5. **Submit:** copy the frame (24 or 312 bytes), take references on the listed buffers, and attach completion to `out_sync`. Honour `in_sync[0]` when it is non-zero; it will always be signalled with the shim above. Running tasks strictly in submission order across GP and PP meets the implicit GP→PP dependency (§1.4) without tracking buffer fences. It gives up the GP/PP overlap two PLBs allow (`LIMA_CTX_PLB_DEF_NUM` = 2, `lima_context.h:240`).
6. **Buffer waits:** track, per buffer, the last task that reads and the last that writes it. WRITE waits for both, READ for the writer. Timeouts are absolute (§1.5).
7. **A bad command stream hangs the GP** (Mesa's own comment, `lima_draw.c:1158-1159`). The kernel needs a task timeout and a GPU reset. Otherwise an application waiting with `INT64_MAX` waits forever.
8. **Memory per context** (Mali-450, version 1.0): 2 PLBs of 2 MiB (`plb_max_blk` 4096 × 512 bytes, `lima_screen.c:466-467`, `lima_context.c:242`), 2 fixed tile heaps of 1 MiB (`lima_context.c:255`), a 32 KiB PLB stream, plus command streams and the PP stream cache. Each GL context costs about 6 MiB before textures.
9. **Mali-450 PP tasks** come in two forms. With full-screen damage, lima sets `use_dlbu` and `dlbu_regs` (`lima_job.c:1015-1022`). Otherwise it gives per-core `plbu_array_address` (`:1010-1013`). The kernel must handle both, with `num_pp` cores.

---

## 2. Screen creation and the GL that lima offers

### 2.1 Creating the screen

- **`lima_drm_screen_create(int fd)`** (`lima_drm_winsys.c:67-101`) takes an fd open on the DRM device. It looks the fd up in a table so one fd gives one screen. A new screen gets `os_dupfd_cloexec(fd)`: lima keeps its own dup and closes it when the last reference goes (`:41-65`). The caller can close its fd afterwards.
- It calls **`lima_screen_create(dup_fd, NULL)`** (`lima_screen.c:655-769`). The second argument is a `struct renderonly`, used on Linux when a separate display controller allocates scanout buffers (kmsro). With NULL, lima allocates every buffer itself, and the renderonly code is never reached (`lima_resource.c:219-223`).
- `lima_drm_screen_create_renderonly()` (`:104-107`) is the kmsro path. We don't need it.
- The Haiku target calls `lima_drm_screen_create()` directly, like Mesa's `pipe_lima_create_screen()` (`gallium/auxiliary/target-helpers/drm_helper.h:389-395`).

### 2.2 What happens at screen and context creation

Screen (`lima_screen_create`):

1. `DRM_IOCTL_VERSION` (growable heap if > 1.0).
2. `GET_PARAM GPU_ID` and `NUM_PP`, then `drmGetDevice2`. `plb_max_blk` is 4096 for Mali-450.
3. Buffer cache and handle tables.
4. PP register allocator.
5. A 4 KiB `pp_buffer`: `GEM_CREATE`, `GEM_INFO`, map, then the built-in clear and reload programs, the index list and the frame render state written into it (`:692-736`).

Context (`lima_context_create`, `lima_context.c:203-300`):

1. `CTX_CREATE`.
2. The upload manager.
3. 2 PLBs and 2 tile heaps (§1.8 item 8).
4. The PLB stream, mapped and filled with PLB addresses.
5. 4 `SYNCOBJ_CREATE`s (`lima_job_init`, `lima_job.c:1126-1150`).

A clear-only frame needs no shader compile. `pp_clear_program` is already in `pp_buffer`. It still runs a GP task (PLBU commands) and a PP task. That makes the clear the smallest end-to-end hardware test.

### 2.3 GL version and extensions

lima's caps (`lima_screen_get_param`, `lima_screen.c:96-176`) and the defaults it falls back to (`gallium/auxiliary/util/u_screen.c`) give:

| Requirement (`main/version.c:254-276`) | Comes from | Result |
|---|---|---|
| 1.4: ARB_shadow | `PIPE_CAP_TEXTURE_SHADOW_MAP`, default 1 (`u_screen.c:421-422`) | yes, but lima has no depth-compare sampling anywhere: **UNVERIFIED** whether shadow lookups work |
| 1.5: ARB_occlusion_query | always on (`main/extensions.c:279`) | yes; lima's queries always return 0 (`lima_query.c:70-78`) |
| 2.0: point sprites, NPOT, separate blend equation | lima returns 1 (`:100-113`). Point sprites: "Unimplemented, but for exporting OpenGL 2.0" (`:111`) | yes |
| 2.0: vertex and fragment shaders, two-sided stencil | always on (`extensions.c:274-288`) | yes |
| 2.1: PBO, sRGB textures | PBO always; sRGB because lima samples `R8G8B8A8_SRGB` (`lima_format.c:102`) | **OpenGL 2.1** |
| 3.0: GLSL 1.30, float textures, ... | `PIPE_CAP_GLSL_FEATURE_LEVEL` default 120 (`u_screen.c:117-120`) | no |

So: **OpenGL 2.1 (compatibility), GLSL 1.20, OpenGL ES 2.0.** GLInfo should show renderer "Mali450" and vendor "lima" (`lima_screen.c:69-88`).

Other limits:

- **Textures:** 4096x4096 at most (`1 << (LIMA_MAX_MIP_LEVELS - 1)`, `lima_screen.c:130-131`, `lima_resource.h:31`). Renderbuffers and viewports share that limit.
- **NPOT:** full NPOT is claimed (`:100`).
- **Float textures:** only 16-bit float texel formats (`lima_format.c:122-127`). No `R32G32B32A32_FLOAT`, so no OES_texture_float and no ARB_texture_float (`st_extensions.c:895-896, 1673-1675`).
- **Render targets:** BGRA/RGBA 8888, 565, 5551, 4444, R8, RG8, half float (`lima_format.c:130-148`). One colour buffer (`PIPE_CAP_MAX_RENDER_TARGETS` default 1). 4x MSAA (`lima_screen.c:341-343`).
- **Depth:** Z16, Z24S8, Z24X8 (`:354-363`).
- **Shaders:** vertex constants 304 vec4 (`:225-228`); fragment constants 2048 vec4 (`:264-269`); 16 samplers in the fragment shader and none in the vertex shader; 12 varyings plus position (`lima_context.h:71`).
- **Precision:** the Mali-400/450 fragment processor computes in FP16 (a known hardware property; lima's comment on precision loss, `lima_screen.c:163-166`). Large texture coordinates lose precision.
- **Lines and points:** width and size up to 100 (`:190-194`).

### 2.4 Legacy GL 1.x on lima

Mesa's state tracker turns fixed-function GL into shaders (ffvertex_prog, ff_fragment_shader) and immediate mode into vertex buffers (vbo). For lima those are ordinary NIR shaders compiled by gpir (vertex) and ppir (fragment). Legacy programs run, apart from what lima doesn't implement:

| Feature | lima 22.0.5 | Effect |
|---|---|---|
| Immediate mode, display lists, lighting, fog, texture environment | shaders through the state tracker | works, if the generated vertex shader fits the GP (**UNVERIFIED** for large light setups) |
| `glShadeModel(GL_FLAT)` | `PIPE_CAP_FLATSHADE` 0 (`:155-158`), so the state tracker runs `nir_lower_flatshade` (`st_context.c:604-605`, `st_program.c:1354-1355`). That only marks colour inputs flat. lima never looks at interpolation modes: ppir reads plain `load_input` (`ir/pp/nir.c:271`), and nothing in `ir/`, `lima_program.c` or `lima_draw.c` mentions "interp" or "flat". | renders smooth |
| `glPolygonMode(GL_LINE / GL_POINT)` | the state tracker passes `fill_front`/`fill_back` (`st_atom_rasterizer.c:133-148`); lima never reads them (rasterizer use: `lima_draw.c:60, 103, 364-365, 410, 577-581, 631-636, 722`) | draws filled |
| Polygon stipple | `lima_set_polygon_stipple()` is empty (`lima_state.c:60-63`) | ignored |
| User clip planes | `PIPE_CAP_CLIP_PLANES` 0; the state tracker writes clip distances (`st_program.c:720-750`); lima has no clip-distance handling | **UNVERIFIED**: probably no clipping |
| Two-sided lighting | lowered with `gl_FrontFacing` (`st_program.c:1366-1368`); lima has the face sysval (`:122-125`) | should work |
| Alpha test | native (`:152-153`) | works |
| Occlusion queries | result always 0 (`lima_query.c:75`) | programs that cull with them draw nothing |
| `GL_QUADS`, `GL_QUAD_STRIP`, `GL_POLYGON` | lima keeps the default `PIPE_CAP_SUPPORTED_PRIM_MODES` (all, `u_screen.c:91-93`), so the modes reach lima unchanged. lima puts the mode number (7, 8, 9) straight into the PLBU draw command (`lima_draw.c:423, 429`, `lima_gpu.h:152-156`). | **UNVERIFIED** whether the PLBU draws them. GLES has no quads, and lima's CI runs dEQP-GLES2 only (`ci/lima-fails.txt`). If it doesn't: patch P7 returns a mask without them, and `u_vbuf` converts them to triangles in 22.0.5 (`u_vbuf.c:329-331, 1774`). |
| Accumulation buffer (`BGL_ACCUM`) | hgl asks for `R16G16B16A16_SNORM` (`hgl.c:309-310`), which lima can't render | **UNVERIFIED**; rarely used |
| 3D textures | claimed (`lima_screen.c:330, 132-134`) | **UNVERIFIED**; Utgard has no 3D textures |

### 2.5 GLTeapot

GLTeapot asks for `BGL_RGB | BGL_DEPTH | BGL_DOUBLE` in a BDirectWindow (`haiku/src/apps/glteapot/TeapotWindow.cpp:22-24`). It draws:

- `GL_QUAD_STRIP` in immediate mode (`GLObject.cpp:75`, `USE_QUAD_STRIPS 1`; `:371-387`);
- up to 3 lights with a local viewer, `GL_NORMALIZE` (`ObjectView.cpp:221-240`);
- fog (`:663-667`), blending (`GLObject.cpp:354-358`);
- flat or Gouraud shading (`ObjectView.cpp:640`), and a wireframe mode with `glPolygonMode(GL_LINE)` (`:658-660`);
- the FPS digits as `GL_LINES` (`FPS.cpp:53`).

Expected on lima 22.0.5: depth, lighting, fog and blending work if the shaders compile. The quad strips depend on the open PLBU question (P7 if needed). Flat shading renders smooth, and wireframe renders filled.

---

## 3. The Haiku side

### 3.1 How the Software Pipe renderer works today

- **Roster:** `BGLView` asks `GLRendererRoster` for a renderer (`hgl/GLView.cpp:51`). The roster loads every add-on in the user and system `add-ons/opengl` folders, user ones first (`GLRendererRoster.cpp:110-127, 198-231`). `GetRenderer()` returns the first add-on's `instantiate_gl_renderer()` result, even when it is NULL (`:93-106`).
- **Renderer:** `SoftwareRenderer` (`targets/haiku-softpipe/SoftwareRenderer.cpp`) is a BGLRenderer and an `HGLWinsysContext`. That interface has one method, `Display(BBitmap*, BRect*)` (`winsys/sw/hgl/hgl_sw_winsys.h:34-37`). `Display()` draws the bitmap into the view, or copies it into the frame buffer clip by clip in direct mode (`:218-243`).
- **Screen:** `GalliumContext::CreateDisplay()` makes one screen per team: `sw_screen_create(hgl_create_sw_winsys())`, softpipe or llvmpipe (`GalliumContext.cpp:81-119`). `hgl_create_display()` wraps it in an `st_manager` (`frontends/hgl/hgl.c:346-361`). The value `debug_screen_wrap()` returns is dropped (`GalliumContext.cpp:105`), so trace and noop wrappers never take effect.
- **Framebuffers:** the frontend's `validate` creates the colour buffers with `PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_RENDER_TARGET` and depth with `PIPE_BIND_DEPTH_STENCIL` (`hgl.c:82-149`). Colour formats: `BGRA8888` or `BGRX8888`; depth: `Z24_UNORM_S8_UINT` (`hgl.c:300-306`).
- **Display targets:** for a display target, the sw winsys allocates a BBitmap, and softpipe or llvmpipe render straight into its bits (`hgl_sw_winsys.cpp:131-138, 190-201`).
- **Present:** `screen->flush_frontbuffer()`:
  - from the frontend on a front-buffer flush (`hgl.c:75`);
  - from `GalliumContext::SwapBuffers()` for the back buffer, after which front and back are swapped (`GalliumContext.cpp:341-351`);
  - from `GalliumContext::Draw()` when the view redraws (`:373`).
  
  All three pass a NULL pipe context. softpipe and llvmpipe call `winsys->displaytarget_display()` (`sp_screen.c:516`, `lp_screen.c:836`), which ends in `HGLWinsysContext::Display()` (`hgl_sw_winsys.cpp:212-224`).

### 3.2 What a hardware screen breaks

1. **No present hook:** lima's screen has no `flush_frontbuffer` (`lima_resource_screen_init()` sets only resource functions; `lima_screen.c:738-753`). The three calls in §3.1 would jump to NULL.
2. **No context:** reading a lima resource needs a pipe context (`texture_map`), and all three present calls pass NULL. The frontend has one at hand (`stctxi->pipe` in `flush_front`, `frontend/api.h` `st_context_iface`), and so has `SwapBuffers()` (`context->st->pipe`). The TODO at `hgl.c:74` foresaw this.
3. **Wrong thread:** `Draw()` runs in the window thread. It must not use the GL thread's pipe context. It should show the last frame again without touching the GPU.
4. **No BBitmap:** there is no display target BBitmap. lima's buffers are GPU memory, and a BBitmap's memory belongs to app_server's shared areas, so the GPU can't render into it directly in v1.
5. **Tiled colour buffers:** lima tiles every colour buffer unless `PIPE_BIND_LINEAR` or `PIPE_BIND_SCANOUT` is set (`lima_resource.c:182-204`). Reading a tiled buffer means a CPU detile into a staging copy (`lima_resource.c:674-693`). A linear buffer is mapped directly (`:694-711`). The PP writes linear buffers (`pixel_layout` 0, `lima_job.c:796-801`), and the reload path samples them (`lima_texture.c:94-99`).
6. **Roster order:** the first add-on wins and a NULL renderer is returned as is. A hardware add-on that finds no device would leave BGLView without a renderer. Also, order within packagefs folders is not guaranteed (**UNVERIFIED**).

### 3.3 The haiku-lima target

A second renderer add-on, built from the same Mesa build and installed next to "Software Pipe" as `add-ons/opengl/Lima Pipe` (name open).

- **`instantiate_gl_renderer()`:**
  - returns NULL when `LIBGL_ALWAYS_SOFTWARE` is set (Mesa's usual switch), or when the device node can't be opened;
  - otherwise returns a `LimaRenderer`.
  
  The screen is made once per team, as today.
- **`LimaRenderer`:** a copy of `SoftwareRenderer` (LockGL, UnlockGL, SwapBuffers, Draw, Display and direct mode unchanged), plus a BBitmap of the view's size, guarded by a lock against the window thread.
- **Screen creation** goes behind a hook so `GalliumContext.cpp` serves both add-ons: `extern "C" struct pipe_screen* hgl_target_create_screen(void)`. "Software Pipe" moves its sw screen creation there (`GalliumContext.cpp:90-103`). "Lima Pipe" defines it as:
  1. open the device node (`LIMA_DEVICE` overrides the path for tests);
  2. call `lima_drm_screen_create(fd)`, then close its own fd;
  3. install two screen hooks, as `lima_drm_winsys.c:93-94` already does for `destroy`:
     - `flush_frontbuffer` = `haiku_lima_flush_frontbuffer`;
     - `resource_create` = a wrapper that adds `PIPE_BIND_LINEAR` when `PIPE_BIND_DISPLAY_TARGET` is set.
- **The frontend stays as it is,** apart from passing a context (P4). `hgl_create_display()` already takes any `pipe_screen`. With `PIPE_CAP_NPOT_TEXTURES` set it uses `PIPE_TEXTURE_2D` targets (`hgl.c:243-246`). lima accepts both visual formats (`lima_format.c:131-136`).

### 3.4 Presenting a frame

`haiku_lima_flush_frontbuffer(screen, ctx, resource, level, layer, drawable, box)`:

1. Cast `drawable` to `HGLWinsysContext*`, then to `LimaRenderer*`. It is the pointer the renderer passed to `CreateContext()`.
2. If `ctx` is NULL (the window thread's `Draw()`): call `Display(fBitmap)` and stop.
3. Otherwise:
   - `ctx->texture_map(ctx, resource, 0, PIPE_MAP_READ, whole box, &transfer)`. lima flushes any job writing the buffer and waits with `GEM_WAIT` READ (`lima_resource.c:650-654`). For a linear buffer it returns a pointer into the buffer's mapping and `transfer->stride` (16-pixel aligned, `lima_resource.c:206-211`).
   - Copy the rows into `fBitmap`, recreating it on a size change. `B_RGB32`/`B_RGBA32` have the byte order of `BGRX8888`/`BGRA8888`, so each row is a plain `memcpy`.
   - `texture_unmap()`, then `Display(fBitmap)`.

What it costs:

- Each frame waits for the GPU (no overlap), then makes one CPU read of the buffer, plus the copy `DrawBitmap` or direct mode makes, as today.
- For a GLTeapot-sized window this is small.
- At 1920x1080, an earlier measurement put softpipe's clear and present at 45 ms a frame. The GPU takes the clear, but the present copies remain, and the first one reads write-combining memory.
- Later steps, in order of payoff (stage 5):
  - read back frame N-1 while the GPU draws frame N (one frame of latency);
  - copy from the buffer straight into the frame buffer in direct mode;
  - a cached readback buffer, if write-combining reads prove slow;
  - zero-copy full screen: the GPU's MMU can map any pages, for instance an OSD layer's buffer.

### 3.5 Files to add or patch

All Mesa changes become `tools/patches/mesa-22.0.5-*.patch`, which `mesa-build.sh` applies after the HaikuPorts patchset. New files must be inside a patch too, because the tree is extracted again whenever `.patched` is missing.

| # | File | Change |
|---|---|---|
| P1 | `src/gallium/drivers/lima/lima_bo.c` | `lima_bo_map()` and `lima_bo_unmap()` call `drmHaikuMap(fd, offset, size)` and `drmHaikuUnmap(ptr, size)` under `__HAIKU__` (§1.3) |
| P2 | `src/gallium/drivers/lima/lima_disk_cache.c` | `lima_disk_cache_init()` builds its id with `disk_cache_get_function_identifier()` (build fails today, §1.6) |
| P3 | `src/util/libsync.h` | `#include <sys/ioccom.h>` under `__HAIKU__` (build fails today, §1.6) |
| P4 | `src/gallium/frontends/hgl/hgl.c`, `src/gallium/targets/haiku-softpipe/GalliumContext.cpp`, `SoftwareRenderer.cpp` | pass `stctxi->pipe` at `hgl.c:75` and `context->st->pipe` at `GalliumContext.cpp:347`; move screen creation behind `hgl_target_create_screen()`. sw drivers ignore the context, so Software Pipe is unchanged. |
| P5 | `src/hgl/GLRendererRoster.cpp` | `GetRenderer()` skips a NULL renderer and tries add-ons in priority order: an optional exported `int32 gl_renderer_priority`, then name |
| P6 | `src/gallium/meson.build` (new hunk after `:220`); new `src/gallium/targets/haiku-lima/{meson.build, LimaRenderer.cpp, LimaRenderer.h, limatest.c}` | the add-on `liblimapipe.so`: `GalliumContext.cpp` from haiku-softpipe plus `LimaRenderer.cpp`, linked like `libswpipe` (`targets/haiku-softpipe/meson.build`) with `driver_lima` and `dep_libdrm` in place of `driver_swrast`; and the test program of §4 |
| P7 | `src/gallium/drivers/lima/lima_screen.c` | only if stage 2 shows the PLBU can't draw them: `PIPE_CAP_SUPPORTED_PRIM_MODES` without QUADS, QUAD_STRIP and POLYGON |

Outside Mesa:

- `tools/libdrm-haiku/`: `xf86drm.h`, `sys/ioccom.h` (Linux encoding), `xf86drm.c` (§1.7, with the no-op backend of stage 0) and `libdrm.pc.in`. About 300 lines, MIT, built as a static, position-independent `libdrm.a`. Static means libGL, OSMesa and Software Pipe get no new runtime dependency, even though libgallium now carries `-ldrm`.
- `tools/mesa-build.sh`:
  - build the shim first;
  - write `libdrm.pc` (Version 2.4.109) into `deps/build/pkgconfig`;
  - add `-I<shim>/include` to `c_args` and `cpp_args` in the generated cross file;
  - pass `-Dgallium-drivers=swrast,lima`;
  - extend the private-headers `sed` to `targets/haiku-lima/meson.build`;
  - package `mesa_lima` (`add-ons/opengl/Lima Pipe`, requires `mesa == $VERSION-$REVISION`).
- The kernel driver needs the same uapi. Its copy of `lima_drm.h`, the `drm.h` subset and the ioctl encoding must match the shim's. Stage 1's first ioctl shows any mismatch.

### 3.6 Meson options

Today's call (`tools/mesa-build.sh`) with one change: `-Dgallium-drivers=swrast,lima`.

- Keep swrast. `targets/haiku-softpipe` is always built on Haiku (`gallium/meson.build:218-221`) and needs `driver_swrast`.
- Nothing else is needed. lima's meson and `src/gallium/meson.build:161-166` don't depend on the platform.
- libpanfrost_shared is pulled in for lima (`src/meson.build:97-98`). The midgard and bifrost compilers are not built by default.
- `lima_compiler` and `lima_disasm` are built only with `-Dtools=lima` (`drivers/lima/meson.build`); not needed.
- The drm-shim (`-Dtools=drm-shim`) is Linux-only (LD_PRELOAD of `open`, `ioctl` and `mmap`). Our shim's no-op backend plays its part.

---

## 4. Staged plan (userland)

Each stage ends with something that runs on the board.

**Stage 0: the stack without a kernel driver.**
- Build the shim and patches P1 to P6. The shim gets a no-op backend selected by `LIMA_HAIKU_NOOP=1`, modelled on Mesa's `lima_noop.c`:
  - `GET_PARAM` answers Mali-450 and 3 PP cores; version 1.0;
  - `GEM_CREATE` makes an anonymous area; `GEM_INFO` returns a bump-allocated address and the area id;
  - submit, wait and the context ioctls do nothing; syncobjs are counters.
- `limatest` (below) and the "Lima Pipe" add-on then run without hardware:
  - screen and context creation, the clear and the flush complete; the readback is zeros;
  - `LIMA_DEBUG=dump` writes `lima.dump` with the GP and PP command streams;
  - GLInfo through "Lima Pipe" reports Mali450 and OpenGL 2.1;
  - GLTeapot runs on a black view, and `LIMA_DEBUG=gp,pp` shows its fixed-function shaders compiling.
- This checks the build, the add-on loading, the state tracker and the shader compilers before any kernel code exists.

**Stage 1: clear and read back (first hardware test).**
- Kernel v1 per §1.8.
- `limatest`, a small C program in the target directory linked against `driver_lima`, libgallium and the shim:
  1. open the node and call `lima_drm_screen_create()`; print the name and `NUM_PP`;
  2. `context_create`;
  3. a 64x64 `B8G8R8A8_UNORM` resource with `RENDER_TARGET | LINEAR`;
  4. `create_surface`, `set_framebuffer_state`;
  5. `clear(PIPE_CLEAR_COLOR0, red)`, `flush`;
  6. `texture_map(PIPE_MAP_READ)` and check every pixel; repeat with a second colour.
- Then 1920x1080: time 100 clear-plus-readback rounds and report the readback rate (the write-combining question of §1.3).
- 1000 rounds with no MMU fault and no hang.
- Run it with `LIMA_DEBUG=dump` too, which adds `SYNCOBJ_WAIT` after each task.

**Stage 2: draw.**
- A triangle as in Mesa's `src/gallium/tests/trivial/tri.c`, with `pipe_loader` replaced by `lima_drm_screen_create()`. Passthrough TGSI shaders (`util_make_*_passthrough_shader`) go through `tgsi_to_nir` (`lima_program.c:397, 644`), gpir and ppir. Check the centre pixel and a corner.
- A textured quad (`quad-tex.c`): tiled upload, sampling, varyings. A depth test with Z24S8.
- The same quad drawn as `PIPE_PRIM_QUADS`, `QUAD_STRIP` and `POLYGON`, compared with triangles. This decides P7.

**Stage 3: BGLView.**
- `tools/probes/gltest.cpp`: a BGLView that clears, draws a triangle, compares the result through `glReadPixels`, and calls glFinish.
- Then: single and double buffering, window resizing, two GL windows (two contexts), quit and restart (the kernel's buffer tables empty again), and `LIBGL_ALWAYS_SOFTWARE=1` falling back to Software Pipe.

**Stage 4: applications.**
- GLTeapot: frames per second against llvmpipe's 129 (60 on HDMI, M21), and a picture check, minding §2.4.
- GLInfo, Haiku3d, and the Flurry, GLife and Gravity screen savers.
- A screen saver's OpenGL path against its software renderer.

**Stage 5: speed and robustness.**
- Present: asynchronous readback, direct-mode copy, readback buffer caching (§3.4).
- Kernel: GP/PP overlap with real implicit fences, the growable heap (version 1.1), task timeout and reset, per-file address spaces. Real sync files only if something needs glWaitSync across contexts.

---

## 5. Open questions

1. Does Mali-450's PLBU draw primitive modes 7 to 9 (QUADS, QUAD_STRIP, POLYGON)? Mesa 22.0.5 sends them. GLTeapot uses QUAD_STRIP. Stage 2 answers it; P7 is the fallback.
2. Buffer mapping:
   - a cloneable area named in `GEM_INFO.offset` (house pattern; any team that guesses the id can clone it), or an ioctl that maps into the calling team?
   - write-combining or cached CPU views of render targets, given a readback every frame?
3. Device node path: `/dev/misc/lima/0`, or another name outside `/dev/graphics`?
4. Renderer choice: a priority symbol in the add-ons (P5), name order, or a setting? Should Lima Pipe be the default once it works, with `LIBGL_ALWAYS_SOFTWARE` as the way back?
5. Is a single GPU address space for all teams acceptable for v1?
6. Is the fixed 1 MiB tile heap (version 1.0) enough for GLTeapot and Haiku3d, or does the kernel need heap growth (1.1) early? **Answered (milestone M43 stage 4):** not enough with several GL applications and full-screen frames (GP error 0x4, then failed PP jobs); the kernel grows heaps since fork b4257aee02.
7. Stay on Mesa 22.0.5 (one build, HaikuPorts' patches, ours), or move lima to a newer Mesa for its fixes? Newer lima versions were not examined.
8. Add-on and package names: "Lima Pipe" and `mesa_lima`?
9. Flat shading, `glPolygonMode` lines and occlusion query results are missing in lima 22.0.5. Do any of the applications we care about depend on them?
