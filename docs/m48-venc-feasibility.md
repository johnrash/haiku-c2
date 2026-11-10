# M48: hardware H.264 encoding on the S905, feasibility

> **Status:** Decision note before M48. The outcome: the encoder works with the vendor microcode, which each owner builds into a package of their own with `tools/venc-firmware.sh` and which is never in this repository. Rate control came in M52, and MJPEG decoding in M51. See `docs/m48-venc-facts.md`.

Written 2026-11-01, before any code, for hardware encoding of NeoCam's clips at 1920x1080 and 30 frames a second, which the CPU can't encode next to the camera's MJPEG decoding (M47).

Facts from GPL code (Amlogic's vendor kernel) are recorded as facts only. No code is copied.

## The hardware

- The S905's video block (DOS) has an encoder next to the decoders: **HCODEC**. M25 called it out of scope.
- Like VDEC_1, HCODEC is driven by an AMRISC processor that runs microcode. The microcode works the hardware's motion search, transform and entropy coding, and writes the bitstream.
- In Amlogic's vendor driver (`drivers/amlogic/amports/encoder.c` in Hardkernel's `odroidc2-3.14.y`), the CPU asks the microcode for one thing at a time:
  - a sequence header (SPS, `ENCODER_SEQUENCE`);
  - a picture header (PPS, `ENCODER_PICTURE`);
  - an IDR picture (`ENCODER_IDR`);
  - a non-IDR picture (`ENCODER_NON_IDR`).
- The result is a complete H.264 elementary stream: the driver reports the SPS and PPS sizes and the size of each picture.
- **Input** goes through HCODEC's frame reader (MFDIN), which takes:
  - NV12, NV21 or three-plane 4:2:0;
  - 4:2:2 or 4:4:4 single plane;
  - RGB888, with an RGB-to-YUV converter in the reader. An RGBA8888 mode exists too.

  Which of these modes GXBB has was still to be read out (RGBA turned out to exist). If RGBA isn't among them, NeoCam's frames go through the NEON converter from M47, 8 ms a 1080p frame.
- **Sizes:** the vendor driver's buffer levels go up to 1920 wide (1080p).
- **The microcode for GXBB** is `h264_enc_mc_gx`, which the driver chooses for any chip from GXBB on in its "full" mode.

## The microcode, and the decision it needs

- **Where it is:** the microcode exists only inside the vendor kernel. It is compiled in as a hex array (`arch/ucode/h264_enc/h264_enc_gx.h`, 25,931 bytes of source).
- **Its licence:** Amlogic's header puts that file under the GPL, version 2 or later. There is no source for the microcode itself, only the hex.
- **Not in linux-firmware:** linux-firmware's redistributable Amlogic set (`LICENSE.amlogic_vdec`, what M26 ships) has decoder microcode only, no encoder.
- **What that means:**
  - The microcode can't go into the fork (MIT).
  - It could live in a package of its own, built locally with Amlogic's GPL notice next to it (`tools/venc-firmware.sh`), installed on one's own boards and never in this repository.
  - Without it, there is no hardware encoding: the hardware does nothing without microcode, and its instruction set isn't documented.
- This is the same source, and the same kind of terms, as the HEVC microcode deleted in M26 as not needed; this one would be needed.

## What would be built, in stages (each reported)

1. **Facts:** `docs/m48-venc-facts.md`, from the vendor driver, facts only. It covers:
   - HCODEC's power, clocks and reset;
   - loading the microcode;
   - the buffers: input frame, reference and reconstructed frames, the bitstream, deblocking and motion vector memory;
   - the command and interrupt protocol;
   - the quantiser and motion search settings the driver gives.
2. **`meson_venc` kernel driver and a probe tool.** It encodes test frames, which are checked by decoding them with FFmpeg on the build machine: frame count, size, and the difference from the input.
3. **A Media Kit encoder (`meson_encoder`):**
   - "H.264 (hardware)" for `BMediaFile`, writing Matroska through the FFmpeg writer.
   - Our FFmpeg build has no H.264 encoder (x264 is GPL), so this would be the only one.
   - CodyCam, MediaConverter and anything else that encodes get it too.
4. **NeoCam** records H.264 clips at the camera's own rate, 1080p at 30 frames a second included.
   - The CPU then only decodes the camera's MJPEG: about 1.4 cores at 1080p30.
   - The S905's MJPEG decoder could take that over too, but linux-firmware's `gxl_mjpeg.bin` was untested on GXBB then. That would be a later step (it came in M51).

## Risks

- Nothing in mainline Linux or LibreELEC drives this block, so there is no second source as M26 had. The vendor driver is the only description.
- What the microcode needs at each step (buffer layouts, register handshakes) is only known from that driver. M26's decoders needed some trial on the board for things the drivers didn't say.
- Quality and rate control are whatever the microcode and the driver's quantiser settings give. The vendor driver keeps a constant quantiser per picture type, with an optional simple rate control.
- Stage 2 is where it works or doesn't. If it doesn't, the fallback is recording the camera's MJPEG frames as they come (large files, little CPU).

## Downloads for this study

Not kept in this repository, and never run. Hardkernel's `linux` repository, branch `odroidc2-3.14.y`:
- Directory listings from api.github.com: `drivers/amlogic/amports` (69 KB), `arch`, `arch/ucode` and `arch/ucode/h264_enc` (a few KB each).
- Files from raw.githubusercontent.com:
  - `encoder.c` (123,261 bytes)
  - `encoder.h` (26,740 bytes)
  - `arch/firmware.c` (5,315 bytes)
  - `arch/ucode/h264_enc/h264_enc_mc.c` (3,510 bytes)
  - `arch/ucode/h264_enc/h264_enc_gx.h` (25,931 bytes; read for its header only)

## Sources

- Vendor kernel (GPL): https://github.com/hardkernel/linux/tree/odroidc2-3.14.y/drivers/amlogic/amports
- linux-firmware's Amlogic set: https://gitlab.com/kernel-firmware/linux-firmware/-/tree/main/meson/vdec
