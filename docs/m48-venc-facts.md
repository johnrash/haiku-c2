Facts read from Amlogic's GPL vendor driver (`drivers/amlogic/amports/encoder.c` and its helpers) in Hardkernel's ODROID-C2 kernel `odroidc2-3.14.y` at c3e4c730feb1 (2018-03-03); no code copied.

# M48: S905 (GXBB) HCODEC H.264 encoding, hardware facts

This document is the input for `meson_venc`, a clean-room, MIT-licensed Haiku kernel driver for the S905's H.264 encoder (HCODEC). It describes how Amlogic's own kernel drove that block on GXBB. That kernel had the encoder built in for the ODROID-C2 (`CONFIG_AM_ENCODER=y` in `odroidc2_defconfig`).

**Related documents:**
- `m26-vdec-facts.md` is the **base document**, cited as "base §N". It covers the DOS block, VDEC_1, the AMRISC microcode load by IMEM DMA, the canvas LUT and the mailbox interrupts. None of that is repeated here.
- `m26-vdec-hevc-vendor-facts.md` (cited as "HEVC-vendor §N") describes the same vendor tree for HEVC: how the vendor reaches HHI, the "vdec" clock gate, and `vdec_poweron`.
- `m48-venc-feasibility.md` is the decision note that led to this document.

**Conventions (as in the base document):**
- DOS offsets are **byte offsets from the DOS base 0xC8820000**. Amlogic's headers number DOS registers by 32-bit word index, and byte offset = word index × 4. Both are given where a register first appears, and §13 lists them all.
- "Set bits" and "clear bits" mean read-modify-write; "write" means a full 32-bit store.
- **UNVERIFIED** marks a meaning the vendor source does not state, or one that is inferred.
- "The vendor" means this driver as built for GXBB: `get_cpu_type()` is MESON_CPU_MAJOR_ID_GXBB (0x1F). That is at least GXBB and below GXTVBB (0x20); this kernel knows no later chip. Where the driver branches on the chip, only the GXBB branch is described, and the others are mentioned in a word.
- "Full mode" is the vendor's `UCODE_MODE_FULL` (0), the mode this document is about. §12.4 notes the other modes briefly.

Nothing in this document has been run on the board yet.

---

## 0. Address map

| Block | Physical base | Size | How the vendor reaches it |
|---|---|---|---|
| DOS (decoders and HCODEC) | 0xC882_0000 | 64 KiB | `codec_io/io_dos_base` in the C2 DT. A register with word index *r* is at base + *r* × 4 |
| HCODEC registers | inside DOS, word indexes 0x1000–0x1FFF (byte offsets 0x4000–0x7FFC) | | "HREG" accessors. They OR 0x1000 into the word index, a no-op for names that already carry it |
| HIU (HHI clock registers) | 0xC883_C000 | 8 KiB | Vendor HHI names use the old CBUS numbering (HHI_VDEC_CLK_CNTL = 0x1078). On GXBB only, the vendor subtracts 0x1000 and multiplies by 4, giving 0xC883C1E0 (HEVC-vendor §1.2) |
| AO (always-on) | 0xC810_0000 | 1 MiB window | AO register names are already byte offsets (AO_RTI_GEN_PWR_SLEEP0 = 0x3A << 2 = 0xE8) |
| DMC (canvas LUT) | 0xC883_8000 | 0x400 | canvas driver (base §6) |
| IRQ | GIC SPI 45 (INTID 77), edge rising | | `amvenc_avc` DT node, "mailbox_2" (§3) |

The vendor's register access is plain `readl`/`writel` (with barriers) through these mappings. Every address it writes into an HCODEC register is a 32-bit physical address.

---

## 1. At a glance

| Item | Vendor driver on GXBB |
|---|---|
| Block | HCODEC: a third DOS core next to VDEC_1 and HEVC, with its own AMRISC processor, frame reader (MFDIN), motion search (ME), intra estimation (IE), transform/quantiser (QDCT), entropy coder (VLC) and reconstruction/deblocking writer |
| Microcode | `h264_enc_mc_gx`: 2,070 words (8,280 bytes), zero-padded to 16 KiB and loaded once per session by IMEM DMA (§4) |
| Power island | AO_RTI_GEN_PWR_SLEEP0 bits 1:0, AO_RTI_GEN_PWR_ISO0 bits 5:4, DOS_MEM_PD_HCODEC, DOS_SW_RESET1 (§2) |
| Clock | upper half of HHI_VDEC_CLK_CNTL (bit 24 = gate). The vendor sets 500 MHz (fclk/4) on GXBB. DOS_GCLK_EN0 bits 26:12 are HCODEC's internal gates (§2) |
| Interrupt | HCODEC mailbox 2, GIC SPI 45, edge rising. Acknowledged by writing 1 to HCODEC_ASSIST_MBOX2_CLR_REG (§3) |
| Host protocol | command and status in HCODEC_HENC_SCRATCH_0: the CPU writes 1 (SPS), 2 (PPS), 3 (IDR) or 4 (P). The microcode answers 7, 8, 9 or 10 and raises the mailbox (§7) |
| Result | the byte count in HCODEC_VLC_TOTAL_BYTES; the bytes are at the start of the bitstream buffer (§7.4) |
| Input | MFDIN reads NV12, NV21, 3-plane 4:2:0, packed or planar 4:4:4, packed 4:2:2, RGB888 (packed or planar) or RGBA8888. RGB goes through a built-in RGB-to-YUV converter. Sizes are rounded up to 16 (§6) |
| Frames | two reconstruction buffers (4:2:0 with interleaved chroma) that swap roles after every picture: one is written as the new reconstruction while the other is read as the reference (§5, §7.5) |
| Coding | IDR and P pictures only, one reference picture, entropy by VLC (CAVLC presumed: §8). The CPU writes no header bits |
| Rate control | none in the kernel. One QP per picture chosen by the caller, or three 32-entry QP tables supplied by user space (§10) |
| Size limit | 1920 × 1088; the vendor always uses its 1080p buffer layout (§5) |
| Buffers | one 18 MiB region per encoder instance (CMA) in the vendor's layout (§5) |

**What the microcode does and what the CPU does.**
- The **microcode** runs the whole picture: it walks the macroblocks, runs MFDIN, ME, IE, QDCT and VLC, writes the reconstruction, and writes every byte of the SPS, PPS and slice data. In full mode on GXBB it also writes 80 bytes of statistics per macroblock to a dump buffer (§5.3).
- The **CPU** does the following:
  - powers the block and loads the microcode;
  - programs every hardware unit's static configuration before every picture: QP, quant tables, ME weights, buffers, canvases and the input format;
  - writes the picture-level syntax values into scratch registers: idr_pic_id, frame_num, the POC LSB and the two log2_max values;
  - writes the command, waits for the interrupt and reads the byte count;
  - swaps the reconstruction and reference roles;
  - decides which pictures are IDR.

**Per-picture flow (full mode, §11 has it register by register).**
1. Pulse the HCODEC sub-unit resets (the AMRISC keeps running).
2. Reprogram the canvases, the bitstream pointers, the QDCT/ME/IE/VLC settings, the scratch registers and MFDIN.
3. Write the command to HENC_SCRATCH_0.
4. Wait for mailbox 2. Status 9 or 10 means done.
5. Read HCODEC_VLC_TOTAL_BYTES and invalidate the bitstream range in the cache.
6. Advance frame_num and the POC; swap the two reconstruction buffers.

---

## 2. Power, isolation, memories, clocks and resets

### 2.1 What HCODEC owns

| Resource | HCODEC part | Shared with |
|---|---|---|
| AO_RTI_GEN_PWR_SLEEP0 (0xC81000E8) | bits 1:0 (1 = powered off) | VDEC_1 bits 3:2, HEVC bits 7:6 |
| AO_RTI_GEN_PWR_ISO0 (0xC81000EC) | bits 5:4 (1 = isolated) | VDEC_1 bits 7:6, HEVC bits 11:10 |
| AO_RTI_PWR_CNTL_REG0 (0xC8100010) | bits 4:3 are cleared at power-up (meaning **UNVERIFIED**) | unknown |
| DOS_MEM_PD_HCODEC | whole register (DOS + 0xFCC8, word 0x3F32); 0 = memories on | — |
| DOS_SW_RESET1 | whole register (DOS + 0xFC1C, word 0x3F07) | — |
| DOS_GCLK_EN0 (DOS + 0xFC04, word 0x3F01) | bits 26:12 | VDEC_1 bits 9:0 |
| DOS_GEN_CTRL0 (DOS + 0xFC08, word 0x3F02) | bit 0 is pulsed at power-up | bits 2:1 select the VIFIFO consumer (base §3.3) |
| HHI_VDEC_CLK_CNTL (0xC883C1E0) | bits 31:16 | VDEC_1 bits 15:0 |
| HHI_GCLK_MPEG0 (0xC883C140) bit 1 | the DOS bus clock | everything in DOS |

All the AO and DOS_GCLK_EN0 accesses in the vendor are read-modify-write of HCODEC's own bits.

### 2.2 The DOS clock gate

- Before anything else, the encoder turns on the amports "vdec" gate.
  - In the C2 DT this is the reset-controller entry GCLK_IDX_DOS, which is HHI_GCLK_MPEG0 bit 1 (HEVC-vendor §1.1; base §1.1 has the same gate as `dos`).
  - The gate is reference-counted across all amports users. The encoder releases its reference after power-down.
- The parser-top gate (HHI_GCLK_MPEG1 bit 25) is **not** needed by the encoder; the encoder never asks for it.

### 2.3 HHI_VDEC_CLK_CNTL bits 31:16: the HCODEC clock

The lower half is VDEC_1's clock (base §1.2). The upper half has the same layout, shifted by 16:

| Bits | Field | Values (fclk = 2000 MHz) |
|---|---|---|
| 27:25 | source | 0 = fclk/4 (500 MHz), 1 = fclk/3 (666.7), 2 = fclk/5 (400), 3 = fclk/7 (285.7), 4 = MPLL out 1, 5 = MPLL out 2 (the vendor's clkgx.c comment lists 0–5 for the lower half's 3-bit field) |
| 24 | gate | 1 = HCODEC clock on |
| 22:16 | divider | divisor − 1 |

**What the encoder writes.**
- It has its own table of seven clock levels (encoder.h).
- Each level is one full write of bits 31:16 that keeps bits 15:0: source, divider and gate = 1 together. There is no "program while gated" step.
- On GXBB the encoder picks **level 5** (set at probe): source 0, divider 0, gate 1. That is 500 MHz, and bits 31:16 read 0x0100.

| Level | Source / divider field | GXBB rate |
|---|---|---|
| 0 | fclk/5, field 1 (÷2) | 200 MHz |
| 1 | fclk/4, field 1 (÷2) | 250 MHz |
| 2 | fclk/7, field 0 | 285.7 MHz |
| 3 | fclk/3, field 1 (÷2) | 333.3 MHz |
| 4 | fclk/5, field 0 | 400 MHz |
| **5** | **fclk/4, field 0** | **500 MHz (GXBB default)** |
| 6 | fclk/3, field 0 | 666.7 MHz |

- After the HHI write, the same "clock on" step sets DOS_GCLK_EN0 bits 26:12 to 0x7FFF (read-modify-write).
- "Clock off" sets DOS_GCLK_EN0 bits 26:12 to 0, then clears HHI_VDEC_CLK_CNTL bit 24.
- `clock_level` is a module parameter, so the vendor allowed other levels at run time. That the encoder keeps up at 500 MHz, and whether level 6 works, are **UNVERIFIED** (§14).
- The shared helper `hcodec_clock_set` in arch/clkgx.c would pick 200 MHz instead (§2.7). The encoder does not use it.

### 2.4 Power-up order (encoder's own `avc_poweron`, GXBB)

| # | Register | Address | Action | Value / note |
|---|---|---|---|---|
| 1 | HHI_GCLK_MPEG0 | 0xC883C140 | set bit 1 | the "vdec" (DOS) gate, §2.2 |
| 2 | AO_RTI_PWR_CNTL_REG0 | 0xC8100010 | clear bits 4:3 | mask 0x18. Meaning **UNVERIFIED**; done for every chip from M8 on |
| 3 | wait | | 10 µs | |
| 4 | AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 | clear bits 1:0 | HCODEC power on |
| 5 | wait | | 10 µs | |
| 6 | DOS_SW_RESET1 | DOS + 0xFC1C | write 0xFFFFFFFF | every HCODEC reset asserted |
| 7 | DOS_SW_RESET1 | DOS + 0xFC1C | write 0 | released (no delay between) |
| 8 | HHI_VDEC_CLK_CNTL | 0xC883C1E0 | write (bits 15:0 kept) | bits 31:16 = 0x0100: 500 MHz, gate on (§2.3) |
| 9 | DOS_GCLK_EN0 | DOS + 0xFC04 | bits 26:12 = 0x7FFF | read-modify-write |
| 10 | DOS_MEM_PD_HCODEC | DOS + 0xFCC8 | write 0 | all HCODEC memories on |
| 11 | AO_RTI_GEN_PWR_ISO0 | 0xC81000EC | clear bits 5:4 | isolation off |
| 12 | wait | | 10 µs | |
| 13 | DOS_GEN_CTRL0 | DOS + 0xFC08 | set bit 0, then clear bit 0 | the vendor labels it as turning off automatic clock gating (**UNVERIFIED** what the pulse does) |
| 14 | wait | | 10 ms | after leaving the spinlock |

- Steps 2–13 run under a spinlock with interrupts off.
- The reset pulse (6–7) comes **before** the clock is turned on (8–9). The shared helper `vdec_poweron` does the same.
- The vendor never writes HCODEC's own GCLK_EN (word 0x1983) or HCODEC_MDEC_PIC_DC_CTRL (word 0x198E), unlike VDEC_1's power-up (base §1.3 steps 13–14).
- No DMC request bit is set for HCODEC. On GXBB the vendor sets DMC_REQ_CTRL bit 13 for VDEC_1 only (HEVC-vendor §1.3). Whether HCODEC's DMC port is enabled at reset is **UNVERIFIED**.
- Nothing is polled during power-up.

### 2.5 Power-down order (`amvenc_avc_stop`, then `avc_poweroff`)

| # | Register | Action |
|---|---|---|
| 1 | (IRQ) | the handler is freed. HCODEC_ASSIST_MBOX2_MASK is **not** cleared |
| 2 | AMRISC stop | §4.4: MPSR = 0, CPSR = 0, wait for the IMEM DMA to be idle, reset pulse 0x000359C4 |
| 3 | AO_RTI_GEN_PWR_ISO0 | set bits 5:4 (isolation on) |
| 4 | DOS_MEM_PD_HCODEC | write 0xFFFFFFFF |
| 5 | DOS_GCLK_EN0 | bits 26:12 = 0 |
| 6 | HHI_VDEC_CLK_CNTL | clear bit 24 |
| 7 | AO_RTI_GEN_PWR_SLEEP0 | set bits 1:0 |
| 8 | HHI_GCLK_MPEG0 bit 1 | the "vdec" gate reference is dropped. The gate goes off only when no other amports user holds it |

- AO_RTI_PWR_CNTL_REG0 bits 4:3 are not restored at power-down.
- **When:** the vendor powers up when the first encoder instance starts, and powers down when the last instance is closed. Between pictures the block stays powered and the AMRISC keeps running.

### 2.6 DOS_SW_RESET1 (DOS + 0xFC1C) values

| Value | Bits | When |
|---|---|---|
| 0xFFFFFFFF, then 0 | all | power-up; microcode reload (mode switch) |
| 0x000341C4, then 0 | 2, 6, 7, 8, 14, 16, 17 | **before every picture** (and once at session start, after the microcode load). Bit 14 is GXBB-only; older chips use the same set without it |
| 0x00001800, then 0 | 12, 11 | AMRISC start, just before MPSR = 1 |
| 0x000359C4, then 0 | 2, 6, 7, 8, 11, 12, 14, 16, 17 | AMRISC stop |

- The 0x000341C4, 0x1800 and 0x000359C4 pulses are each wrapped in three dummy reads of DOS_SW_RESET1 before and three after. The 0xFFFFFFFF pulses (power-up and reload) have no dummy reads; the reload one is followed by a 10 µs wait.
- The vendor names none of these bits.
  - By analogy with VDEC_1, bits 12:11 reset the AMRISC core (base §1.4 uses the same pair). This is **UNVERIFIED**.
  - The per-picture set leaves bits 11 and 12 alone, so the microcode keeps running while the other units are reset.
  - vdec_reg.h names bit 2 IQIDCT, bit 3 MC and bit 4 PSCALE for the decoder; whether that holds for HCODEC is **UNVERIFIED**.

### 2.7 The shared helper `vdec_poweron(VDEC_HCODEC)` (not used by the encoder)

- vdec.c has an HCODEC case. Only the M8 "DOS top register fix" calls it (§12.3); the encoder does not.
  1. SLEEP0 bits 1:0 are cleared, then a 10 µs wait.
  2. DOS_SW_RESET1 is pulsed with 0xFFFFFFFF.
  3. The clock is set through clkgx.c: clear bit 24, write source 2 (fclk/5) and divider field 1 into bits 31:16, then set bit 24 and DOS_GCLK_EN0 bits 26:12 = 0x7FFF. That is **200 MHz**.
  4. DOS_MEM_PD_HCODEC = 0, and ISO0 bits 5:4 are cleared.
- It does not touch AO_RTI_PWR_CNTL_REG0 or DOS_GEN_CTRL0, and it has no 10 ms wait.
- Its power-down is the same as §2.5 steps 3–7, except that the clock-off only clears bit 24.
- **"Powered" test (`vdec_on`):** HCODEC counts as on when SLEEP0 bits 1:0 are both 0 and HHI_VDEC_CLK_CNTL bit 24 is 1.

### 2.8 Registers shared with Haiku's VDEC driver (coexistence)

These are observations about this repository's `meson_vdec` (haiku/src/add-ons/kernel/drivers/video/meson_vdec/meson_vdec.cpp), not vendor facts. They matter only if VDEC_1 and HCODEC are used at the same time.

| Register | What meson_vdec does | Effect on a running HCODEC |
|---|---|---|
| DOS_GCLK_EN0 | power-up writes the whole register = 0x3FF (as base §1.3 says) | clears HCODEC's gates, bits 26:12 |
| HHI_GCLK_MPEG0 bit 1 | power-down clears it | stops the DOS bus clock under HCODEC |
| AO SLEEP0 / ISO0, HHI_VDEC_CLK_CNTL | read-modify-write of its own bits | safe, but the two drivers need a common lock |
| DOS_GEN_CTRL0 | written 0 at parser init (base §3.3) | harmless (HCODEC only pulses bit 0) |
| Canvas indexes | 0x80–0x8F (base §10) | no overlap with the vendor encoder's 0xE4–0xEC (§5.4) |

---

## 3. The interrupt

**Source and line.**
- The microcode signals completion through **HCODEC mailbox 2**.
- In the C2 device tree the `amvenc_avc` node (compatible "amlogic, amvenc_avc") has `interrupts = <0 45 1>` with the name "mailbox_2". That is **GIC SPI 45 (INTID 77), edge rising**.
- The `jpegenc` node lists the same line; the JPEG encoder uses HCODEC too.
- The `vdec` node lists mailbox_0/1/2 as SPI 43/44/45, and vdec_reg.h defines INT_DOS_MAILBOX_0/1/2 the same way. VDEC_1 uses mailbox 1 (SPI 44, base §7).
- Whether VDEC_1's or HEVC's own mailbox 2 also drives SPI 45 is **UNVERIFIED**. Haiku's VDEC driver never unmasks a mailbox 2, so this does not matter as long as it stays that way.

**Registers.**

| Operation | Register | Byte offset (word) | Value |
|---|---|---|---|
| Clear a stale request | HCODEC_ASSIST_MBOX2_CLR_REG | 0x41E4 (0x1079) | write 1 |
| Enable | HCODEC_ASSIST_MBOX2_MASK | 0x41E8 (0x107A) | write 1 |
| Acknowledge (first thing in the handler) | HCODEC_ASSIST_MBOX2_CLR_REG | 0x41E4 | write 1 |
| Reason | HCODEC_HENC_SCRATCH_0 (ENCODER_STATUS) | 0x6B00 (0x1AC0) | read: 7, 8, 9 or 10 = done (§7.2) |

- The clear-then-enable pair is written at the end of the protocol init (§9.2), so it is re-armed before every command that gets the full init: every IDR and P command, and SEQUENCE. PICTURE gets no init (§7.3).
- The mask is never written 0: at stop the vendor only frees the handler and powers down.
- No other status register is read. DOS_HCODEC_INT_EN (DOS + 0xFC18, word 0x3F06) exists but is never written. HCODEC_ASSIST_MBOX2_IRQ_REG (0x41E0) is not touched.
- **Vendor handler:**
  - It is a plain hard-IRQ handler on a shared line, and it always claims the interrupt.
  - It acknowledges the mailbox, then reads the status.
  - The status value is stored on every interrupt. Only the first done value after a command does more: it marks the command as answered and, if the value is 8, 9 or 10 (anything but SEQUENCE_DONE), sets a "full re-init before the next command" flag (§7.3).
  - A tasklet then wakes the waiting thread, which checks the stored status.
- **Board experience (base §10):** the DOS mailbox interrupts are short pulses. With the GIC set to level, many arrive as spurious interrupts. Program SPI 45 as edge-triggered, as the DT says.

---

## 4. Microcode

### 4.1 Which image

- In full mode the vendor chooses the image by chip:
  - from GXTVBB on: `h264_enc_mc_gxtv`;
  - **from GXBB to below GXTVBB: `h264_enc_mc_gx`**;
  - from MG9TV: `mix_dump_mc_m2_dblk`;
  - older chips: `mix_dump_mc_dblk` or `mix_dump_mc`.
- A build option (`USE_OLD_DUMP_MC`, **not** defined in this tree) would select `mix_dump_mc_gx_dblk` for GXBB instead.
- Registration: arch/ucode/h264_enc/h264_enc_mc.c registers three images for GXBB under the format VFORMAT_H264_ENC, each with the version string "0.0.0.1":
  - `mix_sw_mc_hdec_gx_dblk` (SW-mix mode);
  - `mix_dump_mc_gx_dblk`;
  - `h264_enc_mc_gx`.

  On a GXBB kernel only GXBB's images are kept.
- Source of `h264_enc_mc_gx`: arch/ucode/h264_enc/h264_enc_gx.h (25,931 bytes of C source, md5 06dcbd9785fe94f71e6b2876bb655ed2). Its header carries Amlogic's GPL v2-or-later notice. There is no microcode source, only the hex (`m48-venc-feasibility.md` covers what that means for shipping).
- Loading from the filesystem (`video/<cpu>/decoder/<format>/<name>`) is compiled out; the image is built into the kernel.

### 4.2 Array format and the image that is DMA'd

- **Array:** one C array of 32-bit words, written as hex constants, five per line.
  - **2,070 words = 8,280 bytes.**
  - Word 2,050 is the last non-zero word; the final 19 words are zero.
- **Byte order:** on AArch64 the words sit in memory little-endian. The CPU copies the bytes unchanged and does no swapping. The IMEM DMA reads memory in address order. This is the same convention as the decoders (base §2; HEVC-vendor §2).
- **How the vendor builds the DMA image:**
  1. At module init, the registration keeps a heap copy of the array's bytes (sizeof = 8,280).
  2. The loader gives `get_decoder_firmware_data` a 16 KiB (16,384-byte) buffer.
  3. That function finds the built-in copy by name and copies min(16,384, 8,280) bytes.
  4. It zero-fills the rest of the 16 KiB and returns 8,280.
- **What the DMA transfers:** 0x1000 words (16 KiB): the 8,280 image bytes followed by 8,104 zero bytes.
- **Fingerprints** of that image, for checking a separately obtained copy. They were computed in memory from the array in h264_enc_gx.h; no file was written:
  - the 8,280-byte image: SHA-256 778e4b7b71f2fb85fe0ede08a93835a94c7d8fa9f665c17cdbc9dbcaa0d41687;
  - the 16 KiB zero-padded image: SHA-256 16f05873f9460ec6f761a8931dccb517a163cadb102c594a7b2d32f5eb7a2f2d, MD5 899155e8fbc031b50a0aee3dfcf821cc.

### 4.3 Load sequence (`amvenc_loadmc`)

The procedure is the VDEC_1 one (base §2), with the HCODEC copies of the AMRISC registers. Each sits 0x1000 words (0x4000 bytes) above VDEC_1's.

| # | Register | Byte offset (word) | Action / value |
|---|---|---|---|
| 0 | HCODEC_ASSIST_MMC_CTRL1 | 0x4008 (0x1002) | write 0x32. GXBB (and anything from MG9TV on) writes 0x32, older chips 0x2. Done just before every load. Meaning **UNVERIFIED** |
| 1 | (staging) | | 16 KiB physically contiguous buffer with a 32-bit bus address, holding the image from §4.2. The vendor uses a cached heap buffer, cleaned to memory for the device (DMA map "to device") |
| 2 | HCODEC_MPSR | 0x4C04 (0x1301) | write 0 (main AMRISC stopped) |
| 3 | HCODEC_CPSR | 0x4C84 (0x1321) | write 0 (co-processor stopped) |
| 4 | HCODEC_MPSR | 0x4C04 | read it twice (dummy reads, labelled "for timing") |
| 5 | HCODEC_IMEM_DMA_ADR | 0x4D04 (0x1341) | write the staging bus address |
| 6 | HCODEC_IMEM_DMA_COUNT | 0x4D08 (0x1342) | write 0x1000 (32-bit words) |
| 7 | HCODEC_IMEM_DMA_CTRL | 0x4D00 (0x1340) | write 0x00078000: bit 15 = go/busy, bits 18:16 = 7 (meaning **UNVERIFIED**, the same value as for the decoders) |
| 8 | HCODEC_IMEM_DMA_CTRL | 0x4D00 | poll until bit 15 reads 0. Timeout 1 s, yielding between polls. On timeout the vendor reports an error and fails the start |
| 9 | (staging) | | unmapped; the buffer is kept for the next load and freed at module removal |

- The alignment the IMEM DMA needs is **UNVERIFIED**. The vendor's buffer is a 16 KiB heap allocation, page-aligned in practice. Base §10 found that page alignment works for VDEC_1.
- The AMRISC's instruction memory is 4 K × 32 bits; encoder.h's memory map puts the microcode at 0x0000–0x3FFF.

### 4.4 Start, stop and reload

**Start (`amvenc_start`)**, done once per session after the whole setup of §11.1:
1. Three dummy reads of DOS_SW_RESET1.
2. DOS_SW_RESET1 ← 0x00001800 (bits 12, 11), then ← 0.
3. Three dummy reads.
4. **HCODEC_MPSR ← 1.** CPSR is never set to 1.

ENCODER_STATUS (HENC_SCRATCH_0) is written 0 (idle) before the start. The microcode then waits for a command (§7.2).

**Stop (`amvenc_stop`):**
1. HCODEC_MPSR ← 0, HCODEC_CPSR ← 0.
2. Wait up to 1 s for HCODEC_IMEM_DMA_CTRL bit 15 to read 0 (busy-wait, no yielding; on timeout it simply continues).
3. Three dummy reads, then DOS_SW_RESET1 ← 0x000359C4, then ← 0, then three dummy reads.

**Reload (`reload_mc`)** happens only when the requested microcode mode differs from the loaded one. In full mode on GXBB that means a switch to or from SW-mix mode.
1. Stop (above).
2. DOS_SW_RESET1 ← 0xFFFFFFFF, then ← 0.
3. Wait 10 µs.
4. HCODEC_ASSIST_MMC_CTRL1 ← 0x32.
5. Load (§4.3).
6. The command path then forces the full per-picture init and writes the command to ENCODER_STATUS **before** calling start. So after a reload, the command is already waiting when MPSR becomes 1.

In full mode the microcode is loaded once per power-up and never reloaded between pictures or between encoder instances.

### 4.5 Microcode-side constants in encoder.h (for orientation only)

- **Microcode-internal memory map:** microcode 0x0000–0x3FFF, "HencTop" 0x4000–0x4FFF, "PredTop" 0x5000–0x5FFF, MB-bottom buffers from 0x6000 and 0x8000.
- **Interrupt request bits:** VB_FULL 0x01, MAIN 0x02, VLC 0x04, QDCT 0x08, LDMA 0x10.
- **AMRISC register aliases and a local-memory layout,** commented out.
- These describe the microcode's own view; the CPU writes none of them. Purpose **UNVERIFIED** beyond the names.

---

## 5. Memory

### 5.1 The vendor's work region

- **Size:** each encoder instance owns one physically contiguous region. On the C2, the `amvenc_avc` DT node's `memory-region` is commented out, so the vendor takes **18 MiB (0x1200000)** from the codec_mm CMA pool.
  - The vendor caps its use of the pool at 40 MiB, so at most two instances can exist.
  - With a reserved region instead, the instance count is size / 0x1160000, at most 8.
- **Layout:** the vendor always uses the **1080p buffer layout** (the third entry of its level table), whatever the picture size.
  - When an instance is opened, the region size is checked against the smallest level's minimum. Then the 1080p entry is copied in unconditionally.
  - CONFIG_INIT rejects sizes above 1920 × 1088.
- **Addressing:** every address below is region start + offset. The region start comes from CMA and is page-aligned at least; any stronger alignment is **UNVERIFIED**. All values are 32-bit physical addresses.

### 5.2 Buffer level table (offsets / sizes, bytes)

| Buffer | 480p level (≤ 640 × 480) | 720p level (≤ 1280 × 720) | **1080p level (≤ 1920 × 1088) — always used** |
|---|---|---|---|
| minimum region size | 0x580000 | 0x9E0000 | **0x1160000** (17.375 MiB) |
| dct (input / QDCT) | 0x000000 / 0xFE000 | 0x000000 / 0x2F8000 | **0x000000 / 0x6BA000** |
| dec0_y (reconstruction A) | 0x100000 / 0x80000 | 0x300000 / 0x180000 | **0x6D0000 / 0x300000** |
| dec1_y (reconstruction B) | 0x180000 / 0x80000 | 0x480000 / 0x180000 | **0x9D0000 / 0x300000** |
| assit (microcode assist) | 0x240000 / 0xC0000 | 0x640000 / 0xC0000 | **0xD10000 / 0xC0000** |
| bitstream | 0x300000 / 0x100000 | 0x700000 / 0x100000 | **0xE00000 / 0x100000** |
| inter_bits_info | 0x400000 / 0x2000 | 0x800000 / 0x4000 | **0xF00000 / 0x8000** |
| inter_mv_info | 0x402000 / 0x13000 | 0x804000 / 0x40000 | **0xF08000 / 0x80000** |
| intra_bits_info | 0x420000 / 0x2000 | 0x848000 / 0x4000 | **0xF88000 / 0x8000** |
| intra_pred_info | 0x422000 / 0x13000 | 0x84C000 / 0x40000 | **0xF90000 / 0x80000** |
| qp_info | 0x438000 / 0x8000 | 0x890000 / 0x8000 | **0x1010000 / 0x8000** |

- **The dct size** (0x6BA000) is SW-mix mode's 864 bytes per macroblock: 120 × 68 × 864 = 0x6B9400, rounded up. In full mode the region serves as the input frame buffer for the "local" input type (§6.4).
- **Chroma placement:** the chroma (UV) part of each reconstruction buffer starts right after its luma: dec*_uv = dec*_y + CW × CH, size CW × CH / 2. Here CW = width rounded up to 32 and CH = height rounded up to 16.
  - At 1920 × 1080: CW = 1920 and CH = 1088, so Y = 0x1FE000 and UV = 0xFF000 (0x2FD000 in all, inside the 0x300000 slot).
  - dec0_uv is at 0x8CE000 and dec1_uv at 0xBCE000.

### 5.3 What is used in full mode on GXBB, and which register points at it

| Buffer | Where (1080p layout) | Size | Register(s) and value |
|---|---|---|---|
| **Input frame** (local type only) | dct region, offset 0 | up to 0x6BA000 | goes into the MFDIN canvases or HCODEC_MFDIN_REG5 (§6). With the "physical" or "canvas" input types the frame can be anywhere |
| **QDCT macroblock buffer** | the same dct region | 0x6BA000 | HCODEC_QDCT_MB_START_PTR = start; HCODEC_QDCT_MB_END_PTR = start + size − 1; HCODEC_QDCT_MB_WR_PTR = HCODEC_QDCT_MB_RD_PTR = start; HCODEC_QDCT_MB_BUFF = 0. In full mode the QDCT's DDR reader is off (QDCT_MB_CONTROL bit 1 = 0, §9.2), so this is presumably unused (**UNVERIFIED**) |
| **Reconstruction A** | dec0: Y at 0x6D0000, UV at Y + CW·CH | CW·CH·3/2 | canvases 0xE4 (Y) and 0xE5/0xE6 (UV), §5.4 |
| **Reconstruction B** | dec1: Y at 0x9D0000, UV at Y + CW·CH | CW·CH·3/2 | canvases 0xE7 (Y) and 0xE8/0xE9 (UV) |
| **Assist buffer** | 0xD10000 | 0xC0000 (768 KiB) | HCODEC_HENC_SCRATCH_1 (MEM_OFFSET_REG) = its physical address. Contents and how much is used are **UNVERIFIED** |
| **Bitstream (VLC VB)** | 0xE00000 | 0x100000 (1 MiB) | HCODEC_VLC_VB_START_PTR = HCODEC_VLC_VB_WR_PTR = HCODEC_VLC_VB_SW_RD_PTR = start; HCODEC_VLC_VB_END_PTR = start + 0xFFFFF (inclusive end) |
| **Info dump** (GX only) | 0xF00000 (the inter_bits_info offset) | 80 bytes × macroblocks, rounded up to 4 KiB; at 1080p: 8,160 MBs × 80 = 652,800, rounded to **0xA0000** | HCODEC_HENC_SCRATCH_I (INFO_DUMP_START_ADDR) = its physical address. It spans the inter/intra info slots, which full mode does not otherwise use |
| **Microcode staging** | separate heap buffer | 16 KiB | HCODEC_IMEM_DMA_ADR, during the load only (§4.3) |

- **Not used in full mode on GXBB:** inter_mv_info, intra_bits_info and intra_pred_info have no register pointing at them; the BITS/MV-info pointers HENC_SCRATCH_M/N are written only on pre-GX chips. qp_info is used only in SW-mix mode.
- **The dump format** (80 bytes per macroblock) is read only by Amlogic's user-space library, for its rate control. The kernel does not describe it (**UNVERIFIED**).
  - The vendor gives user space three weight constants with it: ME 0x3320, I4 0x3655, I16 0x3320 (§9.2).
  - Whether the microcode always writes the dump is **UNVERIFIED**. A driver should keep the buffer allocated and pointed at.

### 5.4 Canvases

The vendor uses canvas indexes **0xE4 to 0xEC**.

| Index | Use | Address | Width (bytes) | Height (lines) |
|---|---|---|---|---|
| 0xE4 | reconstruction A, Y | dec0_y | CW | CH |
| 0xE5 | reconstruction A, chroma | dec0_uv | CW | CH / 2 |
| 0xE6 | reconstruction A, "third plane" (same address as 0xE5) | dec0_uv | CW | CH / 2 |
| 0xE7 | reconstruction B, Y | dec1_y | CW | CH |
| 0xE8 | reconstruction B, chroma | dec1_uv | CW | CH / 2 |
| 0xE9 | reconstruction B, "third plane" (same as 0xE8) | dec1_uv | CW | CH / 2 |
| 0xEA–0xEC | input planes 0–2 (§6.4) | input | per format | per format |

- **Canvas settings:** every encoder canvas is **linear (block mode 0), no wrap, endian 0** (no byte swap). This differs from the decoder canvases, which use endian 7 (base §6). §6.7 explains why.
- **LUT entry details:** the LUT entry holds the address in 8-byte units and the width in 8-byte units. The vendor's canvas driver rounds both **up** to a multiple of 8, so a misaligned address would be silently moved.
- **Canvas words:** HCODEC's canvas registers take three indexes packed as (plane 2 << 16) | (plane 1 << 8) | plane 0.
  - Reconstruction A = **0x00E6E5E4**; reconstruction B = **0x00E9E8E7**.
  - At session start the "deblock/reconstruction" word is A and the "reference" word is B; they swap after every picture (§7.5).
- **Registers:**

| Register | Byte offset (word) | Value |
|---|---|---|
| HCODEC_REC_CANVAS_ADDR | 0x66C8 (0x19B2) | reconstruction word |
| HCODEC_DBKR_CANVAS_ADDR | 0x66C0 (0x19B0) | reconstruction word |
| HCODEC_DBKW_CANVAS_ADDR | 0x66C4 (0x19B1) | reconstruction word |
| HCODEC_ANC0_CANVAS_ADDR | 0x6640 (0x1990) | reference word |
| HCODEC_VLC_HCMD_CONFIG | 0x7550 (0x1D54) | 0, written together with ANC0 |
| HCODEC_CURR_CANVAS_CTRL | 0x66CC (0x19B3) | 0, written in the protocol init |

- The reconstruction is 4:2:0 with **interleaved chroma** (both chroma canvases point at one plane as wide as Y). Whether the order is Cb-first (NV12) or Cr-first is **UNVERIFIED**; it matters only for reading reconstructions back for a test.
- The canvases 0xE4–0xE9 are programmed again before every picture (§11.3). The input canvases are programmed per picture too.

### 5.5 Alignment

- **Canvas addresses:** 8-byte units (above). All the vendor's offsets are at least 32 KiB-aligned within a page-aligned region.
- **Bitstream pointers:** the vendor's buffer is 1 MiB-aligned within the region. The hardware's own requirement is **UNVERIFIED**; 8 bytes is a likely minimum, given the 64-bit byte reversal in §6.7.
- **Assist and dump buffers:** 64 KiB- and 1 MiB-aligned in the vendor layout. The requirement is **UNVERIFIED**.
- **Address width:** every address register is 32 bits; buffers must be below 4 GiB.

### 5.6 Cache maintenance in the vendor

User space maps the whole region **cached** (the mmap does not ask for uncached pages). The driver therefore does explicit maintenance:

| When | What | Range |
|---|---|---|
| Before an IDR/P command, local input type, if the request asks for it | clean ("sync for device", to-device) | dct region start, length = the frame size the caller gives |
| After SPS+PPS, if asked | invalidate ("sync for CPU", from-device) | bitstream start, SPS size + PPS size |
| After a picture, if asked | invalidate | bitstream start, picture size |
| After a picture, if asked (info) | invalidate | the dump region, its page-rounded size |
| After a picture, if asked (reference) | invalidate | the Y start of the buffer that was the **reference** for this picture (not the new reconstruction), CW × CH × 3/2 |
| Microcode load | clean | the 16 KiB staging buffer |
| User-space ioctls | clean or invalidate any sub-range of the input, bitstream, reference or dump buffers on request | |

- Nothing is done for the reconstruction buffers or the assist buffer between pictures; only the device touches them.
- **For a Haiku driver (inference):** map the device-private buffers (reconstructions, assist, dump, QDCT) uncached, or clean and invalidate them once after allocation. Clean the input before each command and invalidate the bitstream range before reading it.

---

## 6. The input frame reader (MFDIN)

### 6.1 Register numbering on GXBB

- The vendor's hcodec_regs.h names the MFDIN registers in the M8 layout: HCODEC_MFDIN_REG*n* = word 0x1010 + *n* (REG0_CRST = 0x1010 … REGB_AMPC = 0x101B).
- **On GXBB the encoder adds −8 to every MFDIN register**, so on GXBB **MFDIN_REG*n* = word 0x1008 + *n***.
  - The GX-only noise-reduction registers REG0D–REG16 are defined relative to REGB_AMPC: M8 words 0x101D–0x1026, GXBB words 0x1015–0x101E.
  - On M8 those words would collide with HCODEC_ASSIST_AMR1_INT0/1 (0x1025/0x1026), which is consistent with the block having moved.
- Registers used on GXBB:

| Register (vendor name) | GXBB word | Byte offset | Use |
|---|---|---|---|
| MFDIN_REG1_CTRL | 0x1009 | 0x4024 | format and conversion control |
| MFDIN_REG3_CANV | 0x100B | 0x402C | canvas indexes and bytes-per-pixel codes |
| MFDIN_REG4_LNR0 | 0x100C | 0x4030 | linear mode: bytes per pixel code, bytes per line |
| MFDIN_REG5_LNR1 | 0x100D | 0x4034 | linear mode: start address |
| MFDIN_REG8_DMBL | 0x1010 | 0x4040 | picture size |
| MFDIN_REG9_ENDN | 0x1011 | 0x4044 | byte-lane order |
| MFDIN_REG0D … REG11 | 0x1015 … 0x1019 | 0x4054 … 0x4064 | luma noise reduction |
| MFDIN_REG12 … REG16 | 0x101A … 0x101E | 0x4068 … 0x4078 | chroma noise reduction |

REG0 (CRST), REG2 (STAT), REG6, REG7, REGA, REGB and REGC exist at 0x1008 + *n* but are not written.

### 6.2 Fields

The source gives each field's low bit (the shift). The upper bit of a multi-bit field is inferred from the value range or from the next field's shift, and is **UNVERIFIED** where noted.

**MFDIN_REG1_CTRL (byte 0x4024):**

| Bits | Field (vendor variable name) | Meaning |
|---|---|---|
| 3:0 | iformat | input format code, §6.3 |
| 5:4 | oformat | output format (width inferred). The vendor always writes 0 (4:2:0). Its logic implies 1 = 4:2:2 and 2 = 4:4:4 (**UNVERIFIED**) |
| 6 | dsample_en | 1 = downsample chroma: set for 4:4:4 input (when oformat ≠ 2) and for 4:2:2 input (when oformat = 0) |
| 8 | y_size | 0 = 16-pixel vertical pickup, 1 = 8 pixels; the vendor sets it 1 only when oformat ≠ 0 |
| 9 | interp_en | 1 = interpolate chroma (4:2:2 input to 4:4:4 output, or 4:2:0 input to a non-4:2:0 output); 0 in every case the vendor uses |
| 12 | r2y_en | 1 = RGB-to-YUV conversion on |
| 14:13 | r2y_mode | conversion mode, range 0–3 (width inferred from the range). The vendor writes 1 whenever r2y_en is 1, else 0 (§6.5) |
| 19 | nr_enable | 1 = noise reduction on (GX only) |

**MFDIN_REG3_CANV (byte 0x402C):**

| Bits | Content |
|---|---|
| 7:0 / 15:8 / 23:16 | canvas index of plane 0 / 1 / 2 (canvas input only; 0 in linear mode) |
| 25:24 | plane 0, bytes per pixel in x: 0 = half, 1 = 1, 2 = 2, 3 = 3 |
| 27:26 | planes 1–2, bytes per pixel in x (same codes) |
| 29:28 | plane 0, bytes per pixel in y (same codes) |
| 31:30 | planes 1–2, bytes per pixel in y (same codes) |

The vendor's codes:
- plane 0 x: 3 for iformat 1, 2 for iformat 0, otherwise 1;
- planes 1–2 x: 0 for iformat 4, otherwise 1;
- plane 0 y: always 1;
- planes 1–2 y: 1 for iformat 5, otherwise 0.

**MFDIN_REG4_LNR0 (byte 0x4030):**
- Canvas input: written 0.
- Linear input: bits 16 and up = the "bytes4p" code, bits 15:0 = bytes per line.
  - The code is 3 for iformats 8, 9 and 12, 2 for 10, 1 for 11 and 0 otherwise.
  - The vendor computes bytes per line as (width rounded to 16) × code.

**MFDIN_REG5_LNR1 (byte 0x4034):** canvas input: 0. Linear input: the physical start address of the frame.

**MFDIN_REG8_DMBL (byte 0x4040):** **on GXBB (picture width << 14) | picture height**, both rounded up to 16. (M8 puts the width at bit 12.) Field widths beyond these shifts are **UNVERIFIED**.
- 1920 × 1080 → 1920 × 1088 → 0x01E00440;
- 1280 × 720 → 0x014002D0;
- 640 × 480 → 0x00A001E0.

**MFDIN_REG9_ENDN (byte 0x4044):** eight 3-bit fields. Field *i* (bits 3*i*+2:3*i*) holds 7 − *i*, so the whole register is **0x00053977**. §6.7 says what it does.

### 6.3 iformat codes

| iformat | Meaning in the vendor driver | Planes / access | Class |
|---|---|---|---|
| 0 | 4:2:2 packed, one plane (2 bytes per pixel) | canvas | 4:2:2 |
| 1 | 4:4:4 packed, one plane (3 bytes per pixel), including **RGB888** with r2y_en | canvas | 4:4:4 |
| 2 | **NV21**: Y plane + interleaved chroma plane | 2 canvases | 4:2:0 |
| 3 | **NV12**: Y plane + interleaved chroma plane | 2 canvases | 4:2:0 |
| 4 | 4:2:0 three planes (Y, U, V) | 3 canvases | 4:2:0 |
| 5 | 4:4:4 three planes, including planar RGB888 with r2y_en | 3 canvases | 4:4:4 |
| 6, 7 | not used by the vendor | (canvas) | — |
| 8, 9 | not used by the vendor; linear 4:4:4 with code 3 (**UNVERIFIED** what they are) | linear | 4:4:4 |
| 10 | 4:2:2 packed, one plane | **linear** | 4:2:2 |
| 11 | not used by the vendor; linear 4:2:0 with code 1 (**UNVERIFIED**) | linear | 4:2:0 |
| 12 | **RGBA8888** (with r2y_en) | **linear** | 4:4:4 |

Rule: iformat < 8 reads through canvases; iformat ≥ 8 reads linear memory from REG5. Which byte holds which component (YUYV versus UYVY; R or B first; where alpha sits) is **UNVERIFIED** for every packed format.

### 6.4 Per-format setup in the vendor driver

- **Input types.** The caller passes a type, a format and a source:
  - "local": the frame is in the dct region at offset 0 of the work region;
  - "physical": the source is a physical address;
  - "canvas": the source holds canvas indexes the caller already programmed.
- **RGB565** is rejected.
- **Notation:** PW = width rounded up to 16, PH = height rounded up to 16, and "src" = the frame's physical address.

| Format | iformat | r2y | Canvases (local and physical types) | REG1_CTRL (GXBB, nr on) | REG3 high bits |
|---|---|---|---|---|---|
| NV12 | 3 | 0 | 0xEA: src, width align32(w), height PH. 0xEB: src + align32(w) × PH, width align32(w), height PH/2. REG3 low = 0xEBEA | 0x00080003 | 0x15000000 |
| NV21 | 2 | 0 | as NV12 | 0x00080002 | 0x15000000 |
| YUV420 3-plane | 4 | 0 | 0xEA: src, width align64(w), height PH. 0xEB: src + CW64 × PH, width CW64/2, height PH/2. 0xEC: src + CW64 × PH × 5/4, same size as 0xEB (CW64 = align64(w)) | 0x00080004 | 0x11000000 |
| YUV444 packed | 1 | 0 | 0xEA: src, width align32(PW × 3), height PH | 0x00080041 | 0x17000000 |
| **RGB888 packed** | 1 | **1** | as YUV444 packed | **0x00083041** | 0x17000000 |
| YUV444 planar | 5 | 0 | 0xEA/0xEB/0xEC at src, src + align32(w) × PH and src + 2 × align32(w) × PH, each align32(w) × PH | 0x00080045 | 0x55000000 |
| RGB888 planar | 5 | 1 | as YUV444 planar | 0x00083045 | 0x55000000 |
| YUV422 packed | 10 | 0 | none (linear): REG4 = (2 << 16) \| PW × 2, REG5 = src | 0x0008004A | 0x15000000 |
| **RGBA8888** | **12** | **1** | none (linear): REG4 = (3 << 16) \| **PW × 3** (§6.9), REG5 = src | **0x0008304C** | 0x15000000 |

- **Canvas input type:** iformat 0 (4:2:2), 1 (4:4:4), 2/3 (NV21/NV12), 4 or 5. The source supplies one, two or three canvas indexes (masked to 8, 16 or 24 bits). r2y is set only for planar RGB. RGBA and linear formats are refused.
- **Where REG1 differs:** with noise reduction off, bit 19 is 0 (for example NV12 = 0x00000003). On pre-GX chips bit 19 is never written.
- **Size of the local input region:**
  - At 1080p, NV12 (0x2FD000) and RGB888 (0x5FA000) fit in the 0x6BA000 dct region.
  - RGBA8888 at 1920 × 1088 is 0x7F8000 bytes and does **not** fit: it would run into reconstruction A at 0x6D0000. In the vendor layout, 1080p RGBA works only with the "physical" input type.
- **Write order per picture:**
  1. the input canvases;
  2. the ten noise-reduction registers (GX only, §6.6);
  3. REG1_CTRL;
  4. REG8_DMBL;
  5. REG3, REG4 and REG5;
  6. REG9_ENDN.

### 6.5 RGB-to-YUV conversion

- The vendor sets r2y_en (REG1 bit 12) for RGB888, planar RGB888 and RGBA8888. It sets r2y_mode (bits 14:13) to 1; its own note says the mode is fixed at 1 for now and could be 0–3.
- **No coefficient or offset registers are written.** The conversion is whatever mode 1 means in hardware.
- Which matrix (BT.601 or BT.709) and which range (limited or full) is **UNVERIFIED**; it must be measured on the board (§14).

### 6.6 Noise reduction (GX only)

- The vendor's module parameter `nr_mode` defaults to **3**:
  - 0 = off, 1 = spatial only, 2 = temporal only, 3 = both;
  - when non-zero it overrides the mode the caller asks for.
- Luma spatial and temporal follow the mode, chroma spatial follows luma spatial, and **chroma temporal is always off**.
- Values written on GXBB with nr_mode 3. Field meanings are **UNVERIFIED**, except bit 0 of REG0D, REG0E, REG12 and REG13, which is the enable:

| Register | GXBB byte offset | Value (nr 3) | Bit 0 means | Value with nr 0 |
|---|---|---|---|---|
| REG0D | 0x4054 | 0xFC07BF13 | luma spatial enable | 0xFC07BF12 |
| REG0E | 0x4058 | 0x001FE40B | luma temporal enable | 0x001FE40A |
| REG0F | 0x405C | 0x08040544 | — | same |
| REG10 | 0x4060 | 0x2020140A | — | same |
| REG11 | 0x4064 | 0x000FC018 | — | same |
| REG12 | 0x4068 | 0xFC07BF11 | chroma spatial enable | 0xFC07BF10 |
| REG13 | 0x406C | 0x001FE40A | chroma temporal enable (always 0) | same |
| REG14 | 0x4070 | 0x08040544 | — | same |
| REG15 | 0x4074 | 0x2020140A | — | same |
| REG16 | 0x4078 | 0x000FC018 | — | same |

- How the luma and chroma sets of these values are built:
  - REG0D and REG12 are bits 1:0 = enable | (1 << 1) for luma (0 for chroma), bits 5:2 = 4, bits 13:6 = −4 as an 8-bit value (0xFC), bits 19:14 = 30, bits 25:20 = 0, bits 31:26 = 63;
  - REG0E and REG13 are enable | 1 << 1 | 1 << 3 | 8 << 7 | 63 << 13 | 3 << 19;
  - REG0F and REG14 hold 4, 4, 5, 4 and 8 at bits 0, 4, 8, 16 and 24;
  - REG10 and REG15 hold 10, 20, 32 and 32 in their four bytes;
  - REG11 and REG16 are 24 | 63 << 14.
- Where the temporal filter keeps its previous frame is **UNVERIFIED**; no buffer is given for it. NR changes the encoder's input, so turn it off (nr 0) for bit-exact tests.

### 6.7 Byte order

- The encoder canvases use **endian 0**: the DMC hands the 64-bit words over unswapped.
- MFDIN_REG9_ENDN = 0x00053977 maps byte lane *i* to source lane 7 − *i*. That is a full byte reversal within each 64-bit word, which is what endian 7 does in the decoder canvases.
- **Net effect:** an input frame in ordinary memory byte order (as an application writes it) is read correctly with canvas endian 0 plus this REG9 value. The same REG9 value is used for linear input.
- A canvas programmed with endian 7 (as Haiku's VDEC driver does for its own canvases) would swap twice: the bytes would arrive reversed. Use endian 0 for every HCODEC canvas.
- The bitstream side has its own setting: HCODEC_VLC_VB_CONTROL bits 5:3 = 7 (§9.2). The bytes at the bitstream start are presumably in stream order (**UNVERIFIED**, but Amlogic's user space reads them directly).

### 6.8 Picture size rounding

| Quantity | Rounding |
|---|---|
| MFDIN picture size (REG8_DMBL) | width and height up to 16 |
| NV12/NV21/planar-444 input canvas width | width up to 32 |
| 3-plane 4:2:0 input canvas width | width up to 64 (chroma half that) |
| RGB888 packed canvas width | (width up to 16) × 3, then up to 32 |
| Input canvas height | height up to 16 |
| Reconstruction canvases | width up to 32, height up to 16 |
| HCODEC_VLC_PIC_SIZE | the **unrounded** width \| height << 16 |
| HDEC_MC_OMEM_AUTO | (width + 15) / 16 − 1 as the last macroblock column |

- **Consequence:** MFDIN reads PH lines. For 1080-line material that is 1088 lines, so in the vendor's layout the chroma plane starts after 1088 luma lines, and rows 1080–1087 belong to the frame.
- What MFDIN returns when a canvas is shorter than PH lines (for example a 1080-line Y plane with its chroma right after it) is **UNVERIFIED**. The safe layout pads the luma to PH lines.

### 6.9 RGBA8888 (iformat 12) on GXBB

- **What the source shows:**
  - The driver accepts RGBA8888 for the local and physical types on **every** chip; no chip check excludes GXBB.
  - It maps RGBA to iformat 12, linear access, r2y on, 4:4:4 → 4:2:0 downsampling.
  - The only GXBB-specific MFDIN differences are the −8 register offset, the width shift of 14 and the NR registers.
- **What it does not establish:**
  - whether GXBB's MFDIN implements iformat 12 at all;
  - the line length: the vendor computes bytes per line as width × 3 for iformat 12, the same as the 3-byte formats 8 and 9. A 4-byte-per-pixel line is width × 4. This is either a vendor bug or a hardware meaning of the "bytes4p" code that the source does not explain;
  - the byte position of alpha and the R/B order.
- All of this is **UNVERIFIED** and is the first thing to test (§14).
- If iformat 12 does not work, RGB888 packed (iformat 1, canvas access, known path) or a CPU conversion to NV12 is the fallback.

---

## 7. The command protocol

### 7.1 Scratch registers (HCODEC_HENC_SCRATCH_0 to _N, words 0x1AC0–0x1AD7)

| Register | Byte offset | Vendor name | Full mode on GXBB |
|---|---|---|---|
| SCRATCH_0 | 0x6B00 | ENCODER_STATUS | command (CPU → microcode) and status (microcode → CPU), §7.2 |
| SCRATCH_1 | 0x6B04 | MEM_OFFSET_REG | assist buffer physical address |
| SCRATCH_2 | 0x6B08 | DEBUG_REG | only in a debug build of the driver |
| SCRATCH_3 / _4 | 0x6B0C / 0x6B10 | CURRENT_Y / C_CANVAS_START | SW-mix only |
| SCRATCH_5 | 0x6B14 | IDR_PIC_ID | idr_pic_id |
| SCRATCH_6 | 0x6B18 | FRAME_NUMBER | frame_num (forced 0 for an IDR) |
| SCRATCH_7 | 0x6B1C | PIC_ORDER_CNT_LSB | POC LSB (forced 0 for an IDR) |
| SCRATCH_8 | 0x6B20 | LOG2_MAX_PIC_ORDER_CNT_LSB | 4 |
| SCRATCH_9 | 0x6B24 | LOG2_MAX_FRAME_NUM | 4 |
| SCRATCH_A | 0x6B28 | ANC0_BUFFER_ID | 0 (never changed) |
| SCRATCH_B | 0x6B2C | QPPICTURE | 26 (the instance's initial picture QP, never changed) |
| SCRATCH_C | 0x6B30 | CANVAS_ROW_SIZE | SW-mix only |
| SCRATCH_D | 0x6B34 | IE_ME_MB_TYPE | macroblock-type control, §7.3 |
| SCRATCH_E | 0x6B38 | IE_ME_MODE | 0x0000000C (§7.3) |
| SCRATCH_F | 0x6B3C | IE_REF_SEL | 0 |
| SCRATCH_G | 0x6B40 | P_INTRA_CONFIG | not written on GXBB in full mode (older modes: 0x001EFFFF) |
| SCRATCH_H | 0x6B44 | — | not used |
| SCRATCH_I | 0x6B48 | INFO_DUMP_START_ADDR (GX) | dump buffer physical address. On older modes it is P_MB_QUANT_CONFIG |
| SCRATCH_J / _K | 0x6B4C / 0x6B50 | P_MB_QUANT_INC / DEC_CFG | not written on GXBB in full mode |
| SCRATCH_L | 0x6B54 | FIXED_SLICE_CFG | slice size, §7.6 |
| SCRATCH_M / _N | 0x6B58 / 0x6B5C | BITS_INFO / MV_INFO_DDR_START | pre-GX chips only |

DOS_SCRATCH9 and DOS_SCRATCH10 are given names in encoder.h (low-latency enable, previous frame number) but the driver never uses them.

### 7.2 Commands and status values (HENC_SCRATCH_0)

| Value | Name | Direction | Used in full mode on GXBB |
|---|---|---|---|
| 0 | ENCODER_IDLE | CPU → | written once before the AMRISC start |
| 1 | ENCODER_SEQUENCE | CPU → | emit an SPS |
| 2 | ENCODER_PICTURE | CPU → | emit a PPS |
| 3 | ENCODER_IDR | CPU → | encode an IDR picture |
| 4 | ENCODER_NON_IDR | CPU → | encode a P picture |
| 5 / 6 | ENCODER_MB_HEADER / MB_DATA | CPU → | no (older modes) |
| 7 | ENCODER_SEQUENCE_DONE | → CPU | yes |
| 8 | ENCODER_PICTURE_DONE | → CPU | yes |
| 9 | ENCODER_IDR_DONE | → CPU | yes |
| 10 | ENCODER_NON_IDR_DONE | → CPU | yes |
| 11 / 12 | MB_HEADER_DONE / MB_DATA_DONE | → CPU | no |
| 13 / 14 | NON_IDR_INTRA / NON_IDR_INTER | | no |
| 0xFF | ENCODER_ERROR | | the driver's own marker for a timeout; never read from the hardware |

- **The command is the write to SCRATCH_0.** There is no separate doorbell: after the setup, writing the command value is the last register write, and the microcode (already running) picks it up.
- That the microcode polls SCRATCH_0 is inferred (**UNVERIFIED**). The exception is right after a microcode reload, when the command is written before MPSR = 1 (§4.4).
- **Completion:** the microcode writes the matching done value into SCRATCH_0 and raises mailbox 2 (§3).

### 7.3 What the CPU sets before each command

The vendor uses a "need reset" flag:
- It is **set** at session start and after every PICTURE_DONE, IDR_DONE and NON_IDR_DONE.
- It is **not** set after SEQUENCE_DONE.

So:
- **Every IDR and P command** is preceded by the full init: reset pulse, canvases, encoder registers, QDCT pointers, bitstream pointers, protocol init and assist pointer.
- The **SEQUENCE** command gets the full init too when it starts a session or follows a picture.
- The **PICTURE** command, which always follows SEQUENCE, gets no init. Its bytes are appended after the SPS.

| Item | SEQUENCE | PICTURE | IDR | P |
|---|---|---|---|---|
| Full init (§11.3 steps 1–7) | yes (first command, or after a picture) | **no** | yes | yes |
| FRAME_NUMBER / PIC_ORDER_CNT_LSB | current counters | (kept) | **0 / 0** | counters |
| IDR_PIC_ID, log2 values, ANC0_BUFFER_ID, QPPICTURE | written | (kept) | written | written |
| Picture QP (quant tables, Q_QUANT_I/P, VLC_QUANT_CTL_0) | written, from the tables' **current** contents (§10) | (kept) | from the request | from the request |
| REC/DBKR/DBKW and ANC0 canvas words | no | no | yes | yes |
| MFDIN (input format, canvases) | no | no | yes | yes |
| IE_ME_MB_TYPE (SCRATCH_D) | 0 | 0 | **0x00000009** | **0xFFFF00FF** |
| IE_ME_MODE (SCRATCH_E) | 0x0C | 0x0C | 0x0C | 0x0C |
| IE_REF_SEL (SCRATCH_F) | 0 | 0 | 0 | 0 |
| FIXED_SLICE_CFG (SCRATCH_L) | written | written | written | written |
| Command value | 1 | 2 | 3 | 4 |
| Done value | 7 | 8 | 9 | 10 |

**IE_ME_MB_TYPE** uses the microcode's macroblock-type codes from encoder.h: 0 = P/B skip, 1 = P16×16, 2 = P16×8, 3 = P8×16, 4–7 = sub-8×8 sizes, 8 = P8×8, 9 = I4MB, 10 = I16MB, 11 = I-block, 12 = SI4MB, 13 = I8MB, 14 = I_PCM, 15 = automatic.
- **IDR:** 9 (I4MB). Whether that forces all macroblocks to Intra 4×4, or the IE still picks I16 (QDCT_ADV_CONFIG has I16 detection and I16 enabled, §9.2), is **UNVERIFIED**.
- **P:** bits 31:16 = 0xFFFF (skip run automatic), bits 7:4 and 3:0 = 15 (type automatic). The hardware decides each macroblock: skip, inter or intra.

**IE_ME_MODE** = 12 on GXBB in full mode.
- Bits 4:0 are the IE pipeline block count; the vendor uses 12 on GXBB and 3 elsewhere.
- Bit 5 disables I4×4 on GXBB, and bit 6 disables I16×16 (on M8 they were half-pixel and sub-pixel ME). Both are 0.
- So half- and sub-pixel motion search are not asked for on GXBB; the driver notes that they are currently disabled.

**Where the values come from:** the scratch values in §7.1 are written by the encoder init; the slice size by the IE/ME parameter step, twice in a row (the second write is redundant).

### 7.4 Results

| After | Read | Meaning |
|---|---|---|
| SEQUENCE_DONE | HCODEC_VLC_TOTAL_BYTES (byte 0x7468, word 0x1D1A) | **SPS size** in bytes. The SPS starts at the bitstream start (VLC_VB_START_PTR) |
| PICTURE_DONE | HCODEC_VLC_TOTAL_BYTES | SPS + PPS. **PPS size** = this − SPS size; the PPS follows the SPS directly |
| IDR_DONE / NON_IDR_DONE | HCODEC_VLC_TOTAL_BYTES | **picture size** in bytes. The picture starts at the bitstream start, because the full init resets the write pointer and the byte counter before every picture |

- The vendor reports SPS and PPS together as (SPS size << 16) | PPS size.
- Nothing else is read back: no per-slice sizes, no write pointer, no error code.
- In full mode on GXBB the per-macroblock statistics land in the dump buffer (§5.3). The vendor hands three weight constants to user space with each picture size (§9.2).
- **Consequence:** every picture overwrites the previous bitstream, the SPS/PPS included. The caller must copy the bytes out before the next command.

### 7.5 Frame numbering and the role swap

**Counters** (kept per instance, written into SCRATCH_5/6/7):
- **At instance creation:** idr_pic_id = 0, frame_number = 0, POC LSB = 0, log2_max_frame_num = 4, log2_max_poc_lsb = 4, initial QP = 26.
- **IDR:** the picture is encoded with frame_num 0, POC 0 and the current idr_pic_id. Afterwards:
  - idr_pic_id goes up by 1, wrapping from 65,535 to 0;
  - POC LSB is set to 2 and frame_number to 1, ready for the next picture.
- **P:** the picture is encoded with the current frame_number and POC LSB. Afterwards frame_number goes up by 1 (wrapping above 65,535 to 0) and the POC goes up by 2.
- The driver never reduces these modulo 2^log2_max (16). That the microcode keeps only the low 4 bits is **UNVERIFIED**. A driver can write them already reduced, which is valid either way.

**Role swap:**
- After each IDR or P picture, the reconstruction canvas word and the reference canvas word are exchanged. The picture just written becomes the next picture's reference.
- At session start the reconstruction is buffer A (0x00E6E5E4) and the reference is buffer B (0x00E9E8E7). The first IDR therefore reads B as its reference, uninitialised but unused.
- In the vendor this swap, like the counter update, happens on a separate user-space call after the result has been read. A driver can do both at completion.
- There is exactly one reference picture (ANC0 only).

### 7.6 Slices

**FIXED_SLICE_CFG (SCRATCH_L) in full mode on GXBB:**
- If the module parameter `fixed_slice_cfg` is non-zero, that value is written.
- Otherwise, if the caller's rows-per-slice differs from the picture height in macroblocks, the value is **(height in MBs) × rows-per-slice**.
- Otherwise it is **0**: one slice per picture.

The formula multiplies by the **height** in macroblocks, where a count of macroblocks per slice would need the width. Older modes instead write (rows << 16) | rows. Whether the GX microcode reads this register as a macroblock count, and whether the vendor's formula is a bug, is **UNVERIFIED**. Use **0** (one slice) until tested.

### 7.7 Errors and recovery

- The vendor waits for the done value with the caller's timeout in ms (5,000 ms for SEQUENCE+PICTURE; a timeout of 0 means one tick).
- If the wait ends without a done value, the vendor does a "light reset":
  1. free the IRQ, stop the AMRISC and power down;
  2. wait 30 ms;
  3. power up and run the whole session start of §11.1 again (microcode load included, in full mode).
- The picture is reported with size 0.
- The vendor does not force the next picture to be an IDR. After a reset the reference is lost, so a driver should (inference).

---

## 8. The bitstream

**What the source establishes:**
- **The microcode writes every byte.** The CPU writes no header field into memory and no emulation-prevention bytes; it inserts no start codes and edits no headers. It only reports byte counts.
- SPS, PPS and each picture are produced by separate commands with separate byte counts (§7.4).
- encoder.h defines the NAL header bytes the microcode uses:
  - SPS **0x67** and PPS **0x68** (nal_ref_idc 3, types 7 and 8);
  - IDR slice **0x65** (nal_ref_idc 3, type 5);
  - non-IDR slice **0x41** (nal_ref_idc 2, type 1).

  So P pictures are reference pictures.
- encoder.h also defines the slice_type values: I = 2 or 7, P = 5 or 0, B = 6 or 1. **B pictures are never produced:** there is no B command and only one reference canvas.
- **Values the CPU supplies for the syntax** (§7.1): log2_max_frame_num = 4 and log2_max_pic_order_cnt_lsb = 4, presumably sent as the SPS's minus-4 fields = 0 with POC type 0 (**UNVERIFIED**); idr_pic_id; frame_num; the POC LSB; and QPPICTURE = 26, presumably the PPS's pic_init_qp (**UNVERIFIED**).
- **Picture size:** the microcode gets the unrounded size from HCODEC_VLC_PIC_SIZE (§9.2). Whether the SPS carries frame cropping for 1080 lines is **UNVERIFIED**.
- **Slices:** one per picture by default (§7.6).

**What the source does not establish (all UNVERIFIED):**
- whether each NAL unit is preceded by an Annex B start code (00 00 00 01 or 00 00 01) in the buffer. The kernel's silence on start codes, and the fact that Amlogic's user space hands the bytes straight on, suggest it is;
- profile_idc and level_idc;
- the entropy coder. Only CAVLC-style VLC registers exist in hcodec_regs.h and nothing CABAC-like, so CAVLC presumably, which means Baseline or Main without CABAC;
- whether the deblocking filter is applied (the reconstruction canvases are named for deblocking read and write) and what the slice header says about it;
- VUI and timing information (none is configured);
- max_num_ref_frames (presumably 1).

The first board test settles all of these by parsing the output (§14).

---

## 9. Motion estimation and quality settings (GXBB, full mode)

- **Names:** the field names in this section are the vendor's own names for each field.
- **Widths:** the source gives each field's low bit. A range [h:l] is inferred from the next field's position, so its top bit is **UNVERIFIED**. "From bit *n*" means the top is unknown.
- **Use:** the full register values are what a driver needs; the split into fields is for orientation.

### 9.1 Values computed once at probe (`InitEncodeWeight`, GXBB branch)

These are module parameters in the vendor, written into the registers in the per-picture protocol init (§9.2).

| Register | Byte offset (word) | GXBB value | Fields (vendor names) |
|---|---|---|---|
| HCODEC_ME_MV_MERGE_CTL | 0x7D54 (0x1F55) | **0xFF4AB080** | bits 31–28: me_merge_mv_en_16, small_mv_en_16, flex_en_16, sad_en_16 = 1; bits 27–24: the same four for 8×8 = 1; [23:18] mv_diff_16 = 0x12; [17:12] mv_diff_8 = 0x2B; [11:0] min_sad = 0x80 |
| HCODEC_ME_STEP0_CLOSE_MV | 0x7D48 (0x1F52) | **0x00040042** | from bit 10: me_step0_big_sad = 0x100; [9:5] close_mv_y = 2; [4:0] close_mv_x = 2 |
| HCODEC_ME_F_SKIP_SAD | 0x7D4C (0x1F53) | **0x00203030** | force_skip_sad_3/2/1/0 = 0x00, 0x20, 0x30, 0x30 (one byte each, 3 highest) |
| HCODEC_ME_F_SKIP_WEIGHT | 0x7D50 (0x1F54) | **0x00040404** | force_skip_weight_3/2/1/0 = 0, 4, 4, 4 |
| HCODEC_ME_SAD_ENOUGH_01 | 0x7D40 (0x1F50) | **0x00004000** | [23:12] me_sad_enough_1 = 4; [11:0] me_sad_enough_0 = 0 |
| HCODEC_ME_SAD_ENOUGH_23 | 0x7D44 (0x1F51) | **0x00020011** | [23:12] adv_mv_8x8_enough = 0x20; [11:0] me_sad_enough_2 = 0x11 |
| HCODEC_ME_MV_WEIGHT_01 | 0x7D58 (0x1F56) | **0** | step and pre weights, all 0 on GXBB |
| HCODEC_ME_MV_WEIGHT_23 | 0x7D5C (0x1F57) | **0** | all 0 |
| HCODEC_ME_SAD_RANGE_INC | 0x7D60 (0x1F58) | **0** | ME_SAD_RANGE_0..3 = 0 |

- GXTVBB zeroes the two force-skip registers; GXBB keeps the values above.
- Pre-GX chips use different values: weight 0x40302030 and 0x00004030, range 0x01010101, close-MV 0x0000AC21, skip SAD 0x18181818, skip weight 0x12121212.

### 9.2 Per-picture protocol init (`avc_prot_init`), in the order written

The constants used: ME weight offset 0x3320, I4 weight offset 0x3655, I16 weight offset 0x3320 (MAX_WEIGHT is not defined); IE and ME SAD shifts 1; ADV_MV weights 16×16 = 0, 16/8 = 0x2000, 8×8 = 0x3000, 4×4×4 = 0x3000.

**GX full-mode block:**

| # | Register | Byte offset | Value | Fields |
|---|---|---|---|---|
| 1 | HCODEC_HDEC_MC_OMEM_AUTO | 0x64C0 | (1 << 31) \| ((MB width − 1) << 16); 1920 → **0x80770000** | bit 31 use_omem_mb_xy; from bit 16 omem_max_mb_x |
| 2 | HCODEC_VLC_ADV_CONFIG | 0x7494 | **0x0000033E** | below |
| 3 | HCODEC_QDCT_ADV_CONFIG | 0x7CD0 | **0x3F6C577F** | below |
| 4 | HCODEC_IE_WEIGHT | 0x7CD4 | **0x33203655** | [31:16] I16 offset 0x3320, [15:0] I4 offset 0x3655 |
| 5 | HCODEC_ME_WEIGHT | 0x7D80 | **0x00003320** | ME offset |
| 6 | HCODEC_SAD_CONTROL_0 | 0x7CE8 | **0x33203655** | [31:16] ie_sad_offset_I16, [15:0] ie_sad_offset_I4 |
| 7 | HCODEC_SAD_CONTROL_1 | 0x7CEC | **0x01113320** | [27:24] ie_sad_shift_I16 = 1, [23:20] ie_sad_shift_I4 = 1, [19:16] me_sad_shift_INTER = 1, [15:0] me_sad_offset_INTER = 0x3320 |
| 8 | HCODEC_ADV_MV_CTL0 | 0x7DA4 | **0xF0003000** | bit 31 adv_mv_large_16x8 = 1, bit 30 adv_mv_large_8x16 = 1, from bit 16 adv_mv_8x8_weight = 0x3000, [15:0] adv_mv_4x4x4_weight = 0x3000 |
| 9 | HCODEC_ADV_MV_CTL1 | 0x7DA8 | **0x0000A000** | [31:16] adv_mv_16x16_weight = 0, bit 15 adv_mv_large_16x16 = 1, [14:0] adv_mv_16_8_weight = 0x2000 |
| 10 | quant tables | 0x7CD8 / 0x7CE4 | §10 | three 8-word tables |
| 11 | HCODEC_QDCT_VLC_QUANT_CTL_0 | 0x7CF0 | (QP << 13) \| QP; QP 26 → **0x0003401A** | [24:19] vlc_delta_quant_1 = 0, [18:13] vlc_quant_1 = QP, [12:6] vlc_delta_quant_0 = 0, [5:0] vlc_quant_0 = QP |
| 12 | HCODEC_QDCT_VLC_QUANT_CTL_1 | 0x7CF4 | **0x00000699** | [11:6] vlc_max_delta_q_neg = 26, [5:0] vlc_max_delta_q_pos = 25 |

**HCODEC_VLC_ADV_CONFIG = 0x33E:**

| Bit | Name | Value |
|---|---|---|
| 10 | early_mix_mc_hcmd | 0 (noted "enabled in P pictures") |
| 9 | update_top_left_mix | 1 |
| 8 | p_top_left_mix | 1 |
| 7 | mv_cal_mixed_type | 0 (noted "P") |
| 6 | mc_hcmd_mixed_type | 0 (noted "P") |
| 5 | use_seperate_int_control | 1 |
| 4 | hcmd_intra_use_q_info | 1 |
| 3 | hcmd_left_use_prev_info | 1 |
| 2 | hcmd_use_q_info | 1 |
| 1 | use_q_delta_quant | 1 |
| 0 | detect_I16_from_I4 | 0 |

**HCODEC_QDCT_ADV_CONFIG = 0x3F6C577F:**

| Bit | Name | Value |
|---|---|---|
| 29 | mb_info_latch_no_I16_pred_mode | 1 |
| 28 | ie_dma_mbxy_use_i_pred | 1 |
| 27 | ie_dma_read_write_use_ip_idx | 1 |
| 26 | ie_start_use_top_dma_count | 1 |
| 25 | i_pred_top_dma_rd_mbbot | 1 |
| 24 | i_pred_top_dma_wr_disable | 1 |
| 23 | i_pred_mix | 0 (noted "P") |
| 22 | me_ab_rd_when_intra_in_p | 1 |
| 21 | force_mb_skip_run_when_intra | 1 |
| 20 | mc_out_mixed_type | 0 (noted "P") |
| 19 | ie_start_when_quant_not_full | 1 |
| 18 | mb_info_state_mix | 1 |
| 17 | mb_type_use_mix_result | 0 (noted "P") |
| 16 | me_cb_ie_read_enable | 0 (noted "P") |
| 15 | ie_cur_data_from_me | 0 (noted "P") |
| 14 | rem_per_use_table | 1 |
| 13 | q_latch_int_enable | 0 |
| 12 | q_use_table | 1 |
| 11 | q_start_wait | 0 |
| 10 | LUMA_16_LEFT_use_cur | 1 |
| 9 | DC_16_LEFT_SUM_use_cur | 1 |
| 8 | c_ref_ie_sel_cur | 1 |
| 7 | c_ipred_perfect_mode | 0 |
| 6 | ref_ie_ul_sel | 1 |
| 5 | mb_type_use_ie_result | 1 |
| 4 | detect_I16_from_I4 | 1 |
| 3 | ie_not_wait_ref_busy | 1 |
| 2 | ie_I16_enable | 1 |
| 1:0 | ie_done_sel | 3 (the vendor notes it as the fastest) |

The bits noted "enabled in P pictures" are never set by the kernel, for P pictures either. That the microcode sets them itself is **UNVERIFIED**.

**Common part, continuing the order:**

| # | Register | Byte offset | Value |
|---|---|---|---|
| 13 | HCODEC_VLC_PIC_SIZE | 0x74C4 | width \| (height << 16), unrounded; 1920 × 1080 → 0x04380780 |
| 14 | HCODEC_VLC_PIC_POSITION | 0x74CC | 0 (start MB number, MB y and MB x all 0) |
| 15 | HCODEC_QDCT_Q_QUANT_I | 0x7C70 | §10 |
| 16 | HCODEC_QDCT_Q_QUANT_P | 0x7C74 | §10 |
| 17 | HCODEC_IGNORE_CONFIG | 0x7C08 | **0x84228423**: bit 31 ignore_lac_coeff_en = 1, [30:26] ignore_lac_coeff_else = 1, [25:21] ignore_lac_coeff_2 = 1, [20:16] ignore_lac_coeff_1 = 2, bit 15 ignore_cac_coeff_en = 1, [14:10] ignore_cac_coeff_else = 1, [9:5] ignore_cac_coeff_2 = 1, [4:0] ignore_cac_coeff_1 = 3 (the source annotates this last field as "< 2" yet writes 3) |
| 18 | HCODEC_IGNORE_CONFIG_2 | 0x7C0C | **0x84250000** on GXBB: bit 31 ignore_t_lac_coeff_en = 1, [30:26] else = 1, [25:21] _2 = 1, [20:16] _1 = 5, the rest 0. GXTVBB writes a different value with chroma-DC fields |
| 19 | HCODEC_QDCT_MB_CONTROL | 0x7C54 | 0x00000201 (bit 9 mb_info_soft_reset, bit 0 read-buffer soft reset) |
| 20 | HCODEC_QDCT_MB_CONTROL | 0x7C54 | **0x301E7400**: bit 29 ie_start_int_enable, bit 28 ignore_t_p8x8, bit 20 ie_sub_enable, bit 19 i_pred_enable, bit 18 iq_enable, bit 17 idct_enable, bit 14 mb_pause_enable, bit 13 q_enable, bit 12 dct_enable, bit 10 mb_info_en, all 1. Bits 27–23 (the mc_out options) = 0, endian (from bit 3) = 0, bit 1 mb_read_en = 0, bit 0 = 0 |
| 21 | HCODEC_SAD_CONTROL | 0x7D0C | 0x00000005 (bit 2 IE result buffer soft reset, bit 0 SAD soft reset) |
| 22 | HCODEC_IE_RESULT_BUFFER | 0x7D10 | 0 |
| 23 | HCODEC_SAD_CONTROL | 0x7D0C | 0x0000000A (bit 3 IE result buffer enable, bit 1 SAD enable) |
| 24 | HCODEC_IE_CONTROL | 0x7D00 | 0x40000001 (bit 30 active_ul_block, bit 0 IE soft reset; bit 1 ie_enable = 0) |
| 25 | HCODEC_IE_CONTROL | 0x7D00 | 0x40000000 |
| 26 | HCODEC_ME_SKIP_LINE | 0x7D34 | **0x08202000**: [29:24] step_3_skip_line = 8, [23:18] step_2 = 8, [17:12] step_1 = 2, [11:6] step_0 = 0 |
| 27–35 | the nine ME registers of §9.1 | | in this order: MV_MERGE_CTL, STEP0_CLOSE_MV, SAD_ENOUGH_01, SAD_ENOUGH_23, F_SKIP_SAD, F_SKIP_WEIGHT, MV_WEIGHT_01, MV_WEIGHT_23, SAD_RANGE_INC |
| 36 | HCODEC_IE_DATA_FEED_BUFF_INFO | 0x6B60 | 0 |
| 37 | HCODEC_CURR_CANVAS_CTRL | 0x66CC | 0 |
| 38 | HCODEC_VLC_CONFIG | 0x7404 | set bit 0 (pop_coeff_even_all_zero). It was written 0x07 earlier (§11.3), so it stays 0x07 |
| 39 | HCODEC_HENC_SCRATCH_I | 0x6B48 | dump buffer address (§5.3) |
| 40 | HCODEC_ASSIST_MBOX2_CLR_REG | 0x41E4 | 1 |
| 41 | HCODEC_ASSIST_MBOX2_MASK | 0x41E8 | 1 |

**Other one-off settings written elsewhere in the per-picture init (§11.3):**

| Register | Byte offset (word) | Value | Note |
|---|---|---|---|
| HCODEC_VLC_TOTAL_BYTES | 0x7468 (0x1D1A) | 0 | byte counter reset |
| HCODEC_VLC_CONFIG | 0x7404 (0x1D01) | 0x07 | bit 0 as above; bits 2:1 **UNVERIFIED** |
| HCODEC_VLC_INT_CONTROL | 0x74C0 (0x1D30) | 0 | |
| HCODEC_ASSIST_AMR1_INT0 | 0x4094 (0x1025) | 0x15 | AMRISC interrupt routing; meaning **UNVERIFIED** |
| HCODEC_ASSIST_AMR1_INT1 | 0x4098 (0x1026) | 0x08 | as above |
| HCODEC_ASSIST_AMR1_INT3 | 0x40A0 (0x1028) | 0x14 | as above. INT2, INT4 and the DMA interrupt masks are **not** written in full mode on GXBB (older modes write them) |
| HCODEC_VLC_VB_MEM_CTL | 0x745C (0x1D17) | **0xBF200002** | bit 31 = 1, bits 29:24 = 0x3F, bits 23:16 = 0x20, bits 1:0 = 2; field meanings **UNVERIFIED** |
| HCODEC_VLC_VB_CONTROL | 0x7458 (0x1D16) | 1, then **0x0000003A** | bit 0 pulse (presumably latches the pointers); then bit 1 = 1, bits 5:3 = 7 (presumably the 64-bit byte swap), bit 14 = 0; **UNVERIFIED** |

### 9.3 GXTVBB-only settings (not for GXBB)

GXTVBB and later also program the "V3" force-skip and intra-prediction weighting registers:
- HCODEC_V3_SKIP_CONTROL, _SKIP_WEIGHT, _L1_SKIP_MAX_SAD, _L2_SKIP_WEIGHT, _F_ZERO_CTL_0/1, _IPRED_TYPE_WEIGHT_0/1 and _LEFT_SMALL_MAX_SAD;
- a 64-entry HCODEC_V3_MV_SAD_TABLE (words 0x1F6C–0x1F7A).

**None of these is written on GXBB.** Whether GXBB even has them is **UNVERIFIED**.

---

## 10. Rate control and QP

**There is no rate control in the kernel for full mode on GXBB.**
- The older in-microcode macroblock rate control is not configured on GXBB: its registers are P_MB_QUANT_CONFIG (target bits per MB, min and max QP), P_MB_QUANT_INC/DEC_CFG and P_INTRA_CONFIG, and only the non-GX paths write them.
- Each IDR or P request carries a `quant` value. The vendor then does one of two things.

**Fixed QP (any quant other than 64):**
- All three quant tables (I4, I16 and ME, 32 bytes each) are filled with that QP, and table set 0 is used.
- So the whole picture uses one QP, and the caller may choose a different QP for every picture.
- **Simple rate control is done by the caller choosing quant per picture.**

**Adjusted tables (quant = 64, "ADJUSTED_QP_FLAG"):**
- The request carries 3 × 8 words of QP bytes (I4, I16, ME), computed by Amlogic's user-space library from the dump statistics (§5.3). These go into table set 1.
- When copying, the vendor reverses the four bytes of each 32-bit group, so the caller lists the bytes most-significant first.
- What the 32 entries of a table are indexed by is **UNVERIFIED**; presumably a per-macroblock activity or SAD class.

**Programming the tables (in §9.2 step 10):**

| Write | Register | Byte offset | Value |
|---|---|---|---|
| select table I4 | HCODEC_Q_QUANT_CONTROL | 0x7CD8 (0x1F36) | 0x00400000: table address 0 (field from bit 23) \| bit 22 address update |
| 8 words | HCODEC_QUANT_TABLE_DATA | 0x7CE4 (0x1F39) | I4 table, word 0 first |
| select table I16 | HCODEC_Q_QUANT_CONTROL | | 0x04400000 (address 8 \| bit 22) |
| 8 words | HCODEC_QUANT_TABLE_DATA | | I16 table |
| select table ME | HCODEC_Q_QUANT_CONTROL | | 0x08400000 (address 16 \| bit 22) |
| 8 words | HCODEC_QUANT_TABLE_DATA | | ME table |

- Each word holds four QP values, one per byte. That the address auto-increments on each data write is inferred from the single address write per table (**UNVERIFIED**).
- In fixed mode with QP *q*, every word is *q* × 0x01010101 (QP 26 → 0x1A1A1A1A).
- **Built-in defaults, before the first request:**
  - set 0 is 21, 22, …, 28, each repeated four times (words 0x15151515 … 0x1C1C1C1C);
  - set 1 rises from 30 to 38 (words 0x1F1F1E1E … 0x26262525).

**The picture QP** written to the QP registers:
- **IDR:** QP = the low byte of I4 word 0.
- **P:** QP = (low byte of I4 word 0 + low byte of ME word 0) / 2, rounded down. The same value is used for both the I and the P registers.
- In fixed mode both cases give *q*.

**HCODEC_QDCT_Q_QUANT_I** (byte 0x7C70) and **HCODEC_QDCT_Q_QUANT_P** (byte 0x7C74) take the luma QP and the chroma QP (QPc):

| Bits | Content |
|---|---|
| 27:22 | QPc |
| 21:16 | QP |
| 15:12 | QPc mod 6 |
| 11:8 | QPc / 6 |
| 7:4 | QP mod 6 |
| 3:0 | QP / 6 |

- **QPc** is H.264's chroma QP mapping with chroma_qp_index_offset 0: QPc = QP below 30; above that, 30→29, 31→30, 32→31, 33→32, 34→32, 35→33, 36→34, 37→34, 38→35, 39→35, 40→36, 41→36, 42→37, 43→37, 44→37, 45→38, 46→38, 47→38, and 48 and up → 39.
- Examples: QP 20 → 0x05142323; QP 24 → 0x06180404; QP 26 → **0x069A2424**; QP 28 → 0x071C4444; QP 30 → 0x075E5405; QP 32 → 0x07E01525; QP 36 → 0x08A44506; QP 40 → 0x09280646.
- HCODEC_QDCT_VLC_QUANT_CTL_0 carries the same QP (§9.2 step 11).

**Default QPs:**
- The instance's initial QP is **26**: it goes into QPPICTURE (SCRATCH_B) and is passed to the protocol init at session start.
  - On GXBB the protocol init does not use that argument for the QP registers: it always takes the QP from the tables (above).
- The session-start init and the SEQUENCE/PICTURE commands therefore use whatever the tables hold at that moment. Before the first picture request that is the built-in set 0, giving QP 21 (§11.1, §11.2).
- The QP Amlogic's user space normally asks for is not in the kernel.

---

## 11. End-to-end sequences (vendor order)

### 11.1 Session start (`amvenc_avc_start`, first instance)

| # | Step |
|---|---|
| 1 | Power-up, §2.4 (clock level 5 = 500 MHz) |
| 2 | Program canvases 0xE4–0xE9 (§5.4) |
| 3 | HCODEC_ASSIST_MMC_CTRL1 ← 0x32 |
| 4 | Load the microcode `h264_enc_mc_gx`, §4.3 |
| 5 | Set "need reset"; ENCODER status idle (software) |
| 6 | Reset pulse: DOS_SW_RESET1 ← 0x000341C4, then 0 (with the dummy reads), §2.6 |
| 7 | Encoder init as for an IDR (§11.3 step 3): FRAME_NUMBER = 0, POC = 0 |
| 8 | QDCT buffer pointers (§5.3) |
| 9 | Bitstream pointers (§11.3 step 5) |
| 10 | Protocol init (§9.2) with the IDR rules. The vendor passes QP 26, but on GXBB the QP registers take the low byte of I4 table word 0 instead; before any request that is the built-in default 21 (§10). This step includes the mailbox clear and unmask |
| 11 | Request the IRQ (SPI 45, shared) |
| 12 | Reconstruction canvas word → REC/DBKR/DBKW; reference word → ANC0, VLC_HCMD_CONFIG ← 0 |
| 13 | SCRATCH_1 ← assist buffer address |
| 14 | SCRATCH_E ← 0x0C, SCRATCH_F ← 0, SCRATCH_D ← 0, SCRATCH_L ← slice size |
| 15 | SCRATCH_0 ← 0 (idle) |
| 16 | SCRATCH_L ← slice size again |
| 17 | AMRISC start: DOS_SW_RESET1 ← 0x1800, then 0 (with the dummy reads); **HCODEC_MPSR ← 1** |

"Need reset" stays set, so the first command repeats the init.

### 11.2 SPS and PPS (one request)

1. **SEQUENCE command,** with the full init (§11.3 steps 1–7, where step 3 uses the current counters, not forced zeros):
   - no reconstruction/reference canvas words and no MFDIN;
   - SCRATCH_D ← 0, SCRATCH_E ← 0x0C, SCRATCH_F ← 0, SCRATCH_L ← slice size (twice);
   - **SCRATCH_0 ← 1**.
2. Wait for SCRATCH_0 = 7, up to 5,000 ms. SPS size = HCODEC_VLC_TOTAL_BYTES.
3. **PICTURE command,** with no init: SCRATCH_D ← 0, E ← 0x0C, F ← 0, L ← slice size (twice); **SCRATCH_0 ← 2**.
4. Wait for 8. PPS size = HCODEC_VLC_TOTAL_BYTES − SPS size.
5. Invalidate the bitstream range [start, start + SPS + PPS) in the cache. The SPS bytes, then the PPS bytes, are at the bitstream start.
6. Copy them out before the next command overwrites them.

The quant tables are not refilled for a SEQUENCE request. The QP registers during the SPS/PPS commands therefore come from whatever the tables hold (§10).

### 11.3 One IDR picture

The request gives: type, format, source, frame size, QP, flush flags, timeout (and the NR mode, overridden by the module parameter).

| # | Step | Registers |
|---|---|---|
| 0 | When the request arrives: fill the three quant tables in memory with QP (fixed mode) | — |
| 1 | Reset pulse | DOS_SW_RESET1: 3 reads; ← 0x000341C4; ← 0; 3 reads |
| 2 | Canvases | 0xE4–0xE9 (§5.4) |
| 3 | Encoder init | HCODEC_VLC_TOTAL_BYTES ← 0; HCODEC_VLC_CONFIG ← 0x07; HCODEC_VLC_INT_CONTROL ← 0; HCODEC_ASSIST_AMR1_INT0 ← 0x15, INT1 ← 0x08, INT3 ← 0x14; SCRATCH_5 ← idr_pic_id; SCRATCH_6 ← **0**; SCRATCH_7 ← **0**; SCRATCH_8 ← 4; SCRATCH_9 ← 4; SCRATCH_A ← 0; SCRATCH_B ← 26 |
| 4 | QDCT pointers | QDCT_MB_START_PTR ← dct start; QDCT_MB_END_PTR ← dct start + 0x6BA000 − 1; QDCT_MB_WR_PTR ← dct start; QDCT_MB_RD_PTR ← dct start; QDCT_MB_BUFF ← 0 |
| 5 | Bitstream | HCODEC_VLC_VB_MEM_CTL ← 0xBF200002; VB_START_PTR ← start; VB_WR_PTR ← start; VB_SW_RD_PTR ← start; VB_END_PTR ← start + 0xFFFFF; VB_CONTROL ← 1; VB_CONTROL ← 0x3A |
| 6 | Protocol init | §9.2 steps 1–41, IDR QP rule; ends with the mailbox clear and unmask |
| 7 | Assist | SCRATCH_1 ← assist address |
| 8 | Reconstruction and reference | REC_CANVAS_ADDR, DBKR_CANVAS_ADDR, DBKW_CANVAS_ADDR ← reconstruction word; ANC0_CANVAS_ADDR ← reference word; VLC_HCMD_CONFIG ← 0 |
| 9 | Input | local type, if the request asks for it: clean the input frame in the cache (dct start, frame size). Then the input canvases 0xEA–0xEC; NR registers; MFDIN REG1, REG8, REG3, REG4, REG5, REG9 (§6.4) |
| 10 | IE/ME parameters | SCRATCH_E ← 0x0C; SCRATCH_F ← 0; **SCRATCH_D ← 0x00000009**; SCRATCH_L ← slice size |
| 11 | Slice size again | SCRATCH_L ← slice size |
| 12 | **Command** | **SCRATCH_0 ← 3** |
| 13 | Wait | mailbox 2 IRQ: CLR ← 1, read SCRATCH_0; done at **9** (timeout from the request) |
| 14 | Result | size = HCODEC_VLC_TOTAL_BYTES; invalidate [bitstream start, + size) in the cache; optionally the dump region and the reference buffer (§5.6) |
| 15 | Afterwards | idr_pic_id += 1 (65,535 → 0); POC LSB = 2; frame_number = 1; swap the reconstruction and reference words |

### 11.4 One P picture

The steps are the same as §11.3, with these differences:

| Step | Difference |
|---|---|
| 3 | SCRATCH_6 ← frame_number; SCRATCH_7 ← POC LSB |
| 6 | P QP rule (§10): QP = (I4[0] + ME[0]) / 2, the same value in Q_QUANT_I and Q_QUANT_P |
| 8 | the canvas words are the swapped ones: the previous picture's reconstruction is now ANC0 |
| 10 | **SCRATCH_D ← 0xFFFF00FF** |
| 12 | **SCRATCH_0 ← 4** |
| 13 | done at **10** |
| 15 | frame_number += 1 (wrapping above 65,535 to 0); POC LSB += 2; swap the canvas words |

### 11.5 Session stop (last instance closed)

1. Free the IRQ.
2. AMRISC stop (§4.4).
3. Power-down (§2.5).

The mailbox mask is left at 1 and the canvases are not released.

### 11.6 What this implies for a minimal driver (inference, not vendor fact)

- **Everything is re-programmed before every picture,** and the microcode keeps no per-stream state outside the buffers and the scratch registers. So the vendor can interleave up to eight instances without reloading the microcode in full mode. A single-stream driver can follow §11.3 literally for every picture.
- **The SW-mix pieces can be omitted:** the QDCT input streaming, P_MB_QUANT_* and the qp_info buffer. The QDCT pointers are cheap to keep and the vendor always sets them; keep them pointing at a valid region.
- **Keep the assist and dump buffers** allocated and pointed at, since the microcode presumably writes to both.
- **Write the counters already reduced** modulo 16 (log2_max = 4), and use FIXED_SLICE_CFG = 0.
- **Use endian 0 canvases,** and give HCODEC its own canvas indexes (0xE4–0xEC as the vendor does, or any free range).

---

## 12. Timing, limits and quirks

### 12.1 Limits

- **Picture size:** at most 1920 × 1088; the vendor rejects anything larger at configuration. No minimum is enforced. Width and height need not be multiples of 16; MFDIN and the canvases round up (§6.8), and VLC_PIC_SIZE gets the exact size.
- **Bitstream buffer:** 1 MiB in every buffer level; the bitstream of one command must fit.
  - What happens when a picture exceeds it is **UNVERIFIED**. encoder.h names a "VB full" interrupt request bit for the microcode, but the kernel handles no such case.
  - For scale: 1080p30 at 10 Mbit/s averages about 42 KB per picture. IDR pictures at low QP are the risk.
- **Frame rate:** the vendor kernel makes **no** throughput claim. The clock is 500 MHz on GXBB. 1080p30 needs one picture every 33 ms including the CPU-side setup; whether the hardware manages that is **UNVERIFIED** (§14).
- **Instances:** up to 8 queued requests and up to 8 instances; on the C2 at most 2 (40 MiB of CMA ÷ 18 MiB). Only one picture is on the hardware at a time; a real-time kernel thread serialises them.
- **Sharing:** HCODEC is shared with the JPEG encoder (jpegenc): opening the H.264 encoder fails while JPEG encoding runs. Both use SPI 45.

### 12.2 Waits and timeouts used by the vendor

| Where | Wait |
|---|---|
| Power-up | 10 µs after AO_RTI_PWR_CNTL_REG0, 10 µs after SLEEP0, 10 µs after ISO0, 10 ms at the end |
| Microcode load | poll IMEM DMA busy, at most 1 s, yielding |
| AMRISC stop | poll IMEM DMA busy, at most 1 s |
| Reload | 10 µs after the DOS_SW_RESET1 pulse |
| Reset pulses | none; 3 dummy reads before and after |
| SPS+PPS | 5,000 ms per command |
| Picture | the caller's timeout in ms (0 → one tick) |
| Light reset after a failure | 30 ms between power-down and power-up |
| M8 DOS-top fix (§12.3) | 1 ms of microcode run time |

### 12.3 Quirks and workarounds in the source

1. **MFDIN register shift:** on GXBB every MFDIN register sits 8 words below its header name (§6.1), and the picture width field moves from bit 12 to bit 14.
2. **Fixed-slice formula** uses the MB height (§7.6); probably wrong, untested.
3. **RGBA line length** is computed as width × 3 (§6.9); RGBA at 1080p does not fit the vendor's local input region (§6.4).
4. **Two HCODEC clock paths:** the encoder's own (500 MHz on GXBB) and arch/clkgx.c's (200 MHz, §2.7). The encoder's wins because power-up uses it.
   - A separate condition in arch/clk.c meant to route encoder formats to the HCODEC clock can never be true (it requires the format to equal two different values), so that path is dead.
5. **Several "enabled in P pictures" bits** in VLC_ADV_CONFIG and QDCT_ADV_CONFIG are never set by the kernel (§9.2).
6. **Mailbox mask** is never cleared at stop; the shared IRQ handler always claims the interrupt.
7. **The "reference" cache flush** after a picture covers the buffer that was the reference, not the new reconstruction (§5.6).
8. **ME_MV_WEIGHT_01** on GXBB puts the step-1 weight in two byte positions; it is all zeros anyway.
9. **Instance memory:** the reserved-memory path records each instance's size as the reserve's base address (a vendor bug; unused on the C2, which uses CMA). The open path checks the region against the smallest level's minimum but then always uses the 1080p layout.
10. **DOS top-register fix (`amvenc_dos_top_reg_fix`)** is not used on GXBB. It is a workaround for M8's 4K dual-decoder bus bug:
    - if HCODEC is running, it writes HCODEC_CCPU_INTR_MSK ← 1;
    - otherwise it powers HCODEC through `vdec_poweron`, loads a 4-word stub microcode (count 0x100), runs it for 1 ms and stops it.

    encoder.c never calls it itself.

---

## 13. Register table

### 13.1 HCODEC registers (DOS base 0xC8820000)

| Name | Word | Byte offset | Used in | Purpose |
|---|---|---|---|---|
| HCODEC_ASSIST_MMC_CTRL1 | 0x1002 | 0x4008 | §4.3 | 0x32 before each microcode load |
| HCODEC_MFDIN_REG1_CTRL (GXBB) | 0x1009 | 0x4024 | §6 | format, downsample, RGB-to-YUV, NR enable |
| HCODEC_MFDIN_REG3_CANV (GXBB) | 0x100B | 0x402C | §6 | input canvases, bytes-per-pixel codes |
| HCODEC_MFDIN_REG4_LNR0 (GXBB) | 0x100C | 0x4030 | §6 | linear input: code and bytes per line |
| HCODEC_MFDIN_REG5_LNR1 (GXBB) | 0x100D | 0x4034 | §6 | linear input: address |
| HCODEC_MFDIN_REG8_DMBL (GXBB) | 0x1010 | 0x4040 | §6 | picture size (width << 14 \| height) |
| HCODEC_MFDIN_REG9_ENDN (GXBB) | 0x1011 | 0x4044 | §6.7 | byte lanes: 0x00053977 |
| HCODEC_MFDIN_REG0D … REG11 (GXBB) | 0x1015 … 0x1019 | 0x4054 … 0x4064 | §6.6 | luma NR |
| HCODEC_MFDIN_REG12 … REG16 (GXBB) | 0x101A … 0x101E | 0x4068 … 0x4078 | §6.6 | chroma NR |
| (M8 names: MFDIN_REG0_CRST … REGB_AMPC) | 0x1010 … 0x101B | 0x4040 … 0x406C | §6.1 | header numbering; **not** the GXBB addresses |
| HCODEC_ASSIST_AMR1_INT0 | 0x1025 | 0x4094 | §9.2 | 0x15 |
| HCODEC_ASSIST_AMR1_INT1 | 0x1026 | 0x4098 | §9.2 | 0x08 |
| HCODEC_ASSIST_AMR1_INT2 | 0x1027 | 0x409C | — | older modes only |
| HCODEC_ASSIST_AMR1_INT3 | 0x1028 | 0x40A0 | §9.2 | 0x14 |
| HCODEC_ASSIST_AMR1_INT4 | 0x1029 | 0x40A4 | — | older modes only |
| HCODEC_ASSIST_DMA_INT_MSK / _MSK2 | 0x1065 / 0x1067 | 0x4194 / 0x419C | — | older modes only |
| HCODEC_ASSIST_MBOX2_IRQ_REG | 0x1078 | 0x41E0 | §3 | not touched |
| HCODEC_ASSIST_MBOX2_CLR_REG | 0x1079 | 0x41E4 | §3 | write 1 = acknowledge |
| HCODEC_ASSIST_MBOX2_MASK | 0x107A | 0x41E8 | §3 | 1 = interrupt enabled |
| HCODEC_MPSR | 0x1301 | 0x4C04 | §4 | AMRISC run (1) / stop (0) |
| HCODEC_CPSR | 0x1321 | 0x4C84 | §4 | co-processor; only ever 0 |
| HCODEC_CCPU_INTR_MSK | 0x1324 | 0x4C90 | §12.3 | M8 fix only |
| HCODEC_IMEM_DMA_CTRL | 0x1340 | 0x4D00 | §4.3 | 0x00078000; bit 15 busy |
| HCODEC_IMEM_DMA_ADR | 0x1341 | 0x4D04 | §4.3 | staging address |
| HCODEC_IMEM_DMA_COUNT | 0x1342 | 0x4D08 | §4.3 | 0x1000 words |
| HCODEC_HDEC_MC_OMEM_AUTO | 0x1930 | 0x64C0 | §9.2 | last MB column |
| HCODEC_ANC0_CANVAS_ADDR | 0x1990 | 0x6640 | §5.4 | reference canvases |
| HCODEC_DBKR_CANVAS_ADDR | 0x19B0 | 0x66C0 | §5.4 | reconstruction canvases |
| HCODEC_DBKW_CANVAS_ADDR | 0x19B1 | 0x66C4 | §5.4 | reconstruction canvases |
| HCODEC_REC_CANVAS_ADDR | 0x19B2 | 0x66C8 | §5.4 | reconstruction canvases |
| HCODEC_CURR_CANVAS_CTRL | 0x19B3 | 0x66CC | §9.2 | 0 |
| HCODEC_HENC_SCRATCH_0 … _N | 0x1AC0 … 0x1AD7 | 0x6B00 … 0x6B5C | §7.1 | command/status and parameters |
| HCODEC_IE_DATA_FEED_BUFF_INFO | 0x1AD8 | 0x6B60 | §9.2 | 0 |
| HCODEC_VLC_CONFIG | 0x1D01 | 0x7404 | §9.2 | 0x07 |
| HCODEC_VLC_VB_START_PTR | 0x1D10 | 0x7440 | §5.3 | bitstream start |
| HCODEC_VLC_VB_END_PTR | 0x1D11 | 0x7444 | §5.3 | bitstream end (inclusive) |
| HCODEC_VLC_VB_WR_PTR | 0x1D12 | 0x7448 | §5.3 | = start at init |
| HCODEC_VLC_VB_RD_PTR | 0x1D13 | 0x744C | — | not written |
| HCODEC_VLC_VB_SW_RD_PTR | 0x1D14 | 0x7450 | §5.3 | = start at init |
| HCODEC_VLC_VB_CONTROL | 0x1D16 | 0x7458 | §9.2 | 1, then 0x3A |
| HCODEC_VLC_VB_MEM_CTL | 0x1D17 | 0x745C | §9.2 | 0xBF200002 |
| HCODEC_VLC_TOTAL_BYTES | 0x1D1A | 0x7468 | §7.4 | 0 at init; read = bytes produced |
| HCODEC_VLC_ADV_CONFIG | 0x1D25 | 0x7494 | §9.2 | 0x33E |
| HCODEC_VLC_INT_CONTROL | 0x1D30 | 0x74C0 | §9.2 | 0 |
| HCODEC_VLC_PIC_SIZE | 0x1D31 | 0x74C4 | §9.2 | width \| height << 16 |
| HCODEC_VLC_PIC_POSITION | 0x1D33 | 0x74CC | §9.2 | 0 |
| HCODEC_VLC_HCMD_CONFIG | 0x1D54 | 0x7550 | §5.4 | 0 |
| HCODEC_IGNORE_CONFIG | 0x1F02 | 0x7C08 | §9.2 | 0x84228423 |
| HCODEC_IGNORE_CONFIG_2 | 0x1F03 | 0x7C0C | §9.2 | 0x84250000 |
| HCODEC_QDCT_MB_START_PTR | 0x1F10 | 0x7C40 | §5.3 | dct start |
| HCODEC_QDCT_MB_END_PTR | 0x1F11 | 0x7C44 | §5.3 | dct start + size − 1 |
| HCODEC_QDCT_MB_WR_PTR | 0x1F12 | 0x7C48 | §5.3 | dct start |
| HCODEC_QDCT_MB_RD_PTR | 0x1F13 | 0x7C4C | §5.3 | dct start |
| HCODEC_QDCT_MB_CONTROL | 0x1F15 | 0x7C54 | §9.2 | 0x201, then 0x301E7400 |
| HCODEC_QDCT_MB_BUFF | 0x1F17 | 0x7C5C | §5.3 | 0 |
| HCODEC_QDCT_Q_QUANT_I | 0x1F1C | 0x7C70 | §10 | intra QP/QPc |
| HCODEC_QDCT_Q_QUANT_P | 0x1F1D | 0x7C74 | §10 | inter QP/QPc |
| HCODEC_QDCT_ADV_CONFIG | 0x1F34 | 0x7CD0 | §9.2 | 0x3F6C577F |
| HCODEC_IE_WEIGHT | 0x1F35 | 0x7CD4 | §9.2 | 0x33203655 |
| HCODEC_Q_QUANT_CONTROL | 0x1F36 | 0x7CD8 | §10 | quant-table address |
| HCODEC_QUANT_TABLE_DATA | 0x1F39 | 0x7CE4 | §10 | quant-table data |
| HCODEC_SAD_CONTROL_0 | 0x1F3A | 0x7CE8 | §9.2 | 0x33203655 |
| HCODEC_SAD_CONTROL_1 | 0x1F3B | 0x7CEC | §9.2 | 0x01113320 |
| HCODEC_QDCT_VLC_QUANT_CTL_0 | 0x1F3C | 0x7CF0 | §9.2 | QP for the VLC |
| HCODEC_QDCT_VLC_QUANT_CTL_1 | 0x1F3D | 0x7CF4 | §9.2 | 0x699 |
| HCODEC_IE_CONTROL | 0x1F40 | 0x7D00 | §9.2 | 0x40000001, then 0x40000000 |
| HCODEC_SAD_CONTROL | 0x1F43 | 0x7D0C | §9.2 | 0x5, then 0xA |
| HCODEC_IE_RESULT_BUFFER | 0x1F44 | 0x7D10 | §9.2 | 0 |
| HCODEC_ME_SKIP_LINE | 0x1F4D | 0x7D34 | §9.2 | 0x08202000 |
| HCODEC_ME_SAD_ENOUGH_01 | 0x1F50 | 0x7D40 | §9.1 | 0x00004000 |
| HCODEC_ME_SAD_ENOUGH_23 | 0x1F51 | 0x7D44 | §9.1 | 0x00020011 |
| HCODEC_ME_STEP0_CLOSE_MV | 0x1F52 | 0x7D48 | §9.1 | 0x00040042 |
| HCODEC_ME_F_SKIP_SAD | 0x1F53 | 0x7D4C | §9.1 | 0x00203030 |
| HCODEC_ME_F_SKIP_WEIGHT | 0x1F54 | 0x7D50 | §9.1 | 0x00040404 |
| HCODEC_ME_MV_MERGE_CTL | 0x1F55 | 0x7D54 | §9.1 | 0xFF4AB080 |
| HCODEC_ME_MV_WEIGHT_01 | 0x1F56 | 0x7D58 | §9.1 | 0 |
| HCODEC_ME_MV_WEIGHT_23 | 0x1F57 | 0x7D5C | §9.1 | 0 |
| HCODEC_ME_SAD_RANGE_INC | 0x1F58 | 0x7D60 | §9.1 | 0 |
| HCODEC_ME_WEIGHT | 0x1F60 | 0x7D80 | §9.2 | 0x3320 |
| HCODEC_ADV_MV_CTL0 | 0x1F69 | 0x7DA4 | §9.2 | 0xF0003000 |
| HCODEC_ADV_MV_CTL1 | 0x1F6A | 0x7DA8 | §9.2 | 0x0000A000 |
| HCODEC_V3_* (SKIP_CONTROL … LEFT_SMALL_MAX_SAD) | 0x1F6C … 0x1F7A | 0x7DB0 … 0x7DE8 | §9.3 | GXTVBB only, not written on GXBB |

### 13.2 DOS top-level registers

| Name | Word | Byte offset | Physical | Purpose |
|---|---|---|---|---|
| DOS_GCLK_EN0 | 0x3F01 | 0xFC04 | 0xC882FC04 | bits 26:12 = HCODEC clock gates (0x7FFF on, 0 off) |
| DOS_GEN_CTRL0 | 0x3F02 | 0xFC08 | 0xC882FC08 | bit 0 pulsed at power-up |
| DOS_HCODEC_INT_EN | 0x3F06 | 0xFC18 | 0xC882FC18 | defined, never written |
| DOS_SW_RESET1 | 0x3F07 | 0xFC1C | 0xC882FC1C | HCODEC resets (§2.6) |
| DOS_SCRATCH9 / 10 | 0x3F19 / 0x3F1A | 0xFC64 / 0xFC68 | | named in encoder.h, never used |
| DOS_MEM_PD_HCODEC | 0x3F32 | 0xFCC8 | 0xC882FCC8 | HCODEC memory power-down (0 = on) |

### 13.3 Registers outside DOS

| Name | Physical | Purpose |
|---|---|---|
| AO_RTI_PWR_CNTL_REG0 | 0xC8100010 (AO + 0x10) | bits 4:3 cleared at power-up; meaning **UNVERIFIED** |
| AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 (AO + 0xE8) | bits 1:0 = HCODEC power off (1) / on (0) |
| AO_RTI_GEN_PWR_ISO0 | 0xC81000EC (AO + 0xEC) | bits 5:4 = HCODEC isolation on (1) / off (0) |
| HHI_GCLK_MPEG0 | 0xC883C140 | bit 1 = DOS clock (vendor HHI number 0x1050) |
| HHI_VDEC_CLK_CNTL | 0xC883C1E0 | bits 31:16 = HCODEC clock: [27:25] source, [24] gate, [22:16] divider − 1 (vendor HHI number 0x1078) |
| DMC_CAV_LUT_DATAL / DATAH / ADDR | 0xC8838048 / 0xC883804C / 0xC8838050 | canvas LUT (base §6) |
| GIC | SPI 45, INTID 77 | HCODEC mailbox 2, edge rising |

---

## 14. Provenance and open questions

### 14.1 Sources

All from https://github.com/hardkernel/linux, branch `odroidc2-3.14.y`, whose head on 2026-11-01 was **c3e4c730feb1750940971cae9ca1da3ca50f1d56** (committed 2018-03-03). That is the same commit as `m26-vdec-hevc-vendor-facts.md` and `m43-gxbb-mali-integration-facts.md`. Raw files came from https://raw.githubusercontent.com/hardkernel/linux/c3e4c730feb1750940971cae9ca1da3ca50f1d56/ + path, and listings from https://api.github.com/repos/hardkernel/linux/.

Everything was kept in the session's scratch directory and never run. The microcode was neither extracted nor written to disk; its fingerprints (§4.2) were computed in memory.

**Already in the scratch directory at the start of this study** (fetched for `m48-venc-feasibility.md`, branch head):

| Path in the repository | Bytes | Used for |
|---|---|---|
| drivers/amlogic/amports/encoder.c | 123,261 | the whole document |
| drivers/amlogic/amports/encoder.h | 26,740 | clock levels, mailbox, scratch map, status values, NAL constants |
| drivers/amlogic/amports/arch/firmware.c | 5,315 | microcode lookup and padding (§4.2) |
| drivers/amlogic/amports/arch/ucode/h264_enc/h264_enc_mc.c | 3,510 | microcode registration (§4.1) |
| drivers/amlogic/amports/arch/ucode/h264_enc/h264_enc_gx.h | 25,931 | microcode array format, word count, fingerprints (§4.2) |
| listing of drivers/amlogic/amports (API JSON) | 69,071 | file list |
| listing of drivers/amlogic/amports/arch (API JSON) | 19,684 | file list |
| listing of drivers/amlogic/amports/arch/ucode (API JSON) | 14,502 | file list |
| listing of drivers/amlogic/amports/arch/ucode/h264_enc (API JSON) | 17,086 | file list |
| a code-search API reply (JSON) | 120 | failed: the search API needs authentication; nothing used |

**Downloaded for this document:**

| Path in the repository | Bytes | Used for |
|---|---|---|
| branch info for odroidc2-3.14.y (API JSON) | 5,604 | head commit and date |
| listing of drivers/amlogic/amports/arch/regs (API JSON) | 25,486 | file list |
| listing of drivers/amlogic/canvas (API JSON) | 6,854 | file list |
| drivers/amlogic/amports/arch/regs/hcodec_regs.h | 25,399 | every HCODEC word index |
| drivers/amlogic/amports/arch/regs/dos_regs.h | 9,484 | DOS_SW_RESET1, DOS_MEM_PD_HCODEC, DOS_GCLK_EN0, DOS_GEN_CTRL0 |
| drivers/amlogic/amports/arch/regs/ao_regs.h | 853 | AO_RTI_PWR_CNTL_REG0, SLEEP0, ISO0 |
| drivers/amlogic/amports/arch/regs/hhi_regs.h | 1,664 | HHI_VDEC_CLK_CNTL, HHI_GCLK_MPEG0 numbering |
| drivers/amlogic/amports/arch/regs/amrisc_regs.h | 1,459 | AMRISC register names (cross-check) |
| drivers/amlogic/amports/arch/regs/dmc_regs.h | 6,486 | checked for HCODEC DMC bits (none) |
| drivers/amlogic/amports/arch/regs/enc_regs.h | 15,059 | checked: the display encoder (VENC), not HCODEC; not used |
| drivers/amlogic/amports/arch/firmware.h | 3,159 | microcode registration macros |
| drivers/amlogic/amports/arch/register_ops.c | 3,578 | bus accessors |
| drivers/amlogic/amports/arch/register_ops.h | 2,766 | bus accessors |
| drivers/amlogic/amports/arch/register_ops_m8.c | 2,128 | GXBB HHI offset (−0x1000) |
| drivers/amlogic/amports/arch/register_map.c | 6,040 | word-to-byte shifts per bus |
| drivers/amlogic/amports/arch/register.h | 1,288 | header includes |
| drivers/amlogic/amports/arch/clkgx.c | 12,614 | HCODEC clock fields, the 200 MHz helper (§2.7) |
| drivers/amlogic/amports/arch/clk.c | 6,950 | clock dispatch (§12.3 item 4) |
| drivers/amlogic/amports/arch/clk.h | 3,419 | clock manager registration |
| drivers/amlogic/amports/arch/chips.c | 3,155 | chip tables (cross-check) |
| drivers/amlogic/amports/vdec_reg.h | 6,184 | HREG accessors, mailbox IRQ numbers |
| drivers/amlogic/amports/vdec.c | 29,994 | vdec_poweron/off/on for VDEC_HCODEC (§2.7) |
| drivers/amlogic/amports/vdec.h | 2,499 | core enumeration |
| drivers/amlogic/amports/amvdec.c | 17,679 | amhcodec_loadmc/start/stop (§12.3 item 10) |
| drivers/amlogic/amports/amvdec.h | 2,316 | prototypes |
| drivers/amlogic/amports/amports_gate.c | 3,785 | the "vdec" gate (§2.2) |
| drivers/amlogic/amports/Kconfig | 4,676 | AM_ENCODER (no throughput claims) |
| drivers/amlogic/canvas/canvas.c | 15,629 | canvas_config uses endian 0; address/width rounding |
| drivers/amlogic/canvas/canvas_reg.h | 2,651 | LUT field layout, block-mode values |
| include/linux/amlogic/cpu_version.h | 2,940 | chip IDs (GXBB 0x1F, GXTVBB 0x20, MG9TV 0x1C) |
| arch/arm64/boot/dts/meson64_odroidc2.dts | 23,563 | amvenc_avc node (SPI 45, edge), codec_io bases, CMA pools |
| arch/arm64/configs/odroidc2_defconfig | 111,699 | CONFIG_AM_ENCODER=y, CONFIG_CMA=y |

**From this repository:** `docs/m26-vdec-facts.md` (board results, §10), `docs/m26-vdec-hevc-vendor-facts.md`, and `haiku/src/add-ons/kernel/drivers/video/meson_vdec/meson_vdec.cpp` (for §2.8 only).

### 14.2 Open questions for the first board test

**Bring-up:**
1. AO_RTI_PWR_CNTL_REG0 bits 4:3: read them before power-up. Are they already 0, and is clearing them needed on GXBB? What do they control?
2. Does the power-up of §2.4 plus the microcode load of §4.3 complete on GXBB? (IMEM DMA busy clears; MPSR reads 1.) How long does the 16 KiB load take, and what alignment does the staging buffer need?
3. Does the SEQUENCE command return status 7 with a mailbox 2 interrupt on SPI 45? Is the interrupt a pulse (edge), as for VDEC (base §10)? Is SPI 45 also driven by another DOS core's mailbox 2?
4. Is any DMC request-enable bit needed for HCODEC's memory port? The vendor sets none.

**Bitstream (parse the SPS, PPS and slices with FFmpeg or a bitstream dumper):**
5. Are there Annex B start codes before each NAL unit?
6. Which profile_idc and level_idc, and which entropy mode (CAVLC presumed)?
7. Is there frame cropping for 1080 lines? POC type and log2 fields (4 and 4 expected); max_num_ref_frames; VUI.
8. Is deblocking applied, and does the encoder's reconstruction match FFmpeg's decode bit-exactly? (This also shows whether the reconstruction chroma is NV12 or NV21 ordered.)
9. Does the microcode mask frame_num and POC to 4 bits, or must the driver?
10. Does the bitstream sit in stream byte order in memory with VLC_VB_CONTROL = 0x3A?

**Input:**
11. Does iformat 12 (RGBA8888) work on GXBB? If so, what line length (width × 3 as the vendor computes, or width × 4), which byte is alpha, and what is the R/G/B order? The same R/B question applies to RGB888 (iformat 1).
12. Which RGB-to-YUV matrix and range does r2y_mode 1 apply? Encode flat RGB patches and measure the decoded YUV.
13. NV12 versus NV21 (iformat 3 versus 2): confirm the chroma order with a coloured test frame.
14. What happens if the input planes have only 1080 lines while MFDIN reads 1088 (chroma placed right after 1080 luma lines)?
15. The effect of NR (nr_mode 3) on the output, and whether its temporal part keeps hidden state between pictures. Use nr 0 for the bit-exact tests.

**Limits and behaviour:**
16. Encode time per 1920 × 1088 picture at 500 MHz, IDR and P. Does it fit 33 ms for 30 frames a second? Does level 6 (666.7 MHz) work and help?
17. What happens when a picture exceeds the 1 MiB bitstream buffer: a hang, a wrap, an error status?
18. FIXED_SLICE_CFG: is it a macroblock count for the GX microcode? Is the vendor formula (MB height × rows) a bug?
19. Does an IDR with IE_ME_MB_TYPE = 9 still use I16 macroblocks?
20. Must the assist and dump buffers be zeroed? Does the microcode always write the dump (80 bytes per MB)? Is the QDCT buffer used at all in full mode?
21. The meaning of the remaining fixed values: DOS_SW_RESET1 bits, HCODEC_ASSIST_MMC_CTRL1 = 0x32, VLC_VB_MEM_CTL, VLC_CONFIG bits 2:1, ASSIST_AMR1_INT0/1/3, IMEM_DMA_CTRL bits 18:16. They are needed only as values, but a failure would point here first.
22. Coexistence with `meson_vdec` (§2.8): both drivers must stop full writes of DOS_GCLK_EN0 and share the DOS clock gate and the AO/HHI read-modify-writes under one lock, before VDEC and HCODEC can run at once (for example MJPEG decoding on VDEC while encoding).
