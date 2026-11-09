Facts read from Linux's GPL drivers/staging/media/meson/vdec (mainline master at a9547379cb38, 2026-10-30; the last change to that directory is b3a805dd5a83, 2026-10-04) and the out-of-tree HEVC codec `codec_hevc.c` by Maxime Jourdan (https://github.com/chewitt/linux/blob/amlogic-7.2.y/drivers/staging/media/meson/vdec/codec_hevc.c, branch amlogic-7.2.y at 61cde89f8386, file last changed in 8f424f83e565 on 2026-09-04; the same code as LibreELEC.tv b91353c808a8 patches 0051, 0052 and 0066), cross-checked against its successor, the `meson-amvdec` driver in LibreELEC.tv a8aae15e27b2 (patches 0119 to 0128); no code copied.

# M26 stage 3: S905 (GXBB) HEVC decoder block, hardware facts for a feasibility test

This is the HEVC counterpart of `m26-vdec-facts.md` (the **base document**, cited as "base §N") and `m26-vdec-h264-facts.md` (cited as "H.264 §N"). Everything those documents say about the DOS block, the ESPARSER, the canvas LUT and the session order still applies and is not repeated. This document covers the separate **HEVC core** in the DOS block, with its own AMRISC processor, its own register bank and its own stream fetcher.

Conventions are those of base §0:
- DOS offsets are **byte offsets from the DOS base 0xC882_0000**. The word index that Amlogic's headers use is the byte offset divided by 4; §8 gives both.
- "Set bits" and "clear bits" mean read-modify-write. "Write" means a full 32-bit store.
- **UNVERIFIED** marks a meaning that no source states, or that is inferred.

---

## 0. Sources, and what they prove

### 0.1 The three generations of driver code

| Tag | Source | Status | Relevance |
|---|---|---|---|
| **ML** | mainline `vdec_hevc.c`, `codec_vp9.c`, `codec_hevc_common.c`, `hevc_regs.h` | in Linux | Power-up, firmware load, stream hookup and buffer tables of the HEVC core, used only for VP9 on GXL, GXM, G12A and SM1 |
| **B** | out-of-tree `codec_hevc.c` (Maxime Jourdan 2018; v2 posted 2023 by Benjamin Roszak; later fixes by Marek Czerski in 2026) for the staging driver above | carried by LibreELEC until 2026-10-02 and in chewitt/linux amlogic-7.2.y | A **stateful** HEVC decoder: the firmware parses VPS, SPS, PPS and slice headers itself from an Annex B stream. Its commit message names GXBB, GXL and GXM. |
| **C** | `meson-amvdec`, a new platform driver by Gus Bourg and Christian Hewitt (2026-09/10), LibreELEC.tv a8aae15e27b2 patches 0119 (core), 0122 (HEVC), 0127, 0128 | replaces the staging driver in LibreELEC since commit 22c2df4f50b7 (2026-10-02); a8aae15e27b2 (2026-10-10) is the current version | A **stateless** (V4L2 request API) driver that rebuilds VPS/SPS/PPS bitstreams from controls and feeds them to the same firmware. Its hardware sequences derive from B. It has GXBB-specific code and was tested on a GXBB board. |

In a few places this document also cites the ODROID-C2's own vendor kernel (**V**: hardkernel/linux branch odroidc2-3.14.y, `drivers/amlogic/amports/vh265.c` and neighbours, GPL, Amlogic). V runs its own firmware build (`vh265_mc`), not `gxl_hevc.bin`, so V is used only to name register bits and to confirm GXBB behaviour, and is labelled every time.

### 0.2 Evidence that the feasibility test can succeed

- **GXBB HEVC decoding works with `gxl_hevc.bin`.** C's patch 0128 (Christian Hewitt, 2026-10-02) says that on a WeTek Play 2, which is a GXBB board, 4K HEVC Main decodes bit-exact with software at 33 frames/s, and Main 10 decodes at 50 frames/s. The firmware path for GXBB in C is `meson/vdec/gxl_hevc.bin`.
- B's commit message says the codec is for "GXBB/GXL/GXM", and B's format table has a GXBB HEVC entry with `gxl_hevc.bin`.
- Mainline's `codec_hevc_common.c` still carries a **GXBB-only** branch for programming the reference-address table (§5.7.1). Mainline never reaches it, because GXBB has no VP9 entry; it is left over from the HEVC work.
- V lists, for GXBB and later, an HEVC capability string of "4k, 9bit, 10bit, dwrite, compressed".
- Nothing in any source says that HEVC is unavailable on GXBB. The only GXBB exclusions are for **VP9**: mainline's GXBB table has no VP9, and C's dt-binding patch 0126 states that GXBB and GXLX have no VP9.

### 0.3 The HEVC core in the address map

- The HEVC core's registers sit at DOS + 0xC000 to DOS + 0xE4FF (physical 0xC882_C000 to 0xC882_E4FF).
- Its assist, AMRISC and IMEM registers repeat VDEC_1's layout shifted by 0xC000 (word index + 0x3000): for example ASSIST_MBOX1_CLR_REG 0x01D4 becomes HEVC_ASSIST_MBOX1_CLR_REG 0xC1D4, MPSR 0x0C04 becomes HEVC_MPSR 0xCC04, IMEM_DMA_CTRL 0x0D00 becomes HEVC_IMEM_DMA_CTRL 0xCD00, and PSCALE_CTRL 0x2444 becomes HEVC_PSCALE_CTRL 0xE444. The pattern is a reading of hevc_regs.h, not a stated rule.
- The HEVC core has its own scratch registers, HEVC_ASSIST_SCRATCH_0 to _N (0xC300 to 0xC35C), separate from VDEC_1's AV_SCRATCH_x.
- The DOS-wide control registers used by HEVC are DOS_GEN_CTRL0 (0xFC08), DOS_MEM_PD_HEVC (0xFCCC), DOS_SW_RESET3 (0xFCD0) and DOS_GCLK_EN3 (0xFCD4).
- The HEVC core does **not** use the DMC canvas LUT (base §6). It has its own table of physical addresses (§5.7.1).

---

## 1. Power-up and power-down of the HEVC core

### 1.1 Clocks

The vdec core turns on `dos_parser` and `dos` before it starts any decoder block (base §1.1, same bits). The HEVC block then needs only its own clock. It does **not** need the VDEC_1 clock; mainline never enables `vdec_1` on the HEVC path.

| DT clock name | Clock ID (gxbb-clkc.h) | Register | Physical | Fields (GXBB) |
|---|---|---|---|---|
| `vdec_hevc` | CLKID_VDEC_HEVC = 156 (mux 154, divider 155) | HHI_VDEC2_CLK_CNTL, HHI + 0x1E4 | 0xC883_C1E4 | [26:25] source: 0 fclk_div4, 1 fclk_div3, 2 fclk_div5, 3 fclk_div7; [24] gate (1 = on); [22:16] divider − 1 |

- These are the same parents and field widths as VDEC_1's clock (base §1.2), placed in the upper half of a different register.
- Linux asks for **666,666,666 Hz**. That selects fclk_div3 with divider 0: bits [26:16] = 0x300, i.e. the register's upper half reads 0x0300_xxxx. With the fixed PLL at 2 GHz, this is 666.67 MHz.
- Order, as for VDEC_1: source and divider first while the gate is off, then set bit 24. Linux changes only these fields.
- **GXBB and GXL are identical here.** `gxbb.c` defines the HEVC mux, divider and gate once and registers them in both the GXBB and the GXL clock tables. The GXBB device tree lists `vdec_hevc` as the vdec node's fourth clock.
- Linux describes nothing in bits [15:0] of HHI_VDEC2_CLK_CNTL on GXBB.

**G12A and SM1 (for comparison only):**
- The same register also holds a second clock, `vdec_hevcf` (the HEVC "front"): source in bits [11:9], divider in [6:0], gate at bit 8. The `vdec_hevc` source field there is 3 bits wide, [27:25].
- Mainline sets `vdec_hevcf` to 666 MHz and enables it **before** `vdec_hevc`, and disables it after.
- GXBB has no `vdec_hevcf`.

### 1.2 AO power and isolation bits

| Register | Physical | HEVC bits on GXBB, GXL, GXM, G12A | SM1 |
|---|---|---|---|
| AO_RTI_GEN_PWR_SLEEP0 | 0xC810_00E8 | **bits 7:6** (mask 0xC0): 1 = HEVC powered off | bit 2 |
| AO_RTI_GEN_PWR_ISO0 | 0xC810_00EC | **bits 11:10** (mask 0xC00): 1 = HEVC isolated | bit 2 |

- Mainline's HEVC path always does read-modify-write on just these bits.
- For comparison, VDEC_1 uses SLEEP0 bits 3:2 and ISO0 bits 7:6 (base §1.3).
- **Conflict with the VDEC_1 path.** Base §1.3 and §1.4 record that Linux's VDEC_1 code writes the *whole* ISO0 register: 0 at power-up and 0xC0 at power-down. Both values clear bits 11:10, so stopping or starting VDEC_1 also removes HEVC isolation, whether or not HEVC is powered. A Haiku driver that has both blocks should use read-modify-write for both (inferred from the bit positions; the effect of de-isolating an unpowered block is **UNVERIFIED**).
- Mainline's GX power-domain driver (`meson-ee-pwrc`, compatible "amlogic,meson-gxbb-pwrc") defines only VPU and Ethernet-memory domains for GXBB. The VPU uses SLEEP0 bits 8 and 9. There is no VDEC or HEVC domain; the vdec driver writes the AO bits itself.

### 1.3 Power-up order (mainline, GXL path; GXBB takes the same path in B and C)

This runs after the DOS_PARSER and DOS clocks are on (base §1.3 steps 1–2).

| # | Register | Address | Action | Value | Note |
|---|---|---|---|---|---|
| 1 | HHI_VDEC2_CLK_CNTL | 0xC883_C1E4 | fields [26:25] = 1, [22:16] = 0, then set bit 24 | | 666.67 MHz, §1.1 |
| 2 | AO_RTI_GEN_PWR_SLEEP0 | 0xC810_00E8 | clear bits 7:6 | | HEVC power on |
| 3 | wait | | 10–20 µs | | |
| 4 | DOS_SW_RESET3 | DOS + 0xFCD0 | write | 0xFFFF_FFFF | all HEVC resets asserted. C holds them for 10 µs before step 5; mainline does not wait |
| 5 | DOS_SW_RESET3 | DOS + 0xFCD0 | write | 0 | |
| 6 | DOS_GCLK_EN3 | DOS + 0xFCD4 | write | 0xFFFF_FFFF | HEVC internal clock gates |
| 7 | DOS_MEM_PD_HEVC | DOS + 0xFCCC | write | 0 | HEVC memories powered |
| 8 | AO_RTI_GEN_PWR_ISO0 | 0xC810_00EC | clear bits 11:10 | | isolation removed |
| 9 | DOS_SW_RESET3 | DOS + 0xFCD0 | write 0xFFFF_FFFF, then 0 | | second full reset, now with memories powered and isolation off (C again holds 10 µs) |
| 10 | (C only) DOS_SW_RESET3 | DOS + 0xFCD0 | write 0x000E_D918, wait 10 µs, write 0 | | the "sub-engine" reset of §1.6 |
| 11 | stream buffer init | | §3.1 | | |
| 12 | firmware load | | §2.2 | | |
| 13 | codec start | | §5.2 | | |
| 14 | DOS_SW_RESET3 | DOS + 0xFCD0 | write 0x0000_1800 (bits 12 and 11), then 0, then read back | | the read flushes the posted writes |
| 15 | HEVC_MPSR | DOS + 0xCC04 | write | 1 | AMRISC runs |
| 16 | wait | | 10–20 µs | | "let the firmware settle" |
| 17 | ESPARSER init with the HEVC hookup | | §3.2 | | |

- Nothing is polled during power-up.
- Bits 12 and 11 of DOS_SW_RESET3 are pulsed just before MPSR = 1. Their meaning is **UNVERIFIED**. The VDEC_1 power-down pulses the same bit numbers in DOS_SW_RESET0 (base §1.4).

### 1.4 Power-down order

Linux first waits for 50 ms without a vdec IRQ (base §8).

| # | Register | Address | Action | Value | Source |
|---|---|---|---|---|---|
| 1 | HEVC_ASSIST_MBOX1_MASK | DOS + 0xC1D8 | write | 0 | ML, C |
| 2 | HEVC_MPSR | DOS + 0xCC04 | write | 0 | ML, C |
| 3 | HEVC_CPSR | DOS + 0xCC84 | write | 0 | C only |
| 4 | drain checks, each polled up to 1000 × 10 µs: HEVC_IMEM_DMA_CTRL (0xCD00) bit 15 clear; HEVC_LMEM_DMA_CTRL (0xCD40) bit 15 clear; HEVC_WRRSP_LMEM (0xCD4C) bits 11:0 zero | | read | | C only (both registers are local definitions in C, not in hevc_regs.h) |
| 5 | HEVC_STREAM_CONTROL | DOS + 0xC404 | write | 0 (stream fetch off) | C only |
| 6 | DOS_SW_RESET3 | DOS + 0xFCD0 | write 0x000E_D918, wait 10 µs, write 0 | | C only |
| 7 | (free the codec buffers) | | | | ML, C |
| 8 | AO_RTI_GEN_PWR_ISO0 | 0xC810_00EC | set bits 11:10 | | ML, C |
| 9 | DOS_MEM_PD_HEVC | DOS + 0xFCCC | write | 0xFFFF_FFFF | ML, C |
| 10 | AO_RTI_GEN_PWR_SLEEP0 | 0xC810_00E8 | set bits 7:6 | | ML, C |
| 11 | HHI_VDEC2_CLK_CNTL | 0xC883_C1E4 | clear bit 24 | | ML, C |
| 12 | DOS clock, then DOS_PARSER clock off | | as base §1.4 steps 12–13 | | ML, C |

C frees the codec buffers only if steps 4–6 succeed, so that no DMA can still reach them. Its comment says a halted processor can leave a DMA busy flag set until the core reset clears it. Mainline frees them right after MPSR = 0.

### 1.5 G12A and SM1 differences (for comparison; not needed on GXBB)

| Item | G12A | SM1 |
|---|---|---|
| Extra clock | `vdec_hevcf` (§1.1), on before and off after `vdec_hevc` | same |
| AO bits | as GXBB | SLEEP0 bit 2, ISO0 bit 2 |
| HEVC_STREAM_CONTROL | also bits [28:25] = 0xF ("arwlen_axi_max") at codec start | same |
| Deblock | HEVC_DBLK_CFGE = second deblock data buffer; HEVC_DBLK_CFGB selects which writes happen (§6.2) | as G12A, with a different base value |
| Frame MMU | HEVC_ASSIST_MMU_MAP_ADDR (0xC024) | same |
| C only | parks the DMC request ports of AXI channels 4 and 8 around resets; uses a larger sub-engine reset set, 0x050E_FD18; holds HEVC_SAO_MMU_RESET_CTRL (0x9904) bit 0; on G12B pulses the level reset bits 14:13 at 0xFFD0_109C | |

### 1.6 What is GXBB-specific in power and reset

- **Nothing in power-up differs between GXBB and GXL** in ML, B or C. All three take the non-SM1 branch for the AO bits, and the clock definitions are shared.
- C's sub-engine reset for all GX parts (GXBB, GXL, GXLX, GXM) is DOS_SW_RESET3 = **0x000E_D918**: bits 3, 4, 8, 11, 12, 14, 15, 17, 18 and 19.
  - V uses exactly the same set when it restarts the stream on its GXBB-era code.
  - V names two of the bits: bit 3 resets the whole parser, and bit 14 resets the IQIT (inverse quantiser and transform) and restarts its memory initialisation. The other bits are **UNVERIFIED**.
- C never parks DMC ports on GX; that hook exists only for G12.

---

## 2. Firmware

### 2.1 The files

| File (linux-firmware 20260319.git217ca6e4, decompressed) | Size | md5 | Used by |
|---|---|---|---|
| `meson/vdec/gxl_hevc.bin` | **16,384** | 3a18630bb2b614dba18084020de3415b | B and C on GXBB, GXL, GXLX and GXM |
| `meson/vdec/gxl_hevc_mmu.bin` | 16,384 | ba58e62fcbf0342a6300397a226a6915 | nothing in ML, B or C |
| `meson/vdec/g12a_hevc_mmu.bin` (`sm1_hevc_mmu.bin` is a symlink to it) | 16,384 | 07fbbf7ff141a281eeee35f490d66ce8 | C on G12A, G12B and SM1 |
| `meson/vdec/gxl_vp9.bin` (for comparison) | 16,384 | 8fe51865ec824a7cdbffc96c19d608bf | ML VP9 on GXL and GXM |

There is **no GXBB-specific HEVC firmware** in linux-firmware. Both B and C load `gxl_hevc.bin` on GXBB.

**Layout.** This comes from looking at the files, not from Linux:
- Each HEVC file is exactly one 16 KiB AMRISC instruction-memory image: 4096 little-endian 32-bit words, with no header.
- Unlike `gxbb_h264.bin` (H.264 §1), there is **no extension part** beyond the 16 KiB, and nothing in ML, B or C takes one. Their HEVC loader copies exactly the first 16 KiB and has no "extended firmware" hook.
- In `gxl_hevc.bin`, the last non-zero word is at byte 0x3E5C, and the last 104 words (0x3E60 to 0x3FFF) are zero. The two MMU builds fill the image almost to the end: their last non-zero words are at 0x3FF0 and 0x3F40.
- The first words look alike in all AMRISC images. The first word of `gxl_hevc.bin` is 0x0681_0001; that of both MMU builds and of `gxl_vp9.bin` is 0x0681_4001. What this difference means is **UNVERIFIED**.
- Only about 250 of 4,096 non-zero words match between `gxl_hevc.bin` and either MMU build at the same offsets. They are different builds, not patches of each other.

### 2.2 Loading the firmware into the HEVC AMRISC

This is the VDEC_1 sequence of base §2 on the HEVC copies of the registers.

| # | Register | Offset | Action | Value |
|---|---|---|---|---|
| 1 | HEVC_MPSR | 0xCC04 | write | 0 |
| 2 | HEVC_CPSR | 0xCC84 | write | 0 |
| 3 | HEVC_IMEM_DMA_ADR | 0xCD04 | write | physical address of the 16 KiB staging copy |
| 4 | HEVC_IMEM_DMA_COUNT | 0xCD08 | write | 0x1000 (length in 32-bit words) |
| 5 | HEVC_IMEM_DMA_CTRL | 0xCD00 | write | 0x0007_8000: bit 15 starts the DMA and reads 1 while busy; bits 18:16 = 7 (meaning **UNVERIFIED**, as in base §2) |
| 6 | HEVC_IMEM_DMA_CTRL | 0xCD00 | poll | until bit 15 reads 0 |

- **Poll limit.** Mainline gives up after only **100** back-to-back reads with no delay; the VDEC_1 loader allows 1000. C waits up to one second (100,000 × 10 µs). That suggests 100 reads were not always enough (inferred, **UNVERIFIED**).
- **Staging buffer.** The file bytes are copied unchanged into 16 KiB of device-visible memory with a 32-bit physical address.
  - Mainline allocates it per load and frees it right after the poll.
  - C keeps one staging buffer for the whole device lifetime. Its commit message says it retains "the microcode DMA source"; the reason it gives is quiescing DMA safely (inferred).
- The firmware is loaded at every session start, after the stream buffer init (§3.1) and before the codec start (§5.2). C also reloads it whenever it restarts the stream (§3.5).
- The processor starts only at §1.3 step 15 (HEVC_MPSR = 1), after the codec registers and the mailbox are set up. HEVC_CPSR is never set to 1.

---

## 3. Stream input

### 3.1 The HEVC stream buffer registers

The HEVC core fetches its bitstream from the same kind of DDR ring as VDEC_1's VIFIFO (base §4), but through its own registers. **None of the VLD_MEM_VIFIFO_* registers of base §4 is written on the HEVC path.**

**Init**, before the firmware load (ML, B and C):

| # | Register | Offset | Action | Value |
|---|---|---|---|---|
| 1 | HEVC_STREAM_CONTROL | 0xC404 | clear bit 0 | stream fetch off |
| 2 | HEVC_STREAM_START_ADDR | 0xC408 | write | ring base (physical) |
| 3 | HEVC_STREAM_END_ADDR | 0xC40C | write | ring base + ring size. This is the end itself, **not** end − 8 as for the parser |
| 4 | HEVC_STREAM_RD_PTR | 0xC414 | write | ring base |
| 5 | HEVC_STREAM_WR_PTR | 0xC410 | write | ring base |

**Ring size.** ML and C allocate 16 MiB. The board ran VDEC_1 with 8 MiB (base §10). Whether 8 MiB is enough for HEVC is not stated; for a one-picture test any size above the stream plus padding works (inferred).

**Other stream registers:**

| Register | Offset | Use |
|---|---|---|
| HEVC_STREAM_LEVEL | 0xC418 | bytes in the ring. ML and C use it in place of VLD_MEM_VIFIFO_LEVEL in the parser's free-space formula (base §3.6) |
| HEVC_STREAM_FIFO_CTL | 0xC41C | bit 29 set at hookup (§3.2) |
| HEVC_SHIFT_BYTE_COUNT | 0xC434 | the shifter's cumulative byte position (§3.5) |
| HEVC_STREAM_RD_PTR, HEVC_STREAM_WR_PTR | 0xC414, 0xC410 | C only reads them, for diagnostics. V reads HEVC_STREAM_WR_PTR as the parser's write position for HEVC streams |

### 3.2 Hooking the ESPARSER to the HEVC core

The ESPARSER is set up exactly as in base §3.3 (reset pulse, config, search pattern, PARSER_VIDEO_START_PTR = base, PARSER_VIDEO_END_PTR = base + size − 8, PARSER_ES_CONTROL bit 0 cleared). Steps 11 and 12 of base §3.3, the VDEC_1 consumer select and the VLD BUF_CNTL handover, are **replaced** by:

| # | Register | Offset | Action | Value | Meaning |
|---|---|---|---|---|---|
| 1 | DOS_GEN_CTRL0 | 0xFC08 | write | **0x6** (3 << 1) | Mainline and V name the field "vififo_vbuf_rp_sel" and say the value selects the HEVC core. 0 selects VDEC_1 (base §3.3) |
| 2 | HEVC_STREAM_CONTROL | 0xC404 | set bit 3 | | V: "use_parser_vbuf_wp", i.e. take the write pointer from the parser. V leaves bit 4 at 0 |
| 3 | HEVC_STREAM_CONTROL | 0xC404 | set bit 0 | | V: "stream_fetch_enable" |
| 4 | HEVC_STREAM_FIFO_CTL | 0xC41C | set bit 29 | | V: a 256-byte "stream buffer hole". `codec_vp9.c` sets the same bit again at codec start, but only for G12A and later |

- Then, as in base §3.3, PARSER_INT_STATUS ← 0xFFFF and PARSER_INT_ENABLE ← 0x100.
- In ML and C this runs **after** HEVC_MPSR = 1 (§1.3 step 17), as for VDEC_1.
- The codec start (§5.2) also sets HEVC_STREAM_CONTROL bit 0 before the hookup.
- V names PARSER_ES_CONTROL bit 0 "ES_VID_MAN_RD_PTR" (manual video read pointer) and clears it in the same place. This answers part of base open question 8.
- **Switching back to VDEC_1.** V writes DOS_GEN_CTRL0 = 0 when it tears the HEVC hookup down. Mainline's VDEC_1 path writes 0 at every start (base §3.3 step 11), so a later VDEC_1 session undoes the HEVC selection.

### 3.3 Shifter and parser set-up at codec start

These are written by B and C at codec start (§5.2), before MPSR = 1. Bit names in the right-hand column come from V's comments, where V gives them; other meanings are **UNVERIFIED**.

| Register | Offset | Action | Value | Bits |
|---|---|---|---|---|
| HEVC_PARSER_INT_CONTROL | 0xC480 | keep bits 25:0, replace bits 31:26, then set bits | OR 0x6940_0091 | [31:29] = 3 and [28:26] = 2 (no names); 24 stream-buffer-empty interrupt to the AMRISC; 22 stream-FIFO-empty interrupt to the AMRISC; 7 decode-done interrupt to the CPU; 4 start-code-found interrupt to the CPU; 3 = 0, no start-code interrupt to the AMRISC; 0 parser interrupt enable. Mainline VP9 uses 0x6140_0091, without bit 27 |
| HEVC_SHIFT_STATUS | 0xC42C | set bits | 0x3 | 1 emulation-prevention check on; 0 start-code check on. VP9 sets only bit 0 |
| HEVC_SHIFT_CONTROL | 0xC420 | write | **0xE5** | [7:6] = 3 "valid write position"; [5:4] = 2, emulation code length − 1; [3:1] = 2, start-code length − 1; [0] stream shift enable. VP9 writes 0x6E7 |
| HEVC_CABAC_CONTROL | 0xC440 | write | 1 | CABAC enable |
| HEVC_PARSER_CORE_CONTROL | 0xC44C | write | 1 | parser core clock enable |
| HEVC_PARSER_CMD_WRITE | 0xC448 | write | 0x0001_0000, then the 37 values of the table below, in order | presumably the parser's command memory, loaded from address 0 (**UNVERIFIED**) |
| HEVC_PARSER_CMD_SKIP_0 | 0xC4A0 | write | 0x0000_090B | |
| HEVC_PARSER_CMD_SKIP_1 | 0xC4A4 | write | 0x1B14_140F | |
| HEVC_PARSER_CMD_SKIP_2 | 0xC4A8 | write | 0x001B_1910 | |
| HEVC_PARSER_IF_CONTROL | 0xC488 | write | 0x25 | 5 parser→SAO interface enable; 2 parser→MPRED interface enable; 0 parser→scaler interface enable |
| HEVCD_IPP_TOP_CNTL | 0xD000 | write 1, then write 2 | | bit 0 is a software reset of the IPP and MPP; bit 1 enables the IPP |

**The 37 parser command values**, the same in ML (shared with VP9), B, C and V:

| # | 0–7 | 8–15 | 16–23 | 24–31 | 32–36 |
|---|---|---|---|---|---|
| values | 0x0401 0x8401 0x0800 0x0402 0x9002 0x1423 0x8CC3 0x1423 | 0x8804 0x9825 0x0800 0x04FE 0x8406 0x8411 0x1800 0x8408 | 0x8409 0x8C2A 0x9C2B 0x1C00 0x840F 0x8407 0x8000 0x8408 | 0x2000 0xA800 0x8410 0x04DE 0x840C 0x840D 0xAC00 0xA000 | 0x08C0 0x08E0 0xA40E 0xFC00 0x7C00 |

**Not written by HEVC.** HEVC_SHIFT_STARTCODE and HEVC_SHIFT_EMULATECODE are left at their reset values. V checks that these read 0x0000_0100 and 0x0000_0300. Mainline's VP9 writes HEVC_SHIFT_STARTCODE = 0x0000_0001.

### 3.4 Feeding the bitstream

**B (stateful).** The ESPARSER rules of base §3.4 apply unchanged:
- Pad a payload shorter than 4096 bytes with zeros to 4096.
- Put the `00 00 01 FF` trailer after it, outside the pack size.
- Fetch the pack plus 512 bytes.

B adds one flow-control rule on top of the free-space check. A buffer is submitted only while the number of submitted, not yet matched input buffers is below (pictures held by the codec + free capture buffers − 3). Mainline applies the same rule to VP9.

**C (stateless, tested on GXBB).** Each picture's payload, with its rebuilt VPS/SPS/PPS in front, is followed by a **4096-byte zero tail inside the pack**, and then by the usual 512-byte search area that starts with `00 00 01 FF`:
- pack size = payload + 4096;
- fetch length = payload + 4096 + 512;
- the free-space check requires payload + 4096.

C's comment calls the tail lookahead that is not part of the coded data. The firmware reads ahead, so it needs bytes after the picture to finish it (inferred).

**Bitstream form.** Annex B with start codes, passed unchanged. B gives no rule about access units, but one picture per submission keeps timestamps simple, as for H.264 (H.264 §12). Zero bytes between NAL units are legal in HEVC. `00 00 01 FF` would parse as a NAL with forbidden_zero_bit set, and base §10 found that no trailer bytes reach the decoder anyway.

### 3.5 Stream position, endpoints and the 2 GiB limit

- **HEVC_SHIFT_BYTE_COUNT (0xC434)** counts the bytes the HEVC shifter has consumed since the stream was set up.
  - B records it at a picture's first slice report as that picture's stream offset, and matches timestamps with it as in base §5.6.
  - C reads it right after the codec start and treats any non-zero value as an error. So it reads **0** after a fresh power-up.
  - Because the ring starts at its base and the parser writes every padding byte too, this count is in the same byte space as the parser's cumulative write offset (base §3.7). C relies on that equality for its endpoints.
  - V writes this register when it restarts a stream mid-way, so it is writable (V only).
- **HEVC_DECODE_SIZE = HEVC_ASSIST_SCRATCH_N (0xC35C)** holds a cumulative endpoint in the same byte space.
  - B writes 0 once, at start, and never again.
  - C writes 0 at start, 0 before kicking a slice that is not the picture's last, and the picture's end (cumulative offset + payload size, tail excluded) before kicking the picture's **last** slice. The firmware then reports picture completion (§5.5).
  - C also checks that HEVC_SHIFT_BYTE_COUNT at a slice report is no more than that endpoint + 8, which allows for one 64-bit fetch of read-ahead.
  - What 0 means to the firmware is inferred from B: "no endpoint, keep parsing".
- **The 2 GiB limit.** C's comment says the firmware stops consuming input once the cumulative position reaches 0x8000_0000.
  - C therefore restarts the core once HEVC_SHIFT_BYTE_COUNT passes 1024 MiB, at a boundary with no pending picture.
  - Its restart sequence: stop the AMRISC and drain; stop fetching; the sub-engine reset; stream buffer init; firmware reload; codec re-init; the DOS_SW_RESET3 bits 12:11 pulse; HEVC_MPSR = 1; parser re-init. Host offsets then restart at 0.
  - A feasibility test never gets near this limit.

---

## 4. The workspace

### 4.1 Sub-buffers

B allocates one block whose layout comes from the vendor's 4K table. C uses the same layout but moves the per-picture MV storage out into separate buffers (§4.2). All addresses are physical, 32-bit, and written as the block's base + offset.

| Sub-buffer | Offset | Size | Register (offset) | Purpose, from V's comments where given |
|---|---|---|---|---|
| IPP | 0x00_0000 | 0x4000 | HEVCD_IPP_LINEBUFF_BASE (0xD024) | IPP line buffer. V: 4096 × (Y + CbCr + flags) = 12 KiB, rounded up to 16 KiB |
| SAO_ABV | 0x00_4000 | 0x3_0000 | HEVC_SAO_ABV = SCRATCH_A (0xC328), written per slice (§5.7.5) | SAO data from the row above |
| SAO_VB | 0x03_4000 | 0x3_0000 | HEVC_SAO_VB = SCRATCH_C (0xC330), written per slice | SAO vertical-boundary data |
| SH_TM_RPS | 0x06_4000 | 0x800 | HEVC_SHORT_TERM_RPS = SCRATCH_2 (0xC308) | V: up to 64 short-term RPS sets of 16 entries of 2 bytes |
| VPS | 0x06_4800 | 0x800 | HEVC_VPS_BUFFER = SCRATCH_3 (0xC30C) | V: up to 16 VPS of 0x80 bytes |
| SPS | 0x06_5000 | 0x800 | HEVC_SPS_BUFFER = SCRATCH_4 (0xC310) | V: up to 16 SPS of 0x80 bytes |
| PPS | 0x06_5800 | 0x2000 | HEVC_PPS_BUFFER = SCRATCH_5 (0xC314) | V: up to 64 PPS of 0x80 bytes |
| SAO_UP | 0x06_7800 | 0x2800 | HEVC_SAO_UP = SCRATCH_6 (0xC318) | V: up to 640 LCUs (10240/16) of 16 bytes |
| SWAP_BUF | 0x06_A000 | 0x800 | HEVC_STREAM_SWAP_BUFFER = SCRATCH_7 (0xC31C), **only before G12A and only without the frame MMU** | V: 256 cycles × 64 bits (144 used) |
| SWAP_BUF2 | 0x06_A800 | 0x800 | HEVC_STREAM_SWAP_BUFFER2 = SCRATCH_8 (0xC320), same condition | |
| SCALELUT | 0x06_B000 | 0x8000 | HEVC_SCALELUT = SCRATCH_D (0xC334) | V: up to 32 scaling tables of 1024 bytes |
| DBLK_PARA | 0x07_3000 | 0x2_0000 | HEVC_DBLK_CFG4 (0xD410) | deblock parameters. V: up to 256 LCUs × 512 bytes, 0x4_0000 with 10-bit support |
| DBLK_DATA | 0x09_3000 | 0x8_0000 | HEVC_DBLK_CFG5 (0xD414) | deblock data. V's 1080p table uses 0x4_0000 |
| DBLK_DATA2 | 0x11_3000 | 0x8_0000 | HEVC_DBLK_CFGE (0xD438), **G12A and later only** | unused on GX (still reserved in the layout) |
| MMU_VBH | 0x19_3000 | 0x5000 | HEVC_SAO_MMU_VH0_ADDR (0xD8E8) = base, HEVC_SAO_MMU_VH1_ADDR (0xD8EC) = base + 0x2800; **only with the frame MMU** | unused on GX |
| MPRED_ABV | 0x19_8000 | 0x8000 | HEVC_MPRED_ABV_START_ADDR (0xC848), written at each picture's first slice | motion prediction data from the row above |
| MPRED_MV (B only) | 0x1A_0000 | 24 × 0x12_0000 | one slot per capture buffer, §4.2 | co-located MV storage |
| RPM | B: 0x1CA_0000; C: 0x1A_0000 | 0x100 | HEVC_RPM_BUFFER = SCRATCH_1 (0xC304) | the parameters the firmware reports (§5.6) |
| LMEM | B: 0x1CA_0100; C: 0x1A_0100 | 0xA00 | LMEM_DUMP_ADR = SCRATCH_F (0xC33C) | dump of the firmware's local memory. V sizes it 0x800 |

**Totals**, with the block rounded up to 64 KiB:
- B: ends at 0x1CA_0B00, so the block is **0x1CB_0000** (30,081,024 bytes, 28.7 MiB), most of it MV storage.
- C: ends at 0x1A_0B00, so the block is **0x1B_0000** (1,769,472 bytes, 1.69 MiB).

V's 1080p table (V only) is smaller. It uses the same fields with DBLK_DATA 0x4_0000 and MPRED_MV 16 × 0x4_0000, and has no DBLK_DATA2 or MMU_VBH. All workspace addresses are firmware-visible pointers, so only the sizes matter, not the order (inferred).

### 4.2 Motion-vector buffers

- **Size per picture.** B and C reserve **0x12_0000** bytes per picture. C's comment explains it: at 3840×2160 the largest extent is 60 × 34 CTBs × 512 bytes = 0xF_F000.
- **Bytes per CTB** ("mv_mem_unit"): 0x200 for 64×64 CTBs, 0x80 for 32×32, 0x20 for 16×16.
- **Extent of one picture** = CTB count × unit. At 1920×1080 that is 0x3_FC00 for all three CTB sizes (510, 2040 or 8160 CTBs).
- **Which buffer.**
  - B places picture k's MV storage at workspace + 0x1A_0000 + k × 0x12_0000, where k is the capture buffer index.
  - C gives each capture buffer its own 0x12_0000 allocation. Its comment says the hardware takes an independent MV address per picture.
- **Writes and reads.** Every picture writes its MVs, intra pictures included (MV_WR_EN is always set, §5.7.4). P and B pictures read the co-located picture's MVs.
- **Zeroing.** Linux's allocator returns zeroed memory. Whether the firmware needs zeroed memory is **UNVERIFIED**.

### 4.3 AUX buffer

- B and C allocate a separate **16 KiB** buffer.
- HEVC_AUX_ADR = SCRATCH_L (0xC354) ← its physical address.
- HEVC_AUX_DATA_SIZE = SCRATCH_M (0xC358) ← **0x0400_0000**: (16384 / 16) in the upper half and 0 in the lower half.
- Neither driver reads the buffer. That it receives SEI data, and that the two halves are two size limits in 16-byte units, is inferred from the names (**UNVERIFIED**).

### 4.4 Allocation, alignment and when the pointers are read

- All buffers are physically contiguous, with 32-bit physical addresses, and device-visible. Linux uses uncached coherent memory.
- **Alignment.**
  - Linux's allocations are at least page aligned. From CMA they are in practice aligned to their size rounded up to a power of two, capped at 1 MiB, so the workspace sits on a 1 MiB boundary (inferred from the allocator).
  - What the HEVC hardware needs is **UNVERIFIED**.
  - On this board the H.264 workspace failed with 4 KiB alignment and worked with 8 KiB or more (H.264 §16), so match Linux's alignment.
  - The sub-buffer offsets above are all multiples of 0x100.
- **Write the pointer registers before HEVC_MPSR = 1, and do not rewrite them on GX.** C's comment says that GXBB, GXL and GXM firmware moves the RPM pointer from scratch 1 to scratch 8 while it boots and then reuses scratch 1 as parser state. So:
  - C replays the workspace pointers after buffer allocation only on G12A and later;
  - B writes them once, in the codec start.
  - Scratch 1 therefore does not keep its value once the firmware runs. A host should keep its own copy of the RPM address.

---

## 5. The decode protocol

### 5.1 HEVC scratch registers as used by B and C (gxl_hevc.bin)

| Register | Offset | B/C name | Direction | Use |
|---|---|---|---|---|
| SCRATCH_0 | 0xC300 | HEVC_DEC_STATUS_REG | both | status and command (§5.4) |
| SCRATCH_1 | 0xC304 | HEVC_RPM_BUFFER | host → fw at start | RPM address; on GX the firmware reuses this register afterwards (§4.4) |
| SCRATCH_2 | 0xC308 | HEVC_SHORT_TERM_RPS | host → fw | |
| SCRATCH_3, 4, 5 | 0xC30C, 0xC310, 0xC314 | HEVC_VPS_BUFFER, _SPS_, _PPS_ | host → fw | |
| SCRATCH_6 | 0xC318 | HEVC_SAO_UP | host → fw | |
| SCRATCH_7 | 0xC31C | HEVC_STREAM_SWAP_BUFFER; with the frame MMU before G12A, H265_MMU_MAP_BUFFER | host → fw | |
| SCRATCH_8 | 0xC320 | HEVC_STREAM_SWAP_BUFFER2 | host → fw | |
| SCRATCH_9 | 0xC324 | HEVC_sao_mem_unit | host → fw, per slice | §5.7.5 |
| SCRATCH_A | 0xC328 | HEVC_SAO_ABV | host → fw, per slice | |
| SCRATCH_B | 0xC32C | HEVC_sao_vb_size | host → fw, per slice | |
| SCRATCH_C | 0xC330 | HEVC_SAO_VB | host → fw, per slice | |
| SCRATCH_D | 0xC334 | HEVC_SCALELUT | host → fw | |
| SCRATCH_E | 0xC338 | HEVC_WAIT_FLAG | host → fw | 1 at start; bit 1 set before each slice kick (§5.5) |
| SCRATCH_F | 0xC33C | LMEM_DUMP_ADR (alias RPM_CMD_REG) | host → fw | |
| SCRATCH_G | 0xC340 | DEBUG_REG1 | | not written |
| SCRATCH_H | 0xC344 | HEVC_DECODE_MODE2 | host → fw | 0 |
| SCRATCH_I | 0xC348 | NAL_SEARCH_CTL | host → fw | **0xC** (§5.3) |
| SCRATCH_J | 0xC34C | HEVC_DECODE_MODE | host → fw | 0 = single-instance decode |
| SCRATCH_K | 0xC350 | DECODE_STOP_POS | host → fw | 0 |
| SCRATCH_L | 0xC354 | HEVC_AUX_ADR | host → fw | §4.3 |
| SCRATCH_M | 0xC358 | HEVC_AUX_DATA_SIZE | host → fw | 0x0400_0000 |
| SCRATCH_N | 0xC35C | HEVC_DECODE_SIZE | host → fw | endpoint (§3.5) |

**These meanings belong to the firmware, not to the hardware.** V's firmware build uses SCRATCH_7 the same way, but uses SCRATCH_J as the current NAL unit type, SCRATCH_L as a swap-test register, SCRATCH_M as "decode picture begin" and SCRATCH_N as "decode picture count". Mainline's VP9 firmware gives most of them other meanings again. Use the gxl_hevc.bin map above.

### 5.2 Codec start (B and C), between the firmware load and HEVC_MPSR = 1

| # | Register | Value |
|---|---|---|
| 1 | allocate the workspace (and in C the AUX buffer); write all workspace pointers of §4.1 except SAO_ABV, SAO_VB and MPRED_ABV, which are written per slice or per picture (§5.7) | |
| 2 | HEVC_STREAM_CONTROL | set bit 0 |
| 3 | the shifter and parser set-up of §3.3, in that table's order, up to and including HEVC_PARSER_CORE_CONTROL | |
| 4 | HEVC_DEC_STATUS_REG | 0 |
| 5 | HEVC_IQIT_SCALELUT_WR_ADDR (0xDC08) | 0, then 1024 writes of 0 to HEVC_IQIT_SCALELUT_DATA (0xDC10). V's comment says this only avoids undefined values in simulation |
| 6 | HEVC_DECODE_SIZE | 0 |
| 7 | HEVC_PARSER_CMD_WRITE, the parser command table, the SKIP registers, HEVC_PARSER_IF_CONTROL, HEVCD_IPP_TOP_CNTL | as §3.3 |
| 8 | HEVC_WAIT_FLAG | 1 |
| 9 | HEVC_ASSIST_MBOX1_CLR_REG (0xC1D4) | 1 (clear a stale request) |
| 10 | HEVC_ASSIST_MBOX1_MASK (0xC1D8) | 1 (mailbox interrupt on) |
| 11 | HEVC_PSCALE_CTRL (0xE444) | 0 (the comment says this disables PSCALE because the hardware is shared) |
| 12 | NAL_SEARCH_CTL | 0xC |
| 13 | DECODE_STOP_POS | 0 |
| 14 | HEVC_DECODE_MODE | 0 |
| 15 | HEVC_DECODE_MODE2 | 0 |
| 16 | HEVC_AUX_ADR, HEVC_AUX_DATA_SIZE | §4.3 |

- **No output buffers are given at start.** The firmware needs none to parse headers. Output addresses are programmed per picture (§5.7).
- No DMC canvases are used at any point.

### 5.3 NAL_SEARCH_CTL (SCRATCH_I)

V documents the bit fields for its firmware build. B and C write **0xC** and get the behaviour below, so the fields evidently carry over to gxl_hevc.bin (inferred).

| Bits | Meaning (V) |
|---|---|
| 0 | 0 = the firmware parses headers itself; 1 = manual header parsing |
| 1 | 0 = skip automatically for non-seamless streams; 1 = no skip. Bits 3:2 are valid only when this bit is 0 |
| 3:2 | 0 = skip NALs before the first VPS/SPS/PPS/IDR; 1 = skip before the first VPS/SPS/PPS; 2 = as 1, but decode nothing until the first I slice with slice address 0; **3 = skip everything before the first IRAP slice (NAL types 16–21)** |
| 15:4 | count of NALs to skip in manual mode |
| 16 | when bit 0 is 0, what an end-of-stream NAL does: 0 = report "search done" to the host; 1 = report nothing |
| 31:20 | used by the firmware for debugging |

So 0xC means: automatic header parsing, automatic skipping, and nothing before the first IRAP picture. Mainline's VP9 writes 0x8.

### 5.4 Status codes in HEVC_DEC_STATUS_REG

| Value | B/C name | Who writes it | Meaning and handling |
|---|---|---|---|
| 0x00 | HEVC_DEC_IDLE | host at start | idle |
| 0x01–0x04 | NAL_UNIT_VPS, _SPS, _PPS, _CODED_SLICE_SEGMENT | | defined in B; never seen or handled by B or C (**UNVERIFIED** whether gxl_hevc.bin ever reports them with NAL_SEARCH_CTL = 0xC) |
| **0x05** | HEVC_CODED_SLICE_SEGMENT_DAT | **host** | command: "decode the slice data of the segment you just reported" (§5.5) |
| 0x06 | HEVC_SLICE_DECODING | | defined in B, unused |
| 0x07 | HEVC_NAL_UNIT_SEI | | defined in B, unused. In V's build, 7 means "dump LMEM" |
| **0x08** | HEVC_SLICE_SEGMENT_DONE | firmware | **a slice segment header has been parsed**; the RPM holds its parameters, and the firmware waits for the host. This is the main report |
| 0x09 | HEVC_NAL_SEARCH_DONE | firmware | defined in B and V, not handled. V's NAL_SEARCH_CTL bit 16 controls it for end-of-stream NALs |
| **0x0A** | HEVC_DECPIC_DATA_DONE | firmware | **the picture is decoded up to HEVC_DECODE_SIZE** (C). The firmware then parks and polls the status register (§5.5) |
| 0x0B | HEVC_DECPIC_DATA_ERROR | firmware | picture decode error. B's DVB fixes count it and continue without writing anything; the effect on the firmware is **UNVERIFIED** |
| 0x0C, 0x0D | HEVC_SEI_DAT, _DONE | | defined in B, unused |
| 0x20 | HEVC_DECODE_BUFEMPTY | firmware | the stream buffer ran dry while decoding. C: not an error; the firmware resumes by itself once data arrives |
| 0x22 | HEVC_SEARCH_BUFEMPTY | firmware | the same while searching for the next NAL |
| 0xF0 | (V: DISCARD_NAL) | | V only |
| 0xFE | (V: ACTION_ERROR) | | V only |
| **0xFF** | HEVC_ACTION_DONE | **host** | command: "carry on" (skip the reported slice, or resume after 0x0A) |

B treats anything other than 0x08 and 0x0B as fatal and abandons the session. C also accepts 0x0A, 0x20 and 0x22.

No source defines a status called HEVC_NAL_DECODE_DONE or HEVC_DECODE_PIC_BEGIN/END. The nearest names are V's HEVC_DECODE_PIC_BEGIN_REG and HEVC_DECODE_PIC_NUM_REG, which are scratch registers in V's firmware build (§5.1), not status values. In gxl_hevc.bin, "picture done" is 0x0A and "buffer empty" is 0x20 or 0x22.

### 5.5 The handshake

**The report.** The firmware writes the status (0x08, 0x0A, …), then raises mailbox 1, which reaches the GIC as the "vdec" interrupt (§7). Both drivers do all the work in the threaded handler. **Neither acknowledges the mailbox in its handler**, and nor do mainline's VP9 and V's vh265 (§7).

**Status 0x08: slice segment header parsed.** The host:
1. Reads the RPM (§5.6).
2. If this is the first slice of the stream, or the geometry or bit depth changed, sets up output buffers. B raises a V4L2 source change and continues in a resume call once buffers exist. **The firmware simply waits at status 8 meanwhile**; no timeout is known.
3. Then **either decodes** the segment:
   1. programs the per-picture and per-slice registers (§5.7);
   2. C only: HEVC_DECODE_SIZE ← 0, or the picture's endpoint for its last slice segment;
   3. HEVC_WAIT_FLAG: set bit 1;
   4. HEVC_DEC_STATUS_REG ← **0x05**;
   5. HEVC_MCPU_INTR_REQ (0xCC14) ← **0x4**, AMRISC_MAIN_REQ, which interrupts the firmware's processor.

   C puts a DMA write barrier before step 4, so that all register and memory writes are visible before the doorbell.
4. **Or skips** it (C; used for slices before the first IRAP picture):
   1. HEVC_WAIT_FLAG: clear bit 1;
   2. HEVC_DEC_STATUS_REG ← **0xFF**;
   3. HEVC_MCPU_INTR_REQ ← 0x4.

After a decode kick, the firmware decodes the segment's data, then searches on and reports the **next** slice segment header with 0x08. That header may belong to the same picture (slice_segment_address ≠ 0) or start a new one (slice_segment_address = 0).

**Status 0x0A: picture done** (C, when HEVC_DECODE_SIZE was set for the last segment):
1. The host checks that HEVC_PARSER_LCU_START (0xC490) bits 23:0 equal **CTB count − 1**, i.e. the last CTB was reached, and that this was the picture's last slice.
2. It writes HEVC_DECODE_SIZE ← 0. The picture in the output buffer is complete.
3. The firmware stays parked, polling HEVC_DEC_STATUS_REG. To start the next picture, once its data is in the ring: clear HEVC_WAIT_FLAG bit 1, write HEVC_DEC_STATUS_REG ← **0xFF** (with no MCPU interrupt), then write HEVC_DECODE_SIZE ← the next picture's endpoint.

C's comment says bit 1 must stay set until the picture completes, because the parking uses up an MPRED event.

**How B tells that a picture is done.** B never sets an endpoint, so it never sees 0x0A:
- a picture counts as finished when the first slice of the next picture is reported;
- at the end of the stream, B waits until 50 ms pass with no interrupt (base §8) and then hands out every picture it holds.

B sets HEVC_WAIT_FLAG bit 1 before every kick and never clears it.

**Watchdog (C).** After every kick C expects an interrupt within 200 ms. If none comes, it samples HEVC_DEC_STATUS_REG, HEVC_PARSER_LCU_START and HEVC_SHIFT_BYTE_COUNT; any change counts as progress. With input queued and three unchanged samples 10 ms apart, it fails the request. These three registers are the useful progress indicators for a polling driver.

**HEVC_MCPU_INTR_REQ values.** V names 0x1 a stream-empty request and 0x2 a parser request; both drivers use only 0x4. HEVC_MCPU_INTR_MSK (0xCC10) is defined but never written.

### 5.6 The RPM area: what the firmware reports

**Where it is.** It is the 0x100-byte RPM sub-buffer of the workspace (§4.1). V calls it a copy of the firmware's local memory words 0x100–0x17F, the same 128 16-bit values. The firmware writes it before it reports status 0x08.

**Byte order.** The buffer holds 128 16-bit values in groups of four, **reversed within each group**. Parameter number p (0–127) is at u16 position (p rounded down to a multiple of 4) + 3 − (p mod 4). Equivalently: read the buffer as little-endian 64-bit words, and parameter 4n + j is bits [63 − 16j : 48 − 16j] of word n. Linux's buffer is uncached; with a cached mapping, invalidate before reading.

**Layout, for gxl_hevc.bin as used by B and C.** p = parameter number after reordering.

| p | Field | Notes |
|---|---|---|
| 0–15 | CUR_RPS[16] | the current picture's reference picture set, one entry per picture; see below |
| 16 | num_ref_idx_l0_active | |
| 17 | num_ref_idx_l1_active | |
| 18 | slice_type | 0 = B, 1 = P, **2 = I** |
| 19 | slice_temporal_mvp_enable_flag | |
| 20 | dependent_slice_segment_flag | |
| 21 | slice_segment_address | in CTBs; 0 = first segment of a picture |
| 22 | num_title_rows_minus1 | spelled so in the source; not used |
| 23 | **pic_width_in_luma_samples** | coded width |
| 24 | **pic_height_in_luma_samples** | coded height |
| 25 | log2_min_coding_block_size_minus3 | |
| 26 | log2_diff_max_min_coding_block_size | CTB size = 1 << (p25 + 3 + p26) |
| 27 | log2_max_pic_order_cnt_lsb_minus4 | |
| 28 | POClsb | slice_pic_order_cnt_lsb |
| 29 | collocated_from_l0_flag | |
| 30 | collocated_ref_idx | |
| 31 | log2_parallel_merge_level | |
| 32 | five_minus_max_num_merge_cand | |
| 33 | sps_num_reorder_pics_0 | B uses it as the reorder depth for output |
| 34 | modification_flag | bit 0 = L0 list modification present; bit 1 = L1 list modification present |
| 35 | tiles_flags | bit 0 = tiles enabled; bit 1 = uniform spacing (C) |
| 36 | num_tile_columns_minus1 | |
| 37 | num_tile_rows_minus1 | |
| 38–45 | tile_width[8] | in CTBs. C's comment says the firmware's own parser fills at most five |
| 46–53 | tile_height[8] | |
| 54 | misc_flag0 | bit flags, below |
| 55 | pps_beta_offset_div2 | |
| 56 | pps_tc_offset_div2 | |
| 57 | slice_beta_offset_div2 | |
| 58 | slice_tc_offset_div2 | |
| 59 | pps_cb_qp_offset | |
| 60 | pps_cr_qp_offset | |
| 61 | first_slice_segment_in_pic_flag | |
| 62 | temporal ID | B and C take the low 3 bits. The source names it m_temporalId; that it is the NAL header's TemporalId plus 1 is suggested by B's "temporal_id − 1 == 0" test (**UNVERIFIED**) |
| 63 | **nal_unit_type** | 19 = IDR_W_RADL, 20 = IDR_N_LP, 16–18 BLA, 21 CRA |
| 64, 65 | VUI num_units_in_tick, high and low halves | |
| 66, 67 | VUI time_scale, high and low halves | |
| 68 | **bit_depth** | B and C treat any non-zero value as 10-bit. V: bits 3:0 = luma bit depth − 8, bits 7:4 = chroma bit depth − 8 |
| 69 | profile_etc | not decoded by Linux (**UNVERIFIED**) |
| 70 | sei_frame_field_info | not decoded (**UNVERIFIED**) |
| 71 | video_signal_type | not decoded (**UNVERIFIED**) |
| 72–103 | modification_list[32] | list entries, L0 first and then L1 when both lists are modified |
| 104 | conformance_window_flag | |
| 105–108 | conf_win_left, _right, _top, _bottom offsets | in chroma units: × 2 horizontally and × 2 vertically for 4:2:0 |
| 109 | chroma_format_idc | 1 = 4:2:0 |
| 110 | color_description | not decoded |
| 111 | aspect_ratio_idc | not decoded by B |
| 112, 113 | sar_width, sar_height | not decoded by B |
| 114–127 | unused | |

**The layout depends on the firmware build.**
- V's build has only four entries each for tile_width and tile_height, so every field from misc_flag0 onwards sits 8 places lower. It also has a reserved word where B has video_signal_type, and V's local-memory map puts modification_list at RPM word 0x40.
- C's S4 notes say a later firmware has room for 12 tile columns.
- Check the layout on the board with a known stream (§10).

**CUR_RPS entries.**
- Bit 15 set = end of the list. Stop at the first such entry.
- Bit 14 = "used by the current picture"; entries with bit 14 clear are kept for later pictures but not referenced now.
- Bits 13:0 = the delta POC as a 14-bit two's-complement number: bit 13 set means negative, and the value is then −(2¹³ − bits 12:0).

**misc_flag0 bits:**

| Bit | Flag |
|---|---|
| 0 | pcm_loop_filter_disabled_flag |
| 1 | pcm_enabled_flag |
| 2 | loop_filter_across_tiles_enabled_flag |
| 3 | pps_loop_filter_across_slices_enabled_flag |
| 4 | deblocking_filter_override_enabled_flag |
| 5 | pps_deblocking_filter_disabled_flag |
| 6 | deblocking_filter_override_flag (slice) |
| 7 | slice_deblocking_filter_disabled_flag |
| 8 | slice_sao_luma_flag |
| 9 | slice_sao_chroma_flag |
| 10 | slice_loop_filter_across_slices_enabled_flag |

**What the host derives** (B and C):
- CTB size L = 1 << (log2_min_cb + 3 + log2_diff). C rejects any L other than 16, 32 or 64.
- CTB columns = ceil(width / L), CTB rows = ceil(height / L), CTB count = columns × rows.
- Display size = coded size minus the conformance window, scaled by the chroma subsampling.
- B and C read the geometry only from a report with slice_segment_address = 0 and a non-zero width and height.

**C's validity checks**, useful as test expectations:
- coded size at least 16×16;
- not above the format maximum;
- equal to what userspace negotiated;
- **a picture only one CTB wide but more than one CTB tall is refused** as unsupported.

### 5.7 What the host programs for each picture and slice

B and C do the steps in this order: reference table (once per buffer set), decode head (once per buffer set), then for each slice the MC reference list, MCRCC, MPRED, SAO and deblock, and finally the kick. Values are for 8-bit output to NV12 unless stated otherwise.

#### 5.7.1 The reference address table (ANC2AXI): **GXBB differs from GXL**

The HEVC core addresses picture memory through an internal table of up to 24 pictures (MAX_REF_PIC_NUM).
- With NV12 output each picture uses **two slots**: slot 2k = Y plane, slot 2k + 1 = UV plane.
- With compressed (FBC) references each picture uses one slot, k.
- The slot numbers are what the reference list words of §5.7.3 refer to. They are **not** DMC canvas indexes.

**GXBB form** (mainline's GXBB branch, B and C):

| # | Register | Offset | Value |
|---|---|---|---|
| 1 | HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR | 0xD180 | 0 |
| 2 | HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR | 0xD184 | one write per slot: **physical address \| (slot << 8) \| 1**. Bit 0 = valid; bits 15:8 = slot number; bits 31:16 = the address |
| 3 | HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR | 0xD180 | 1 |
| 4 | HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR | 0xD300 | 1 |
| 5 | HEVCD_MPP_ANC_CANVAS_DATA_ADDR | 0xD304 | 32 writes of 0 (clears the reference list) |

- **Only address bits 31:16 survive this encoding**, so on GXBB every plane the HEVC core writes or reads (Y, UV, or the compressed body) must be **64 KiB aligned**. C states this and refuses misaligned buffers. C also rounds each NV12 plane's size up to a multiple of 64 KiB on GXBB, so that CMA aligns it.
- **Unused slots.**
  - Mainline's GXBB branch repeats the last real buffer's command for the unused slots, which rewrites that same slot.
  - C programs **all 24 pictures' slots** and gives the unused ones a valid surface (the first real buffer). C's comment says GXBB commands carry the slot number, so every index is programmed.
  - Which of the two the hardware needs is **UNVERIFIED**; C's form was tested on GXBB.

**GXL and later form** (for comparison):
1. HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR ← 0x6 (bits 2:1).
2. HEVCD_MPP_ANC2AXI_TBL_DATA (0xD190) ← physical address >> 5, once per slot in order (Y, then UV, per picture). This needs 32-byte alignment only.
3. Then steps 3–5 of the GXBB form.

#### 5.7.2 Decode head (how references are read back)

| Case | HEVCD_MPP_DECOMP_CTL1 (0xD308) | Other registers |
|---|---|---|
| **8-bit, NV12 references (the test case)** | **0x8000_0000** (bit 31). B calls it "two-plane reference read mode"; V calls it NV21 reference read for motion compensation | none |
| 10-bit, compressed references, GX (no MMU) | 0 | HEVCD_MPP_DECOMP_CTL2 (0xD30C) ← body size / 32 (before SM1); HEVC_CM_BODY_LENGTH (0xD89C) ← body size; HEVC_CM_HEADER_OFFSET (0xD8AC) ← body size; HEVC_CM_HEADER_LENGTH (0xD8A4) ← header size (§6.2) |
| 8-bit compressed, no MMU (C, GXM export only) | bit 3; HEVC_SAO_CTRL5 bit 9 set ("memory saving") | as above |
| compressed with the frame MMU (G12A and later) | bit 4; C sets CTL2 = 0 | |

#### 5.7.3 Motion-compensation reference lists (P and B slices only; nothing for I)

- **L0.** HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR ← 1. Then one HEVCD_MPP_ANC_CANVAS_DATA_ADDR write per L0 entry: (UV slot << 16) | (UV slot << 8) | Y slot, with Y slot = 2k and UV slot = 2k + 1 for NV12, or k for both with FBC.
- **L1** (B slices). ACCCONFIG ← 0x1001 (bits 12 and 0), then the same for each L1 entry.
- **MCRCC** (the reference cache):
  - **I slice: HEVCD_MCRCC_CTL1 (0xD3C0) ← 0**, and nothing more.
  - P and B slices: write CTL1 ← 0x2 in mainline VP9 (a reset); read back the first two list entries through ACCCONFIG (P: 0x2; B: 0 and 0x1002); put each entry's low 16 bits into both halves of HEVCD_MCRCC_CTL2 / CTL3 (0xD3C4 / 0xD3C8); then CTL1 ← 0xFF0, which the source calls progressive mode.

The meanings of the ACCCONFIG bits are **UNVERIFIED**. They are not needed for an intra picture.

#### 5.7.4 Motion prediction (MPRED), written for every slice, intra slices included

**HEVC_MPRED_CTRL0 (0xC804).** Field names are from hevc_regs.h:

| Bits | Content |
|---|---|
| 1:0 | slice_type (2 for I) |
| 2 | NEW_PIC: set when slice_segment_address = 0 |
| 3 | NEW_TILE: B sets it with NEW_PIC; C sets it at each new tile |
| 4 | NEW_SLI_SEG: B and C set it when dependent_slice_segment_flag is 1 |
| 5 | TMVP: = slice_temporal_mvp_enable_flag |
| 6 | LDC (low-delay check): all references precede the current POC |
| 7 | COL_FROM_L0: = collocated_from_l0_flag |
| 9 | ABOVE_EN: always 1 |
| 10 | MV_WR_EN: always 1 |
| 11 | MV_RD_EN: 1 unless I slice |
| 13 | BUF_LINEAR: always 1 |
| 16+ | log2(CTB size) |
| 20+ | 3, which the source labels cu_size_log2 |
| 24+ | log2_parallel_merge_level |

The widths of the fields at bits 16, 20 and 24 are **UNVERIFIED**.

**HEVC_MPRED_CTRL1 (0xC808)** = max merge candidates (5 − five_minus_max_num_merge_cand) in bits 3:0, | 2 << 4 | 3 << 8 | 5 << 12 | 36 << 16. The constants are named AMVP max candidates, AMVP max candidates in memory, number of chroma modes, and DM chroma index.

**MV addresses and the rest.** B and C first read HEVC_MPRED_CURR_LCU (0xC864) and discard the value. Then:

| Register | Offset | Value |
|---|---|---|
| HEVC_MPRED_MV_WR_START_ADDR | 0xC84C | the current picture's MV buffer |
| HEVC_MPRED_MV_RD_START_ADDR | 0xC850 | the co-located picture's MV buffer. **For an I picture the co-located picture is the current one** |
| at slice_segment_address = 0: HEVC_MPRED_ABV_START_ADDR | 0xC848 | workspace + MPRED_ABV |
| at slice_segment_address = 0: HEVC_MPRED_MV_WPTR | 0xC854 | current MV buffer + slice address × mv_mem_unit |
| at slice_segment_address = 0: HEVC_MPRED_MV_RPTR | 0xC858 | co-located MV buffer |
| otherwise HEVC_MPRED_MV_RPTR | 0xC858 | co-located buffer + slice address × unit. C skips this for dependent slice segments |
| HEVC_MPRED_MV_RD_END_ADDR | 0xC988 | co-located buffer + CTB count × unit |
| HEVC_MPRED_MV_WR_ROW_JUMP, _RD_ROW_JUMP | 0xC85C, 0xC860 | C only: (CTB columns − tile width) × unit; 0 without tiles |
| HEVC_MPRED_TILE_START | 0xC81C | C only: tile column start \| tile row start << 16; 0 without tiles |
| HEVC_MPRED_TILE_SIZE_LCU | 0xC820 | C only: tile width \| tile height << 16, in CTBs. Without tiles: columns \| rows << 16. B does not write these four registers |
| HEVC_MPRED_PIC_SIZE | 0xC814 | width \| height << 16 (coded, in pixels) |
| HEVC_MPRED_PIC_SIZE_LCU | 0xC818 | (columns − 1) \| (rows − 1) << 16 |
| HEVC_MPRED_REF_NUM | 0xC824 | L1 count << 8 \| L0 count. The counts come from the RPM; for an I slice the lists stay all zero |
| HEVC_MPRED_REF_EN_L0, _L1 | 0xC830, 0xC834 | (1 << count) − 1 |
| HEVC_MPRED_CUR_POC | 0xC980 | the current POC (0 for an IDR picture) |
| HEVC_MPRED_COL_POC | 0xC984 | the co-located POC, or 0x8000_0000 ("invalid") when there is none, as for an I slice |
| HEVC_MPRED_L0_REF00_POC + 4i, i = 0–15 | 0xC880–0xC8BC | L0 reference POCs (zeros for I) |
| HEVC_MPRED_L1_REF00_POC + 4i, i = 0–15 | 0xC8C0–0xC8FC | L1 reference POCs (zeros for I) |

C refuses a picture whose MV extent (CTB count × unit) exceeds 0x12_0000.

#### 5.7.5 SAO, deblocking and the output addresses, written for every slice

Let L = CTB size and rows = CTB rows.

| Register | Offset | Value |
|---|---|---|
| HEVC_SAO_CTRL0 | 0xD804 | bits 3:0 = log2(L) (read-modify-write) |
| HEVC_SAO_PIC_SIZE | 0xD814 | width \| height << 16 |
| HEVC_SAO_PIC_SIZE_LCU | 0xD818 | (columns − 1) \| (rows − 1) << 16 |
| HEVC_SAO_Y_START_ADDR, HEVC_SAO_Y_WPTR | 0xD82C, 0xD83C | **NV12 Y plane** of the output buffer |
| HEVC_SAO_C_START_ADDR, HEVC_SAO_C_WPTR | 0xD834, 0xD840 | **NV12 UV plane** |
| HEVC_SAO_Y_LENGTH | 0xD830 | the output size (§6.1) |
| HEVC_SAO_C_LENGTH | 0xD838 | the output size / 2 |
| first slice of a picture only: HEVC_DBLK_CFG2 | 0xD408 | width \| height << 16 |
| first slice only: HEVC_DBLK_CFG1 | 0xD404 | bit 3 = pcm_loop_filter_disabled, if PCM is enabled; bits 8:4 = pps_cb_qp_offset & 0x1F; bits 13:9 = pps_cr_qp_offset & 0x1F; bits 1:0 = 0 for L = 64, 1 for 32, 2 for 16 |
| HEVC_SAO_CTRL1 | 0xD808 | clear 0x3FF3, set **0xFF0** (the comment calls bits 11:4 the endianness for 2-byte swaps, NV12). **GX**: also set bit 0 when references are not compressed (8-bit NV12), which disables the compressed write. C sets bit 1 to disable the double write when the output is not NV12 |
| HEVC_SAO_CTRL5 | 0xD88C | without FBC: clear bits 23:16 (no downscaling of the NV12 output) |
| HEVCD_IPP_AXIIF_CONFIG | 0xD02C | clear bits 5:4, set bits 3:0. Mainline VP9 also clears bit 12, which its comment calls NV12. Meanings **UNVERIFIED** |
| HEVC_DBLK_CFG9 | 0xD424 | see the list below |
| HEVC_SAO_CTRL0 | 0xD804 | bits 9:8 first cleared; bit 9 = loop filter across slices (same rule as CFG9 bit 1); bit 8 = across tiles (C) |
| HEVC_sao_mem_unit (SCRATCH_9) | 0xC324 | (9 for L = 16, 14 for L = 32, 24 for L = 64) << 4, i.e. 0x90, 0xE0 or 0x180 |
| HEVC_SAO_ABV (SCRATCH_A) | 0xC328 | workspace + SAO_ABV |
| HEVC_sao_vb_size (SCRATCH_B) | 0xC32C | (sao_mem_unit + 0x20) × rows |
| HEVC_SAO_VB (SCRATCH_C) | 0xC330 | workspace + SAO_VB |

**HEVC_DBLK_CFG9 bits:**
- bit 2: deblocking disabled. It is the slice's flag if both override flags of misc_flag0 are set, otherwise the PPS flag.
- bits 6:3: beta_offset_div2 & 0xF, and bits 10:7: tc_offset_div2 & 0xF, from the slice or the PPS on the same rule, written only when deblocking is enabled.
- bit 1: loop filter across slices. It is the slice's flag if the PPS flag is set and (SAO luma or SAO chroma or deblocking) is on, otherwise the PPS flag.
- bit 0: loop filter across tiles (C, when tiles are on).

**Not written on GX:**
- HEVC_DBLK_CFGB (0xD42C) is never written by the HEVC codec on GX. Mainline VP9 writes 0x4040_0001 there on GX, with bit 0 meaning VP9 format. So on GX the reset value presumably selects HEVC (**UNVERIFIED**).
- The register at 0xD440 (bit 28) and HEVC_SAO_CTRL9 (0xD8B4, bit 0, MMU) are G12A-only.

### 5.8 Minimal path: one 8-bit IDR picture, no references

This combines B's stateful flow with C's completion detection. Both were exercised on GX parts, and C on GXBB.

**Test stream.**
- Main profile, 8-bit, 4:2:0, Annex B: VPS, SPS, PPS, then one IDR picture (NAL type 19 or 20) in a single slice segment.
- No tiles, no PCM, no scaling lists, no long-term references.
- The coded size should be at least 16×16 and **at least two CTBs wide** (§5.6). For example 64×64 with 16×16 or 32×32 CTBs, or 128×64 with 64×64 CTBs.

**Buffers.**

| Buffer | Size |
|---|---|
| ring | as §3.1 |
| firmware staging | 16 KiB |
| workspace | C layout, 0x1B_0000 |
| one MV buffer | 0x12_0000, or at least CTB count × unit |
| AUX | 16 KiB |
| output Y plane | §6.1 |
| output UV plane | half the Y plane |

On GXBB both output planes must be **64 KiB aligned**.

**Steps:**

| # | Step | Expected result |
|---|---|---|
| 1 | Clocks and HEVC power-up (§1.3 steps 1–10) | |
| 2 | Liveness probe (V's own checks): HEVC_PARSER_VERSION (0xC400) reads **0x0001_0001**; HEVC_SHIFT_STARTCODE (0xC424) reads **0x0000_0100**; HEVC_SHIFT_EMULATECODE (0xC428) reads **0x0000_0300**. V also writes test patterns to these registers and reads them back. It restores the two shift registers but leaves its pattern in the version register, so reading alone is the safer probe | registers respond |
| 3 | Stream buffer init (§3.1) | |
| 4 | Firmware load (§2.2) | the DMA bit clears |
| 5 | Codec start (§5.2) | |
| 6 | DOS_SW_RESET3 pulse of bits 12:11, read-back, HEVC_MPSR ← 1, wait 10–20 µs | |
| 7 | ESPARSER init with the HEVC hookup (base §3.3 and §3.2 here) | |
| 8 | Feed the stream (§3.4). Record its end E = the cumulative write offset before the feed + the stream length (§3.5) | parser interrupt (base §3.5) |
| 9 | Wait for HEVC_DEC_STATUS_REG = **0x08**, by the vdec interrupt or by polling | **milestone 1: the firmware runs and parses headers** |
| 10 | Read the RPM (§5.6) | expect nal_unit_type 19 or 20, slice_type 2, first_slice_segment_in_pic_flag 1, slice_segment_address 0, the stream's width, height and CTB size, bit_depth 0; HEVC_SHIFT_BYTE_COUNT at or before E + 8 |
| 11 | Program the ANC2AXI table in the GXBB form (§5.7.1), with slot 0 = Y, slot 1 = UV, and the same surface in every other slot | |
| 12 | HEVCD_MPP_DECOMP_CTL1 ← 0x8000_0000 (§5.7.2) | |
| 13 | HEVCD_MCRCC_CTL1 ← 0; no reference lists (§5.7.3) | |
| 14 | MPRED for an I slice (§5.7.4): co-located = current, CUR_POC 0, COL_POC 0x8000_0000, all reference POCs 0 | |
| 15 | SAO and deblock (§5.7.5), with HEVC_SAO_CTRL1 bit 0 set | |
| 16 | HEVC_DECODE_SIZE ← E (C's method) or 0 (B's method) | |
| 17 | HEVC_WAIT_FLAG: set bit 1; HEVC_DEC_STATUS_REG ← 0x05; HEVC_MCPU_INTR_REQ ← 0x4 | |
| 18 | With E: wait for status **0x0A** and check HEVC_PARSER_LCU_START bits 23:0 = CTB count − 1. With 0: there is no completion report. Either feed a second copy of the picture and wait for its 0x08 report (that is how B sees the end of a picture), or wait until HEVC_PARSER_LCU_START and HEVC_SHIFT_BYTE_COUNT stop moving | **milestone 2: the intra picture is decoded** |
| 19 | Read the NV12 planes (§6.1) and compare them with a software decoder; an 8-bit IDR should match bit for bit, as C reports for GXBB | |
| 20 | Power down (§1.4) | |

### 5.9 Reference and output management (beyond one IDR picture)

Unlike VDEC_1's H.264 firmware (H.264 §9), **the HEVC firmware leaves picture management to the host**. In B and C the host does all of this:
- computes the full POC from POClsb (most-significant-bit tracking from the previous TemporalId-0 picture; 0 at IDR; MSB 0 at BLA);
- builds the L0 and L1 POC lists from CUR_RPS, num_ref_idx and modification_list;
- marks pictures as unreferenced when they leave the RPS;
- picks a free output buffer for each new picture;
- chooses the co-located picture (from collocated_ref_idx in list 1 − collocated_from_l0 for B slices, or list 0 otherwise);
- outputs pictures in POC order, holding back sps_num_reorder_pics_0 of them.

The firmware sees only what the registers of §5.7 tell it.

---

## 6. Output formats

### 6.1 8-bit streams: plain NV12, written directly

**Yes, an 8-bit stream comes out as plain two-plane NV12** (Y plane, then a separate plane of interleaved Cb and Cr):
- The SAO unit writes it to HEVC_SAO_Y_START_ADDR and HEVC_SAO_C_START_ADDR.
- The same buffers serve as references, read back in two-plane mode through the ANC2AXI table.
- No compressed copy is made: HEVC_SAO_CTRL1 bit 0 is set on GX.
- **No DMC canvases are involved.** For display, the Haiku side can map the planes into canvases of its own as before (base §6).

**Geometry**, for coded width W and height H:

| Item | Value |
|---|---|
| Row pitch, both planes | **ALIGN(W, 32) bytes**. C sets NV12 pitch to 32-byte alignment for HEVC and VP9 (64 for its other codecs) |
| Y plane lines | at least H |
| UV plane lines | H / 2 |
| HEVC_SAO_Y_LENGTH | ALIGN(ALIGN(W, 32) × ALIGN(H, 32), 64 KiB) in mainline and B. C uses ALIGN(W, 64) in the product, which gives the same number for every size in the table below |
| HEVC_SAO_C_LENGTH | half of that |
| GXBB | planes 64 KiB aligned, and C rounds each plane's size up to 64 KiB |

What the hardware does with the two length registers is **UNVERIFIED**. They are presumably write limits.

| Coded size | Pitch | Y bytes (rounded) | UV bytes |
|---|---|---|---|
| 64×64 | 64 | 0x1_0000 | 0x8000 → 0x1_0000 on GXBB |
| 352×288 | 352 | 0x2_0000 | 0x1_0000 |
| 1280×720 | 1280 | 0xF_0000 | 0x7_8000 |
| 1920×1080 (coded 1080, or 1088 with a crop) | 1920 | 0x20_0000 | 0x10_0000 |
| 3840×2160 | 3840 | 0x80_0000 | 0x40_0000 |

- The visible picture is the top-left part after the conformance window.
- Which byte comes first in the UV pair: the 0xFF0 endianness setting of HEVC_SAO_CTRL1 is what Linux uses for V4L2 NV12, where Cb comes first.
- Whether the HEVC core's 64-bit word order matches the canvas endian 7 that VDEC_1 output needs is **UNVERIFIED**: compare on the board.

### 6.2 Compressed references and "double write" (10-bit, and the GX path for it)

**AMFBC.** Amlogic's lossless frame-buffer compression is used for references when the stream is 10-bit (B, C) or when compressed output is wanted (C: AM21B on GXM, AM21C on G12A and later). Sizes, for coded W × H:

| Part | Size |
|---|---|
| body | blocks × ALIGN(W, 64)/64 × ALIGN(H, 32)/32, where a block is 4096 bytes for 10-bit, 3072 for 8-bit without the MMU ("memory saving"), and 3200 for 8-bit with the MMU |
| header | 32 bytes per 128×64 area: 32 × ALIGN(W, 128)/128 × ALIGN(H, 64)/64 |
| one allocation | body + header, rounded up to 64 KiB, with the header directly after the body |

| Example | Body | Header | Allocation |
|---|---|---|---|
| 1920×1080, 10-bit | 0x3F_C000 | 0x1FE0 | 0x40_0000 |
| 1920×1080, 8-bit | 0x2F_D000 | 0x1FE0 | 0x30_0000 |
| 3840×2160, 10-bit | 0xFF_0000 | 0x7F80 | 0x100_0000 |

**Registers for compressed references on GX (no MMU):**
- HEVC_CM_BODY_START_ADDR (0xD898) ← body address, per picture.
- HEVC_SAO_CTRL5 bits 23:16 cleared.
- Decode head of §5.7.2.
- HEVC_SAO_CTRL1 bit 0 clear, so the compressed write is on.
- The ANC2AXI slot of the picture holds the body address.

**Double write.** The NV12 planes are written in addition to the compressed reference, through HEVC_SAO_Y/C_START_ADDR as in §6.1. That is how B and C turn a **10-bit stream into 8-bit NV12** on GX. The compressed bodies are extra allocations, one per picture, that only the decoder uses. V's double-write modes are 0 = off, 1 = full size, 2 = quarter size (V sets HEVC_SAO_CTRL5 bits 23:16 to 0xFF then), and 0x10 = double write only.

**On G12A and later,** HEVC_DBLK_CFGB enables the writes explicitly:
- bit 8 = compressed write;
- bit 9 = uncompressed (double) write;
- bit 4 = a faster deblock pipeline mode, set for widths of 1280 and more;
- base value 0x54 << 8 on G12A and 0xFC << 8 on SM1.

On GX, HEVC_SAO_CTRL1 bits 0 and 1 do the equivalent job, as disables.

### 6.3 The frame MMU (compressed scatter mode)

- In ML, B and C, the MMU is used **only on G12A and later**, and only for compressed references.
- `gxl_hevc_mmu.bin` is used by none of them. Its GX register path (the map address in HEVC_ASSIST_SCRATCH_7, and HEVC_SAO_MMU_VH0/VH1) exists in B's code but is unreachable.
- C's patch 0127 says the frame MMU used by the AM21C scatter layout "does not work on GX". The non-MMU compressed frames that GX does produce use the basic layout that the display overlay can read.

### 6.4 What gxl_hevc.bin supports, according to the sources

- 8-bit Main: NV12 references and output (B, C).
- 10-bit Main 10: non-MMU compressed references with 8-bit NV12 double write (B, C; C also on GXBB).
- Compressed 8-bit export without NV12 (C on GXM).
- Sizes: B allows up to 3840×2160. C allows up to 4096×2304 on GXBB after patch 0128; before that patch C limited GXBB HEVC to 1920×1088.
- Not supported by these drivers: the frame MMU on GX; long-term references (C refuses them; B ignores them); pictures one CTB wide and several tall (C).

---

## 7. Interrupts

- **Which GIC interrupt.** The HEVC core has **no interrupt line of its own in any mainline device tree**. The GX vdec node lists only "vdec" (SPI 44, edge rising) and "esparser" (SPI 32), and G12A lists the same two.
  - Mainline, B and C request only these two. VP9 on GXL, GXM, G12A and SM1, and HEVC in B and C (C tested on GXBB), all deliver their reports through the **"vdec" interrupt, SPI 44 (INTID 76)**.
  - So the HEVC core's mailbox 1 reaches the same GIC line as VDEC_1's mailbox 1. This is inferred from these drivers working, not stated anywhere.
  - V's GXBB kernel asks for the same interrupt index for HEVC as for H.264. V's device tree lists mailbox_0, mailbox_1 and mailbox_2 as SPI 43, 44 and 45. Which index maps to which name in V is **UNVERIFIED**.
- **Mask and clear:**

| Operation | Register | Offset | Value |
|---|---|---|---|
| clear a stale request, at codec start | HEVC_ASSIST_MBOX1_CLR_REG | 0xC1D4 | write 1 |
| enable | HEVC_ASSIST_MBOX1_MASK | 0xC1D8 | write 1 |
| disable, at stop | HEVC_ASSIST_MBOX1_MASK | 0xC1D8 | write 0 |
| acknowledge in the handler | **none** | | Mainline VP9, B, C and V's vh265 never write HEVC_ASSIST_MBOX1_CLR_REG in their handlers, while every VDEC_1 handler writes ASSIST_MBOX1_CLR_REG. Presumably the firmware drops its request when the host writes the status register (**UNVERIFIED**) |

- **No status register** other than HEVC_DEC_STATUS_REG is read.
- **Software re-trigger (V only).** V writes 1 to a register it calls HEVC_ASSIST_MBOX1_IRQ_REG to make its own handler run again, for example after freeing a buffer. That register is not in mainline's headers; by the pattern of base §9.1 it would be at 0xC1D0 (**UNVERIFIED**).
- **Host to firmware.** HEVC_MCPU_INTR_REQ (0xCC14) ← 0x4 (§5.5).
- **On this board.** Base §10 found the vdec interrupt arriving as short pulses, often seen as spurious, and polled instead. HEVC_DEC_STATUS_REG is easy to poll: after the host writes 0x05 or 0xFF, a report shows up as a change to 0x08, 0x0A, 0x0B, 0x20 or 0x22. The register keeps 0x08 until the host answers. Note that 0x20 and 0x22 can be transient (§5.4).

---

## 8. Register reference

**Sources:** "ML hevc" = mainline hevc_regs.h; "ML dos" = mainline dos_regs.h; "B" = added to hevc_regs.h by B; "C" = C's headers or local definitions. **Used by:** B, C, and V9 = mainline's codec_vp9 / vdec_hevc / codec_hevc_common.

### 8.1 HEVC core registers

| Name | Byte offset | Word | Source | Used by | Use here |
|---|---|---|---|---|---|
| HEVC_ASSIST_MMU_MAP_ADDR | 0xC024 | 0x3009 | ML hevc | V9, B, C (G12A+) | frame MMU map |
| HEVC_ASSIST_MBOX1_CLR_REG | 0xC1D4 | 0x3075 | ML hevc | V9, B, C | §7 |
| HEVC_ASSIST_MBOX1_MASK | 0xC1D8 | 0x3076 | ML hevc | V9, B, C | §7 |
| HEVC_ASSIST_SCRATCH_0 … _N | 0xC300 … 0xC35C | 0x30C0 … 0x30D7 | ML hevc | V9, B, C | §5.1 (G unused) |
| HEVC_PARSER_VERSION | 0xC400 | 0x3100 | ML hevc | (V: liveness probe) | §5.8 step 2 |
| HEVC_STREAM_CONTROL | 0xC404 | 0x3101 | ML hevc | V9, B, C | §3.1, §3.2 |
| HEVC_STREAM_START_ADDR | 0xC408 | 0x3102 | ML hevc | V9, B, C | §3.1 |
| HEVC_STREAM_END_ADDR | 0xC40C | 0x3103 | ML hevc | V9, B, C | §3.1 |
| HEVC_STREAM_WR_PTR | 0xC410 | 0x3104 | ML hevc | V9, B, C | §3.1 |
| HEVC_STREAM_RD_PTR | 0xC414 | 0x3105 | ML hevc | V9, B, C | §3.1 |
| HEVC_STREAM_LEVEL | 0xC418 | 0x3106 | ML hevc | V9, B, C | flow control |
| HEVC_STREAM_FIFO_CTL | 0xC41C | 0x3107 | ML hevc | V9, B, C | §3.2 |
| HEVC_SHIFT_CONTROL | 0xC420 | 0x3108 | ML hevc | V9, B, C | §3.3 |
| HEVC_SHIFT_STARTCODE | 0xC424 | 0x3109 | ML hevc | V9 (V: probe) | §3.3 |
| HEVC_SHIFT_EMULATECODE | 0xC428 | 0x310A | ML hevc | (V: probe) | §3.3 |
| HEVC_SHIFT_STATUS | 0xC42C | 0x310B | ML hevc | V9, B, C | §3.3 |
| HEVC_SHIFT_BYTE_COUNT | 0xC434 | 0x310D | ML hevc | B, C | §3.5 |
| HEVC_CABAC_CONTROL | 0xC440 | 0x3110 | ML hevc | V9, B, C | §3.3 |
| HEVC_PARSER_CMD_WRITE | 0xC448 | 0x3112 | ML hevc | V9, B, C | §3.3 |
| HEVC_PARSER_CORE_CONTROL | 0xC44C | 0x3113 | ML hevc | V9, B, C | §3.3 |
| HEVC_PARSER_INT_CONTROL | 0xC480 | 0x3120 | ML hevc | V9, B, C | §3.3 |
| HEVC_PARSER_IF_CONTROL | 0xC488 | 0x3122 | ML hevc | V9, B, C | §3.3 |
| HEVC_PARSER_PICTURE_SIZE | 0xC48C | 0x3123 | ML hevc | V9 only | |
| HEVC_PARSER_LCU_START | 0xC490 | 0x3124 | ML hevc | C | §5.5 (bits 23:0 = current CTB) |
| HEVC_PARSER_CMD_SKIP_0 / _1 / _2 | 0xC4A0 / 0xC4A4 / 0xC4A8 | 0x3128–0x312A | ML hevc | V9, B, C | §3.3 |
| HEVC_MPRED_CTRL0 | 0xC804 | 0x3201 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_CTRL1 | 0xC808 | 0x3202 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_PIC_SIZE | 0xC814 | 0x3205 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_PIC_SIZE_LCU | 0xC818 | 0x3206 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_TILE_START | 0xC81C | 0x3207 | ML hevc | C | §5.7.4 |
| HEVC_MPRED_TILE_SIZE_LCU | 0xC820 | 0x3208 | ML hevc | C | §5.7.4 |
| HEVC_MPRED_REF_NUM | 0xC824 | 0x3209 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_REF_EN_L0 / _L1 | 0xC830 / 0xC834 | 0x320C / 0x320D | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_ABV_START_ADDR | 0xC848 | 0x3212 | ML hevc | V9, B, C | §5.7.4 |
| HEVC_MPRED_MV_WR_START_ADDR | 0xC84C | 0x3213 | ML hevc | V9, B, C | §5.7.4 |
| HEVC_MPRED_MV_RD_START_ADDR | 0xC850 | 0x3214 | ML hevc | V9, B, C | §5.7.4 |
| HEVC_MPRED_MV_WPTR | 0xC854 | 0x3215 | ML hevc | V9, B, C | §5.7.4 |
| HEVC_MPRED_MV_RPTR | 0xC858 | 0x3216 | ML hevc | V9, B, C | §5.7.4 |
| HEVC_MPRED_MV_WR_ROW_JUMP / _RD_ROW_JUMP | 0xC85C / 0xC860 | 0x3217 / 0x3218 | ML hevc | C | §5.7.4 |
| HEVC_MPRED_CURR_LCU | 0xC864 | 0x3219 | ML hevc | B, C (read only) | §5.7.4 |
| HEVC_MPRED_CTRL3 | 0xC874 | 0x321D | ML hevc | V9 only | |
| HEVC_MPRED_L0_REF00_POC (+4i, 16 entries) | 0xC880 | 0x3220 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_L1_REF00_POC (+4i, 16 entries) | 0xC8C0 | 0x3230 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_CTRL4 | 0xC930 | 0x324C | ML hevc | V9 only | |
| HEVC_MPRED_CUR_POC | 0xC980 | 0x3260 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_COL_POC | 0xC984 | 0x3261 | ML hevc | B, C | §5.7.4 |
| HEVC_MPRED_MV_RD_END_ADDR | 0xC988 | 0x3262 | ML hevc | V9, B, C | §5.7.4 |
| HEVC_MPSR | 0xCC04 | 0x3301 | ML hevc | V9, B, C | §1.3, §2.2 |
| HEVC_MCPU_INTR_MSK | 0xCC10 | 0x3304 | ML hevc | none | |
| HEVC_MCPU_INTR_REQ | 0xCC14 | 0x3305 | ML hevc | B, C | §5.5 |
| HEVC_CPSR | 0xCC84 | 0x3321 | ML hevc | V9, B, C | §2.2 |
| HEVC_IMEM_DMA_CTRL | 0xCD00 | 0x3340 | ML hevc | V9, B, C | §2.2 |
| HEVC_IMEM_DMA_ADR | 0xCD04 | 0x3341 | ML hevc | V9, B, C | §2.2 |
| HEVC_IMEM_DMA_COUNT | 0xCD08 | 0x3342 | ML hevc | V9, B, C | §2.2 |
| HEVC_LMEM_DMA_CTRL | 0xCD40 | 0x3350 | C | C | §1.4 |
| HEVC_WRRSP_LMEM | 0xCD4C | 0x3353 | C | C | §1.4 |
| HEVCD_IPP_TOP_CNTL | 0xD000 | 0x3400 | ML hevc | V9, B, C | §3.3 |
| HEVCD_IPP_LINEBUFF_BASE | 0xD024 | 0x3409 | ML hevc | V9, B, C | §4.1 |
| HEVCD_IPP_AXIIF_CONFIG | 0xD02C | 0x340B | ML hevc | V9, B, C | §5.7.5 |
| VP9D_MPP_REF_SCALE_ENBL, _REFINFO_TBL_ACCCONFIG, _REFINFO_DATA | 0xD104, 0xD108, 0xD10C | 0x3441–0x3443 | ML hevc | V9 only | |
| HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR | 0xD180 | 0x3460 | ML hevc | V9, B, C | §5.7.1 |
| HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR | 0xD184 | 0x3461 | ML hevc | V9, B, C (**GXBB**) | §5.7.1 |
| HEVCD_MPP_ANC2AXI_TBL_DATA | 0xD190 | 0x3464 | ML hevc | V9, B, C (GXL+) | §5.7.1 |
| HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR | 0xD300 | 0x34C0 | ML hevc | V9, B, C | §5.7.1, §5.7.3 |
| HEVCD_MPP_ANC_CANVAS_DATA_ADDR | 0xD304 | 0x34C1 | ML hevc | V9, B, C | §5.7.1, §5.7.3 |
| HEVCD_MPP_DECOMP_CTL1 | 0xD308 | 0x34C2 | ML hevc | V9, B, C | §5.7.2 |
| HEVCD_MPP_DECOMP_CTL2 | 0xD30C | 0x34C3 | ML hevc | V9, B, C | §5.7.2 |
| HEVCD_MCRCC_CTL1 / 2 / 3 | 0xD3C0 / 0xD3C4 / 0xD3C8 | 0x34F0–0x34F2 | ML hevc | V9, B, C | §5.7.3 |
| HEVC_DBLK_CFG1 | 0xD404 | 0x3501 | ML hevc | B, C | §5.7.5 |
| HEVC_DBLK_CFG2 | 0xD408 | 0x3502 | ML hevc | B, C | §5.7.5 |
| HEVC_DBLK_CFG4 | 0xD410 | 0x3504 | ML hevc | V9, B, C | §4.1 |
| HEVC_DBLK_CFG5 | 0xD414 | 0x3505 | ML hevc | V9, B, C | §4.1 |
| HEVC_DBLK_CFG9 | 0xD424 | 0x3509 | ML hevc | V9 (another meaning), B, C | §5.7.5 |
| HEVC_DBLK_CFGA | 0xD428 | 0x350A | ML hevc | V9 only | |
| HEVC_DBLK_CFGB (same offset as HEVC_DBLK_STS0 in the header) | 0xD42C | 0x350B | ML hevc | V9; B and C on G12A+ | §6.2 |
| HEVC_DBLK_CFGE | 0xD438 | 0x350E | ML hevc | V9, B, C (G12A+) | §4.1 |
| (B: HEVC_DBLK_STS1 + 16; C: HEVC_G12A_DBLK_STS1) | 0xD440 | 0x3510 | C | B, C (G12A+, bit 28) | |
| HEVC_SAO_CTRL0 | 0xD804 | 0x3601 | ML hevc | B, C | §5.7.5 |
| HEVC_SAO_CTRL1 | 0xD808 | 0x3602 | ML hevc | V9, B, C | §5.7.5 |
| HEVC_SAO_PIC_SIZE | 0xD814 | 0x3605 | ML hevc | B, C | §5.7.5 |
| HEVC_SAO_PIC_SIZE_LCU | 0xD818 | 0x3606 | ML hevc | B, C | §5.7.5 |
| HEVC_SAO_Y_START_ADDR | 0xD82C | 0x360B | ML hevc | V9, B, C | §5.7.5 |
| HEVC_SAO_Y_LENGTH | 0xD830 | 0x360C | ML hevc | V9, B, C | §5.7.5 |
| HEVC_SAO_C_START_ADDR | 0xD834 | 0x360D | ML hevc | V9, B, C | §5.7.5 |
| HEVC_SAO_C_LENGTH | 0xD838 | 0x360E | ML hevc | V9, B, C | §5.7.5 |
| HEVC_SAO_Y_WPTR | 0xD83C | 0x360F | ML hevc | V9, B, C | §5.7.5 |
| HEVC_SAO_C_WPTR | 0xD840 | 0x3610 | ML hevc | V9, B, C | §5.7.5 |
| HEVC_SAO_CTRL5 | 0xD88C | 0x3623 | ML hevc | V9, B, C | §5.7.5, §6.2 |
| HEVC_CM_BODY_START_ADDR | 0xD898 | 0x3626 | ML hevc | V9, B, C | §6.2 |
| HEVC_CM_BODY_LENGTH | 0xD89C | 0x3627 | ML hevc | V9, B, C | §5.7.2 |
| HEVC_CM_HEADER_START_ADDR | 0xD8A0 | 0x3628 | ML hevc | V9, B, C (MMU) | |
| HEVC_CM_HEADER_LENGTH | 0xD8A4 | 0x3629 | ML hevc | V9, B, C | §5.7.2 |
| HEVC_CM_HEADER_OFFSET | 0xD8AC | 0x362B | ML hevc | V9, B, C | §5.7.2 |
| HEVC_SAO_CTRL9 | 0xD8B4 | 0x362D | **B** | B, C (G12A+ MMU) | |
| HEVC_SAO_MMU_VH0_ADDR / VH1_ADDR | 0xD8E8 / 0xD8EC | 0x363A / 0x363B | ML hevc | V9, B, C (MMU) | §4.1 |
| HEVC_IQIT_SCALELUT_WR_ADDR | 0xDC08 | 0x3702 | ML hevc | B, C | §5.2 |
| HEVC_IQIT_SCALELUT_DATA | 0xDC10 | 0x3704 | ML hevc | B, C | §5.2 |
| HEVC_PSCALE_CTRL | 0xE444 | 0x3911 | ML hevc | V9, B, C | §5.2 |
| HEVC_SAO_MMU_RESET_CTRL | 0x9904 | 0x2641 | C | C (G12 only) | §1.5 |

hevc_regs.h defines further HEVC registers (parser status and info, SAO interface, other MPRED, deblock and SAO control registers, IQIT clock and reset) that no HEVC or VP9 path writes.

### 8.2 DOS-wide registers used by the HEVC path (ML dos_regs.h)

| Name | Byte offset | Word | Use |
|---|---|---|---|
| DOS_GEN_CTRL0 | 0xFC08 | 0x3F02 | 6 = ring read pointer from the HEVC core (§3.2); 0 = VDEC_1 |
| DOS_MEM_PD_HEVC | 0xFCCC | 0x3F33 | 0 = HEVC memories on, 0xFFFF_FFFF = off |
| DOS_SW_RESET3 | 0xFCD0 | 0x3F34 | HEVC resets (§1.3, §1.6) |
| DOS_GCLK_EN3 | 0xFCD4 | 0x3F35 | 0xFFFF_FFFF = HEVC clock gates on |

### 8.3 Registers outside DOS

| Name | Physical | Use |
|---|---|---|
| HHI_VDEC2_CLK_CNTL | 0xC883_C1E4 | HEVC clock (§1.1) |
| AO_RTI_GEN_PWR_SLEEP0 | 0xC810_00E8 | bits 7:6 = HEVC power (§1.2) |
| AO_RTI_GEN_PWR_ISO0 | 0xC810_00EC | bits 11:10 = HEVC isolation (§1.2) |
| ESPARSER, RESET1, canvas LUT | as base §9 | parser unchanged; canvases not used by HEVC |

---

## 9. Everything the sources say about GXBB and the HEVC core

1. **Mainline:**
   - GXBB's format table has H.264, MPEG-1 and MPEG-2, and no HEVC or VP9.
   - `vdec_hevc.c` has no GXBB branch: GXBB would take the GXL path, with SLEEP0 bits 7:6, ISO0 bits 11:10 and one HEVC clock.
   - `codec_hevc_common.c` has a **GXBB-only** way of filling the reference address table: commands through HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR with the slot number in bits 15:8 and a valid bit, instead of the GXL data port. Mainline never reaches it.
   - The GXBB DT lists the `vdec_hevc` clock, and `gxbb.c` defines it exactly like GXL's.
2. **B:** the commit message reads "Add initial HEVC codec for the Amlogic GXBB/GXL/GXM SoCs". The GXBB table entry is HEVC with `gxl_hevc.bin`, at most 3840×2160, 4 to 24 capture buffers, NV12M capture only. B uses mainline's GXBB table path.
3. **C:**
   - GXBB HEVC: Main and Main 10, `gxl_hevc.bin`, 16 to 24 capture buffers, NV12M only. The size limit was 1920×1088, raised to 4096×2304 by the TESTING patch 0128.
   - Every HEVC reference plane must be **64 KiB aligned on GXBB**, because only address bits 31:16 survive the table command. NV12M plane sizes are rounded up to 64 KiB for GXBB HEVC.
   - All 24 table slots are programmed on GXBB.
   - The GX firmware moves the RPM pointer from scratch 1 to scratch 8 when it boots, so the workspace pointers are not rewritten on GX.
   - The GX sub-engine reset set is 0x000E_D918.
   - There is no DMC parking on GX.
   - GXBB and GXLX have no VP9 (also dt-binding patch 0126).
   - Patch 0128: on a WeTek Play 2 (GXBB), 4K HEVC Main decodes bit-exact with software at 33 frames/s, and Main 10 decodes at 50 frames/s.
4. **V (ODROID-C2 vendor kernel):**
   - HEVC capability string for GXBB and later: "4k, 9bit, 10bit, dwrite, compressed".
   - HEVC and H.264 use the same interrupt index.
   - V's own firmware build reports parameters in a layout with 4 tile entries per axis instead of 8.
   - The vendor reset set is the same as C's GX set.
5. **No source states** a GXBB-specific HEVC firmware, a different register meaning on GXBB other than the table command, or that HEVC is unavailable on GXBB.

---

## 10. Open questions

### 10.1 Only a test on the GXBB board can answer

1. **Power and isolation:**
   - Does the HEVC core come up with SLEEP0 bits 7:6 and ISO0 bits 11:10?
   - Do HEVC_PARSER_VERSION (0x0001_0001), HEVC_SHIFT_STARTCODE (0x100) and HEVC_SHIFT_EMULATECODE (0x300) read as V expects?
   - Does the VDEC_1 path's whole-register ISO0 write disturb HEVC (§1.2)?
   - Are register reads well-behaved while the HEVC core is still isolated or powered off? (Do not read before step 8 of §1.3.)
2. **Clock:** does 666.67 MHz from fclk_div3 on HHI_VDEC2_CLK_CNTL run reliably on this board, which ran VDEC_1 at the same rate?
3. **Firmware load:** does the HEVC IMEM DMA finish within mainline's 100 polls, or does it need C's longer wait? Is `gxl_hevc.bin` accepted by the GXBB HEVC AMRISC? C's GXBB tests say yes.
4. **First report:** does the firmware report 0x08 for a tiny IDR picture with NAL_SEARCH_CTL = 0xC? Is the RPM layout the 8-tile layout of §5.6 (check width, height, CTB size, nal_unit_type and slice_type against the test stream)?
5. **HEVC_SHIFT_BYTE_COUNT:** does it read 0 after power-up and count exactly the bytes the parser wrote, padding included (so that endpoints line up)?
6. **Interrupt:** does the HEVC report arrive on SPI 44 (INTID 76) on GXBB, rather than SPI 43 or 45? Does the line ever need HEVC_ASSIST_MBOX1_CLR_REG written by the host? Is the pulse as short as VDEC_1's, making polling HEVC_DEC_STATUS_REG the practical choice?
7. **Alignment:**
   - Does the HEVC workspace need more than page alignment (the H.264 workspace needed 8 KiB on this board)?
   - Do the MV and AUX buffers?
   - Is 64 KiB plane alignment really required on GXBB? C enforces it from the command format, but a single test with a misaligned plane would show it.
8. **Completion:** after the IDR picture's slice data, which status comes when HEVC_DECODE_SIZE = E (expected 0x0A with HEVC_PARSER_LCU_START = CTB count − 1), and which when it is 0: 0x20 or 0x22, nothing, or 0x09? How long does a small intra picture take?
9. **Feeding:** which feed works with the HEVC stream fetcher: base §3.4 (4 KiB minimum pack, marker after it) or C's 4 KiB zero tail inside the pack? Does any trailer byte reach the HEVC shifter? Does HEVC_STREAM_FIFO_CTL bit 29 matter?
10. **Output:**
    - Is the NV12 output bit-exact against a software decoder for 8-bit?
    - Is the row pitch ALIGN(W, 32) for widths that are not multiples of 64?
    - Is the UV byte order Cb first?
    - Does the canvas endian setting the display needs (7 for VDEC_1's output) also suit the HEVC core's output with HEVC_SAO_CTRL1 = 0xFF0 in bits 11:4?
11. **The ANC2AXI table:** are unused slots harmless if left unprogrammed (mainline's GXBB form), or must all 24 be valid (C)?
12. **Minimum size:** what is the smallest picture the firmware accepts, and is the "one CTB wide, several tall" restriction real on GXBB?
13. **First session after boot:** does the HEVC core show a first-session defect like the one VDEC_1 showed (base §10, H.264 §16)?
14. **Switching:** after an HEVC session, does a VDEC_1 session work again once DOS_GEN_CTRL0 is back at 0 and the HEVC block is powered off?

### 10.2 Questions about meanings (not needed for the test)

15. DOS_SW_RESET3 bits other than 3 and 14, especially 11 and 12, which are pulsed before MPSR = 1.
16. HEVC_IMEM_DMA_CTRL bits 18:16 = 7 (the same as base question 1).
17. HEVC_PARSER_INT_CONTROL bits 31:26; the meanings of HEVC_PARSER_CMD_WRITE, the 37 command values and the SKIP registers; HEVCD_IPP_AXIIF_CONFIG bits 3:0, 5:4 and 12; the HEVC_SAO_CTRL1 endianness field.
18. HEVC_WAIT_FLAG bit 0 (written 1 at start) and the exact meaning of bit 1.
19. HEVC_AUX_DATA_SIZE's two halves, and what the firmware writes into the AUX buffer.
20. RPM fields profile_etc, sei_frame_field_info, video_signal_type and color_description, and the exact encoding of the temporal-ID field.
21. The first-word difference between `gxl_hevc.bin` and the MMU builds; what `gxl_hevc_mmu.bin` would need on GX (C's note says the frame MMU does not work on GX).
22. Status codes 0x01–0x04, 0x06, 0x07, 0x09, 0x0C and 0x0D in gxl_hevc.bin, and what the firmware does after 0x0B.
23. HEVC_SAO_Y_LENGTH and HEVC_SAO_C_LENGTH: limits or plane sizes?
24. The reset value of HEVC_DBLK_CFGB on GX, and whether HEVC needs it set.
25. Whether the 16 MiB ring is needed for HEVC at higher bit rates, or 8 MiB is enough as for VDEC_1.

---

## 11. Found on the board (ODROID-C2, 2026-10-30)

Observations from Haiku's experimental `meson_vdec` HEVC mode (haiku 37e84ef42e), not facts from Linux. They answer the first questions of §10.1.

| §10.1 item | Found |
|---|---|
| 1 (liveness) | After the §1.3 power-up: HEVC_PARSER_VERSION 0x00010001, HEVC_SHIFT_STARTCODE 0x100, HEVC_SHIFT_EMULATECODE 0x300 |
| 2 (firmware load) | The IMEM DMA finishes at once, for `gxl_hevc.bin` and the vendor microcode alike |
| 3 (first report) | Status 0x08 arrives about 9 ms after the first write. The RPM matches the **8-tile layout** for `gxl_hevc.bin`: slice type 2, NAL type 20, address 0, the stream's width and height, CTB 64, first_slice 1, bit depth 0, chroma 1. The vendor microcode's RPM matches the same fields 8 places lower from misc_flag0 on |
| 4 (byte count) | HEVC_SHIFT_BYTE_COUNT reads 0 after the start and counts from there |
| 5 (interrupt) | Not tried: the mailbox stays masked (HEVC_ASSIST_MBOX1_MASK = 0) and the host polls HEVC_DEC_STATUS_REG. It reads 0xFF right after the start, then 0x08 at each slice header |
| 6 (alignment) | 64 KiB aligned planes, a 1 MiB aligned workspace (buffers aligned to their size, at most 1 MiB, as for H.264) |
| 7 (status after the data, no end position) | After the last picture's data and an end-of-sequence NAL (00 00 01 48 01), the firmware reports status **0x09** with HEVC_DECODE_MODE (scratch J) = 0x24, NAL type 36. HEVC_PARSER_LCU_START bits 23:0 = CTB count − 1 by then: the picture is complete. Between pictures, the next picture's 0x08 report marks the previous one complete |
| 8 (padding) | The ESPARSER scheme of base §3.4 (4 KiB minimum, `00 00 01 FF` after the pack) works; the end-of-sequence NAL is written the same way |
| 9 (output) | NV12, Cb first, row pitch ALIGN(W, 32), with HEVC_SAO_CTRL1 = 0xFF0 \| 1: **bit for bit** equal to FFmpeg for 320×240 (1 and 10 intra pictures) and 1920×1080 (30 intra pictures), with `gxl_hevc.bin`; the vendor microcode too (one picture). Read through the CPU after a cache invalidate; display not tried |
| 10 (unused table slots) | Programming all 48 slots (unused ones pointing at frame 0, C's way) works |
| 12 (first session) | No first-session defect: the first HEVC session after each of two boots was bit exact |
| 13 (VDEC_1 after HEVC) | H.264 decodes bit exact after HEVC sessions in the same boot |

**A trap: the powered-down block's registers.** In an HEVC session, VDEC_1 stays powered down and isolated. Any CPU read of a VDEC_1 register (VLD_MEM_VIFIFO_LEVEL, for one) then hangs the whole system, KDL included; only a power cycle helped. A driver that has both blocks must touch only the registers of the block that is powered.

**Speed:** 1920×1080 intra pictures at 20 Mbit/s decode at about 60–64 frames/s (no output written), with HEVC at 666.7 MHz.

### 11.1 Full decoding (haiku c918a3cfdd, 2026-10-30)

Observations from the finished HEVC mode: P and B pictures, 10 bits, the interrupt. Not facts from Linux.

- **Frames in the reference table:** NV12 frames take two slots each (Y in 2k, CbCr in 2k + 1); compressed frames (10-bit) take one (k). A reference list word is `(c << 16) | (c << 8) | y` with the frame's slots; compressed: y = c = k.
- **10 bits:** the references compressed (a body of 4096 bytes per 64×32 block, then a header of 32 bytes per 128×64), HEVCD_MPP_DECOMP_CTL1 = 0, DECOMP_CTL2 = body size >> 5, HEVC_CM_BODY_LENGTH, HEVC_CM_HEADER_OFFSET (= the body size) and HEVC_CM_HEADER_LENGTH, HEVC_CM_BODY_START_ADDR = the frame, HEVC_SAO_CTRL1 = 0xFF0 (bit 0 clear). The NV12 "double write" next to it is 8-bit and rounded, `(x + 2) >> 2`: it equals FFmpeg's 10-bit output rounded to 8 bits, every frame.
- **Bit for bit like FFmpeg (8-bit):** B-pyramids with 4 references (720p), 4 slices a picture, P only, 1080p, and a stream starting at a CRA picture (its RASL pictures skipped, as FFmpeg does).
- **The interrupt:** with HEVC_ASSIST_MBOX1_MASK = 1, every firmware report raises SPI 44 (INTID 76, shared with VDEC_1). HEVC_ASSIST_MBOX1_CLR_REG = 1 acknowledges it.
- **Speed:** 1080p 8-bit decodes at about 277 frames/s (no output written).
- **Stopping in the middle of a picture:** with HEVC_MPSR = 0, then a DOS_SW_RESET3 0x000ED918 pulse and the power-down right after, the whole board hung once, KDL included, in a test of seeks (it is a session stop and a new start each); a run of the same six seeks right after passed. Every stop logged in that run came while the status register read 5 (the host's "decode slice") and the CTB counter was partway through the picture. Letting the firmware finish the slice first (until the status leaves 5), with no reset pulse at the stop, as the vendor's Android driver does: 216 seeks without a hang.
- **A slice whose data is cut off** (the host stops writing after it) stays at status 5 and its CTB counter stops moving: it waits for more of the stream. HEVC_PARSER_LCU_START read 0x400001B7 in that state; bit 30 is set during decoding (its meaning is **UNVERIFIED**).

---

## Sources

**Mainline Linux** (torvalds/linux master, HEAD a9547379cb38, 2026-10-30), fetched raw:
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_hevc.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_hevc.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/hevc_regs.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/codec_vp9.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/codec_hevc_common.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/codec_hevc_common.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/dos_regs.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_1.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_helpers.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_platform.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_platform.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/esparser.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/meson/gxbb.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/meson/g12a.c (HEVC clocks on G12A only)
- https://raw.githubusercontent.com/torvalds/linux/master/include/dt-bindings/clock/gxbb-clkc.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/pmdomain/amlogic/meson-ee-pwrc.c
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gx.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxbb.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxl.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-g12-common.dtsi
- Commit history via https://api.github.com/repos/torvalds/linux/commits?path=drivers/staging/media/meson/vdec (last change b3a805dd5a83, 2026-10-04)

**B: the out-of-tree stateful HEVC codec:**
- https://raw.githubusercontent.com/chewitt/linux/amlogic-7.2.y/drivers/staging/media/meson/vdec/codec_hevc.c (branch head 61cde89f8386; file last changed in 8f424f83e565, 2026-09-04, "multiple fixes for HEVC codec", Marek Czerski), plus codec_hevc.h, codec_hevc_common.c/.h, hevc_regs.h, vdec_hevc.c, vdec_platform.c, esparser.c and vdec_helpers.c from the same branch
- The same code as LibreELEC.tv b91353c808a8 (the last LibreELEC commit before the switch to the new driver): https://raw.githubusercontent.com/LibreELEC/LibreELEC.tv/b91353c808a8f51428d317f64d6fa67a0f6195ac/projects/Amlogic/devices/AMLGX/patches/linux/ patches `amlogic-0051-FROMLIST-v2-media-meson-vdec-implement-10bit-bitstre.patch`, `amlogic-0052-FROMLIST-v2-media-meson-vdec-add-HEVC-decode-codec.patch` (Maxime Jourdan and Benjamin Roszak, 2023-01-23) and `amlogic-0066-WIP-media-meson-vdec-multiple-fixes-for-HEVC-codec.patch`. Applied to mainline, they give the chewitt file apart from a leak fix and two helper-call arguments.

**C: the meson-amvdec successor**, LibreELEC.tv a8aae15e27b2 (2026-10-10), https://raw.githubusercontent.com/LibreELEC/LibreELEC.tv/a8aae15e27b291ad4ee03832cc0ce583f1ff48d9/projects/Amlogic/patches/linux/:
- `amlogic-0119-media-amlogic-add-the-video-decoder-core.patch`
- `amlogic-0120-…MPEG-2…`, `amlogic-0121-…H.264…` (applied only to rebuild the tree)
- `amlogic-0122-media-amlogic-amvdec-add-HEVC-decoding.patch`
- `amlogic-0123-media-amlogic-amvdec-add-VP9-decoding.patch`
- `amlogic-0126-dt-bindings-media-amlogic-gx-vdec-VP9-is-not-availab.patch`
- `amlogic-0127-media-amlogic-amvdec-export-HEVC-basic-layout-compre.patch`
- `amlogic-0128-TESTING-media-amlogic-amvdec-allow-4K-HEVC-on-GXBB.patch`
- `amlogic-0209-media-amlogic-amvdec-add-S4-support.patch` (only for the remark about firmware-dependent parameter layouts)

**V: the ODROID-C2 vendor kernel** (hardkernel/linux branch odroidc2-3.14.y), for bit names and GXBB cross-checks only:
- https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/amlogic/amports/vh265.c
- https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/amlogic/amports/vh264.c (interrupt index only)
- https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/amlogic/amports/esparser.c
- https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/amlogic/amports/streambuf.c
- https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/amlogic/amports/vdec.c and vdec.h
- https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/arch/arm64/boot/dts/meson64_odroidc2.dts

**Firmware:** `/lib/firmware/meson/vdec/{gxl_hevc,gxl_hevc_mmu,g12a_hevc_mmu,gxl_vp9}.bin.zst` from the host's linux-firmware 20260319.git217ca6e4, decompressed to a temporary directory and examined locally (sizes, md5, word statistics). Nothing was written to the repository.
