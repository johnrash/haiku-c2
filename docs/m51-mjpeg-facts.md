Facts read from Amlogic's GPL vendor driver (`drivers/amlogic/amports/vmjpeg.c` and its helpers) in Hardkernel's ODROID-C2 kernel `odroidc2-3.14.y` at c3e4c730feb1 (2018-03-03); no code copied.

# M51: S905 (GXBB) VDEC_1 MJPEG decoding, hardware facts

This document is the input for MJPEG support in `meson_vdec`, the clean-room, MIT-licensed Haiku driver for the S905's video decoder. The target stream is the Logitech C920's: 1920×1080 and 1280×720 baseline JPEG frames, 4:2:2, each frame with its own Huffman tables. The microcode is to be linux-firmware's `gxl_mjpeg.bin`. The document describes how Amlogic's own kernel drove MJPEG on VDEC_1 (`amvdec_mjpeg`), and where that differs from what the Haiku driver already does for MPEG-1/2.

**Related documents:**
- `m26-vdec-facts.md` is the **base document**, cited as "base §N". It covers the DOS block, VDEC_1 power and clocks, the AMRISC microcode load by IMEM DMA, the ESPARSER, the VIFIFO, the canvas LUT, the mailbox interrupt and the MPEG-1/2 protocol. None of that is repeated here.
- `m26-vdec-hevc-vendor-facts.md` (cited as "HEVC-vendor §N") describes the same vendor tree for HEVC: how the vendor reaches HHI, the "vdec" gate, `vdec_poweron`, the DMC request bit.
- `m48-venc-facts.md` (cited as "VENC §N") describes the same tree for the encoder.

**Conventions (as in the base document):**
- DOS offsets are **byte offsets from the DOS base 0xC8820000**. Amlogic's headers number DOS registers by 32-bit word index; byte offset = word index × 4. Both are given where a register first appears, and §10 lists them all.
- CBUS registers (reset, parser) are given by their CBUS word number and physical address (0xC1100000 + word × 4).
- "Set bits" and "clear bits" mean read-modify-write; "write" means a full 32-bit store; "pulse" means write the bits, then write 0.
- **UNVERIFIED** marks a meaning the vendor source does not state, or one that is inferred.
- "The vendor" means this driver as built for GXBB (`get_cpu_type()` = MESON_CPU_MAJOR_ID_GXBB). vmjpeg.c itself has no chip branches: its only conditionals are "Meson6 or later" blocks hard-wired on, and a compiled-out NV21 output mode (§5.5).

Nothing in this document has been run on the board yet.

---

## 0. At a glance

| Item | Vendor driver on GXBB |
|---|---|
| Microcode | `vmjpeg_mc`, built into the kernel: 1,096 words (4,384 bytes), zero-padded to 16 KiB, loaded once per session by IMEM DMA. The same image is registered for M8, M8M2 and GXBB (§1) |
| linux-firmware's `gxl_mjpeg.bin` | 16 KiB. Not identical, but evidently a later build of the same program: 768 of the vendor's 1,008 non-zero words recur in the same order. Protocol compatibility is **UNVERIFIED** (§1.3) |
| Power, clock | the generic VDEC_1 power-up (base §1, HEVC-vendor §1.3). Clock 500 MHz at power-up, then lowered from width × height × fps: 166 MHz for 720p30, 333 MHz for 1080p30 (§2.2) |
| MJPEG-specific resets | DOS_SW_RESET0 bits 7:6 (IQIDCT and MC by the vendor's old names) at protocol start; bit 10 (PSCALE) in the scaler setup; bits 12:11 around the AMRISC start and stop (§2.3) |
| Stream input | exactly the MPEG path: ESPARSER into the VIFIFO, raw JPEG bytes, no padding, no per-frame marker or kick. The kernel never tells the decoder where a frame starts or ends (§3) |
| Workspace | none |
| Frames | **4** buffers, each three linear planes: Y, U and V, chroma at half width and **half height** (4:2:0 sizing), canvas endian 0 (§5) |
| Host protocol | the host gives the four canvas words and an initial free-buffer list; the microcode posts a 1-based buffer index in AV_SCRATCH_9 and the frame's stream offset in AV_SCRATCH_A; buffers go back through AV_SCRATCH_8 as index + 1 (§4) |
| Interrupt | mailbox 1, GIC SPI 44, acknowledged by writing 1 to ASSIST_MBOX1_CLR_REG (same as MPEG) |
| Size | the microcode is never told the width; the height goes in once, as a "reference height". The vendor sizes all canvases for 1920 × 1088 (§5.6) |
| Errors | no error, fatal-error or size register is read at all (§4.6) |
| Other units | the picture scaler (PSCALE) is enabled and loaded with a unity, 4-tap filter setup before the start (§6.2) |

**What the microcode does and what the CPU does.**
- The **microcode** finds each JPEG in the byte stream, parses its markers, decodes it into one of the four buffers and reports it. It must take the width, the tables and the sampling from the JPEG headers, since the kernel gives it none of them (inferred).
- The **CPU** powers the block, loads the microcode, programs the canvases, the scaler and a handful of scratch registers once, pushes the raw stream through the parser, takes the reports and hands the buffers back.

---

## 1. Microcode

### 1.1 What the vendor loads

- **Name:** `vmjpeg_mc`, requested by vmjpeg.c with format VFORMAT_MJPEG (3).
- **Where from:** a C array in `drivers/amlogic/amports/arch/ucode/mjpeg/vmjpeg_mc.c`, built into the kernel. The lookup tries the file system first only if `ENABLE_LOAD_FIRMWARE_FROM_FS` is defined, and it is commented out (`arch/firmware.c`). So the built-in copy is always used. Had it been enabled, the path would have been `video/<cpu name>/decoder/<format name>/vmjpeg_mc` under the Android firmware directory.
- **Registration:** the array is registered through `firmware_def.h` for the chip list {M8, M8M2, GXBB}. At boot only the copy for the running chip is kept. The same program therefore served Meson8, Meson8m2 and GXBB unchanged.
- **Version string:** "000000"; nothing else identifies the build.
- **Size:** 1,096 words = 4,384 bytes. 1,008 words are non-zero; the last non-zero word is word 1,094. Words 0 and 1 are zero, and the code starts at word 2.
- **Byte layout:** u32 words, stored little-endian in memory on AArch64. The DMA reads memory in byte order, so the image as loaded is the words little-endian, then zeros.
- **Fingerprint** (computed in memory; the microcode was not extracted or written to disk): md5 of the 16 KiB image as loaded (words little-endian, zero-padded) = `ef81b23f1ffa6de79fdcb0190c61c729`; md5 of the 4,384 bytes alone = `372f1ee1ed0bce86838569e7a188ffcf`.

### 1.2 How it is loaded

The same loader as every VDEC_1 codec (`amvdec_loadmc`):

1. Copy the image into a 16 KiB buffer and zero the rest. The lookup zero-fills everything after the 4,384 bytes.
2. MPSR = 0, CPSR = 0, then two dummy reads of MPSR ("for timing").
3. IMEM_DMA_ADR = the buffer's bus address; IMEM_DMA_COUNT = 0x1000 (words); IMEM_DMA_CTRL = 0x00078000.
4. Poll IMEM_DMA_CTRL bit 15 until it clears, yielding the CPU between reads; give up after 1 s.

This matches base §2. The only difference: the vendor clears MDEC_PIC_DC_CTRL bit 31 earlier, in `amvdec_enable` (§6.1), not in the loader. The image is loaded once per session, at decoder probe. The restart path of §4.7 does not reload it.

### 1.3 Is linux-firmware's `gxl_mjpeg.bin` the same program?

**The file:** `/lib/firmware/meson/vdec/gxl_mjpeg.bin.zst` (2,607 bytes compressed) decompresses to **16,384 bytes**, md5 `edd18fde0f3f47a717b5a628507fa8d3`. That is the full IMEM size, like the other GX files (base §2).

**Structure, from word statistics only (no words were printed or kept):**

| | Vendor `vmjpeg_mc` | `gxl_mjpeg.bin` |
|---|---|---|
| Length | 1,096 words, padded to 4,096 by the loader | 4,096 words |
| Non-zero words | 1,008 | 1,239 |
| Words 0–1 | zero; code from word 2 | zero; code from word 2 |
| Main code | words 2–1,094 | words 2–1,086 |
| Extra regions | none | words 3,073–3,077 and 3,104–3,364 (IMEM bytes 0x3004–0x3017 and 0x3080–0x3493) |

**Similarity:**
- Aligning the two sequences, 768 of the vendor's 1,008 non-zero words recur **in the same order** in the binary's words 0–1,086 (sequence ratio 0.775).
- Only 105 words are equal at the very same index, because insertions and deletions shift the code. The long common runs sit at nearly the same offsets (for example 110 words from vendor 115 at binary 114, and 74 from 341 at 337).
- Where runs of equal length differ, 42 of 57 words keep their top 20 bits. That is consistent with changed addresses or constants in otherwise identical instructions (**UNVERIFIED** reading; the AMRISC instruction format is not documented here).
- Read big-endian, the binary does not match at all (ratio 0.035). So the byte order of the file is the same as the vendor's in-memory image.

**The extra high region:**
- linux-firmware's `gxl_mpeg4_5.bin` has non-zero words at the same places (3,072–3,075 and 3,104–3,195), and 17 non-zero words of `gxl_mjpeg.bin`'s high region are identical at the same index there.
- About 16 of the vendor's early words (32–47) recur at the start of that region (3,104–3,121).
- This looks like code that later builds place at a fixed IMEM address common to several GXL programs, perhaps an interrupt handler (**UNVERIFIED**).
- The loader DMAs all 16 KiB anyway, so the extra region needs no special handling.

**Verdict:**
- `gxl_mjpeg.bin` is a later build of the vendor's MJPEG decoder, not a different program.
- **Chip:** the vendor ran one image on M8 through GXBB. GXBB's VDEC_1 runs linux-firmware's `gxl_mpeg12.bin` (base §10, on this board). So a GXL build is expected to run on GXBB (**UNVERIFIED** for MJPEG).
- **Protocol:** whether `gxl_mjpeg.bin` keeps the exact protocol of §4 (the scratch registers, the free list, the report bits) is **UNVERIFIED**. The shared code makes it likely, not certain. No driver read for this project uses `gxl_mjpeg.bin`: mainline's GXBB and GXL tables have no MJPEG entry (base §8).
- **Fallback:** if `gxl_mjpeg.bin` misbehaves, extract the vendor image for a comparison run only, as was done for HEVC (HEVC-vendor, provenance). It is research material and must not ship.

---

## 2. Power, clock and resets

### 2.1 Power-up

**MJPEG adds nothing to the VDEC_1 power-up.** `vdec_poweron(VDEC_1)` runs when the video port is opened, before the format is known. Its steps:
1. AO_RTI_GEN_PWR_SLEEP0 clear bits 3:2, then a 10 µs wait.
2. DOS_SW_RESET0 pulse 0xFFFFFFFC.
3. The 500 MHz clock (§2.2).
4. DOS_MEM_PD_VDEC = 0.
5. AO_RTI_GEN_PWR_ISO0 clear bits 7:6.
6. DOS_VDEC_MCRCC_STALL_CTRL = 0.
7. On GXBB, DMC_REQ_CTRL (0xC8838000) set bit 13, "enable VDEC_1 DMC request" (HEVC-vendor §1.3).

This is base §1.3 with read-modify-write of the AO bits, plus the DMC bit. Haiku's power-up already works without the DMC bit for MPEG-1/2 and H.264, so nothing MJPEG-specific follows from it.

### 2.2 VDEC_1 clock

**At power-up:** `vdec_clock_hi_enable` asks for 500 MHz: source 0 (fclk/4), divider 1.

**The clock-change sequence on GXBB** (any non-M8 chip):

| # | Register | Action |
|---|---|---|
| 1 | HHI_VDEC3_CLK_CNTL (0xC883C1E8, vendor HHI 0x107A) | copy HHI_VDEC_CLK_CNTL bits 6:0 (the divider) into bits 6:0; set bit 8; set bit 15. The vendor calls this the "safe clock"; reading bit 15 as "VDEC_1 runs from VDEC3 meanwhile" is **UNVERIFIED** |
| 2 | HHI_VDEC_CLK_CNTL (0xC883C1E0) | clear bit 8 |
| 3 | HHI_VDEC_CLK_CNTL | write bits 15:0 = (source << 9) \| (divider − 1). Bit 8 is 0 in this write |
| 4 | HHI_VDEC_CLK_CNTL | set bit 8 |
| 5 | HHI_VDEC3_CLK_CNTL | clear bit 15, then clear bit 8 |
| 6 | DOS_GCLK_EN0 (DOS + 0xFC04) | bits 9:0 = 0x3FF |

- Step 1 copies only the divider, not the source bits (11:9). The fallback therefore runs at VDEC3's own source with VDEC_1's divider, as the vendor wrote it.
- Haiku sets the clock once with the gate off (base §1.2). It never changes the clock while running, so it needs none of this.

**Source codes (vendor comment):** 0 fclk/4, 1 fclk/3, 2 fclk/5, 3 fclk/7, 4 MPLL out 1, 5 MPLL out 2. Value 6 is used for the GP0 PLL.

**Requested rate → setting** (`vdec_get_clk_source`, fclk = 2000 MHz):

| Request (MHz) | Source ÷ divider | Rate |
|---|---|---|
| > 500 | fclk/3 ÷ 1 | 667, but see below |
| 500 | fclk/4 ÷ 1 | 500 |
| 400–499 | fclk/5 ÷ 1 | 400 |
| 333–399 | fclk/3 ÷ 2 | 333 |
| 200–332 | fclk/5 ÷ 2 | 200 |
| 166–199 | fclk/4 ÷ 3 | 166.7 |
| 133–165 | fclk/5 ÷ 3 | 133 |
| 100–132 | fclk/5 ÷ 4 | 100 |
| 50–99 | fclk/5 ÷ 8 | 50 |
| < 50 | fclk/5 ÷ 20 | 10 |

A request above 500 MHz other than exactly 667 takes the GP0 PLL instead: 1296 MHz ÷ 2 = 648 MHz, source 6, divider field 1. If the PLL can't be had, it falls back to fclk/3 (667 MHz).

**After the start (MJPEG-specific):**
- The vendor's 10 ms recycle timer (§4.5) calls `vdec_source_changed(VFORMAT_MJPEG, w, h, fps)` whenever w × h × fps changes, with fps = 96000 / frame duration.
- It does so only when the caller supplied a frame duration (`dec_sysinfo.rate`, in 1/96000 s units). Without one, the clock stays at 500 MHz.
- The rate is the first table entry whose limit is strictly greater than w × h × fps:

| w × h × fps limit (exclusive) | Requested | Actual |
|---|---|---|
| 1280 × 720 × 30 | 100 | 100 MHz |
| 1920 × 1080 × 30 | 166 | 166.7 MHz |
| 1920 × 1080 × 60 | 333 | 333 MHz |
| 4096 × 2048 × 30 | 600 | 648 MHz (GP0) |
| 4096 × 2048 × 60 and above | 600 | 648 MHz (GP0) |

- So the C920's **720p30 runs at 166.7 MHz and 1080p30 at 333 MHz**, since each sits exactly on a limit, and the limit is exclusive.
- The table is the same for MPEG-1/2, MPEG-4 and H.264, so it is generic policy, not a measured MJPEG figure. What MJPEG throughput VDEC_1 reaches per MHz is **UNVERIFIED**.
- Haiku's fixed 666.67 MHz (base §1.2) is above every entry.
- At stop, the clock is gated by the power-down; `vdec_clock_off` also releases the GP0 PLL.

### 2.3 DOS_SW_RESET0 (DOS + 0xFC00) pulses on the MJPEG path

| Bits | When | What the vendor replaced (pre-Meson6 code kept under `#else`) |
|---|---|---|
| 4 | VIFIFO setup (`stbuf_init`), before the pointers are written, followed by one dummy read of CBUS RESET0_REGISTER (0xC1104404) | RESET_VLD in RESET0_REGISTER |
| 7, 6 | protocol start (§6.3 step 1) | RESET_IQIDCT \| RESET_MC in RESET0_REGISTER |
| 10 | scaler setup (§6.2), followed by three dummy reads of CBUS RESET2_REGISTER (word 0x1103, 0xC110440C) | RESET_PSCALE in RESET2_REGISTER |
| 12, 11 | AMRISC start and stop, each with three dummy reads of DOS_SW_RESET0 before and after | RESET_VCPU \| RESET_CCPU in RESET0_REGISTER |

- The "replaced" column gives the vendor's own pairing of new and old reset bits. It suggests a bit map for DOS_SW_RESET0:
  - bit 4 = VLD;
  - bits 7 and 6 = IQIDCT and MC (which one is which is not stated);
  - bit 10 = PSCALE;
  - bits 12 and 11 = the two AMRISC cores.
- This is **UNVERIFIED**, but it agrees with base §1.4, which uses bits 12:11 to stop the AMRISC.
- Haiku's power-up pulse of 0xFFFFFFFC already covers bits 4, 6, 7 and 10 once per session. The vendor's bits 7:6 and 10 pulses come later, after the microcode load (§6.3), so keep them in that order.

---

## 3. How the compressed frames reach the decoder

### 3.1 Nothing MJPEG-specific

- amstream.c, esparser.c and streambuf.c contain no MJPEG case at all.
- An MJPEG stream opened on the ES video port takes the same path as an MPEG-2 or H.264 elementary stream:
  - `stbuf_init` sets up the VIFIFO;
  - `vdec_init` registers the decoder device, whose probe loads the microcode and starts the AMRISC (§6.3);
  - `esparser_init` then hooks the parser to the VIFIFO.
- That is the order of base §8: the parser is hooked up after MPSR = 1.

### 3.2 VIFIFO setup (`stbuf_init`)

Identical to base §4, with one addition at the start: VLD_MEM_VIFIFO_CONTROL = 0, WRAP_COUNT = 0, then the DOS_SW_RESET0 bit-4 pulse and the RESET0 dummy read (§2.3), then POWER_CTL_VLD = 0x10, then the pointers and the control bits exactly as base §4 steps 4–11.
- The end pointer is base + size − 8.
- The ring is 15 MiB by default (`DEFAULT_VIDEO_BUFFER_SIZE`), allocated with 64 KiB alignment, and shrunk to 2/3 on allocation failure.
- Haiku's 8 MiB ring is enough for MJPEG: a C920 1080p frame is about 0.3 MB (§8.2).

### 3.3 Parser hookup and the per-write sequence (`esparser_init`, `_esparser_write`)

- **Hookup:** as base §3.3, with PARSER_CONFIG = 0x000A1810 (PFIFO access width 8 bits, start-code width 24), search pattern 0x00000100 / mask 0xFFFFFF00, PARSER_CONTROL = 0x03. The VIFIFO ends with BUF_CNTL at 0 and DOS_GEN_CTRL0 = 0.
- **Each write:**
  1. PFIFO_RD_PTR = PFIFO_WR_PTR = 0.
  2. PARSER_CONTROL bits 31:8 (ES_PACK_SIZE, 24 bits wide) = len, bits 7:0 = ES_WRITE | ES_SEARCH | ES_PARSER_START (type video = 0 in bits 7:6).
  3. PARSER_FETCH_ADDR = the data; PARSER_FETCH_CMD = (7 << 27) | len.
  4. Then, immediately, a **second fetch**: PARSER_FETCH_ADDR = a separate 512-byte buffer holding `00 00 01 FF` followed by zeros; PARSER_FETCH_CMD = (7 << 27) | 512.
  5. Wait up to 200 ms for the start-code interrupt (base §3.5). On a timeout, PARSER_FETCH_CMD = 0. If the VIFIFO write pointer moved anyway, the data counts as written.
- **Differences from mainline (base §3.4), none of them MJPEG-specific:**
  - The marker is a separate fetch from a separate buffer, issued back to back with the data fetch. That the parser queues the second command behind the first is implied, not stated (**UNVERIFIED**).
  - **There is no 4 KiB minimum and no padding.** The vendor writes short pieces as they are.
  - Pieces are at most 64 KiB (`FETCHBUF_SIZE`). In the default configuration a write thread collects the caller's data into five 64 KiB blocks first.
  - Haiku's working scheme (one fetch with the padding and the marker after the data) needs no change for MJPEG.
- **Flow control:** free = ring size − VLD_MEM_VIFIFO_LEVEL − 6 KiB (when the usable size is at least half the ring) − PARSER_VIDEO_HOLE. That is base §3.6.

### 3.4 Frame boundaries, padding, markers, the per-frame kick

- **The kernel sends the JPEG bytes unchanged.** It adds nothing to the VIFIFO: no start code, no header, no padding between frames, no trailer. The parser's `00 00 01 FF` marker serves only to raise the parser interrupt. Base §10 found on this board that the marker does not reach the VIFIFO.
- **No frame boundary is signalled.**
  - Writes are cut at 64 KiB or wherever the caller's writes end, not at frames.
  - No register is written per frame.
  - There is no "decode this" command and no length.
  - So the microcode must find each frame by parsing the JPEG markers in the stream itself, from SOI (FF D8) through EOI (FF D9) (inferred; the protocol leaves no other way).
- **Resynchronisation:** the restart path of §4.7 resets the AMRISC without touching the VIFIFO read pointer. The microcode must then resume at an arbitrary byte, so it presumably scans for the next SOI (**UNVERIFIED**).
- **When a frame is decoded:** whether the microcode decodes a frame as soon as its EOI is in the VIFIFO, or only once more data follows, is **UNVERIFIED**. Two things could delay it: the VLD's prefetch (the VIFIFO's fill-on-level control, base §4) and any look-ahead past EOI.
  - Haiku's H.264 firmware needed the next start code to finish a slice (the driver's warm-up comment).
  - If the MJPEG microcode behaves alike, each frame comes out one frame late. That costs about 33 ms of latency at 30 fps, not throughput.
  - The last frame of a stream would then need extra data behind it (§3.6).
- **Stream offsets:** the vendor counts the bytes written through the parser since the hookup. It records each write's presentation time against that count, and matches a report's offset (§4.4) against the records (`pts_lookup_offset`). This is the scheme of base §3.7 and §5.6, with the offset in a different register.

### 3.5 What the caller supplies

The player passes width, height and frame duration (`dec_sysinfo`) before the decoder starts. The vendor uses:
- the **height**, for MREG_DECODE_PARAM (§4.2);
- the frame duration, for the clock (§2.2);
- the width and height as the display size of every frame (§5.6).

It parses nothing from the stream itself.

### 3.6 End of stream

The vendor has none for MJPEG: no flush, no end code, no drain wait. Since JPEG frames are independent, every frame the microcode has decoded is already reported. If the microcode does hold a complete frame until more data arrives (§3.4), Haiku's end of stream should push filler behind the last frame and wait for its report. Candidates are a run of zero bytes or a repeat of the last frame whose report is dropped (**UNVERIFIED**; test).

---

## 4. The microcode interface

### 4.1 The vendor's own map of the scratch registers

In its header comment, the vendor splits the scratch registers like this:
- AV_SCRATCH_0–1: the initial display-buffer FIFO;
- AV_SCRATCH_2–3: decoder settings;
- AV_SCRATCH_4–7: the buffer descriptions (canvases);
- AV_SCRATCH_8–9: buffer management between AMRISC and host;
- AV_SCRATCH_A: "time stamp".

The code names three of them:
- MREG_DECODE_PARAM = AV_SCRATCH_2;
- MREG_TO_AMRISC = AV_SCRATCH_8;
- MREG_FROM_AMRISC = AV_SCRATCH_9.

It also names AV_SCRATCH_A MREG_FRAME_OFFSET. Note that MPEG-1/2 uses AV_SCRATCH_D for its frame offset (base §5.3), not A.

### 4.2 Registers written before the start

| Register | Byte offset | Word | Value | Meaning |
|---|---|---|---|---|
| AV_SCRATCH_0 | 0x2700 | 0x09C0 | 12 | initial free-buffer FIFO, length (see below) |
| AV_SCRATCH_1 | 0x2704 | 0x09C1 | 0x0000031A | initial free-buffer FIFO, contents (see below) |
| AV_SCRATCH_2 (MREG_DECODE_PARAM) | 0x2708 | 0x09C2 | (H << 4) \| 0x8000 | vendor comment: bits 3:0 "pico_addr_mode" (0 here), bits 15:4 "reference height". H is the caller's height |
| AV_SCRATCH_3 | 0x270C | 0x09C3 | not written | in the "decoder settings" pair, but the vendor never writes it |
| AV_SCRATCH_4 … _7 | 0x2710 … 0x271C | 0x09C4 … 0x09C7 | 0x020100, 0x050403, 0x080706, 0x0B0A09 | canvas word of buffer 0 … 3 (§5.2) |
| AV_SCRATCH_8 (MREG_TO_AMRISC) | 0x2720 | 0x09C8 | 0 | no buffer being handed back |
| AV_SCRATCH_9 (MREG_FROM_AMRISC) | 0x2724 | 0x09C9 | 0 | no report pending |
| MCPU_INTR_MSK | 0x0C10 | 0x0304 | 0xFFFF | AMRISC interrupt mask; by its name, all 16 sources enabled (**UNVERIFIED**). The MPEG-1/2 path never writes it (base §7) |
| VDEC_ASSIST_AMR1_INT8 | 0x00B4 | 0x002D | 8 | the vendor's comment: "interrupt mapping for VLD". Which VLD event reaches which AMRISC vector is **UNVERIFIED** |
| ASSIST_MBOX1_CLR_REG, then ASSIST_MBOX1_MASK | 0x01D4, 0x01D8 | 0x0075, 0x0076 | 1, 1 | clear any stale request, then enable mailbox 1 to the host (base §7) |
| MDEC_PIC_DC_CTRL | 0x2638 | 0x098E | clear bit 17 | three-plane output (§5.5) |

**The free-buffer FIFO, read field by field (inferred, UNVERIFIED):**
- 0x31A splits into four 3-bit fields. From bit 0 up they hold 2, 3, 4 and 1: each 1-based buffer number exactly once.
- 12 is the bit length of four such fields.
- So AV_SCRATCH_1 is presumably a FIFO of free buffer numbers and AV_SCRATCH_0 its fill in bits, and the microcode starts with all four buffers free.
- Unlike MPEG-1/2, where the firmware simply assumes all eight are free (base §5.5), the MJPEG host states the free list explicitly.
- Which end the microcode takes first is unknown.

**MREG_DECODE_PARAM:**
- Bit 15 is set by the 0x8000 term and also lies inside the documented 15:4 height field. For heights below 2048 the two don't collide: 720 gives 0xAD00, 1080 gives 0xC380.
- What bit 15 means on its own is **UNVERIFIED**.
- The width is never given.
- A plausible use of the reference height is to recognise field pictures. Interlaced "AVI1" MJPEG stores each field as a JPEG of half the height, and the report has interlace bits (§4.4). This is **UNVERIFIED**. Give the true picture height.

**AV_SCRATCH_3:** the scratch registers read 0 after a power cycle (the Haiku H.264 path relies on this), so writing 0 is the safe equivalent of not writing it (inferred).

### 4.3 States

- The vendor driver keeps no microcode state and reads no status register.
- From the host's side there are two events only: a report (§4.4) and a buffer handed back (§4.5).
- Any internal decoding states of the microcode are invisible in the source.

### 4.4 The report (mailbox 1 interrupt)

**Handler order:**
1. ASSIST_MBOX1_CLR_REG ← 1: the acknowledge, first thing, as in base §7.
2. Read AV_SCRATCH_9 (MREG_FROM_AMRISC).
3. If bits 2:0 are 0: nothing to report. Leave the register as it is and return.
4. Otherwise read AV_SCRATCH_A (MREG_FRAME_OFFSET): the frame's stream byte offset, for the timestamp lookup (§3.4). Whether it is the offset of the frame's SOI or of some other point is **UNVERIFIED**.
5. Buffer index = (bits 2:0) − 1, kept to 2 bits. The field is **1-based**, 1–4.
6. Build the frame from the caller's width and height and the buffer's canvases.
7. AV_SCRATCH_9 ← 0: the report is consumed. The vendor writes this only when bits 2:0 were non-zero.

**AV_SCRATCH_9 bits (the vendor's names):**

| Bits | Vendor name | Use |
|---|---|---|
| 2:0 | PICINFO_BUF_IDX_MASK | 1-based buffer index; 0 = no report |
| 4 | PICINFO_INTERLACE_FIRST, and also PICINFO_INTERLACE_AVI1_BOT | field order: top-first for a plain interlaced stream, bottom field for an AVI1 one. Used only in a compiled-out branch |
| 5 | PICINFO_INTERLACE | the picture is interlaced |
| 7 | PICINFO_AVI1 | AVI1-style interlaced MJPEG. Used only in the compiled-out branch |
| others | — | not used |

- With bit 5 clear the frame is progressive. That is the C920's case.
- With bit 5 set, the active code treats the report as one whole frame with both fields woven, not as a field. It gives the display a second canvas set for it (§5.3). Whether the microcode reports once per field pair or once per field is **UNVERIFIED**.
- The vendor's range check on the index can never fail: the index is masked to 2 bits first.
- The vendor queues frames in report order. JPEG frames are independent, so report order is stream order.

### 4.5 Handing a buffer back

- **Protocol:** the same register and convention as MPEG-1/2 (base §5.7). When AV_SCRATCH_8 (MREG_TO_AMRISC) reads 0, write index + 1 to it. The microcode sets it back to 0 once it has taken the buffer (inferred from the host waiting for 0). Only one hand-back can be pending at a time.
- **Timing:** the vendor checks every 10 ms from a timer (`PUT_INTERVAL` = HZ/100). It hands back as many buffers as it can while the register reads 0.
- **Counting:** a buffer goes back only when its use count drops to 0. Each report raises it by one, so with one consumer that is simply when the consumer releases it.
- **All four held:** what the microcode does then is **UNVERIFIED**. Presumably it stops consuming the VIFIFO until a buffer comes back, and the writer then blocks on the free-space check (§3.3).

### 4.6 What the protocol does not have

- **No fatal-error flag.** AV_SCRATCH_F is never read, unlike base §5.10.
- **No error count**: the vendor's status call always reports error_count 0.
- **No skipped-frame report.**
- **No picture size or sampling report.**
- **No end-of-stream handshake.**
- A broken JPEG is therefore either decoded into a damaged picture, dropped silently, or hangs the microcode. Which one happens is **UNVERIFIED**.
- A Haiku driver needs a watchdog: no report for, say, 200 ms while data is waiting in the VIFIFO means stuck. Recover with §4.7 or a full restart.

### 4.7 Restart without a reload (the vendor's receiver-reset path)

When the display side asks for a reset, the vendor does the following:
1. The AMRISC stop of §7, step 1.
2. Resets its software queues.
3. Reruns the whole protocol start of §6.3, steps 3–4: the bits 7:6 reset, the canvases, the scratch registers, the scaler, the interrupt setup.
4. The AMRISC start (§6.3, step 7).

The microcode is not reloaded, and the VIFIFO and the parser are untouched. This is the vendor's only recovery mechanism, and a cheap one to copy for the watchdog.

---

## 5. The output

### 5.1 Buffers

- **Count:** 4 (`DECODE_BUFFER_NUM_MAX`). The protocol has no room for more: four canvas words (AV_SCRATCH_4–7), a 4-entry free list, and an index masked to 2 bits.
- **Memory:** a 32 MiB decoder area (`vdec_default_buf_size` for amvdec_mjpeg), 64 KiB aligned. The vendor uses 12–15 MiB of it.
- **Layout choice:** the vendor picks one of two fixed layouts from the area's size, not from the picture. Since the area is 32 MiB, GXBB always gets the HD one:

| Layout | Condition | Canvas Y (bytes × lines) | Canvas U, V | Y area | U area | V area | Per buffer |
|---|---|---|---|---|---|---|---|
| HD | area > 4 MiB | 1920 × 1088 | 960 × 544 each | 0x200000 at +0 | 0x80000 at +0x200000 | 0x80000 at +0x280000 | 0x300000 (3 MiB) |
| SD | area ≤ 4 MiB | 768 × 576 | 384 × 288 each | 0x80000 at +0 | 0x20000 at +0x80000 | 0x20000 at +0xA0000 | 0x100000 (1 MiB) |

- **Placement:** buffer i is at area + i × per-buffer size.
- **The display exception:** if a buffer's address is the one the video layer is showing at that moment, that buffer's canvases are placed in a fifth slot at area + 4 × per-buffer size instead. This keeps the shown picture intact across a restart. It is display policy, not needed by Haiku.

### 5.2 Canvases given to the microcode

| Buffer | Y canvas | U canvas | V canvas | Canvas word (AV_SCRATCH_4+i) |
|---|---|---|---|---|
| 0 | 0x00 | 0x01 | 0x02 | 0x020100 |
| 1 | 0x03 | 0x04 | 0x05 | 0x050403 |
| 2 | 0x06 | 0x07 | 0x08 | 0x080706 |
| 3 | 0x09 | 0x0A | 0x0B | 0x0B0A09 |

- **Canvas word layout:** as for MPEG-1/2 (base §5.5): bits 7:0 Y, bits 15:8 U, bits 23:16 V, bits 31:24 zero. Any free canvas indexes work. The vendor's low indexes are just its allocation; Haiku can use its own range (it uses 0x80 upwards).
- **Canvas settings** (`canvas_config`): no wrap, linear (block mode 0), **endian 0**. The address and the width are rounded up to 8 bytes.
- **Alignment:** the vendor's own description of the LUT asks for the start address and the width to be 32-byte aligned. 1920, 960, 1280 and 640 all are.
- **When:** the canvases are programmed in the protocol start, after the microcode load and before the AMRISC starts (§6.3). That is the same rule as base §5.5: before MPSR = 1.

### 5.3 The second canvas set (display only)

The vendor also configures a second set per buffer, canvas indexes 0x0C–0x0E, 0x16–0x18, 0x20–0x22 and 0x23–0x25:
- Y at +0x180000, U at +0x240000, V at +0x2C0000 within the buffer;
- the same sizes as the first set.

It hands these to the video layer only for interlaced reports (§4.4). They are **never given to the microcode**. A progressive decoder needs nothing of them.

### 5.4 Plane layout and the chroma subsampling

**Output format:** three separate linear planes (Y, then U = Cb, then V = Cr), with MDEC_PIC_DC_CTRL bit 17 clear. The U and V canvases are **half the width and half the height** of Y, which is 4:2:0 sizing.

**The vendor never looks at the JPEG's sampling factors.**
- Nothing in the kernel depends on whether the stream is 4:2:0, 4:2:2 or anything else.
- The canvases are always 4:2:0-sized.
- MJPEG from cameras is commonly 4:2:2, and the C920's is.
- Taken together, this implies that **the microcode outputs 4:2:0 for 4:2:2 input**, converting the chroma vertically. If it wrote full-height 4:2:2 chroma, a 1080p frame's U plane (960 × 1080 = 1,036,800 bytes) would overrun its 512 KiB area into V and the next buffer, and the vendor's own MJPEG playback would have shown it.
- This is **inferred, UNVERIFIED**.

**How the conversion is done is not in the source.**
- The vendor enables the picture scaler (PSCALE) for MJPEG, and only for MJPEG: MPEG-1/2 writes PSCALE_CTRL = 0.
- It loads the scaler with a 4-tap triangle filter and unity phase steps for luma and chroma, horizontally and vertically (§6.2).
- One plausible reading: the decoded picture leaves through the PSCALE, and the microcode sets the chroma vertical step to 2:1 when the JPEG is 4:2:2. This is **UNVERIFIED**.
- Under that reading, the chroma would be filtered (averaged over neighbouring lines), not decimated.
- How the hardware handles 4:4:4, 4:1:1 or grayscale JPEG is **UNVERIFIED**; the C920 needs none of them.

**Rejection:** none is visible. The protocol has no error report (§4.6).

### 5.5 The compiled-out two-plane mode

vmjpeg.c contains a complete "NV21" variant, disabled by a commented-out define. Its differences:
- **Canvases:** two per buffer:
  - Y: 1920 × 1088 at +0;
  - one interleaved chroma plane, 1920 bytes × 544 lines at +0x200000.
- **Canvas words:** the chroma index appears in both bits 15:8 and bits 23:16: 0x010100, 0x030302, 0x050504, 0x070706. That is the same convention as Haiku's NV12 (base §5.5).
- **MDEC_PIC_DC_CTRL bit 17:** set.
- **Frame label:** frames are labelled VIDTYPE_VIU_NV21.

**Is it really NV21?** The vendor's H.264 driver labels the output VIDTYPE_VIU_NV21 too, with bit 17 set and endian-0 canvases (`vh264.c`; cross-check, §11). Haiku decodes H.264 with endian-7 canvases and gets NV12, Cb first, verified on the board (base §6, H.264 facts). So the vendor's "NV21" label with endian 0 corresponds to what Haiku sees as NV12 with endian 7 (inferred by analogy, **UNVERIFIED** for MJPEG).

**Status:** the mode was compiled out and so probably less tested. Whether the MJPEG microcode honours bit 17 is **UNVERIFIED**. If it does, Haiku can keep its NV12 frame format and hand the frames to the overlay and to `meson_venc` (NV12 input, VENC §1) without a copy.

### 5.6 Width, height, maximum size, stride

- **Width and height:**
  - The vendor takes both from the caller (§3.5) and uses them only as the display size of the frames.
  - The microcode gets the height once (§4.2) and the width never.
  - So the microcode must read the picture size from each JPEG's SOF header (inferred).
  - The vendor never reads a size back. Nothing in this source says that a scratch register reports it.
- **Maximum size:**
  - What the vendor provides is 1920 × 1088 (the HD canvases).
  - The reference-height field holds 12 bits (to 4095, but < 2048 without touching bit 15).
  - The microcode's own limit is **UNVERIFIED**.
  - The C920's MJPEG sizes are all ≤ 1920 × 1080.
- **Stride:**
  - The vendor uses a fixed 1920-byte luma stride and 960-byte chroma stride for every picture size, 1280 × 720 included.
  - So the microcode writes through the canvases with the canvas stride, and any canvas at least as wide as the picture works.
  - Haiku can size the canvases to the picture: Y width W rounded up to 32, chroma W/2 (also a multiple of 32 when W is a multiple of 64; otherwise round the chroma width up to 32 on its own).
- **Lines:**
  - The vendor's 1088-line luma canvas covers 1080 rounded up to 16. That suggests the output is written in whole 16-line macroblock rows (inferred).
  - Size the luma canvas to H rounded up to 16 and the chroma canvases to half of that.
  - For the C920: 1080 → 1088 and 544; 720 → 720 and 360.

| C920 size | Y (stride × lines) | U, V each (3-plane) | CbCr (2-plane) | Bytes per frame |
|---|---|---|---|---|
| 1920 × 1080 | 1920 × 1088 = 2,088,960 | 960 × 544 = 522,240 | 1920 × 544 = 1,044,480 | 3,133,440 |
| 1280 × 720 | 1280 × 720 = 921,600 | 640 × 360 = 230,400 | 1280 × 360 = 460,800 | 1,382,400 |

**Endianness:**
- The vendor's canvases use endian 0, but so does its display, which reads the same memory.
- For a CPU consumer, Haiku uses endian 7 on all its decoder canvases, and the result is correct for MPEG-1/2 and H.264 (base §10).
- Use 7 for MJPEG too (**UNVERIFIED** until the first frame is checked against a software decode).

---

## 6. Register writes to other blocks, and the order of the start

### 6.1 `amvdec_enable` (before the microcode load)

- GCLK_EN (DOS + 0x260C, word 0x0983) = 0x3FF (on M8 and later).
- MDEC_PIC_DC_CTRL: clear bit 31.

This is base §1.3 steps 13–14, which Haiku already does in its power-up. The per-unit clock gates of older chips (`AMVDEC_CLK_GATE_ON`) are empty macros on Meson6 and later.

### 6.2 The picture scaler setup (`init_scaler`, part of the protocol start)

**Scaler registers:**
- PSCALE_RST: DOS + 0x2440, word 0x0910.
- PSCALE_CTRL: DOS + 0x2444, word 0x0911.
- PSCALE_BMEM_ADDR: DOS + 0x247C, word 0x091F.
- PSCALE_BMEM_DAT: DOS + 0x2480, word 0x0920.

**BMEM layout (inferred, UNVERIFIED):**
- The scaler's parameter memory (BMEM) is reached through an address register and a data register.
- The data writes come in sequence after one address write, so the address presumably advances by one per data write.
- An entry k of 64 (or 36) bits seems to occupy two consecutive 32-bit addresses, 2k and 2k + 1: the high word first, then the low word.

**Sequence:**

| # | Register | Value | Vendor's description |
|---|---|---|---|
| 1 | PSCALE_CTRL | 0x0000C000 | bits 15 and 14: "pscale enable" and "CBUS access to the BMEM enable". Which bit is which is **UNVERIFIED** |
| 2 | PSCALE_BMEM_ADDR | 0 | |
| 3 | PSCALE_BMEM_DAT | 33 pairs: 0, then coefficient word i (i = 0 … 32) | filter, "4 point triangle" (formula below) |
| 4 | PSCALE_BMEM_ADDR, DAT, DAT | 74; 0x00000008, 0x60000000 | entry 37: luma horizontal initial state |
| 5 | same | 82; 0x00000008, 0x60000000 | entry 41: chroma horizontal initial state |
| 6 | same | 78; 0x00000008, 0x60000000 | entry 39: luma vertical initial state |
| 7 | same | 86; 0x00000008, 0x60000000 | entry 43: chroma vertical initial state |
| 8 | PSCALE_BMEM_ADDR, DAT | 73; 0x00010000 | low word of entry 36: luma horizontal phase step (field 19:0) |
| 9 | same | 81; 0x00010000 | entry 40 low word: chroma horizontal phase step |
| 10 | same | 77; 0x00010000 | entry 38 low word: luma vertical phase step |
| 11 | same | 85; 0x00010000 | entry 42 low word: chroma vertical phase step |
| 12 | DOS_SW_RESET0 | pulse bit 10 | scaler reset |
| 13 | RESET2_REGISTER (CBUS 0x1103, 0xC110440C) | three dummy reads | timing only |
| 14 | PSCALE_RST | 7, then 0 | |

**Filter coefficients (step 3):**
- Each coefficient word packs four 8-bit taps, most significant byte first.
- With p = ⌊i / 2⌋ for i = 0 … 32, word i holds the taps (32 − p, 64 − p, 32 + p, p): a triangle that always sums to 128, stepped through 17 phases (p = 0 … 16).
- Each phase appears twice, except the last.
- For example: i = 0 and 1 give 0x20402000; i = 32 gives 0x10303010.

**Initial-state entries (steps 4–7):**
- The vendor's field map for these 36-bit entries:
  - bit 35: repeat pixel 0;
  - bits 34:29: receive count;
  - bits 28:16: block x;
  - bits 15:0: phase.
- 0x8 in the high word and 0x60000000 in the low word therefore set "repeat pixel 0" = 1, receive count = 3, block x = 0 and phase = 0.

**Phase steps (steps 8–11):** 0x10000 reads as 1.0 in a 16.16 fixed-point step, i.e. no scaling, for all four (**UNVERIFIED** format). §5.4 says why the microcode might change the chroma vertical step.

### 6.3 The complete start, in the vendor's order

Before this: open → "vdec" gate, `vdec_poweron(VDEC_1)` (§2.1); set format → VIFIFO setup (§3.2). Then the decoder probe:

| # | Step | Registers |
|---|---|---|
| 1 | `amvdec_enable` | §6.1 |
| 2 | microcode load | §1.2 (MPSR = CPSR = 0, IMEM DMA) |
| 3 | protocol start, part 1 | DOS_SW_RESET0 pulse bits 7:6; then the 24 canvases of §5.2 and §5.3 in the DMC LUT |
| 4 | protocol start, part 2 | AV_SCRATCH_0 = 12, AV_SCRATCH_1 = 0x31A, AV_SCRATCH_4–7 = canvas words; the scaler setup of §6.2; AV_SCRATCH_8 = 0, AV_SCRATCH_9 = 0; MCPU_INTR_MSK = 0xFFFF; AV_SCRATCH_2 = (H << 4) \| 0x8000; ASSIST_MBOX1_CLR_REG = 1; ASSIST_MBOX1_MASK = 1; VDEC_ASSIST_AMR1_INT8 = 8; MDEC_PIC_DC_CTRL clear bit 17 |
| 5 | IRQ handler installed | mailbox 1, SPI 44, shared |
| 6 | 10 ms recycle timer started | |
| 7 | AMRISC start (`amvdec_start`) | three dummy reads of DOS_SW_RESET0; pulse bits 12:11; three dummy reads; **MPSR = 1** |

After the probe returns, `esparser_init` hooks the parser to the VIFIFO (§3.3), and the caller starts writing.

- **No DBLK, DCAC, MC or IQIDCT register is written** for MJPEG, beyond the bits 7:6 reset. No VLD register is written beyond the VIFIFO setup either: POWER_CTL_VLD is set only there, and MPEG1_2_REG, PIC_HEAD_INFO and M4_CONTROL_REG are left alone.
- The **only DOS writes after MPSR = 1** are DOS_GEN_CTRL0 = 0 and the BUF_CNTL handover in the parser hookup.

---

## 7. Teardown

At close the vendor runs these, in order:

| # | Step | Registers |
|---|---|---|
| 1 | parser release (`esparser_release`), last user | PARSER_INT_ENABLE = 0; parser IRQ freed |
| 2 | AMRISC stop (`amvdec_stop`) | MPSR = 0; CPSR = 0; wait up to 1 s for IMEM_DMA_CTRL bit 15 to clear; three dummy reads of DOS_SW_RESET0; pulse bits 12:11; three dummy reads |
| 3 | IRQ handler freed | **ASSIST_MBOX1_MASK is not cleared** |
| 4 | recycle timer stopped | |
| 5 | drain (`amvdec_disable`) | wait up to 100 ms for MDEC_PIC_DC_STATUS (DOS + 0x263C) = 0; on timeout set, then clear MDEC_PIC_DC_CTRL bit 0, then 3 dummy reads |
| 6 | | wait up to 100 ms for DBLK_STATUS (DOS + 0x254C) bit 0 = 0; on timeout DBLK_CTRL (DOS + 0x2544) = 3, then 0, then 3 dummy reads |
| 7 | | wait up to 100 ms for MC_STATUS0 (DOS + 0x2424) bit 0 = 0; on timeout set, then clear MC_CTRL1 (DOS + 0x242C) bits 3 and 0 (mask 0x9), then 3 dummy reads |
| 8 | | wait up to 100 ms for DCAC_DMA_CTRL (DOS + 0x3848) bit 15 = 0; no fallback |
| 9 | stream buffer released | |
| 10 | `vdec_poweroff(VDEC_1)` | on GXBB: DMC_REQ_CTRL clear bit 13, wait 10 µs; ISO0 set bits 7:6; DOS_MEM_PD_VDEC = 0xFFFFFFFF; clock gate off (HHI_VDEC_CLK_CNTL bit 8; the GP0 PLL released); SLEEP0 set bits 3:2 |
| 11 | "vdec" gate reference dropped | HHI_GCLK_MPEG0 bit 1 off when no other user holds it |

- In code order, steps 2–8 belong to the decoder's removal, which amstream calls after the parser release.
- The drain waits are the same as in the HEVC stop (HEVC-vendor §1.5), applied to the VDEC_1 registers here.
- Haiku's current stop does: MPSR = CPSR = 0, a 5 ms pause, then base §1.4's power-down (mask off, bits 12:11 pulse, isolation, memories, sleep). That is a superset of the vendor's register effect apart from the drain waits. Adding the four drain waits before the power-down is cheap and matches the vendor.

---

## 8. Restrictions

### 8.1 What the vendor source says

Nothing:
- **Restart intervals (DRI/RSTn):** vmjpeg.c, the scratch map and the microcode registration say nothing.
- **Progressive (SOF2) JPEG:** nothing.
- **Missing Huffman tables:** nothing. MJPEG without DHT segments (common in AVI MJPEG, which relies on the standard tables) is not handled in the kernel: no table is inserted into the stream. Either the microcode carries the standard tables, or the player inserted them; neither is visible (**UNVERIFIED**).
- **Multiple scans, arithmetic coding, 12-bit precision:** nothing.
- **Sizes not a multiple of 16:** nothing, beyond the 1088-line canvas (§5.6).
- **Number of quantisation tables:** nothing.

Everything about the JPEG syntax the microcode accepts is **UNVERIFIED**. Baseline Huffman JPEG is the only safe assumption.

### 8.2 The C920's frames (observation, not a vendor fact)

A 1920 × 1080 MJPEG frame kept in this project's scratch space (322,528 bytes; by its structure a C920 frame, but its source was not recorded) has, in this order:
- **DQT:** three quantisation tables.
- **SOF0 (baseline), 8-bit, 1920 × 1080:**
  - Y sampling 2×1, quantisation table 0;
  - Cb 1×1, table 1;
  - Cr 1×1, **table 2**.
- **DHT:** four segments, of the sizes of the standard luma and chroma DC and AC tables.
- **COM.**
- **APP0:** 16 bytes, whose identifier is five zero bytes. Neither "JFIF" nor "AVI1".
- **DRI with interval 0**, so no restart markers.
- **SOS:** one scan.
- **EOI.**

What this means for the hardware:
- **4:2:2** with 16 × 8 MCUs: 1080 lines are exactly 135 MCU rows and 1920 pixels 120 MCUs. 720p is 80 × 90 MCUs. No partial MCUs, whichever subsampling the output uses.
- **Three quantisation tables, one per component.** Many decoders support only two, luma and chroma. If the microcode does, Cr would be dequantised with the wrong table: a colour error, not a crash. This is **UNVERIFIED** and worth an early check: compare the decoded Cr plane against libjpeg-turbo.
- **The marker order** (APP0 and DRI after the tables) and **the zero-identifier APP0** are unusual. The microcode must skip unknown segments by their length. A zero-identifier APP0 must not be mistaken for AVI1 (§4.4).
- **Tables in every frame:** the C920 sends DHT and DQT in every frame, which a per-frame parser needs anyway.

---

## 9. What Haiku's driver needs to do differently from its MPEG-1/2 path

**What stays the same:**
- Power-up, the VIFIFO, the IMEM DMA load of a 16 KiB image.
- The ESPARSER feed, with Haiku's own padding and marker scheme.
- Mailbox 1 on SPI 44 with polling as a backstop.
- The 1-based report, and the hand-back through AV_SCRATCH_8 as index + 1, one at a time.
- One session at a time, sharing the VDEC_1 code paths.

**What changes:**
1. **Firmware:** `gxl_mjpeg.bin`, 16,384 bytes, from user space as today. Its protocol is presumed to be the vendor's (§1.3); the first board test settles it.
2. **No workspace:** skip the 128 KiB buffer and AV_SCRATCH_B.
3. **Four frames, not eight.**
   - Three-plane output by default: three canvases per frame, Y, U and V, at W×H′, W/2×H′/2 and W/2×H′/2, with H′ = H rounded up to 16.
   - Or try the two-plane mode (MDEC_PIC_DC_CTRL bit 17 set, canvas word Y | C<<8 | C<<16, CbCr plane W×H′/2) to keep the NV12 frame format of `meson_vdec.h` (§5.5).
   - With three planes, `meson_vdec_frame` needs a third plane offset and a chroma stride of half the luma one, or the driver must interleave the chroma itself.
   - Canvas endian 7, linear, as now.
4. **Different start registers** (§4.2, §6.3):
   - Write AV_SCRATCH_0 = 12, AV_SCRATCH_1 = 0x31A, AV_SCRATCH_2 = (H << 4) | 0x8000, AV_SCRATCH_3 = 0, AV_SCRATCH_4–7 = canvas words, AV_SCRATCH_8 = AV_SCRATCH_9 = 0, MCPU_INTR_MSK = 0xFFFF, VDEC_ASSIST_AMR1_INT8 = 8.
   - Pulse DOS_SW_RESET0 bits 7:6 after the microcode load.
   - Run the PSCALE setup of §6.2, including its bit-10 reset, with PSCALE_CTRL = 0xC000 instead of 0.
   - Do **not** write MPEG1_2_REG, PIC_HEAD_INFO, M4_CONTROL_REG, AV_SCRATCH_A/B/C/E/F, or the MPEG canvas table in AV_SCRATCH_0–3.
5. **The height comes from the stream, the width is not passed at all.** Parse the first frame's SOF0 (FF C0: precision, 16-bit height, 16-bit width, components and their sampling) before the start, in the driver or the caller. That is the analogue of the MPEG sequence-header parse of base §5.8. The sampling factors tell what output to expect, and allow a 4:2:2 versus 4:2:0 decision if the hardware's output differs between the two.
6. **Report handling:**
   - AV_SCRATCH_9 bits 2:0 hold the 1-based index (bits 3:0 for MPEG). Bit 5 means interlaced; for the C920 it should stay 0, so log it if set.
   - The stream offset is in **AV_SCRATCH_A**, not AV_SCRATCH_D.
   - There is no BUFFEROUT "skip" pattern, no PIC_INFO and no fatal flag to read.
   - Write AV_SCRATCH_9 = 0 after each report.
7. **Error handling:** the microcode reports no errors (§4.6).
   - Add a watchdog: data in the VIFIFO and no report for a few frame times.
   - Recover with the vendor's restart (§4.7): AMRISC stop, bits 7:6 reset, canvases, scratch, scaler, interrupt, AMRISC start, no reload. Fall back to a full session restart if that fails.
   - A lost isochronous packet truncates a frame, so expect to need this.
8. **Frame boundaries and end of stream:**
   - Write whole frames or any pieces; no per-frame kick exists. Writing one JPEG per WRITE keeps the offset bookkeeping simple: each frame's offset is the WRITE's offset.
   - Measure whether a frame is reported before the next one arrives (§3.4).
   - For end of stream, push filler behind the last frame if needed (§3.6). There is no MPEG-style end code.
9. **Clock:** keep 666.67 MHz. The vendor ran MJPEG at 166.7 MHz for 720p30 and 333 MHz for 1080p30 (§2.2), so speed is not expected to be the limit (**UNVERIFIED**).
10. **Teardown:** optionally add the four drain waits of §7 before the power-down.
11. **First checks on the board, in order:**
    1. A report arrives at all, with gxl_mjpeg.bin and the vendor's register setup.
    2. Y matches libjpeg-turbo.
    3. The chroma planes are 4:2:0 and their content matches a vertical 2:1 reduction of libjpeg-turbo's 4:2:2 chroma, Cr included (the third quantisation table).
    4. Bit 17's two-plane mode.
    5. Latency per frame.
    6. Behaviour on a truncated frame.

---

## 10. Register reference

### 10.1 DOS registers on the MJPEG path (base 0xC8820000)

| Name | Byte offset | Word index | Physical | Use |
|---|---|---|---|---|
| VDEC_ASSIST_AMR1_INT8 | 0x00B4 | 0x002D | 0xC88200B4 | = 8 before start (§4.2) |
| ASSIST_MBOX1_CLR_REG | 0x01D4 | 0x0075 | 0xC88201D4 | 1 = acknowledge / clear (§4.4) |
| ASSIST_MBOX1_MASK | 0x01D8 | 0x0076 | 0xC88201D8 | 1 = mailbox 1 to host enabled; not cleared at stop |
| MPSR | 0x0C04 | 0x0301 | 0xC8820C04 | 0 before load and at stop; 1 = run |
| MCPU_INTR_MSK | 0x0C10 | 0x0304 | 0xC8820C10 | = 0xFFFF before start |
| CPSR | 0x0C84 | 0x0321 | 0xC8820C84 | 0 before load and at stop |
| IMEM_DMA_CTRL / ADR / COUNT | 0x0D00 / 0x0D04 / 0x0D08 | 0x0340–0x0342 | 0xC8820D00 … | microcode load (§1.2) |
| MC_STATUS0 | 0x2424 | 0x0909 | 0xC8822424 | drain at stop (§7) |
| MC_CTRL1 | 0x242C | 0x090B | 0xC882242C | bits 3, 0 pulsed on drain timeout |
| PSCALE_RST | 0x2440 | 0x0910 | 0xC8822440 | 7, then 0 (§6.2) |
| PSCALE_CTRL | 0x2444 | 0x0911 | 0xC8822444 | = 0xC000 (MPEG-1/2 writes 0) |
| PSCALE_BMEM_ADDR | 0x247C | 0x091F | 0xC882247C | scaler parameter memory address |
| PSCALE_BMEM_DAT | 0x2480 | 0x0920 | 0xC8822480 | scaler parameter memory data |
| DBLK_CTRL | 0x2544 | 0x0951 | 0xC8822544 | 3, then 0 on drain timeout |
| DBLK_STATUS | 0x254C | 0x0953 | 0xC882254C | bit 0 = busy (drain) |
| GCLK_EN | 0x260C | 0x0983 | 0xC882260C | = 0x3FF (§6.1) |
| MDEC_PIC_DC_CTRL | 0x2638 | 0x098E | 0xC8822638 | bit 31 cleared at enable; bit 17: 0 = three planes, 1 = two planes; bit 0 pulsed on drain timeout |
| MDEC_PIC_DC_STATUS | 0x263C | 0x098F | 0xC882263C | 0 = idle (drain) |
| AV_SCRATCH_0 | 0x2700 | 0x09C0 | 0xC8822700 | = 12: free-list length in bits (inferred) |
| AV_SCRATCH_1 | 0x2704 | 0x09C1 | 0xC8822704 | = 0x31A: free list, four 3-bit 1-based entries (inferred) |
| AV_SCRATCH_2 (MREG_DECODE_PARAM) | 0x2708 | 0x09C2 | 0xC8822708 | = (H << 4) \| 0x8000 |
| AV_SCRATCH_3 | 0x270C | 0x09C3 | 0xC882270C | "decoder settings", not written by the vendor |
| AV_SCRATCH_4 … _7 | 0x2710 … 0x271C | 0x09C4 … 0x09C7 | 0xC8822710 … | canvas words of buffers 0–3 |
| AV_SCRATCH_8 (MREG_TO_AMRISC) | 0x2720 | 0x09C8 | 0xC8822720 | hand-back: index + 1 when it reads 0 |
| AV_SCRATCH_9 (MREG_FROM_AMRISC) | 0x2724 | 0x09C9 | 0xC8822724 | report: bits 2:0 index + 1, bit 4 field order, bit 5 interlaced, bit 7 AVI1; host writes 0 |
| AV_SCRATCH_A (MREG_FRAME_OFFSET) | 0x2728 | 0x09CA | 0xC8822728 | the reported frame's stream offset |
| POWER_CTL_VLD | 0x3020 | 0x0C08 | 0xC8823020 | = 0x10 in the VIFIFO setup only |
| VLD_MEM_VIFIFO_* | 0x3100 … 0x3144 | 0x0C40 … 0x0C51 | | as base §4 |
| DCAC_DMA_CTRL | 0x3848 | 0x0E12 | 0xC8823848 | bit 15 = busy (drain) |
| DOS_SW_RESET0 | 0xFC00 | 0x3F00 | 0xC882FC00 | pulses: bit 4 (VLD), bits 7:6 (IQIDCT, MC), bit 10 (PSCALE), bits 12:11 (AMRISC) (§2.3) |
| DOS_GCLK_EN0 | 0xFC04 | 0x3F01 | 0xC882FC04 | bits 9:0 = 0x3FF with every VDEC_1 clock-on |
| DOS_GEN_CTRL0 | 0xFC08 | 0x3F02 | 0xC882FC08 | = 0 at parser hookup (VIFIFO read by VDEC_1) |
| DOS_MEM_PD_VDEC | 0xFCC0 | 0x3F30 | 0xC882FCC0 | 0 on, 0xFFFFFFFF off |
| DOS_VDEC_MCRCC_STALL_CTRL | 0xFD00 | 0x3F40 | 0xC882FD00 | = 0 at power-up |

### 10.2 Registers outside DOS

| Name | Physical | Use |
|---|---|---|
| RESET0_REGISTER (CBUS 0x1101) | 0xC1104404 | one dummy read after the VLD reset (§3.2) |
| RESET2_REGISTER (CBUS 0x1103) | 0xC110440C | three dummy reads after the PSCALE reset (§6.2) |
| PARSER_* (CBUS 0x2960 …) | 0xC110A580 … | as base §3; ES_PACK_SIZE is PARSER_CONTROL bits 31:8 (24 bits); FETCH_CMD length field 26:0, endian from bit 27 |
| HHI_VDEC_CLK_CNTL (vendor 0x1078) | 0xC883C1E0 | bits 11:9 source, 8 gate, 6:0 divider − 1 (§2.2) |
| HHI_VDEC3_CLK_CNTL (vendor 0x107A) | 0xC883C1E8 | glitch-free fallback during clock changes: bits 6:0 divider copy, bit 8 gate, bit 15 select (**UNVERIFIED**) |
| HHI_GCLK_MPEG0 | 0xC883C140 | bit 1 = DOS ("vdec" gate) |
| AO_RTI_GEN_PWR_SLEEP0 / ISO0 | 0xC81000E8 / 0xC81000EC | VDEC_1 bits 3:2 / 7:6 |
| DMC_REQ_CTRL | 0xC8838000 | bit 13 = VDEC_1 DMC request (set at power-up, cleared at power-down, GXBB) |
| DMC_CAV_LUT_DATAL / DATAH / ADDR | 0xC8838048 / 0xC883804C / 0xC8838050 | canvas LUT (base §6) |
| GIC | SPI 44 (INTID 76) | mailbox 1 ("mailbox_1"), shared with HEVC; parser SPI 32 |

---

## 11. Provenance and open questions

### 11.1 Sources

All from https://github.com/hardkernel/linux at commit **c3e4c730feb1750940971cae9ca1da3ca50f1d56** (the head of `odroidc2-3.14.y`, committed 2018-03-03), the same commit as the HEVC-vendor and VENC documents. Raw files were fetched one by one; nothing was cloned. The download report (URLs and sizes) was kept outside this document.

| Path | Used for |
|---|---|
| drivers/amlogic/amports/vmjpeg.c | the whole document |
| drivers/amlogic/amports/arch/ucode/mjpeg/vmjpeg_mc.c, Makefile | microcode name, size, registration (§1). Statistics and hashes only were computed, in memory |
| drivers/amlogic/amports/arch/ucode/firmware_def.h, arch/firmware.c, arch/firmware.h | chip list, lookup order, padding (§1.1) |
| drivers/amlogic/amports/amvdec.c, amvdec.h | load, enable, start, stop, disable (§1.2, §6.1, §6.3, §7) |
| drivers/amlogic/amports/vdec.c, vdec.h | power-up and power-down, decoder memory size, IRQ numbering, source change (§2, §5.1) |
| drivers/amlogic/amports/arch/clkgx.c, arch/clk.c | VDEC_1 clock table and change sequence (§2.2) |
| drivers/amlogic/amports/vdec_reg.h, arch/regs/vdec_regs.h, dos_regs.h, amrisc_regs.h, sys_regs.h, hhi_regs.h, parser_regs.h | register numbers (§10) |
| drivers/amlogic/amports/amstream.c, esparser.c, esparser.h, streambuf.c, streambuf.h, streambuf_reg.h | stream path (§3) |
| drivers/amlogic/canvas/canvas.c, canvas_reg.h, include/linux/amlogic/canvas/canvas.h | canvas encoding, endian 0, alignment, block-mode values (§5.2) |
| listing of drivers/amlogic/amports/arch/ucode/mjpeg (GitHub API) | file names |

**Read but not fetched for this document:**
- GitHub listings of `drivers/amlogic/amports`, `arch`, `arch/ucode` and `arch/regs` at the same commit, saved by the VENC study.
- `drivers/amlogic/amports/vh264.c`, saved by the HEVC study at the same commit: grepped only for the NV21 label and bit 17 (§5.5).

**Local files:**
- linux-firmware's `/lib/firmware/meson/vdec/gxl_mjpeg.bin.zst` and `gxl_mpeg4_5.bin.zst`, decompressed into the session's scratch space for the comparison (§1.3).
- A C920-like 1080p frame from the scratch space (§8.2).
- This repository's `docs/m26-vdec-facts.md`, `docs/m26-vdec-hevc-vendor-facts.md`, `docs/m48-venc-facts.md`, `meson_vdec.cpp`, `meson_vdec_private.h` and `headers/private/drivers/meson_vdec.h`.

**Not read:**
- The vendor's still-picture JPEG decoder `drivers/amlogic/amports/jpegdec.c` (`amjpegdec`, its own microcode under `arch/ucode/jpeg`) is in the same tree. It is not used by the MJPEG path and was left out of scope. It may say how the JPEG hardware treats sampling factors and quantisation tables (§5.4, §8.2).

### 11.2 Open questions for the first board test

1. Does `gxl_mjpeg.bin` run on GXBB with the vendor's protocol (§4.2), and post reports in AV_SCRATCH_9 with offsets in AV_SCRATCH_A?
2. The meaning of AV_SCRATCH_0/1 (the free-list reading), of MREG_DECODE_PARAM bit 15 and "pico_addr_mode", and whether AV_SCRATCH_3 matters.
3. What MCPU_INTR_MSK = 0xFFFF and VDEC_ASSIST_AMR1_INT8 = 8 do, and whether the microcode runs without them.
4. **The output for 4:2:2 input:** 4:2:0 as the canvases imply? Filtered or decimated? Does the microcode reprogram the PSCALE (read the BMEM phase-step entries back after a frame, if the BMEM can be read)?
5. Does bit 17 (two-plane output) work with this microcode, and is the chroma Cb-first with endian 7?
6. Is the third quantisation table (Cr) honoured?
7. Is a frame reported as soon as its EOI is in the VIFIFO, or only when more data follows? What does end of stream need?
8. What happens on a truncated or corrupt frame: damaged output, silence, or a hang? Does the restart of §4.7 recover?
9. What happens when all four buffers are held by the host?
10. Decode time per 1080p and 720p frame at 666.67 MHz.
11. Whether the microcode accepts restart intervals, DHT-less frames, odd sizes (not multiples of 16), and sizes above 1920 × 1088. None of these is needed for the C920.
12. The individual DOS_SW_RESET0 bits (§2.3) and the PSCALE_CTRL bits 15/14.
