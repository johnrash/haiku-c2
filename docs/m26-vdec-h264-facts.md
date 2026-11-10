Facts read from Linux's GPL drivers/staging/media/meson/vdec (mainline abf8c33470f7, 2026-10-30); no code copied.

# M26: S905 (GXBB) VDEC_1 H.264 decoding, hardware facts

> **Status:** Facts written 2026-10-30, before the H.264 path existed; board results are in section 16. Later: edge-triggered interrupts (M26 stage 2), mid-stream size changes and portrait video up to the 1920x1088 macroblock budget (M41), and field-coded and MBAFF streams in hardware, woven as FFmpeg does (M60).

This is the H.264 counterpart of `m26-vdec-facts.md`, called the **base document** here and cited as "base §N". The base document already covers everything H.264 shares with MPEG-1/2, and none of it is repeated here:

- conventions (base §0);
- power-up and power-down (base §1);
- the 16 KiB IMEM DMA (base §2);
- the ESPARSER (base §3);
- the VIFIFO (base §4);
- the NV12 layout and canvas LUT (base §6);
- the mailbox interrupt (base §7);
- the session order (base §8);
- register numbering (base §9).

This document covers only what H.264 adds or changes. It uses the same conventions:
- DOS offsets are byte offsets from 0xC8820000.
- "Set bits" means read-modify-write.
- **UNVERIFIED** marks a meaning that Linux does not give, or that is inferred.

**Provenance:**
- codec_h264.c arrived in d7647e7c90f5 (2020-03-03). The commit message names no tested SoC.
- Later commits touching it are a typo fix (2a1a564adab1) and allocator-API conversions (69050f8d6d07, bf4afc53b77a). The firmware protocol in Linux is therefore unchanged since 2020.
- Base §11 "Found on the board" applies to MPEG-1/2 only; this document's board results are in §16.

---

## 0. H.264 versus MPEG-1/2 at a glance

| Item | MPEG-1/2 (base §5) | H.264 (this document) |
|---|---|---|
| Firmware | gxl_mpeg12.bin, 16 KiB | gxbb_h264.bin, 36 KiB: a 16 KiB core plus a 20 KiB extension (§1) |
| Extra host buffers | 128 KiB workspace | 20 KiB extension copy; 0x1EE000-byte workspace; 8 KiB SEI buffer; a reference-MV buffer sized from the stream (§2) |
| Picture size | set by the host before start | reported by the firmware (command 1, §6) |
| When canvases are given | before MPSR = 1, in AV_SCRATCH_0–7 | after command 1, in ANC0_CANVAS_ADDR + 4·i (§6.4) |
| Number of output buffers | exactly 8 | depends on the stream: Linux asks for at least max_refs + 5, between 2 and 24 (§6.3) |
| Report register | AV_SCRATCH_9, one frame per report | command byte in AV_SCRATCH_0; one report can carry several frames (§7) |
| Buffer index in a report | 1-based | 0-based |
| Hand-back | AV_SCRATCH_8 | AV_SCRATCH_7 or AV_SCRATCH_8: two slots (§8) |
| Fatal error | AV_SCRATCH_F = 1 | command 6 in AV_SCRATCH_0. AV_SCRATCH_F is a configuration register for H.264 (§3) |
| End of stream | 1 KiB: sequence_end_code plus zeros | 4 KiB: a complete 16×16 H.264 stream plus its own `00 00 01 FF` (§10) |
| Capture format | NV12 or 3-plane | NV12 only: MDEC_PIC_DC_CTRL bit 17 = 1 |
| Mailbox acknowledge | in the threaded handler | in the hard handler (§5.1); the effect is the same |

---

## 1. Firmware

### 1.1 The file

- **Name:** `meson/vdec/gxbb_h264.bin`, from linux-firmware.
- **This host's copy:** linux-firmware 20260319.git217ca6e4, `/usr/lib/firmware/meson/vdec/gxbb_h264.bin.zst`. Decompressed, it is **36,864 bytes**, md5 `eee6da399af38101b6c001f3cbb0a21d`.
- **This repo's copy:** `firmware/meson/vdec/gxbb_h264.bin` is the same file (same size and md5). The same `LICENSE.amlogic_vdec` covers it.
- **Layout:**

| File range | Size | Part |
|---|---|---|
| 0x0000–0x3FFF | 16,384 | core: the AMRISC instruction-memory image |
| 0x4000–0x8FFF | 20,480 | extension |

- **Other SoCs:** gxl_h264.bin, gxm_h264.bin and g12a_h264.bin are also 36,864 bytes with the same split. They are different builds, though. Comparing 32-bit words at the same offset, only about a third of the core words (1,366–1,397 of 4,096) and under half of the extension words (2,370 of 5,120) match GXBB's. Use the GXBB file on GXBB.

### 1.2 Loading the core

The core loads exactly as base §2: the first 16 KiB, IMEM_DMA_COUNT = 0x1000, IMEM_DMA_CTRL = 0x00078000, then poll bit 15. Nothing differs for H.264.

### 1.3 The extension (Linux's "load_extended_firmware" hook)

**Size check.** The loader passes the rest of the file (file size − 16 KiB) to the codec. The H.264 codec rejects the session if that is less than 20 KiB, so the file must be at least 36 KiB.

**When it is copied:**
- right after the IMEM DMA of the core has completed;
- only if the DMA completed. If the DMA hangs, the session fails before this step.

**What is copied:**
- Exactly 20,480 bytes, file offsets 0x4000–0x8FFF, unchanged, into a separate 20 KiB buffer.
- The buffer is physically contiguous, has a 32-bit physical address and is visible to the device. Linux uses uncached coherent memory, so a cached buffer needs a cache clean.

**How the firmware finds it:**
- The host does no DMA and writes no register for it during the load.
- Its physical address goes into **AV_SCRATCH_G (0x2740)** at codec start (§3, step 4).

**Lifetime:**
- The buffer stays allocated and unchanged for the whole session.
- Linux frees it at codec stop, after the AMRISC has been stopped and VDEC_1 powered off (base §1.4).
- So the firmware reads it while running, not only at start (inferred from the lifetime, **UNVERIFIED**).

**Alignment:** Linux's allocation is at least page aligned, and in practice naturally aligned (32 KiB for a 20 KiB request). What the firmware needs is **UNVERIFIED**.

**Content.** This comes from looking at the file, not from Linux:
- The extension is five 4 KiB pieces.
- The pieces at file 0x5000 and 0x7000 match the core's 0x2000 and 0x3000 pages for most words at the same in-page positions: 995 and 661 identical non-zero words out of 1,024.
- That suggests code overlays that the firmware itself swaps into the upper half of its 16 KiB instruction memory (**UNVERIFIED**).
- The host needs none of this: it passes the buffer as it is.

---

## 2. Host buffers for H.264

| Buffer | Size (bytes) | Allocated | Register(s) | Freed |
|---|---|---|---|---|
| Extension copy | 20,480 | at firmware load (§1.3) | AV_SCRATCH_G = physical address | session stop |
| Workspace | **0x1EE000 = 2,023,424** (1.93 MiB) | codec start (§3) | AV_SCRATCH_1 = physical − 0x0100_0000 | session stop |
| SEI buffer | 8,192 | codec start (§3) | AV_SCRATCH_I = SEI physical − (workspace physical − 0x0100_0000) | session stop |
| Reference MV buffer | mbw4 × mbh4 × 96 × max_refs (§6.5) | at command 1 | AV_SCRATCH_1 = base, AV_SCRATCH_4 = base + size | session stop |
| VIFIFO | as base §4 | | | |
| Output pictures | as base §6; count in §6.3 | | canvas table §6.4 | |

All of these are physically contiguous, have 32-bit physical addresses and are visible to the device; Linux uses uncached memory. Linux's allocations are page aligned at least. What alignment the firmware needs is **UNVERIFIED** for every one of them.

### Workspace

- **The 16 MiB offset.** Linux's comment says the firmware adds 0x0100_0000 to the value it gets, so the host passes the physical address minus 16 MiB.
- The subtraction is 32-bit. On GX the device tree reserves physical 0–16 MiB ("hwrom", no-map), so on Linux the value never wraps. Whether a wrapped value would work is **UNVERIFIED**, so keep the workspace above 16 MiB.
- **Layout.** Linux passes no sub-areas: no DPB, MV or reference regions inside the workspace. The firmware keeps its own state there.
  - The DPB pictures are the output buffers (§6).
  - The co-located MV storage is the separate reference MV buffer (§6.5).
- **Zeroing.** Linux's allocator returns zeroed memory. Whether the firmware needs that is **UNVERIFIED**; zero it.

### SEI buffer

- **Address.** It is passed relative to the AV_SCRATCH_1 value. That implies the firmware adds AV_SCRATCH_1 to AV_SCRATCH_I to get the physical address (inferred, **UNVERIFIED**).
- **Use.** The firmware writes parsed SEI messages there and signals it in AV_SCRATCH_J (§5.3). Linux never reads the buffer, and its layout is **UNVERIFIED**.

### AV_SCRATCH_1 has three jobs

- It holds the workspace value from start until command 1.
- At command 1, the firmware overwrites it with the stream info.
- In the reply, the host overwrites it with the reference MV base.

So the firmware must read the workspace value before it reports command 1, and the reference MV base after the reply (inferred).

---

## 3. Codec start (before MPSR = 1)

This replaces base §5.4 and runs at base §8 step 6, after the firmware load and before the IRQ enable.

| # | Register | Offset | Action | Value |
|---|---|---|---|---|
| 1 | (allocate the workspace and the SEI buffer, §2) | | | |
| 2 | POWER_CTL_VLD | 0x3020 | set bits | 0x00000240 (bits 9 and 6). With the 0x10 written in base §4, the register now holds 0x250 |
| 3 | AV_SCRATCH_1 | 0x2704 | write | workspace physical − 0x01000000 |
| 4 | AV_SCRATCH_G | 0x2740 | write | extension buffer physical (§1.3) |
| 5 | AV_SCRATCH_I | 0x2748 | write | SEI physical − (the value written in step 3) |
| 6 | AV_SCRATCH_F | 0x273C | read-modify-write | new = (old AND 0xFFFFFFC3) OR 0x00000090: clear bits 5:2, then set bits 7 and 4. Linux's comment calls this enabling "error correction" |
| 7 | MDEC_PIC_DC_THRESH | 0x26E0 | write | 0x404038AA |

**What H.264 does not write at start**, unlike MPEG-1/2:
- no canvases;
- no AV_SCRATCH_0 (the command register);
- no AV_SCRATCH_7, 8 or 9;
- none of MPEG1_2_REG, PSCALE_CTRL, PIC_HEAD_INFO or M4_CONTROL_REG.

Whatever these registers hold after the power-up reset, or from an earlier session, stays there. Whether DOS_SW_RESET0 (base §1.3 step 7) clears the AV_SCRATCH registers is **UNVERIFIED**.

**AV_SCRATCH_F's other bits.** Step 6 keeps bits 1:0, bit 6 and bits 31:8 as they were. An earlier MPEG session leaves AV_SCRATCH_F at 0, or at 1 if that session ended in a fatal error (base §5.4, §5.6). If the GXBB firmware misbehaves after an MPEG session, write AV_SCRATCH_F = 0 before step 6. Linux does not do this (**UNVERIFIED**).

**Meanings unknown:** POWER_CTL_VLD bits 9 and 6, AV_SCRATCH_F bits 4 and 7, and the MDEC_PIC_DC_THRESH value are all **UNVERIFIED**.

### Full session order for H.264 (base §8 with the H.264 steps)

| # | Step |
|---|---|
| 1 | Allocate the VIFIFO (base §4) |
| 2 | Clocks and VDEC_1 power-up (base §1.3) |
| 3 | VIFIFO registers (base §4) |
| 4 | IMEM DMA of the 16 KiB core (base §2) |
| 5 | Copy the 20 KiB extension into its buffer (§1.3) |
| 6 | H.264 codec start (the table above) |
| 7 | ASSIST_MBOX1_CLR_REG ← 1, then ASSIST_MBOX1_MASK ← 1 |
| 8 | MDEC_PIC_DC_CTRL: set bit 17 (NV12) |
| 9 | **MPSR ← 1**, then wait 10–20 µs |
| 10 | ESPARSER init (base §3.3) |
| 11 | Feed the bitstream (base §3.4), starting with SPS, PPS and an IDR picture (§12). Linux also starts the hand-back polling here (§8) |
| 12 | Command 1 arrives: describe the output buffers and reply (§6) |
| 13 | Command 2 reports (§7) and hand-backs (§8) until the end of stream (§10) |
| 14 | Stop: as base §8. Power-down (base §1.4), then free the extension, workspace, SEI, reference MV buffer, canvases and VIFIFO |

---

## 4. Scratch-register map used by the H.264 firmware

| Register | Offset | At start (host → fw) | Command 1 report (fw → host) | Command 1 reply (host → fw) | Command 2 report (fw → host) | Other uses |
|---|---|---|---|---|---|---|
| AV_SCRATCH_0 | 0x2700 | not written | bits 7:0 = 1 | reply word (§6.5) | bits 7:0 = 2, bits 15:8 = frame count | command register; the host writes 0 to acknowledge (§5) |
| AV_SCRATCH_1 | 0x2704 | workspace − 16 MiB | stream info (§6.1) | reference MV base | frame 0 status | commands 7 and 8: the size in macroblocks − 1 |
| AV_SCRATCH_2 | 0x2708 | | aspect info (§6.1) | | frame 1 status | |
| AV_SCRATCH_3 | 0x270C | | extended SAR (§6.1) | | frame 2 status | |
| AV_SCRATCH_4 | 0x2710 | | | reference MV end | frame 3 status | |
| AV_SCRATCH_5 | 0x2714 | | | | frame 4 status | |
| AV_SCRATCH_6 | 0x2718 | | crop (§6.1) | | frame 5 status | |
| AV_SCRATCH_7 | 0x271C | | | | | hand-back slot 1 (§8) |
| AV_SCRATCH_8 | 0x2720 | | | | | hand-back slot 2 (§8) |
| AV_SCRATCH_9 | 0x2724 | | | | | not used by Linux's H.264 path |
| AV_SCRATCH_A | 0x2728 | | | | offset high halves, frames 0 and 1 | |
| AV_SCRATCH_B | 0x272C | | | | offset high halves, frames 2 and 3 | |
| AV_SCRATCH_C | 0x2730 | | | | offset high halves, frames 4 and 5 | |
| AV_SCRATCH_D | 0x2734 | | | | decode error count | the host writes 0 after reading a non-zero value |
| AV_SCRATCH_E | 0x2738 | | | | | not used |
| AV_SCRATCH_F | 0x273C | configuration bits (§3 step 6) | | | | |
| AV_SCRATCH_G | 0x2740 | extension buffer physical | | | | |
| AV_SCRATCH_H | 0x2744 | | | | | not used |
| AV_SCRATCH_I | 0x2748 | SEI buffer (relative, §2) | | | | |
| AV_SCRATCH_J | 0x274C | | | | | bit 15 = SEI data ready; the host writes 0 to release it (§5.3) |
| AV_SCRATCH_K, L | 0x2750, 0x2754 | | | | | not used |

---

## 5. The command protocol

### 5.1 Interrupt

The interrupt is the same as in base §7: SPI 44, ASSIST_MBOX1, and the same clear, enable and disable registers.

For H.264, Linux's hard handler writes ASSIST_MBOX1_CLR_REG ← 1 and records the time. Its threaded handler then reads AV_SCRATCH_0. No other status register is read.

### 5.2 Commands: AV_SCRATCH_0 bits 7:0

| Code | Linux name | Meaning | Host action | Acknowledge |
|---|---|---|---|---|
| 0 | | nothing pending | none | none |
| 1 | CMD_SRC_CHANGE | Parameter sets parsed: the stream's geometry is in AV_SCRATCH_1/2/3/6. The firmware waits for output buffers | §6 | **the reply word of §6.5**, whose low byte is 0. Never a plain 0 |
| 2 | CMD_FRAMES_READY | one or more decoded pictures are ready | §7 | AV_SCRATCH_0 ← 0 |
| 6 | CMD_FATAL_ERROR | unrecoverable decoder error | Linux abandons the session | none: Linux writes nothing |
| 7 | CMD_BAD_WIDTH | Width not supported. (AV_SCRATCH_1 + 1) × 16 = the width in pixels | Linux abandons the session | none |
| 8 | CMD_BAD_HEIGHT | Height not supported. (AV_SCRATCH_1 + 1) × 16 = the height in pixels | Linux abandons the session | none |
| 9 | | unknown. Linux ignores it without logging | none | AV_SCRATCH_0 ← 0 |
| other | | unknown. Linux logs "unexpected" | none | AV_SCRATCH_0 ← 0 |

What codes 3–5 and 9 mean is **UNVERIFIED**. Linux never writes a command into AV_SCRATCH_0 itself: no host-to-firmware command codes are known, apart from the reply in §6.5.

### 5.3 Order of handling, per interrupt

| # | Action |
|---|---|
| 1 | ASSIST_MBOX1_CLR_REG ← 1 |
| 2 | Read AV_SCRATCH_0. The command is bits 7:0 |
| 3 | Handle the command: §6 for command 1, §7 for command 2. For commands 6–8, abandon and skip steps 4–5 |
| 4 | If the command is neither 0 nor 1: AV_SCRATCH_0 ← 0 |
| 5 | For every command, including 0 and 1: if AV_SCRATCH_J bit 15 is set, write AV_SCRATCH_J ← 0. This discards the SEI data and comes after step 4 |

- **Read the report before acknowledging.** After step 4 the firmware may post its next report. That report can overwrite AV_SCRATCH_1–6 and A–D, so read all of them before the AV_SCRATCH_0 write (inferred from the shared registers).
- **SEI flag.** Whether the firmware stalls when AV_SCRATCH_J bit 15 is left set is **UNVERIFIED**. Clear it as Linux does.
- **After a fatal command.** Linux makes no register write after commands 6, 7 and 8. The AMRISC keeps running until the session is stopped (base §1.4).
- **Polling.** Base §11 found the vdec IRQ unreliable at the GIC and polled at first (edge-triggered interrupts fixed that in M26 stage 2). For H.264 the equivalent is to poll AV_SCRATCH_0 bits 7:0 for a non-zero value. Command 1 stays in the low byte until the host replies, so a poller must remember that it is already handling it (inferred).

---

## 6. Command 1: stream geometry and output-buffer setup

### 6.1 What the firmware reports

| Register | Bits | Content |
|---|---|---|
| AV_SCRATCH_1 | 7:0 | **mb_width**: picture width in 16×16 macroblocks |
| | 23:8 | **mb_total**: macroblocks per picture |
| | 30:24 | **max_ref field**. Linux uses max_refs = field + 1 |
| | 31 | not read by Linux |
| AV_SCRATCH_2 | 0 | aspect_ratio_info_present (VUI) |
| | 23:16 | aspect_ratio_idc (H.264 Table E-1; 255 = Extended_SAR) |
| AV_SCRATCH_3 | 15:0 | sar_width, only meaningful when the idc is 255 |
| | 31:16 | sar_height, only meaningful when the idc is 255 |
| AV_SCRATCH_6 | 7:0 | **crop_bottom**, in luma lines |
| | 23:16 | **crop_right**, in luma pixels |

The other bits of AV_SCRATCH_2 and AV_SCRATCH_6 are not read by Linux; their meaning is **UNVERIFIED**.

**What the host derives from them:**
- mb_height = mb_total / mb_width (integer division).
- Display width = 16 × mb_width − crop_right.
- Display height = 16 × mb_height − crop_bottom.
- Linux uses this display size as the session's picture size.
- No register Linux reads reports a left or top crop. Whether the firmware supports either is **UNVERIFIED**.

**Pixel aspect:**
- aspect_ratio_idc values 1–16 map to the sample aspect ratios of H.264 Table E-1: 1 = 1:1, 2 = 12:11, …, 16 = 2:1.
- Linux treats an absent aspect, idc 0 and idc 17–254 as 1:1.
- For idc 255, Linux takes the low half of AV_SCRATCH_3 as the width term and the high half as the height term. That order is consistent with Linux's Table E-1 handling, but unchecked on hardware (**UNVERIFIED**).

**max_ref field.** Linux calls max_refs "max reference" and uses it twice: for the buffer minimum (§6.3) and for the MV buffer (§6.5). Whether the field is the SPS max_num_ref_frames, max_dec_frame_buffering or a DPB size derived from the level is **UNVERIFIED**. Compare it with the stream's SPS on the board. Seven bits give max_refs = 1–128.

**Field widths and limits:**
- mb_width has 8 bits, so up to 255 macroblocks (4,080 pixels).
- mb_total has 16 bits.
- For field-coded or MBAFF streams, whether mb_total counts frame macroblocks is **UNVERIFIED**.

**When command 1 comes:**
- Linux's comment says the firmware sends it when it detects a parameter-set change. The first SPS is such a change.
- Whether a repeated, identical SPS sends it again is **UNVERIFIED**. §11.2 explains why Linux's design suggests it does not.

**Example values in AV_SCRATCH_1, ignoring the max_ref field:** 1920×1080 → mb_width 120, mb_total 8160 → 0x001FE078. 1280×720 → 80 and 3600 → 0x000E1050.

### 6.2 Output picture geometry

**Linux's rule.** It sizes canvases and buffers from the display size exactly as base §6:
- W32 = the display width rounded up to a multiple of 32; H32 = the display height rounded up to a multiple of 32.
- Y canvas W32 × H32; UV canvas W32 × H32/2.
- Both canvases linear, with endian 7.
- The Y buffer is W32 × H32 rounded up to 64 KiB, and the UV buffer is half of that.

**Caveat.** The decoder writes whole macroblocks: 16·mb_width × 16·mb_height.
- Rounding the display size up to 32 covers that only while each crop is under 16.
- H.264 allows larger crops, and Linux's rule would then make the canvas smaller than the decoded picture.
- Safer rule for a new driver (a recommendation, not Linux): W32 = 16·mb_width rounded up to 32, and H32 = 16·mb_height rounded up to 32.

**Examples:**

| Stream | Macroblocks (w × h) | Canvas W32 × H32 | Y bytes | UV bytes | One buffer |
|---|---|---|---|---|---|
| 1920×1080 | 120 × 68 | 1920 × 1088 | 0x200000 | 0x100000 | 3.00 MiB |
| 1440×1080 | 90 × 68 | 1440 × 1088 | 0x180000 | 0xC0000 | 2.25 MiB |
| 1280×720 | 80 × 45 | 1280 × 736 | 0xF0000 | 0x78000 | 1.41 MiB |
| 720×576 | 45 × 36 | 736 × 576 | 0x70000 | 0x38000 | 0.66 MiB |
| 720×480 | 45 × 30 | 736 × 480 | 0x60000 | 0x30000 | 0.56 MiB |
| 640×360 | 40 × 23 (coded 368 lines) | 640 × 384 | 0x40000 | 0x20000 | 0.38 MiB |

### 6.3 How many output buffers

**Linux's count:**
- Minimum: **max_refs + 5**, which Linux publishes as the V4L2 "minimum capture buffers".
- The count n is what the application allocates, raised to that minimum, at least 2 and at most **24**.
- The cap of 24 is applied last, so it wins: if max_refs + 5 > 24, then n = 24.
- Why +5 is not explained. Presumably it is headroom for pictures being displayed or in flight (**UNVERIFIED**).

**All n buffers go to the firmware in the reply, and all of them are free at that moment.**
- At the first command 1, V4L2 forces every capture buffer to be queued to the driver before streaming starts.
- So the canvas table covers all n buffers, and none of them is handed back with §8.

**Why 24:**
- The limit matches the 24 canvas registers Linux uses.
- The buffer-index field in a frame report is 5 bits wide (0–31), so the hardware may have more registers.
- Whether the firmware accepts more than 24 is **UNVERIFIED**.

### 6.4 The canvas table

- Firmware buffer index i (0 to n − 1) corresponds to the register at **ANC0_CANVAS_ADDR + 4·i = 0x2640 + 4·i**, which covers 0x2640–0x269C for 24 buffers. dos_regs.h names only ANC0_CANVAS_ADDR; Linux addresses the others as the consecutive words after it.
- Each word has the layout of base §5.5:

| Bits | Content |
|---|---|
| [7:0] | Y canvas index |
| [15:8] | UV canvas index |
| [23:16] | UV canvas index again |
| [31:24] | 0 |

- Two LUT entries per buffer, so up to 48 canvas indexes for 24 buffers. The MPEG-1/2 driver on this board uses 16, at 0x80–0x8F.
- Program each canvas in the DMC LUT (base §6) before the reply.
- Linux takes new canvas indexes at every command 1 and frees them only at session stop. Its per-session table holds 96.

### 6.5 The reference MV buffer and the reply

**Size of the reference MV buffer:**
- mbw4 = mb_width rounded up to a multiple of 4.
- mbh4 = mb_height rounded up to a multiple of 4.
- size = mbw4 × mbh4 × **96** × max_refs bytes.
- Linux's name for the 96 is the motion-vector size per macroblock. So the buffer holds one MV slot per reference picture, not one per output buffer.
- Linux allocates it as one contiguous block. How the firmware divides it is **UNVERIFIED**.

| Stream | mbw4 × mbh4 | One slot (× 96) | With max_refs = 5 |
|---|---|---|---|
| 1920×1080 | 120 × 68 | 783,360 (0xBF400) | 3,916,800 |
| 1280×720 | 80 × 48 | 368,640 (0x5A000) | 1,843,200 |
| 720×576 | 48 × 36 | 165,888 (0x28800) | 829,440 |
| 720×480 | 48 × 32 | 147,456 (0x24000) | 737,280 |
| 640×360 | 40 × 24 | 92,160 (0x16800) | 460,800 |

**The reply, in Linux's order:**

| # | Register | Offset | Value |
|---|---|---|---|
| 1 | Read AV_SCRATCH_1, 2, 3 and 6 first | | step 4 overwrites AV_SCRATCH_1 |
| 2 | ANC0_CANVAS_ADDR + 4·i, for i = 0 … n − 1 | 0x2640 + 4·i | the canvas words (§6.4), with the LUT already programmed |
| 3 | (allocate the reference MV buffer; zeroed by Linux's allocator) | | |
| 4 | AV_SCRATCH_1 | 0x2704 | reference MV base, physical |
| 5 | AV_SCRATCH_4 | 0x2710 | reference MV base + size: the end, exclusive |
| 6 | **AV_SCRATCH_0** | 0x2700 | **(max_refs << 24) \| (n << 16) \| ((max_refs − 1) << 8)** |

- **Step 6 does two jobs:** it hands the parameters over and acknowledges command 1, because its low byte is 0. It must be the last write, because it releases the firmware (inferred).
- **Example:** max_refs = 5 and n = 10 give 0x050A0400.
- Bits 31:24 and bits 15:8 both come from max_refs. What the firmware does with each is **UNVERIFIED**.
- If the allocation in step 3 fails, Linux abandons the session without replying.
- **Before the reply** the firmware stays stopped and sends no more commands. No timeout is known (**UNVERIFIED**). Linux may leave it waiting for as long as the application takes to allocate buffers.

**Memory for one 1080p session** with max_refs = 5 and n = 10:
- pictures: 30 MiB;
- reference MV: 3.7 MiB;
- workspace: 1.9 MiB;
- extension and SEI: 28 KiB;
- the VIFIFO comes on top.

---

## 7. Command 2: frames ready

| Field | Where | Content |
|---|---|---|
| Frame count n | AV_SCRATCH_0 bits 15:8 | number of frames in this report |
| Status of frame i | AV_SCRATCH_(1 + i), i.e. 0x2704 + 4·i | see the rows below |
| — buffer index | status bits 4:0 | firmware buffer index, **0-based** (no −1, unlike MPEG-1/2) |
| — picture structure | status bits 7:5 | 5 = interlaced, top field first; 6 = interlaced, bottom field first; Linux treats every other value as progressive |
| — error | status bit 9 | decoded, but parts of the picture have artefacts (Linux: typically a corrupt bitstream) |
| — stream offset, low half | status bits 31:16 | bits 15:0 of the VIFIFO offset |
| Stream offset, high half | AV_SCRATCH_A + 4·⌊i/2⌋ | even i: bits 15:0 of that register; odd i: bits 31:16. This half supplies bits 31:16 of the offset |
| Error count | AV_SCRATCH_D | number of decode errors. If it is non-zero, Linux logs it and writes AV_SCRATCH_D ← 0 |

**The offset:**
- offset = (high half << 16) | low half, a byte offset into the stream as written to the VIFIFO.
- Linux matches it to input timestamps as in base §5.6 step 8 and §3.7.
- Whether it is cumulative or wraps with the ring is the base document's open question 12. A full 32-bit offset for a 16 MiB ring suggests cumulative (**UNVERIFIED**).

**Order of the host's actions (Linux):**
1. Read AV_SCRATCH_D, and zero it if it is non-zero.
2. For i = 0 … n − 1, read the status and the offset high half, and deliver the frame.
3. AV_SCRATCH_0 ← 0.
4. The SEI check (§5.3).

Frames are delivered in report order.

**Maximum n:**
- Linux puts no bound on n.
- The register layout gives one: frame 6's status would sit in AV_SCRATCH_7, a hand-back slot, and its high offset half in AV_SCRATCH_D, the error count. So n ≤ 6 in practice.
- The firmware's real maximum is **UNVERIFIED**; log n on the board.

**Unknowns:**
- Status bits 8 and 15:10 are **UNVERIFIED**.
- Picture-structure values 0–4 and 7 are **UNVERIFIED**.
- The 5/6 coding is not H.264's SEI pic_struct numbering, where top-bottom and bottom-top are 3 and 4. It is the firmware's own coding.

**Pictures with the error flag:** Linux delivers them normally and only logs the flag.

**Interlaced pictures:** both fields arrive in one buffer, woven, as with MPEG-2 (confirmed in M60 for field-coded and MBAFF streams: both fields woven, as FFmpeg does).

---

## 8. Handing buffers back (recycle)

- **Two slots:** AV_SCRATCH_7 (0x271C) and AV_SCRATCH_8 (0x2720). A slot is free when it reads 0.
- **To hand back buffer k:** write **k + 1** (the firmware index + 1) into a free slot. Linux tries AV_SCRATCH_7 first and uses AV_SCRATCH_8 only if 7 is busy. Linux's comment says AV_SCRATCH_8 serves the same purpose. So at most two hand-backs can be pending.
- **The firmware zeroes a slot** once it has taken the buffer (inferred from Linux's polling).
- **Polling:** as base §5.7, from a thread every 5–10 ms.
- **Index caveat:** as base §5.7, Linux writes its V4L2 index + 1, which is right only while that index equals the firmware index. Use the firmware index + 1.
- **Buffer count:** there is no separate handshake. The count is given once, in the command 1 reply (§6.5).
- **What to hand back:** only buffers the firmware has reported in command 2. The n buffers named in the reply start out free.
- **A Linux quirk to avoid:** if the application queues capture buffers while both queues stream but before command 1 arrives, Linux puts them on its hand-back list. It may then write them into the slots before the firmware has a canvas table. The effect is **UNVERIFIED**.
- **No free buffer:** Linux has no command for this case. The firmware presumably waits.
- **Do not write into a reported buffer before handing it back.** It may still be a reference picture for the firmware (inferred, **UNVERIFIED**). Reading it is fine.

---

## 9. Output order and the DPB

**Linux relies on the firmware for display order, so the firmware manages the DPB.**
- Linux does no reordering for H.264. It delivers frames in report order and matches timestamps by stream offset.
- The V4L2 stateful-decoder interface requires capture buffers in display order.
- Together, these mean the firmware does reference marking, reordering and bumping itself.
- On GXBB this is **UNVERIFIED**. For MPEG-2, base §11 found display order.

**Supporting facts:**
- The host never tells the firmware which pictures are references.
- The host only supplies n ≥ max_refs + 5 picture buffers and MV storage for max_refs pictures.
- Each reported picture carries its own stream offset. In display order these offsets are not monotonic.

**Linux's timestamp matching handles out-of-order offsets:**
1. It takes the newest input record whose offset is ≤ the frame's offset.
2. It removes that record.

With an I P B B input, the B pictures get their own records and P gets its own when it is reported last.

---

## 10. End of stream

### 10.1 What Linux sends

Linux sends one 4,096-byte buffer through the parser:
- **No padding step.** It is exactly 4 KiB already.
- **The parser writes:**
  - PARSER_CONTROL with pack size 4096;
  - PARSER_FETCH_CMD = 0x38000000 | (4096 + 512);
  - the 512 bytes after the buffer are zeros, from a freshly zeroed allocation.
- **Not checked** against VIFIFO free space.
- **No timestamp record**, and it is not counted as an input buffer.

Its content, decoded from Linux's byte array (offsets within the 4,096 bytes):

| Offset | Length | Content |
|---|---|---|
| 0 | 4 | start code `00 00 00 01` |
| 4 | 488 | SEI NAL (header byte 0x06): payload type 5 (user data unregistered), 483 bytes: x264's UUID and its version and options string ("x264 - core 67") |
| 492 | 4 | start code |
| 496 | 20 | SPS NAL (0x67), see below |
| 516 | 4 | start code |
| 520 | 4 | PPS NAL (0x68), see below |
| 524 | 4 | start code |
| 528 | 72 | IDR slice NAL (0x65), see below |
| 600 | 4 | `00 00 01 FF`: the parser marker, inside the 4,096 bytes |
| 604 | 3,492 | zeros |

**SPS:**
- Main profile (77), constraint_set1, level 1.0, SPS id 0.
- 1 × 1 macroblock, i.e. 16×16; frame_mbs_only.
- max_num_ref_frames 1; POC type 0; log2_max_frame_num 9; log2_max_poc_lsb 10; no cropping.
- VUI: timing 1/50, and bitstream_restriction with num_reorder_frames 0 and max_dec_frame_buffering 1.

**PPS:** id 0, CABAC, one reference index per list, initial QP 26, chroma QP offset −2, deblocking control present.

**IDR slice:**
- One I slice covering the picture.
- frame_num 0, idr_pic_id 0, POC lsb 0, slice QP delta −16.
- **no_output_of_prior_pics_flag = 0.**

**Why a dummy picture:**
- Linux's comment says this 16×16 picture makes the firmware drain, and that no alternative is known.
- **Inferred reason:** an IDR with no_output_of_prior_pics_flag = 0 makes an H.264 decoder output every picture still in its DPB before decoding the IDR (H.264 Annex C). The new SPS also changes the size.
- Which of the two the firmware reacts to is **UNVERIFIED**.

**Making your own instead of copying Linux's bytes:**
1. Encode one 16×16 frame with any H.264 encoder to Annex B.
2. Keep the SPS, PPS and the IDR, with no_output_of_prior_pics_flag = 0.
3. Append `00 00 01 FF`.
4. Zero-pad to 4,096 bytes.

Whether the SEI NAL, CABAC rather than CAVLC, Main profile or exactly 16×16 matters is **UNVERIFIED**.

### 10.2 What Linux waits for

**The parser wait.** Linux ignores its result.
- The marker sits inside the pack size here; normal submissions put it after (base §3.4). Whether the parser interrupt fires at all is therefore **UNVERIFIED**.
- If it does not fire, the wait is a 200 ms timeout.
- Unlike a normal submission, Linux does not write PARSER_FETCH_CMD = 0 after an EOS timeout.

**Then** Linux waits until 50 ms pass with no vdec interrupt (base §8). Frames reported meanwhile are handled normally, with the same "last frame" tagging as base §5.9.

**The dummy picture itself (inferred from Linux's code paths, UNVERIFIED):**
- If the firmware treats the 16×16 SPS as a change, it sends command 1.
- Linux takes that as a resolution change. Because the size differs, Linux does not reply.
- The firmware then stays stopped, and the dummy is never decoded or shown.
- If the firmware decoded it instead, it would report a 16×16 picture whose offset matches no input.

**Recommendation for Haiku:**
1. Before submitting the dummy, record its stream offset: PARSER_VIDEO_WP − VIFIFO base, plus wraps (base §3.7).
2. After that, drop any reported frame whose offset is at or past the recorded one.
3. Never reply to a command 1 after EOS.
4. Then power down.
5. To decode again, start a new session. Linux does the same: STREAMOFF powers VDEC_1 down.

---

## 11. Errors and mid-stream resolution changes

### 11.1 Errors

| Event | Linux's reaction |
|---|---|
| Command 6 (fatal) | logs it and abandons the session: no register writes, no recovery. Power-down comes at stop |
| Command 7 or 8 (bad width or height) | logs (AV_SCRATCH_1 + 1) × 16 and abandons, as for command 6 |
| Error bit 9 in a frame status | delivers the frame normally |
| AV_SCRATCH_D ≠ 0 (read only at command 2) | logs the count and writes 0 |
| Parser timeout on an input buffer | drops that buffer and writes PARSER_FETCH_CMD = 0 (base §3.5) |

The firmware's own size limits on GXBB are unknown. Linux's 1920×1080 is policy (§13).

### 11.2 Resolution change mid-stream

**What the firmware does:** it sends command 1 again with the new geometry.
- Linux assumes it outputs every picture of the old sequence before that, because Linux may free the old buffers once it sees command 1 (**UNVERIFIED**).

**Linux, case A:** same display size and enough buffers. Linux replies at once from the interrupt thread and repeats §6.4–§6.5:
- new canvas indexes;
- a new reference MV buffer (Linux leaks the old one until stop);
- canvas words only for the buffers queued to the driver at that moment;
- yet n in the reply is the full count.

A new driver should not copy this case.

**Linux, case B:** the size changed, or there are too few buffers.
- Linux does not reply. It signals a source change to the application, and the firmware waits with command 1 pending.
- The application drains, reallocates and restarts its capture queue. Linux then runs §6 with the new buffers.
- The AMRISC is not restarted and the firmware is not reloaded.

**Repeated SPS (inferred, UNVERIFIED):**
- Every command 1 costs Linux 2·n canvas indexes out of 96 per session. With 24 buffers, the third command 1 would fail.
- Many streams repeat the SPS before every IDR, and they evidently decode on Linux.
- So the firmware most likely sends command 1 only when the parameters really change.

**Recommendation for Haiku:** treat a later command 1 like the first.
1. Wait until the consumer has returned every buffer.
2. Reuse the buffers if they are big enough and there are at least max_refs + 5 of them; otherwise reallocate.
3. Rebuild the whole canvas table and a new reference MV buffer.
4. Reply.

After the reply the firmware presumably treats all n buffers as free (**UNVERIFIED**), so no buffer may be held by the consumer at that point.

---

## 12. Bitstream requirements

**Format:**
- Annex B byte stream with 3- or 4-byte start codes. Linux's input format is "H.264 with start codes".
- Linux passes the bytes unchanged: no NAL parsing, no conversion and no change to emulation-prevention bytes.

**Parameter sets:**
- SPS and PPS must be in the stream before the first slice. No register can pass them to the firmware.
- Container sources such as MP4 or MKV store length-prefixed NAL units, with the SPS and PPS in the avcC header. For those:
  - replace each length prefix with a start code;
  - insert the SPS and PPS before the first IDR.

  This is standard H.264 practice, not something Linux does.
- Start at an IDR. Linux records "keyframe found" at command 1 but never uses it. What the firmware does with slices before any SPS, such as a stream cut mid-GOP, is **UNVERIFIED**.

**Parser:** the rules of base §3 apply unchanged: zero padding to 4 KiB and the `00 00 01 FF` trailer after the data.
- Zero bytes between NAL units are legal in Annex B (trailing_zero_8bits).
- If the trailer reached the decoder, it would parse as a NAL with forbidden_zero_bit set and type 31 (unspecified), which decoders skip. Base §10 found that no trailer bytes reach the decoder anyway.

**One access unit per submission.**
- Linux does not set V4L2's "continuous byte stream" flag, so its contract with applications is one access unit per input buffer. Linux itself does not check this.
- Linux keeps one timestamp record per submission (base §3.7), and a frame takes the newest record at or before its offset. Two consequences:
  - A submission with only SPS or PPS leaves a record that is never matched and never removed. It also keeps Linux's "last frame" test from ever becoming true.
  - Two pictures in one submission give the second picture a wrong timestamp.
- **Rule for Haiku:** submit SPS, PPS and SEI together with the picture that follows them, one picture per submission.

**Limits:** there is no H.264-specific size limit per submission. VIFIFO flow control (base §3.6) applies.

---

## 13. GXBB specifics

**Linux's GXBB H.264 entry:**

| Field | Value |
|---|---|
| Input format | H.264 (Annex B) |
| Capture formats | NV12 (two planes) only |
| Capture buffers | min 2, max 24 (plus the stream minimum max_refs + 5, §6.3) |
| Maximum size | **1920 × 1080** |
| Firmware | `meson/vdec/gxbb_h264.bin` |
| Flags | compressed; dynamic resolution (source-change events) |
| Hardware block | VDEC_1 (`vdec_1_ops`) |

- The minimum of 256×144 applies only to format negotiation. Linux uses the firmware-reported size unclamped.

**Other SoCs:**

| SoC | Firmware | Maximum size |
|---|---|---|
| GXL, GXLX | gxl_h264.bin | 3840×2160 |
| GXM | gxm_h264.bin | 3840×2160 |
| G12A, SM1 | g12a_h264.bin | 3840×2160 |

All other fields of the entry are the same.

**The code path is identical on every SoC.**
- The H.264 codec has no SoC checks.
- VDEC_1's only SoC branch is SM1's power and isolation bits (base §1.3).
- So GXBB differs from GXL here only in the firmware file and Linux's size limit.

**Is 1920×1080 a hardware limit?** Whether it is a limit of the GXBB hardware or firmware, or Linux policy, is **UNVERIFIED**. The register fields allow up to 4,080 pixels wide.

---

## 14. DOS registers on the H.264 path

### 14.1 DOS registers this path uses that base §9.1 does not list as used

| Name | Byte offset | Word index | Physical | Use |
|---|---|---|---|---|
| ANC0_CANVAS_ADDR … ANC23 (the names after ANC0 are by extension) | 0x2640 … 0x269C | 0x0990 … 0x09A7 | 0xC8822640 … 0xC882269C | canvas word for firmware buffer 0 … 23 (§6.4) |
| MDEC_PIC_DC_THRESH | 0x26E0 | 0x09B8 | 0xC88226E0 | 0x404038AA at start (§3) |
| AV_SCRATCH_G | 0x2740 | 0x09D0 | 0xC8822740 | extension buffer physical (§1.3, §3) |
| AV_SCRATCH_I | 0x2748 | 0x09D2 | 0xC8822748 | SEI buffer, relative (§2, §3) |
| AV_SCRATCH_J | 0x274C | 0x09D3 | 0xC882274C | bit 15 = SEI ready; the host writes 0 (§5.3) |

### 14.2 Registers in base §9.1 with a different use or value under H.264

| Name | Byte offset | H.264 use |
|---|---|---|
| AV_SCRATCH_0 | 0x2700 | command register and command 1 reply (§5, §6.5); not a canvas word |
| AV_SCRATCH_1 | 0x2704 | workspace − 16 MiB → stream info → reference MV base → frame 0 status (§4) |
| AV_SCRATCH_2, 3, 5 | 0x2708, 0x270C, 0x2714 | aspect, extended SAR, frame 1, 2 and 4 status |
| AV_SCRATCH_4 | 0x2710 | reference MV end; frame 3 status |
| AV_SCRATCH_6 | 0x2718 | crop; frame 5 status |
| AV_SCRATCH_7, 8 | 0x271C, 0x2720 | hand-back slots (§8) |
| AV_SCRATCH_A, B, C | 0x2728–0x2730 | high halves of the frame offsets (§7) |
| AV_SCRATCH_D | 0x2734 | decode error count (§7) |
| AV_SCRATCH_F | 0x273C | configuration: clear bits 5:2, set bits 7 and 4 (§3) |
| POWER_CTL_VLD | 0x3020 | bits 9 and 6 set on top of base §4's 0x10 (§3) |
| MDEC_PIC_DC_CTRL | 0x2638 | bit 17 always set (NV12 only) |

### 14.3 Base registers the H.264 path does not touch

| Register(s) | Byte offset(s) |
|---|---|
| AV_SCRATCH_9 | 0x2724 |
| AV_SCRATCH_E | 0x2738 |
| AV_SCRATCH_H | 0x2744 |
| AV_SCRATCH_K, L | 0x2750, 0x2754 |
| MPEG1_2_REG | 0x3004 |
| PIC_HEAD_INFO | 0x300C |
| M4_CONTROL_REG | 0x30A4 |
| PSCALE_CTRL | 0x2444 |

The registers that base §9.1 lists as "defined but not on the MPEG-1/2 path" are not used by H.264 either: LMEM_DMA_CTRL, MC_*, PSCALE_RST/BMEM, DBLK_*, MDEC_PIC_DC_STATUS, DCAC_DMA_CTRL, VDEC_ASSIST_AMR1_INT8 and MCPU_INTR_MSK. The exceptions are ANC0_CANVAS_ADDR and MDEC_PIC_DC_THRESH, which are in §14.1.

The ESPARSER registers and the registers outside DOS are exactly those of base §9.2 and §9.3.

---

## 15. Open questions (UNVERIFIED items, collected)

1. **Extension:** whether the firmware reads the 20 KiB extension buffer at run time (Linux keeps it until stop) and what alignment it needs. Whether the extension really is five 4 KiB instruction overlays.
2. **Workspace:**
   - whether the firmware needs it zeroed;
   - what alignment it needs;
   - whether the 16 MiB subtraction may wrap (a workspace below 16 MiB);
   - how much of the 0x1EE000 bytes is used.
3. **Start values:** the meaning of POWER_CTL_VLD bits 9 and 6, AV_SCRATCH_F bits 4 and 7 (and what the preserved bits do), and MDEC_PIC_DC_THRESH = 0x404038AA.
4. **Stale scratch registers:** whether AV_SCRATCH_0/7/8/F survive DOS_SW_RESET0, and whether leftovers from an earlier MPEG session upset the H.264 firmware.
5. **The max_ref field** (AV_SCRATCH_1 bits 30:24) at command 1: is it the SPS max_num_ref_frames, max_dec_frame_buffering or a level-derived DPB size? Also bit 31.
6. **Reply word:** what the firmware does with bits 31:24 and bits 15:8 of the command 1 reply. Why Linux asks for max_refs + 5 buffers.
7. **Buffer limit:** whether more than 24 output buffers (ANC registers beyond 0x269C) are accepted.
8. **Crop and interlace reporting:** left and top crop; the other bits of AV_SCRATCH_2 and AV_SCRATCH_6; whether mb_total counts frame macroblocks for field or MBAFF streams.
9. **Repeated SPS:** whether command 1 is sent again for an identical SPS.
10. **Old sequence on a change:** whether all pictures of the old sequence are output before a mid-stream command 1.
11. **Frame reports:** the maximum number of frames per command 2 (the layout suggests 6). Status bits 8 and 15:10. Picture-structure codes other than 5 and 6.
12. **Stream offset:** whether the 32-bit offset is cumulative or wraps with the VIFIFO (the same question as base open question 12).
13. **Display order:** whether GXBB's H.264 firmware outputs in display order, and whether a reported buffer can still be used as a reference while the host holds it.
14. **Behaviour when blocked:** what the firmware does when all buffers are with the host; whether it stalls when AV_SCRATCH_J bit 15 is not cleared; whether it times out while waiting for the command 1 reply.
15. **Unknown command codes:** 3–5 and 9.
16. **EOS:**
    - whether the parser interrupt fires for the EOS buffer, whose marker is inside the pack;
    - whether the 16×16 dummy raises command 1, is decoded and output, or neither;
    - which parts of the dummy (SEI, CABAC, profile, size, the IDR flag) are needed.
17. **Mid-stream start:** firmware behaviour with slices before the first SPS.
18. **Size limit:** whether 1920×1080 is a GXBB hardware or firmware limit or Linux policy, and what triggers commands 7 and 8 on GXBB.
19. **Untested combination:** whether Linux's H.264 path was ever run on GXBB. The commit names no tested SoC. (The port's own H.264 path runs on GXBB: §16.)

---

## 16. Found on the board (ODROID-C2, 2026-10-30)

These are observations from Haiku's `meson_vdec` on this board, not facts from Linux. They answer some of §15's questions; the driver follows them.

| §15 item | Found |
|---|---|
| 2 (workspace alignment) | Every session whose workspace was aligned to 4 KB only ended in command 6 (fatal) within about 130 ms of the command 1 reply. Every session whose workspace was aligned to 8 KB or more worked. The driver now aligns each buffer to its size rounded up to a power of two, at most 1 MB, as Linux's allocations are. |
| 4 (stale scratch registers) | AV_SCRATCH_0 to _L read 0 at every session start, after the power-down and power-up of base §1. |
| 5 (the max_ref field) | 4 for x264 streams with 4 reference frames (max_refs 5); 3 for the Baseline stream with 3; 1 for the 16×16 picture with 1. Bit 31 was set every time. |
| 11 (frames per report) | Up to 6 frames in one command 2, in the end-of-stream flush. |
| 12 (stream offset) | Cumulative, counted in VIFIFO bytes from the session start: past 32 MiB with an 8 MiB VIFIFO. It equals what the host wrote, padding included. |
| 13 (display order) | Display order, for B-frames and B-pyramids alike: 4 test streams match FFmpeg frame for frame. |
| 15 (command 9) | Command 9 comes right after every command 1 reply; acknowledging it with 0 is enough. |
| 16 (end of stream) | The 16×16 IDR picture makes the firmware put out every frame it holds. After that, it sends **no** command 1 for the picture and stops taking frames back. So the driver ends the stream when the firmware has read everything before the picture and reported nothing for 100 ms. Hand-backs wait in a queue for free slots. |
| 18 (limits) | 1920×1080 works (coded 1088, cropped). Larger sizes weren't tried. |

**The first session after boot** (also MPEG-2's open item, base §11):
- The first macroblock's luma of the first picture comes out wrong: brighter, with the top-left corner garbled.
- Intra prediction spreads the error over the whole picture, and P and B pictures carry it to the next IDR picture. Chroma is right.
- Later sessions of the same boot are right, even though each session powers VDEC_1 down and up.
- A session that decodes a 16×16 IDR picture first fixes it until the next boot. The picture must be written twice (the second start code ends the first picture's slice; one copy followed by zeros was decoded only sometimes), and the firmware must run about 100 ms after the command 1 reply.
- The driver runs such a warm-up session before the first H.264 session of each boot: 5 boots of 5 right.

**Speed:**
- 1080p60 at 10 Mbit/s (High, 3 B-frames): about 145 frames/s.
- A new session (each seek of a player) needs about 40 ms to the command 1 reply when the buffers are kept from the last session, plus the decoding up to the first picture in display order.
- The serial debug output costs about 1 ms for every 11 characters, so nothing is logged per session.

---

## Sources

All files are from torvalds/linux master as of 2026-10-30 (HEAD abf8c33470f7), fetched raw from GitHub:

- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/codec_h264.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/codec_h264.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_1.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_helpers.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_helpers.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_platform.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/esparser.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/esparser.h (ESPARSER_MIN_PACKET_SIZE)
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/dos_regs.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/codec_mpeg12.c (only to compare where the mailbox is acknowledged)
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/TODO
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gx.dtsi (the reserved 0–16 MiB "hwrom" region)
- Commit history of codec_h264.c and of the vdec directory, via https://api.github.com/repos/torvalds/linux/commits?path=drivers/staging/media/meson/vdec (d7647e7c90f5 added H.264; later: 69050f8d6d07, bf4afc53b77a, 2a1a564adab1)
- Firmware: `/usr/lib/firmware/meson/vdec/{gxbb,gxl,gxm,g12a}_h264.bin.zst` from the host's linux-firmware 20260319.git217ca6e4, decompressed and compared locally
- The EOS buffer's NAL structure (§10.1) was decoded locally from Linux's byte array with an H.264 header parser written for this purpose. Only the parsed parameters are reproduced here, not the bytes.
- H.264 syntax facts (Table E-1, the Annex C bumping process, trailing_zero_8bits, NAL type 31) come from ITU-T H.264, not from Linux.
