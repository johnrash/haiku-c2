# M25 item 6: hardware video decoding on the S905, a feasibility study

> **Status:** Feasibility study written 2026-10-30. Option C was taken: M26 built MPEG-1/2, H.264 and HEVC with an NV12 overlay; M31 removed the last copy; M60 added MPEG-1/2 in MediaPlayer and interlaced H.264; M51 added MJPEG. Since M53 the display does 4K at up to 30 Hz; the decoders stay within the 1920x1088 macroblock budget.

Written 2026-10-30, before any code, for M25 item 6. Sources are listed at the end. Facts from GPL code (Linux, Amlogic's vendor kernel) are recorded as facts only: register names, offsets and what a step does. No code.

## Verdict
**Feasible.** It's a driver the size of M6's HDMI work or larger, and it pays off for a narrow set of formats.
- **What it would add:**
  - **HEVC at 1080p**, which the CPU can't play today.
  - **H.264 at 1080p60.**
  - **Near-zero CPU for 1080p30 H.264,** which now takes about 1.8 cores.
- **What it would not add:**
  - **VP9 and AV1.** The S905 has no hardware for them. VP9 arrived with the S905X; no GX chip has AV1. They stay in software, where VP9 1080p30 already plays well and AV1 1080p30 nearly does.
- **What makes it possible on GXBB:**
  - The firmware loads without the secure monitor.
  - The power, clocks and resets are plain register writes.
  - The firmware is redistributable for Amlogic chips.
- **What makes it hard:**
  - The decoder is undocumented.
  - The only working drivers are GPL: Linux's, LibreELEC's and Amlogic's own.
  - Every register and handshake would have to be rebuilt from facts.
- **Recommendation:** go in stages, with a stop after each stage.
  1. Bring up MPEG-2 first. Linux's MPEG-2 path is its smallest, and it would prove the power, firmware, parser, canvas and buffer plumbing.
  2. Then H.264 end to end in MediaPlayer.
  3. Then decide on HEVC, the part with real value and the least public ground.

## What the CPU did then (measured in M21 on the test board)
| Format | Software (FFmpeg, 4 × A53 at 1.5 GHz) | S905 hardware |
|---|---|---|
| H.264 1080p30 | plays, 1.8 cores | yes |
| H.264 1080p60 | beyond the CPU (18.6 s for 20 s) | yes (mainline caps GXBB at 1080p) |
| HEVC 720p | plays | yes |
| HEVC 1080p | beyond the CPU (20.7 s for 20 s) | yes (only out-of-tree drivers) |
| VP9 1080p30 | plays well | **no hardware** |
| AV1 1080p30 | nearly (17 late, 3 dropped in 11 s) | **no hardware** |
| MPEG-2, MPEG-4 ASP | cheap | yes |

The display path topped out at 1920x1200 then (M25 item 5; 4K at up to 30 Hz came in M53), so the S905's 4K decoding would have gained nothing.

## The hardware (S905 datasheet rev 1.1.4)
- **What Amlogic quotes for the S905:**
  - HEVC Main10 up to 4K60.
  - H.264 High@L5.1 up to 4K30.
  - MPEG-1/2/4, VC-1, AVS and RealVideo up to 1080p.
  - MJPEG.
  - No VP9: the datasheet's VP9 interrupt is marked unused.
- **Blocks:**
  - **VDEC_1** (MPEG-1/2/4, H.263, MJPEG, H.264, VC-1) and **VDEC_HEVC.** Each is driven by an AMRISC microcontroller that runs Amlogic firmware. The two share one interrupt (SPI 44), so only one decodes at a time.
  - **ESPARSER** (SPI 32) DMAs the bitstream into a **VIFIFO** ring in DDR, finding start codes.
  - **HCODEC**, the encoder, which is out of scope.
- **Addresses:** DOS (the decoders) at 0xC8820000 (64 KB); the parser at 0xC110A580.
- **Power islands:** AO_RTI_GEN_PWR_SLEEP0 and ISO0, VDEC_1 at bits 2–3 and HEVC at bits 6–7. Also the DOS memory power-down words and DOS_SW_RESET0/3.
- **Clock:** HHI_VDEC_CLK_CNTL and HHI_VDEC2_CLK_CNTL, at 666 MHz in Linux.
- **What the datasheet documents:**
  - It has the address ranges, the clock registers, the interrupts and the power diagram, and it marks the decoder power bits "Reserved".
  - It has **none of the decoder's internal registers** (DOS, VLD, MDEC, assist, parser). The S905X and S905X3 datasheets don't either.

## The software that exists
- **Mainline Linux, `drivers/staging/media/meson/vdec`:**
  - It is still in staging, as a V4L2 stateful decoder of about 7,000 lines.
  - On GXBB it does H.264 up to 1080p and MPEG-1/2, and no HEVC (none on any SoC in mainline).
  - Its H.264 path: core 1123 lines, parser 457, VDEC_1 255, H.264 485, helpers 480.
  - **The firmware does the hard part of H.264:** slice and parameter-set parsing and reference management. The driver feeds the parser, allocates the workspace (about 1.9 MB) and the motion-vector buffers, and swaps buffer indices through the AV_SCRATCH registers. End of stream is signalled by feeding a dummy 16x16 picture.
  - Output is NV12 in linear canvases.
- **HEVC on GXBB has only been done out of tree:**
  - Maxime Jourdan's 2018 driver had partial 8-bit HEVC.
  - Christian Hewitt's 2023 patches were not merged.
  - Since September 2026, LibreELEC replaces the staging driver with a new stateless one that does HEVC Main and Main10 on GXBB, capped at 1920x1088.
  - On GX, HEVC uses compressed reference frames plus an 8-bit NV12 "double write" output. The frame MMU doesn't work on GX, so the `_mmu` firmware isn't used.
- **No BSD or MIT driver exists** (NetBSD, OpenBSD and FreeBSD checked). NetBSD has a BSD-2 canvas header, and the device tree binding is dual GPL/BSD.
- **The firmware** (linux-firmware `meson/vdec`, 16–36 KB each): GXBB uses `gxbb_h264.bin`, `gxl_mpeg12.bin` and, out of tree, `gxl_hevc.bin`. The licence (15 lines, checked):
  - It allows use and redistribution "for the use with devices containing Amlogic chipsets".
  - It forbids putting the files under the GPL or LGPL.
  - It grants no modification.
  - My reading: a separate firmware package next to an MIT driver fits it, and the fork stays MIT. That's not legal advice.
- **Firmware loading on GXBB:** the driver DMAs the image into the AMRISC's instruction memory (IMEM_DMA_ADR/COUNT/CTRL), then starts it (MPSR). There is **no secure monitor call**, in mainline or in the vendor kernel. Newer SoCs are less friendly.

## What a Haiku version would be
1. **Kernel driver `meson_vdec`:**
   - Power, clocks and resets; the firmware load.
   - The parser and VIFIFO (16 MiB in Linux), the interrupts, the canvases (M6 already programs the DMC canvas table), the workspace, and the reference and output buffers.
   - A small ioctl interface: open a session for a codec, write bitstream, get a decoded buffer, return a buffer, flush.
   - About 2,000–2,500 lines for MPEG-2 and H.264, without V4L2's boilerplate.
2. **A Media Kit decoder plugin**, ranked above the ffmpeg plugin for the codecs it handles:
   - Converts MP4's length-prefixed H.264 to Annex B and adds the parameter sets.
   - Handles flush on seek, end of stream and resolution changes.
   - About 1,000 lines.
3. **Display:**
   - Zero-copy, by showing the decoder's NV12 canvases on VD1. M9 already drives VD1 for overlays; it needs an NV12 mode.
   - Or a NEON copy to the B_YCbCr422 overlay format, about 3 ms a 1080p frame.
4. **HEVC:** compressed references and double-write output, and the most driver-side logic. The public ground is LibreELEC's out-of-tree driver only. Size unknown; I'd estimate 1,500–2,500 more lines.

## Risks
- **An undocumented block that runs a binary firmware.** When it hangs, there is little to see. Bring-up is trial and error against facts read from GPL drivers. Keeping that "facts only" for a handshake protocol this specific takes care, and the line is thinner than it was for HDMI.
- **Contiguous memory:**
  - Linux reserves 256 MiB of CMA on GX (LibreELEC uses 384), and Haiku has no CMA.
  - 1080p H.264 needs roughly 50–60 MB of physically contiguous buffers.
  - These would have to be allocated early, as the frame buffer is, or the session can fail on fragmented memory.
- **The 1080p cap:** mainline caps GXBB's H.264 at 1080p without saying why, though the datasheet says 4K30. It didn't matter for the 1920x1200 display of the time.
- **Limits:** one decoding session at a time, and no hardware deinterlacing (interlaced streams would stay in software; M60 later decoded interlaced H.264 in hardware, woven as FFmpeg does).
- **No upstream to follow:** Linux's driver is still in staging after six years, and LibreELEC is replacing it.

## Options
- **A. Stop here.** Software already plays H.264, VP9 and AV1 at 1080p30 and HEVC at 720p. The loss is HEVC 1080p and H.264 1080p60.
- **B. Bring up MPEG-2 and H.264 (recommended to start).** Stop after MPEG-2 decodes bit-exact against FFmpeg in a test probe (M26 found that MPEG-1/2 decoders may differ within the IDCT tolerance, so "within tolerance" was the real goal). If that works, H.264 through MediaPlayer with the zero-copy overlay.
- **C. B, then HEVC**, the part that actually adds a format. It's the riskiest and depends on what B teaches.

## Sources
- S905 datasheet rev 1.1.4: https://cgi.cse.unsw.edu.au/~cs9242/26/project/s905.pdf
- S905X datasheet: https://ieiao.github.io/wiki/embedded-dev/amlogic/s905x/S905X_Datasheet.pdf
- Linux staging driver: https://github.com/torvalds/linux/tree/master/drivers/staging/media/meson/vdec (TODO, vdec_platform.c, vdec_1.c, vdec_hevc.c, esparser.c, codec_h264.c, codec_hevc_common.c, dos_regs.h)
- Device tree binding: https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/media/amlogic,gx-vdec.yaml
- HEVC patches (2023): https://lkml.iu.edu/hypermail/linux/kernel/2301.2/09124.html
- LibreELEC patches: https://github.com/LibreELEC/LibreELEC.tv/tree/master/projects/Amlogic/patches/linux
- Firmware and licence: https://gitlab.com/kernel-firmware/linux-firmware/-/tree/main/meson/vdec, https://gitlab.com/kernel-firmware/linux-firmware/-/blob/main/LICENSES/LICENSE.amlogic_vdec
- Vendor kernel (GPL): https://github.com/hardkernel/linux/tree/odroidc2-3.14.y/drivers/amlogic/amports
- NetBSD Amlogic tree: https://github.com/NetBSD/src/tree/trunk/sys/arch/arm/amlogic
- LibreELEC 11 notes: https://libreelec.tv/2023/03/06/libreelec-nexus-11-0-0/
