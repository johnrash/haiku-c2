Facts read from Amlogic's GPL vendor driver (`drivers/amlogic/amports/`) in Hardkernel's ODROID-C2 kernel `odroidc2-3.14.y` at c3e4c730feb1 (2018-03-03); no code copied.

# M26: S905 (GXBB) HEVC decoding in Amlogic's vendor driver, hardware facts

This document describes how Amlogic's own kernel drove the S905's HEVC decoder (VDEC_HEVC). That kernel played HEVC in hardware on the ODROID-C2.

**Related documents:**
- `m26-vdec-facts.md` is the **base document**, cited as "base §N". It covers the mainline VDEC_1 path, the ESPARSER, the canvas LUT and the shared interrupt. None of that is repeated here.
- The mainline-family HEVC driver (the out-of-tree codec_hevc.c) is covered in its own document. §7 only lists the differences.

**Conventions (as in the base document):**
- DOS offsets are **byte offsets from the DOS base 0xC8820000**. Amlogic's headers number DOS registers by 32-bit word index; byte offset = word index × 4. §9 gives both.
- "Set bits" and "clear bits" mean read-modify-write; "write" means a full 32-bit store.
- **UNVERIFIED** marks a meaning the vendor source does not state, or one that is inferred.

**Provenance:**
- The vendor files used:
  - `vh265.c` (blob 472c128ddb91, h265_version 201510011);
  - `vdec.c`, `amvdec.c`, `esparser.c`, `streambuf.c`, `amstream.c`;
  - `arch/clkgx.c`, `arch/clk.c`, `arch/firmware.c`, `arch/register_ops_m8.c`, `arch/register_map.c`;
  - `arch/regs/*.h`;
  - the C2 device tree `meson64_odroidc2.dts`.
- The same `vh265.c` blob is on `odroidc2-v3.16.y`.
- Hardkernel's Android M branch has a later `vh265.c` (h265_version 201512091) and a later microcode. Where the two drivers differ, the text says so (§5.12).
- The microcode was extracted to `firmware/meson/vdec/vendor/` for a test (it worked: §11 of `docs/m26-vdec-hevc-facts.md`) and deleted on 2026-10-30. It was research material only and must not ship.
- Nothing in this document has been run on the board yet.

---

## 0. At a glance

| Item | Vendor driver on GXBB |
|---|---|
| Microcode | one 16 KiB image, `vh265_mc` (3,445 words used), registered for M8, M8M2 and GXBB alike. Loaded once by IMEM DMA (§2) |
| Power island | AO_RTI_GEN_PWR_SLEEP0 bits 7:6, AO_RTI_GEN_PWR_ISO0 bits 11:10, DOS_MEM_PD_HEVC, DOS_SW_RESET3 (§1) |
| Clock | HHI_VDEC2_CLK_CNTL bits 31:16 (bit 24 = enable). 500 MHz at power-up, then 100 to 648 MHz chosen from resolution × fps (§1.2) |
| Stream input | the ESPARSER writes into the HEVC stream ring (HEVC_STREAM_*), with DOS_GEN_CTRL0 = 3<<1 (§3) |
| Workspace | one region of 0x12DB100 bytes (4K layout) at the start of the decoder memory, plus a separate 256-byte RPM buffer (§4) |
| Host protocol | status in HEVC_ASSIST_SCRATCH_0; the host answers by writing a command there, then 4 to HEVC_MCPU_INTR_REQ (§5) |
| Parameters | 128 halfwords (RPM) DMA'd by the microcode to a host buffer, with halfwords reversed within each 64-bit group (§5.8) |
| Reference frames | always compressed ("lossless compression", 4 KiB or 3 KiB per 64×32 block), unless double-write-only mode is chosen (§5.10) |
| Output | compressed by default, displayed through the VPU's AFBC decoder. Optional "double write" of an uncompressed 4:2:0 copy at full size or 1/4 × 1/4 (§5.10) |
| Interrupt | mailbox 1, GIC SPI 44 (INTID 76), edge rising, shared with VDEC_1 (§6) |
| GXBB-only code | the HEVC power-up self-test on revision-A chips. Not needed on this board, which is revision C (§1.4) |

---

## 1. Power, clocks and resets

### 1.1 Clock gates and the order of events at stream open

When a video port opens, amstream does the following, in this order:

1. It turns on the "vdec" gate. In the vendor DT this is reset/gate index GCLK_IDX_DOS = 1, in the group the header marks 0x50: HHI_GCLK_MPEG0 (0xC883C140) bit 1.
   - The parser-top gate is index 57, HHI_GCLK_MPEG1 bit 25.
   - Reading index n as register 0x50 + n/32, bit n mod 32 is inferred from the header's grouping comments (**UNVERIFIED**), but it matches the base document's DOS and DOS_PARSER gates.
2. It calls `vdec_poweron(VDEC_HEVC)` (§1.3). An HEVC port never powers VDEC_1.
3. When the format is set, it initialises the stream ring (§3), then registers the decoder device. The device's probe loads the microcode and starts the AMRISC (§5.13).
4. It hooks the ESPARSER to the ring (§3).

### 1.2 HHI_VDEC2_CLK_CNTL (0xC883C1E4): the HEVC clock

**How the vendor reaches HHI on GXBB:**
- The vendor numbers HHI registers in the old CBUS style (HHI_VDEC2_CLK_CNTL = 0x1079).
- On GXBB only, it subtracts 0x1000 and multiplies by 4 from the HIU base 0xC883C000. That gives 0xC883C1E4, the same address as the base document.

**Fields used (upper half only; VDEC2's own clock is the lower half):**

| Bits | Use |
|---|---|
| 27:25 | source: 0 = fclk/4 (500 MHz), 1 = fclk/3 (666.7), 2 = fclk/5 (400), 3 = fclk/7 (285.7), 4 = MPLL out 1, 5 = MPLL out 2, 6 = GP0 PLL. fclk = 2000 MHz |
| 22:16 | divider − 1 |
| 24 | HEVC clock enable |

**HHI_VDEC4_CLK_CNTL (0xC883C1EC) is used as a glitch-free fallback while the clock changes:**
- Copy bits 22:16 and 31:25 of VDEC2_CLK_CNTL into the same bits of VDEC4_CLK_CNTL.
- Set VDEC4 bit 24, then bit 31. This switches the HEVC clock to the copy (**UNVERIFIED** reading of bits 24 and 31).
- Clear VDEC2 bit 24, write the new source and divider into VDEC2 bits 31:16, then set VDEC2 bit 24.
- Clear VDEC4 bit 31, then bit 24.
- Write DOS_GCLK_EN3 (DOS + 0xFCD4) = 0xFFFFFFFF. Every "clock on" for HEVC ends with this write.

**Frequencies:**
- **At power-up:** `hevc_clock_hi_enable` asks for 500 MHz, so source 0 (fclk/4) with divider 1.
- **Later, from the stream:** a 10 ms timer in vh265 calls `hevc_source_changed(w, h, fps)` once it knows the frame rate. The clock is the first table entry whose limit is strictly greater than w × h × fps:

| w×h×fps limit (exclusive) | Requested | Source / divider | Actual |
|---|---|---|---|
| 1280×720×30 | 100 | fclk/5 ÷ 4 | 100 MHz |
| 1920×1080×30 | 100 | fclk/5 ÷ 4 | 100 MHz |
| 1920×1080×60 | 166 | fclk/4 ÷ 3 | 166.7 MHz |
| 4096×2048×30 | 333 | fclk/3 ÷ 2 | 333.3 MHz |
| 4096×2048×60 | 630 | GP0 PLL at 1296 MHz ÷ 2 | 648 MHz (fclk/3 = 667 MHz if the GP0 PLL can't be had) |
| above | 630 | as above | 648 MHz |

So 1080p30 gets 166 MHz (1080p30 equals the 1920×1080×30 limit, and the limit is exclusive) and 1080p60 gets 333 MHz.

At removal, `hevc_source_changed(0, 0, 0)` leaves the clock as it is. The one exception: 667 MHz (the GP0 fallback) drops to 200 MHz (fclk/5 ÷ 2). Power-down then gates the clock anyway.

**A quirk at probe:** the HEVC probe calls `vdec_source_changed(H264_4K2K, 4096, 2048, 30)` "for smooth playing". On GXBB this sets the **VDEC_1** clock (to 648 MHz through the GP0 PLL), not the HEVC clock. It looks like a leftover (**UNVERIFIED** intent).

**Comparison:** mainline-family drivers run the HEVC clock at a fixed 666.67 MHz (§7).

### 1.3 Power-up order (`vdec_poweron(VDEC_HEVC)`)

| # | Register | Address | Action | Value / note |
|---|---|---|---|---|
| 1 | AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 | clear bits 7:6 | mask 0xC0, read-modify-write |
| 2 | wait | | 10 µs | |
| 3 | DOS_SW_RESET3 | DOS + 0xFCD0 | write | 0xFFFFFFFF |
| 4 | DOS_SW_RESET3 | DOS + 0xFCD0 | write | 0 |
| 5 | HHI_VDEC2_CLK_CNTL / VDEC4 | 0xC883C1E4 / 0xC883C1EC | §1.2 sequence for 500 MHz | ends with DOS_GCLK_EN3 = 0xFFFFFFFF |
| 6 | DOS_MEM_PD_HEVC | DOS + 0xFCCC | write | 0 (all HEVC memories on) |
| 7 | AO_RTI_GEN_PWR_ISO0 | 0xC81000EC | clear bits 11:10 | mask 0xC00, read-modify-write |
| 8 | (revision-A chips only) | | self-test, §1.4 | |
| 9 | DOS_SW_RESET3 | DOS + 0xFCD0 | write | 0xFFFFFFFF |
| 10 | wait | | 10 µs | |
| 11 | DOS_SW_RESET3 | DOS + 0xFCD0 | write | 0 |

- **Read-modify-write, not whole writes:** the vendor changes only the HEVC bits in SLEEP0 and ISO0. This answers the base document's question about which ISO0 bits HEVC owns: bits 11:10, while VDEC_1 owns 7:6.
- **DMC request bit:** for VDEC_1 the vendor also sets DMC_REQ_CTRL (0xC8838000) bit 13 on GXBB. For HEVC it touches no DMC_REQ_CTRL bit. Whether HEVC's DMC channel is enabled at reset is **UNVERIFIED**.
- **DMC urgency:** once, at vdec probe on GXBB, the vendor writes DMC_AM5_CHAN_CTRL (0xC8838248) = 0x03F203CF ("set vdec dmc request to urgent"). Which DMC channel the HEVC block uses is **UNVERIFIED**.
- **"Powered" test (`vdec_on`):** HEVC counts as on when SLEEP0 bits 7:6 are both 0 and HHI_VDEC2_CLK_CNTL bit 24 is 1.

### 1.4 GXBB revision-A self-test (the only GXBB-specific HEVC power code)

**When it applies:** `hevc_workaround_needed()` is true when the CPU is GXBB and its minor revision is 0xA.

**What it does:**
- Before the power-up it allocates a buffer, aligns it to 64 KiB, and fills its first 4 KiB with 0xFF.
- After step 7 it runs a test of the IPP. If the test fails, it powers HEVC down (§1.5), waits 10 ms and repeats the whole power-up. After 100 failures it panics with a message that the chip has a hardware fault.

**The test, in order:**

| Register | Value |
|---|---|
| HEVCD_IPP_TOP_CNTL | 1, then 0 |
| HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR | 0 |
| HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR | 32 writes of (buffer \| i<<8 \| 1), i = 0..31 |
| HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR | 1 |
| HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR | 1, then 32 writes of 0 to HEVCD_MPP_ANC_CANVAS_DATA_ADDR |
| HEVCD_MCRCC_CTL1 / CTL2 / CTL3 / CTL1 | 2, 0, 0, 0xFF0 |
| HEVCD_MPP_DECOMP_CTL1 / CTL2 | 0, 0 |
| HEVCD_IPP_TOP_FRMCONFIG | 0x01000100 |
| HEVCD_IPP_TOP_TILECONFIG3 | 0 |
| HEVCD_IPP_TOP_LCUCONFIG | 0x20 |
| HEVCD_IPP_BITDEPTH_CONFIG | 0xA |
| HEVCD_IPP_CONFIG, HEVCD_IPP_LINEBUFF_BASE | 0, 0 |
| HEVCD_IPP_SWMPREDIF_CONFIG | 1 ("SWIMP mode") |
| HEVCD_IPP_TOP_CNTL | 2 |
| HEVCD_IPP_TOP_FRMCTL | 1 |
| HEVCD_IPP_SWMPREDIF_CTBINFO | 0x80000000 |
| HEVCD_IPP_SWMPREDIF_PUINFO0..3 | 0x00040800, 0x8, 0, 0 |
| wait | 20 µs |
| HEVCD_IPP_DBG_SEL | 0x20 |
| HEVCD_IPP_DBG_DATA | read; pass when (value & 3) == 1 |

**This board:** it reports "GXBB (S905) Revision 1f:c" in mainline Linux (logs/m9-sep-1.log), so its minor revision is 0xC and the self-test would not run. That mainline's "c" is the same field as the vendor's minor revision is inferred (**UNVERIFIED**).

### 1.5 Power-down order (`vdec_poweroff(VDEC_HEVC)`)

| # | Register | Action |
|---|---|---|
| 1 | AO_RTI_GEN_PWR_ISO0 | set bits 11:10 (isolation on) |
| 2 | DOS_MEM_PD_HEVC | write 0xFFFFFFFF |
| 3 | HHI_VDEC2_CLK_CNTL | clear bit 24 (the GP0 PLL is released if held) |
| 4 | AO_RTI_GEN_PWR_SLEEP0 | set bits 7:6 |

**What happens first, at decoder stop** (`vh265_stop`, then `amhevc_disable`):
1. HEVC_MPSR = 0 and HEVC_CPSR = 0.
2. Wait up to 1 s for HEVC_IMEM_DMA_CTRL bit 15 to clear. Read DOS_SW_RESET3 three times (no reset pulse, unlike VDEC_1's stop).
3. HEVC_ASSIST_MBOX1_MASK = 0, and free the IRQ.
4. Three drain waits, each with a 100 ms timeout:
   - HEVC_MDEC_PIC_DC_STATUS == 0. On timeout, pulse HEVC_MDEC_PIC_DC_CTRL bit 0.
   - HEVC_DBLK_STATUS bit 0 == 0. On timeout, write HEVC_DBLK_CTRL = 3 then 0.
   - HEVC_DCAC_DMA_CTRL bit 15 == 0.
   - These three registers sit at word indexes 0x398E/0x398F, 0x3951/0x3953 and 0x3E12, the VDEC-style mirror range of the HEVC block. That they do anything on GXBB is **UNVERIFIED**.
5. The port closes, then `vdec_poweroff` runs.

The Android M driver additionally waits up to 200 ms for the status register to leave 5 ("decoding slice data") before stopping.

### 1.6 DOS_SW_RESET3 (DOS + 0xFCD0) bits the vendor uses

| Bits | When | Vendor's own label |
|---|---|---|
| all (0xFFFFFFFF) | power-up, twice | "hevc soft reset" |
| 14 | decoder init and re-init. Set and **left set** until the next AMRISC start writes 0 | "reset iqit to start mem init again" |
| 12, 11 | AMRISC start: pulse (write 0x1800, then 0), with 3 dummy reads before and after | none. By analogy with VDEC_1, the AMRISC core resets (**UNVERIFIED**) |
| 3, 4, 8, 11, 12, 14, 15, 17, 18, 19 | error recovery (§5.12): write 0x000ED918, then 0. Bit 2 is commented out of the list | none |
| 3 | in an `#if 0` block only | "reset_whole parser" |

---

## 2. Microcode loading

- **What is loaded:** one image, `vh265_mc`. The array has 3,445 32-bit words (13,780 bytes); the last non-zero word is word 3,402. It is loaded **once**, at decoder start, before the protocol setup. There are no swapped-in parts, unlike the H.264 extension.
  - The image is registered for M8, M8M2 and GXBB (`firmware_def.h`). On a GXBB kernel only the GXBB copy is kept.
- **Where it comes from:** built into the kernel. Loading from the filesystem (`video/<cpu>/decoder/<format>/<name>`) is compiled out (`ENABLE_LOAD_FIRMWARE_FROM_FS` is commented out).
  - Two other names appear in vh265.c and are absent from the tree: `vh265_mc_debug` (debug flag 0x200000) and `vh265_mc_dw` (in an `#if 0` block, for double-write-only mode).
- **Byte layout:** the C array is u32 words, stored little-endian in memory on AArch64. The DMA reads memory bytes in order. The extracted files are 16,384 bytes: the words little-endian, then zeros.
- **The loader:**
  1. Copy min(16 KiB, array size) into a 16 KiB buffer and zero the rest.
  2. HEVC_MPSR (DOS + 0xCC04) = 0, HEVC_CPSR (DOS + 0xCC84) = 0, then two dummy reads of HEVC_MPSR.
  3. HEVC_IMEM_DMA_ADR (DOS + 0xCD04) = bus address of the buffer.
  4. HEVC_IMEM_DMA_COUNT (DOS + 0xCD08) = 0x1000 words.
  5. HEVC_IMEM_DMA_CTRL (DOS + 0xCD00) = 0x00078000 (bit 15 = go/busy; bits 18:16 = 7, the same value VDEC_1 uses; meaning **UNVERIFIED**).
  6. Poll bit 15 until it clears. The timeout is 1 s.
- **Start:** the processor is started later, by `amhevc_start` (§5.13, step 7), not by the loader.

---

## 3. Stream input (ESPARSER into the HEVC stream ring)

The ESPARSER itself is described in base §3. For HEVC the vendor makes these changes.

**Ring setup (`stbuf_init`, done before the decoder probe):**
1. HEVC_STREAM_CONTROL (DOS + 0xC404): clear bit 0 (stream_fetch_enable).
2. HEVC_STREAM_START_ADDR (DOS + 0xC408) = ring start.
3. HEVC_STREAM_END_ADDR (DOS + 0xC40C) = ring start + ring size.
4. HEVC_STREAM_RD_PTR (DOS + 0xC414) = HEVC_STREAM_WR_PTR (DOS + 0xC410) = ring start.

The ring is 15 MiB by default, for 4K and for ≤1080p alike. The vendor allocates it from CMA, so its alignment is at least a page.

**Hooking the parser to the ring (`esparser_init`, after the decoder probe):**

| # | Register | Action | Note |
|---|---|---|---|
| 1 | HEVC_STREAM_CONTROL | clear bit 0 | |
| 2 | PARSER_VIDEO_START_PTR (CBUS 0x2980, 0xC110A600) | = HEVC_STREAM_START_ADDR | |
| 3 | PARSER_VIDEO_END_PTR (CBUS 0x2981, 0xC110A604) | = HEVC_STREAM_END_ADDR − 8 | the same "−8" convention as for the VIFIFO is **UNVERIFIED**; the vendor does it only for HEVC |
| 4 | PARSER_ES_CONTROL (CBUS 0x2977, 0xC110A5DC) | clear bit 0 (ES_VID_MAN_RD_PTR) | |
| 5 | DOS_GEN_CTRL0 (DOS + 0xFC08) | write 0x6 (3 << 1) | "vififo_vbuf_rp_sel => hevc". The VDEC_1 path writes 0 |
| 6 | HEVC_STREAM_CONTROL | set bit 3, bit 4 stays 0 | bit 3 = "use_parser_vbuf_wp". Bit 4 is written as an explicit 0 with no name |
| 7 | HEVC_STREAM_CONTROL | set bit 0 | stream_fetch_enable |
| 8 | HEVC_STREAM_FIFO_CTL (DOS + 0xC41C) | set bit 29 | "stream_buffer_hole with 256 bytes" |

**Data path:**
- Bitstream is pushed through the parser exactly as for VDEC_1 (parser type "video"). The parser's write pointer becomes the ring's write pointer through bit 3 above.
- The free space the host computes is ring size − HEVC_STREAM_LEVEL (DOS + 0xC418) − 6 KiB (when the usable size is at least half the ring) − PARSER_VIDEO_HOLE.

**Read-pointer handover on recovery:** PARSER_VIDEO_RP = HEVC_STREAM_RD_PTR, with PARSER_ES_CONTROL bit 0 set during the restart and cleared afterwards (§5.12).

**Other stream registers the vendor reads:**
- HEVC_SHIFT_BYTE_COUNT (DOS + 0xC434): bytes consumed. It is read on every interrupt and extended to 64 bits by watching bit 31 wrap. The value at a picture's first slice is that picture's stream offset, used to look up the PTS.
- HEVC_PARSER_INT_STATUS (DOS + 0xC484) bit 6: "stream empty", used by the watchdog.
- HEVC_STREAM_LEVEL > 0x200: "decoder has input", also used by the watchdog.

---

## 4. Workspace

**Allocation:**
- The decoder gets one physically contiguous region from `vdec_init`. When no reserved region is configured, that is 48 MiB from CMA (64 MiB when the container says the picture is larger than 1920×1088). Its start is 64 KiB aligned.
- The workspace occupies its start. The vendor always builds the **4K layout**, because SUPPORT_4K2K is defined.
- Every address below is region start + offset. Each is written to the register shown once at init, and again after a recovery.

| Sub-buffer | Offset | Size | Register written | Vendor's note |
|---|---|---|---|---|
| ipp | 0x0000000 | 0x4000 | HEVCD_IPP_LINEBUFF_BASE (DOS + 0xD024) | IPP line buffer: 4096 × (Y + CbCr + flags), rounded up |
| sao_abv | 0x0004000 | 0x30000 | HEVC_SAO_ABV = SCRATCH_A (on every new picture, §5.9) | |
| sao_vb | 0x0034000 | 0x30000 | HEVC_SAO_VB = SCRATCH_C (on every new picture) | |
| short_term_rps | 0x0064000 | 0x800 | HEVC_SHORT_TERM_RPS = SCRATCH_2 | 64 sets × 16 entries × 2 bytes |
| vps | 0x0064800 | 0x800 | HEVC_VPS_BUFFER = SCRATCH_3 | 16 VPS × 0x80 |
| sps | 0x0065000 | 0x800 | HEVC_SPS_BUFFER = SCRATCH_4 | 16 SPS × 0x80 |
| pps | 0x0065800 | 0x2000 | HEVC_PPS_BUFFER = SCRATCH_5 | 64 PPS × 0x80 |
| sao_up | 0x0067800 | 0x2800 | HEVC_SAO_UP = SCRATCH_6 | 640 LCUs × 16 bytes |
| swap_buf | 0x006A000 | 0x800 | HEVC_STREAM_SWAP_BUFFER = SCRATCH_7 | 256 cycles × 64 bits, 144 used |
| swap_buf2 | 0x006A800 | 0x800 | HEVC_STREAM_SWAP_BUFFER2 = SCRATCH_8 | |
| scalelut | 0x006B000 | 0x8000 | HEVC_SCALELUT = SCRATCH_D | 32 scaling lists × 1 KiB |
| dblk_para | 0x0073000 | 0x20000 | HEVC_DBLK_CFG4 (DOS + 0xD410) | the 1080p layout uses 0x40000 here |
| dblk_data | 0x0093000 | 0x40000 | HEVC_DBLK_CFG5 (DOS + 0xD414) | |
| mpred_above | 0x00D3000 | 0x8000 | HEVC_MPRED_ABV_START_ADDR, written per new picture (§5.9) | |
| mpred_mv | 0x00DB000 | 0x1200000 | per picture: HEVC_MPRED_MV_WR/RD_START_ADDR (§5.9) | 16 × 0x120000 (4096×2304 with 64×64 LCUs) |
| rpm | 0x12DB000 | 0x100 | reserved but **not used** | the RPM goes to a separate buffer, below |
| end | 0x12DB100 | | | frame buffers would start at the next 64 KiB (0x12E0000) |

**The 1080p layout** (spec 0, unused) differs only in dblk_para = 0x40000 and mpred_mv = 16 × 0x40000. That moves dblk_data to 0xB3000, mpred_above to 0xF3000, mpred_mv to 0xFB000 and the end to 0x4FB100.

**Outside the region:**
- **RPM buffer:** 0x100 bytes from kmalloc, mapped for device-to-CPU DMA. HEVC_RPM_BUFFER (SCRATCH_1) = its bus address. The host syncs it for the CPU before reading (§5.8).
- **LMEM dump buffer** (debug only, flag 0x4): 0x800 bytes. LMEM_DUMP_ADR (SCRATCH_F) = its address.
- **Frame buffers:** with the default `use_cma = 2`, each picture buffer is a separate CMA allocation aligned to 64 KiB (§5.9). The rest of the 48 MiB region stays unused.
- **No DMC canvases for decoding:** every address the HEVC block uses is a 32-bit physical address in a register or in the ANC2AXI table. DMC canvas-LUT entries are configured only so the VPU can display the output (§5.10).

**MV buffer per picture:**
- Size = number of LCUs × MV_MEM_UNIT. MV_MEM_UNIT is 0x200 bytes for 64×64 LCUs, 0x80 for 32×32 and 0x20 for 16×16.
- Picture slot i uses mpred_mv + i × LCUs × MV_MEM_UNIT.
- At 1080p every LCU size gives 0x3FC00 bytes.

---

## 5. Host–microcode protocol

### 5.1 Scratch registers (HEVC_ASSIST_SCRATCH_x, DOS + 0xC300 + 4·x)

| Reg | DOS offset | Vendor name | Direction | Use |
|---|---|---|---|---|
| 0 | 0xC300 | HEVC_DEC_STATUS_REG | both | status from the microcode / command from the host (§5.2) |
| 1 | 0xC304 | HEVC_RPM_BUFFER | host → µc | RPM DMA address |
| 2 | 0xC308 | HEVC_SHORT_TERM_RPS | host → µc | workspace |
| 3 | 0xC30C | HEVC_VPS_BUFFER | host → µc | workspace |
| 4 | 0xC310 | HEVC_SPS_BUFFER | host → µc | workspace |
| 5 | 0xC314 | HEVC_PPS_BUFFER | host → µc | workspace |
| 6 | 0xC318 | HEVC_SAO_UP | host → µc | workspace |
| 7 | 0xC31C | HEVC_STREAM_SWAP_BUFFER | host → µc | workspace |
| 8 | 0xC320 | HEVC_STREAM_SWAP_BUFFER2 | host → µc | workspace |
| 9 | 0xC324 | HEVC_sao_mem_unit | host → µc | per new picture |
| A | 0xC328 | HEVC_SAO_ABV | host → µc | per new picture |
| B | 0xC32C | HEVC_sao_vb_size | host → µc | per new picture |
| C | 0xC330 | HEVC_SAO_VB | host → µc | per new picture |
| D | 0xC334 | HEVC_SCALELUT | host → µc | workspace |
| E | 0xC338 | HEVC_WAIT_FLAG | both | §5.5 |
| F | 0xC33C | RPM_CMD_REG / LMEM_DUMP_ADR | both | register-mode RPM transfer (debug) or LMEM dump address |
| G | 0xC340 | DEBUG_REG1 | both | 0 normally; 1 enables LMEM dumps (debug) |
| H | 0xC344 | DEBUG_REG2 | µc → host | debug value |
| I | 0xC348 | NAL_SEARCH_CTL | host → µc | §5.4 |
| J | 0xC34C | CUR_NAL_UNIT_TYPE | µc → host | NAL type with status 9 |
| K | 0xC350 | DECODE_STOP_POS | host → µc | 0. A debug stop position |
| L | 0xC354 | HEVC_STREAM_SWAP_TEST | host → µc | 0 (100 enables a swap test) |
| M | 0xC358 | HEVC_DECODE_PIC_BEGIN_REG | host → µc | 0 |
| N | 0xC35C | HEVC_DECODE_PIC_NUM_REG | host → µc | 0xFFFFFFFF |

### 5.2 Status and command values in SCRATCH_0

| Value | Name | Written by | Meaning |
|---|---|---|---|
| 0 | HEVC_DEC_IDLE | host at init | |
| 1 | HEVC_NAL_UNIT_VPS | host | "parse this NAL as a VPS" (answer to status 9) |
| 2 | HEVC_NAL_UNIT_SPS | host | "parse as SPS" |
| 3 | HEVC_NAL_UNIT_PPS | host | "parse as PPS" |
| 4 | HEVC_NAL_UNIT_CODED_SLICE_SEGMENT | host | "parse this slice-segment header" |
| 5 | HEVC_CODED_SLICE_SEGMENT_DAT | host | "decode this slice's data" (answer to status 8) |
| 7 | HEVC_DUMP_LMEM | — | defined, not used by the host |
| 8 | HEVC_SLICE_SEGMENT_DONE | µc | a slice-segment header has been parsed and the RPM is filled |
| 9 | HEVC_NAL_SEARCH_DONE | µc | a NAL was found and its type is in SCRATCH_J |
| 0xF0 | HEVC_DISCARD_NAL | host | "skip this NAL" |
| 0xFE | HEVC_ACTION_ERROR | — | defined, not used |
| 0xFF | HEVC_ACTION_DONE | host | "done; search for the next start code" (skip or restart) |

**The kick:** after writing a command, the host writes HEVC_MCPU_INTR_REQ (DOS + 0xCC14) = 4 (AMRISC_MAIN_REQ). The other request bits defined are 1 (stream empty) and 2 (parser); the host never uses them.

**Values this driver doesn't define:** 6 and 0xA–0xD. The mainline family defines them as slice decoding, picture done, picture error and SEI, for newer firmware (§7).

### 5.3 What the microcode does between statuses

This is inferred from the host's reactions; the microcode is not documented (**UNVERIFIED** as a whole).

- **After `amhevc_start`:** the microcode searches the stream for start codes. With automatic header parsing (§5.4) it parses VPS, SPS and PPS itself and stops at each slice segment with status 8.
- **Status 8 means the microcode is waiting:** it holds the slice header in the RPM until the host writes 5, or writes 0xFF together with clearing WAIT_FLAG bit 1.
- **It waits indefinitely:** the vendor relies on this whenever no free picture buffer exists. The host answers nothing and returns. When the display hands back a buffer, the host raises its own interrupt by writing HEVC_ASSIST_MBOX1_IRQ_REG = 1 and resumes with the same status 8.
- **After command 5:** the microcode decodes the slice data, then searches for the next NAL. No per-slice or per-picture "done" status exists in this protocol.
- **Picture boundaries:** the host learns that a picture is complete when the next picture's first slice header arrives (status 8 with slice_segment_address 0), or at end of stream (§5.11).

### 5.4 NAL_SEARCH_CTL (SCRATCH_I)

**Bits:**

| Bits | Meaning (vendor's comment) |
|---|---|
| 0 | 0 = the microcode parses headers automatically; 1 = manual: report each NAL with status 9 |
| 1 | 0 = auto-skip for non-seamless streams; 1 = no skip |
| 3:2 | when bit 1 = 0: 0 = skip NALs before the first VPS/SPS/PPS/IDR; 1 = before the first VPS/SPS/PPS; 2 = as 1, and decode nothing before the first I slice with slice address 0; 3 = skip before the first IRAP slice (NAL types 16–21) |
| 15:4 | NAL skip count (manual mode) |
| 16 | in automatic mode: 0 = report an EOS NAL with status 9; 1 = don't |
| 31:20 | used by the microcode for debugging |

**Values the vendor writes:**

| Value | When |
|---|---|
| 0x8 | init, default (`nal_skip_policy` = 2, so skip mode 2) |
| 0x4 | init with skip mode 0 |
| 0x0 | init with skip mode 3 |
| 0x2 | once VPS, SPS, PPS and a valid start slice have been seen (only on the status-9 path) |
| 0xC | after recovery (error policy 0) |
| 0x1 or (n << 4) \| 1 | error handling: manual search, skipping n NALs |

### 5.5 HEVC_WAIT_FLAG (SCRATCH_E)

- Written as 1 at init and after recovery.
- The host **sets bit 1** while it processes a slice header that it will let the microcode decode (before writing 5).
- It **clears bit 1** whenever it answers 0xFF to skip.
- The meaning of bit 0 is **UNVERIFIED**.

### 5.6 One-time decoder setup (`vh265_prot_init`), in order

Before this, the workspace registers of §4 are written (scratch 1–8 and D, IPP line buffer, DBLK_CFG4/5).

| # | Register | Action / value | Vendor's label |
|---|---|---|---|
| 1 | HEVC_PARSER_VERSION (DOS + 0xC400) | must read 0x00010001; write 0x5A5A55AA and read it back (it is never restored). On mismatch the vendor logs an error and **skips rows 2–19**; rows 20–26 still run | sanity check |
| 2 | DOS_SW_RESET3 | write 1<<14 | IQIT memory init |
| 3 | HEVC_CABAC_CONTROL (DOS + 0xC440) | clear bit 0 | |
| 4 | HEVC_PARSER_CORE_CONTROL (DOS + 0xC44C) | clear bit 0 | |
| 5 | HEVC_STREAM_CONTROL | set bit 0 | stream_fetch_enable |
| 6 | HEVC_SHIFT_STARTCODE (DOS + 0xC424) / HEVC_SHIFT_EMULATECODE (DOS + 0xC428) | must read 0x00000100 / 0x00000300; write 0x12345678 / 0x9ABCDEF0 and read back; restore 0x00000100 / 0x00000300 | register test |
| 7 | HEVC_PARSER_INT_CONTROL (DOS + 0xC480) | keep bits 25:0, then set bits 31:29 = 3, bits 27:26 = 2, and bits 24, 22, 7, 4 and 0; bit 3 stays 0 | 24 stream_buffer_empty_int_amrisc_enable, 22 stream_fifo_empty_int_amrisc_enable, 7 dec_done_int_cpu_enable, 4 startcode_found_int_cpu_enable, 3 startcode_found_int_amrisc_enable, 0 parser_int_enable; 31:29 and 27:26 unnamed |
| 8 | HEVC_SHIFT_STATUS (DOS + 0xC42C) | set bits 1, 0 | emulation_check_on, startcode_check_on |
| 9 | HEVC_SHIFT_CONTROL (DOS + 0xC420) | write 0xE5 | [7:6]=3 sft_valid_wr_position, [5:4]=2 emulate_code_length_sub_1, [2:1]=2 start_code_length_sub_1, [0] stream_shift_enable |
| 10 | HEVC_CABAC_CONTROL | write 1 | cabac_enable |
| 11 | HEVC_PARSER_CORE_CONTROL | write 1 | parser core clock enable |
| 12 | SCRATCH_0 | write 0 | |
| 13 | HEVC_IQIT_SCALELUT_WR_ADDR (DOS + 0xDC08) | write 0, then 1024 writes of 0 to HEVC_IQIT_SCALELUT_DATA (DOS + 0xDC10) | "avoid X in simulation" |
| 14 | SCRATCH_L, M, N | 0, 0, 0xFFFFFFFF | |
| 15 | HEVC_PARSER_CMD_WRITE (DOS + 0xC448) | write 1<<16, then 37 table entries | see below |
| 16 | HEVC_PARSER_CMD_SKIP_0/1/2 (DOS + 0xC4A0/4/8) | 0x0000090B, 0x1B14140F, 0x001B1910 | |
| 17 | HEVC_PARSER_IF_CONTROL (DOS + 0xC488) | write 0x25 | 5 parser_sao_if_en, 2 parser_mpred_if_en, 0 parser_scaler_if_en; 8 sao_sw_pred_enable left 0 |
| 18 | HEVCD_IPP_TOP_CNTL (DOS + 0xD000) | write 1, then 2 | 1 = reset IPP and MPP; 2 = enable IPP |
| 19 | HEVCD_MPP_DECOMP_CTL1 (DOS + 0xD308) | write 1<<31, **double-write-only mode only** | "NV21 reference read mode for MC" |
| 20 | HEVC_WAIT_FLAG | 1 | |
| 21 | HEVC_ASSIST_MBOX1_CLR_REG (DOS + 0xC1D4) | 1 | clear mailbox |
| 22 | HEVC_ASSIST_MBOX1_MASK (DOS + 0xC1D8) | 1 | enable mailbox interrupt |
| 23 | HEVC_PSCALE_CTRL (DOS + 0xE444) | 0 | "disable PSCALE for hardware sharing" |
| 24 | DEBUG_REG1 (SCRATCH_G) | 0 | |
| 25 | NAL_SEARCH_CTL | 0x8 (§5.4) | |
| 26 | DECODE_STOP_POS (SCRATCH_K) | 0 | |

**The parser command table** (37 halfwords, written in order), identical to the mainline family's table:
0401 8401 0800 0402 9002 1423 8CC3 1423 8804 9825 0800 04FE 8406 8411 1800 8408 8409 8C2A 9C2B 1C00 840F 8407 8000 8408 2000 A800 8410 04DE 840C 840D AC00 A000 08C0 08E0 A40E FC00 7C00.

### 5.7 Interrupt-handler flow

On each interrupt the host reads SCRATCH_0 and HEVC_SHIFT_BYTE_COUNT.

**Status 9 (NAL_SEARCH_DONE):**
- The host reads the NAL type from SCRATCH_J (CUR_NAL_UNIT_TYPE).
- **EOS (type 36):** flush all pictures to output (§5.11) and return. The 3.14 driver writes no command. The Android M driver writes 0xF0 and kicks.
- **Otherwise it picks an answer:**
  - VPS (32) → 1.
  - SPS (33) → 2, and PPS (34) → 3, once a VPS has been seen.
  - Once SPS and PPS have been seen: IDR/CRA/BLA slices (16–21) → 4, and mark "valid start". Other slice types (≤ 21) → 4 if a valid start was seen, or always when skip mode ≠ 3.
  - Anything else → 0xF0.
- Once VPS, SPS, PPS and a valid start have all been seen, the host switches NAL_SEARCH_CTL to 0x2.
- It writes the answer to SCRATCH_0 and kicks.
- **When status 9 appears:** in the default automatic mode the host only sees it for EOS, as the bit-16 comment suggests. That this is the only case is **UNVERIFIED**.

**Status 8 (SLICE_SEGMENT_DONE):**
1. Read the RPM (§5.8).
2. Bit depth: luma = (bit_depth & 0xF) + 8, chroma = ((bit_depth >> 4) & 0xF) + 8. If both are 8, use "memory saving" mode for the compressed frames (§5.10).
3. Frame duration in 1/96000 s = 96000 × num_units_in_tick / time_scale, when the VUI gives both.
4. **First slice of the stream:** if the picture list isn't built yet and slice_segment_address = 0, record width, height and LCU size. Hand buffer allocation to a kernel thread and return **without** answering.
   - The thread allocates the buffers and fills the ANC2AXI table (§5.9).
   - It then writes HEVC_ASSIST_MBOX1_IRQ_REG (DOS + 0xC1D0) = 1. That raises the same interrupt again, with SCRATCH_0 still 8.
   - In that second pass the host uses the RPM copy it already has. It does not re-read the RPM.
5. Process the slice header (POC, reference sets, buffers; §5.9). The result decides the answer:

| Result | Host action |
|---|---|
| decode | set WAIT_FLAG bit 1; record the stream offset on a new picture; SCRATCH_0 = 5; kick |
| skip (RASL after CRA/BLA, missing reference, bad parameters, debug skip) | clear WAIT_FLAG bit 1; SCRATCH_0 = 0xFF; kick |
| no free picture buffer | no answer. The microcode keeps waiting; the host re-raises the IRQ (MBOX1_IRQ_REG = 1) when a buffer comes back |

**Interrupt acknowledge:** the 3.14 handler never writes HEVC_ASSIST_MBOX1_CLR_REG. Only init and recovery do. See §6 and the open questions.

### 5.8 The parsed-parameter area (RPM)

**Transfer:**
- 128 halfwords, written by the microcode with DMA to the address in SCRATCH_1, before it raises status 8.
- The microcode stores each group of four halfwords in reverse order: halfword k of the logical list is at buffer halfword (k & ~3) + 3 − (k & 3).
  - Put differently, each 64-bit word holds four halfwords with the first one in the most significant position.
- The table gives both the logical index and the byte offset in the DMA buffer.
- The microcode source says it was built without SEND_PARAM_WITH_REG, so register transfer is not used.
  - The register alternative, used only under a debug flag: 128 reads of SCRATCH_F, each waiting for bit 16; the low 16 bits are the value; write 0 after each.

| Index (hex) | Byte offset in buffer | Field | Notes |
|---|---|---|---|
| 0–15 (0x00–0x0F) | see rule | CUR_RPS[16] | current short-term RPS, see below |
| 16 (0x10) | 0x26 | num_ref_idx_l0_active | |
| 17 (0x11) | 0x24 | num_ref_idx_l1_active | |
| 18 (0x12) | 0x22 | slice_type | 0 = B, 1 = P, 2 = I |
| 19 (0x13) | 0x20 | slice_temporal_mvp_enable_flag | |
| 20 (0x14) | 0x2E | dependent_slice_segment_flag | |
| 21 (0x15) | 0x2C | slice_segment_address | 0 = first segment of a picture |
| 22 (0x16) | 0x2A | num_title_rows_minus1 | (sic) |
| 23 (0x17) | 0x28 | pic_width_in_luma_samples | |
| 24 (0x18) | 0x36 | pic_height_in_luma_samples | |
| 25 (0x19) | 0x34 | log2_min_coding_block_size_minus3 | |
| 26 (0x1A) | 0x32 | log2_diff_max_min_coding_block_size | LCU size = 1 << (idx25 + 3 + idx26) |
| 27 (0x1B) | 0x30 | log2_max_pic_order_cnt_lsb_minus4 | |
| 28 (0x1C) | 0x3E | POClsb | |
| 29 (0x1D) | 0x3C | collocated_from_l0_flag | |
| 30 (0x1E) | 0x3A | collocated_ref_idx | |
| 31 (0x1F) | 0x38 | log2_parallel_merge_level | |
| 32 (0x20) | 0x46 | five_minus_max_num_merge_cand | |
| 33 (0x21) | 0x44 | sps_num_reorder_pics_0 | |
| 34 (0x22) | 0x42 | modification_flag | bit 0: list 0 modified; bit 1: list 1 modified |
| 35 (0x23) | 0x40 | tiles_enabled_flag | bit 0: tiles on; bit 1: uniform spacing |
| 36 (0x24) | 0x4E | num_tile_columns_minus1 | the vendor handles at most 5 |
| 37 (0x25) | 0x4C | num_tile_rows_minus1 | at most 5 |
| 38–41 | (0x4A, 0x48, 0x56, 0x54) | tile_width[4] | in LCUs |
| 42–45 | (0x52, 0x50, 0x5E, 0x5C) | tile_height[4] | in LCUs |
| 46 (0x2E) | 0x5A | misc_flag0 | bits below |
| 47 (0x2F) | 0x58 | pps_beta_offset_div2 | |
| 48 (0x30) | 0x66 | pps_tc_offset_div2 | |
| 49 (0x31) | 0x64 | slice_beta_offset_div2 | |
| 50 (0x32) | 0x62 | slice_tc_offset_div2 | |
| 51 (0x33) | 0x60 | pps_cb_qp_offset | |
| 52 (0x34) | 0x6E | pps_cr_qp_offset | |
| 53 (0x35) | 0x6C | first_slice_segment_in_pic_flag | |
| 54 (0x36) | 0x6A | m_temporalId | the host compares (value − 1) with 0, so the field appears to be TemporalId + 1 (**UNVERIFIED**) |
| 55 (0x37) | 0x68 | m_nalUnitType | NAL type of this slice (19/20 = IDR) |
| 56–57 | 0x76, 0x74 | vui_num_units_in_tick hi, lo | |
| 58–59 | 0x72, 0x70 | vui_time_scale hi, lo | |
| 60 (0x3C) | 0x7E | bit_depth | [3:0] luma − 8, [7:4] chroma − 8 |
| 61 (0x3D) | 0x7C | profile_etc | not used by the host |
| 62 (0x3E) | 0x7A | sei_frame_field_info | not used by the host |
| 63 (0x3F) | 0x78 | reserved | |
| 64–95 (0x40–0x5F) | first at 0x86 | modification_list[32] | list 0 entries, then list 1 entries (when both lists are modified) |
| 96 (0x60) | 0xC6 | conformance_window_flag | |
| 97–100 | 0xC4, 0xC2, 0xC0, 0xCE | conf_win left, right, top, bottom offsets | in chroma units: × SubWidthC / SubHeightC |
| 101 (0x65) | 0xCC | chroma_format_idc | |
| 102–127 | | unused | |

**CUR_RPS entries** (one per reference picture, in order):

| Bits | Meaning |
|---|---|
| 15 | end-of-list marker. Stop at the first entry with it set |
| 14 | used by the current picture |
| 13 | sign: 1 = the reference is before the current picture |
| 12:0 | magnitude field |

- The POC is current + field when bit 13 is 0, and current − (0x2000 − field) when bit 13 is 1. In other words, bits 13:0 form a 14-bit two's-complement delta.
- Entries with bit 14 set build RefPicSetStCurrBefore (negative deltas) and RefPicSetStCurrAfter (positive deltas), in entry order.
- The default lists are: L0 = Before then After; L1 = After then Before; entry i = set[i mod total].
- When a list is modified, entry i = combined set[modification_list[...]].
- Long-term references are not handled (the "LongTerm" values are always 0).

**misc_flag0 bits:**

| Bit | Flag |
|---|---|
| 0 | pcm_loop_filter_disabled_flag |
| 1 | pcm_enabled_flag |
| 2 | loop_filter_across_tiles_enabled_flag |
| 3 | pps_loop_filter_across_slices_enabled_flag |
| 4 | deblocking_filter_override_enabled_flag |
| 5 | pps_deblocking_filter_disabled_flag |
| 6 | deblocking_filter_override_flag |
| 7 | slice_deblocking_filter_disabled_flag |
| 8 | slice_sao_luma_flag |
| 9 | slice_sao_chroma_flag |
| 10 | slice_loop_filter_across_slices_enabled_flag |

**Picture order count:**
- An IDR has POC 0.
- For other pictures the host derives the POC MSB from the previous picture whose (m_temporalId − 1) equals 0, following the standard rule. BLA pictures force the MSB to 0.
- The POC goes into HEVC_MPRED_CUR_POC.

### 5.9 What the host programs per picture and per slice

**A. Picture buffers** (once per stream, or again when the size changes):

- **Count:** sps_num_reorder_pics_0 + 7, at most 16 (`dynamic_buf_num_margin` = 7).
- **Allocation:** each buffer is one CMA allocation, 64 KiB aligned, sized for the stream's own width and height.
- **Layout of one picture buffer, everything 64 KiB aligned:**

| Part | Present when | Size |
|---|---|---|
| compressed body, then compressed header | double_write_mode is not 0x10 | body = (w/64 rounded up) × (h/32 rounded up) × 4096, or × 3072 in memory-saving mode (8-bit streams). Header = (w/128 up) × (h/64 up) × 32. The total is rounded up to 64 KiB. The header starts right after the body |
| double-write Y | double_write_mode ≠ 0 | 2 × C, where C = (LCUs × LCU² / 2) rounded up to 64 KiB |
| double-write CbCr | double_write_mode ≠ 0 | C |

- **Double-write-only mode (0x10):** no compressed part. Y is at the buffer start and CbCr at start + 2C.
- **Example, 1080p 8-bit with 64×64 LCUs:**
  - compressed part = 3,133,440 + 8,160 bytes → 0x300000;
  - C = 1,044,480 → 0x100000;
  - so double_write_mode 1 needs 6 MiB per picture and mode 0x10 needs 3 MiB.

**B. Decompression set-up** (compressed modes, at list build and whenever the size changes):
- HEVCD_MPP_DECOMP_CTL1 = (memory-saving << 3) | workaround bit 0.
- HEVCD_MPP_DECOMP_CTL2 = body size >> 5.
- HEVC_CM_BODY_LENGTH (word 0x3627, DOS + 0xD89C) = body size.
- HEVC_CM_HEADER_OFFSET (word 0x362B, DOS + 0xD8AC) = body size.
- HEVC_CM_HEADER_LENGTH (word 0x3629, DOS + 0xD8A4) = header size.
- The workaround bit is always 0 in this tree: the flag that would set it (DEC_FLAG_HEVC_WORKAROUND) is never set by vdec.c.

**C. Reference address table ("ANC2AXI"; at list build):**
1. HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR (DOS + 0xD180) = 0.
2. For each picture slot i, write HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR (DOS + 0xD184) = address | (index << 8) | 1.
   - The address is the compressed body (compressed modes) or the Y plane (mode 0x10). The index is i.
   - In mode 0x10, two entries per picture: Y with index 2i and CbCr with index 2i + 1.
3. Unused slots up to 16 repeat the last picture.
4. CONF_ADDR = 1.
5. Clear the reference canvas list: HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR (DOS + 0xD300) = 1, then 32 writes of 0 to HEVCD_MPP_ANC_CANVAS_DATA_ADDR (DOS + 0xD304).

Bits 12:8 carry the index, so addresses must have their low 16 bits free. The vendor's 64 KiB alignment does this; the exact requirement is **UNVERIFIED**.

**D. Per new picture** (slice_segment_address = 0):
- Pick a free slot: not referenced, not waiting for output, not on display; lowest POC first.
- Set the slot's POC to the current POC.
- Output pictures before decoding this one (§5.11).
- Drop references not in the current RPS.
- SAO line memory:
  - sao_mem_unit = 0x90 / 0xE0 / 0x180 for 16 / 32 / 64-pixel LCUs.
  - HEVC_sao_mem_unit (SCRATCH_9) = that value.
  - HEVC_sao_vb_size (SCRATCH_B) = (sao_mem_unit + 0x20) × picture height in LCUs.
  - HEVC_SAO_ABV and HEVC_SAO_VB = the workspace addresses.
  - With tiles, column j uses sao_vb + j × sao_vb_size and row i uses sao_abv + i × sao_mem_unit × width in LCUs. Only the base addresses are written to the scratch registers.
- HEVC_DBLK_CFG2 (DOS + 0xD408) = w | h << 16.
- HEVC_DBLK_CFG1 (DOS + 0xD404) fields:
  - [3] = pcm_loop_filter_disabled (only if PCM is enabled);
  - [8:4] = pps_cb_qp_offset & 0x1F;
  - [13:9] = pps_cr_qp_offset & 0x1F;
  - [1:0] = 0 / 1 / 2 for 64 / 32 / 16-pixel LCUs.
- HEVC_SAO_Y_START_ADDR = 0xFFFFFFFF first, then the real address (part E).

**E. Per slice segment, in this order:**

1. **Reference list** (P and B slices).
   - HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR = 1 (list 0 from slot 0). Then one write to HEVCD_MPP_ANC_CANVAS_DATA_ADDR per L0 entry: (C << 16) | (C << 8) | Y. Here Y and C are the reference's ANC2AXI indexes: both i in compressed modes, 2i and 2i + 1 in mode 0x10.
   - For B slices, ACCCONFIG = (16 << 8) | 1, then the L1 entries.
   - A missing reference marks the picture as erroneous.
2. **MCRCC** (motion-compensation reference cache).
   - Write HEVCD_MCRCC_CTL1 (DOS + 0xD3C0) = 2 (reset).
   - I slice: CTL1 = 0 (cache off).
   - P slice: read back list-0 entries 0 and 1 through ACCCONFIG = 2 (slot 0, read mode, **UNVERIFIED** bit meaning). Write CTL2 = entry 0's low 16 bits, duplicated into the upper half. Write CTL3 = entry 1 the same way, or entry 0 again when only one reference exists (that check applies in mode 0x10; otherwise entry 1 is always used).
   - B slice: CTL2 from list 0 entry 0 (ACCCONFIG = 0). CTL3 from list 1 entry 0 (ACCCONFIG = (16 << 8) | 2), or list 1 entry 1 if entry 0 equals CTL2.
   - Then CTL1 = 0xFF0 ("progressive mode").
3. **Motion prediction** (MPRED):

| Register | DOS offset | Value |
|---|---|---|
| HEVC_MPRED_MV_WR_START_ADDR | 0xC84C | current picture's MV buffer |
| HEVC_MPRED_MV_RD_START_ADDR | 0xC850 | collocated picture's MV buffer (the current picture's own for I slices) |
| HEVC_MPRED_MV_WR_ROW_JUMP, _RD_ROW_JUMP | 0xC85C, 0xC860 | (LCUs per row − tile width in LCUs) × MV_MEM_UNIT |
| HEVC_MPRED_CTRL0 | 0xC804 | [1:0] slice type (0 B, 1 P, 2 I); [2] new picture; [3] new tile; [4] dependent slice segment; [5] TMVP enabled; [6] low-delay (no reference POC above current); [7] collocated_from_l0; [8] 0; [9] 1 (above enable); [10] 1 (MV write); [11] MV read: 1 for P/B, 0 for I; [12] 0; [13] 1 (linear buffer); [14] 0, [15] 0 (long-term); [19:16] log2 LCU size; [23:20] 3 (log2 min CU); [27:24] log2_parallel_merge_level |
| HEVC_MPRED_CTRL1 | 0xC808 | MaxNumMergeCand (5 − idx32) \| 2<<4 \| 3<<8 \| 5<<12 \| 36<<16 |
| HEVC_MPRED_PIC_SIZE | 0xC814 | w \| h<<16 |
| HEVC_MPRED_PIC_SIZE_LCU | 0xC818 | (LCUs across − 1) \| (LCUs down − 1)<<16 |
| HEVC_MPRED_TILE_START | 0xC81C | tile x \| y<<16 (LCUs) |
| HEVC_MPRED_TILE_SIZE_LCU | 0xC820 | tile w \| h<<16 (LCUs) |
| HEVC_MPRED_REF_NUM | 0xC824 | L0 count \| L1 count<<8 |
| HEVC_MPRED_LT_REF | 0xC828 | 0 |
| HEVC_MPRED_REF_EN_L0 / _L1 | 0xC830 / 0xC834 | (1 << count) − 1 |
| HEVC_MPRED_CUR_POC / _COL_POC | 0xC980 / 0xC984 | current POC / collocated POC |
| HEVC_MPRED_L0_REFnn_POC, HEVC_MPRED_L1_REFnn_POC | 0xC880 + 4n, 0xC8C0 + 4n | all 16 pairs, written **in pair order** L0[0], L1[0], L0[1], L1[1], …; the vendor stresses that this order is required |
| new picture only: HEVC_MPRED_ABV_START_ADDR | 0xC848 | workspace mpred_above |
| new picture only: HEVC_MPRED_MV_WPTR | 0xC854 | current MV buffer + slice address × MV_MEM_UNIT |
| new picture: HEVC_MPRED_MV_RPTR | 0xC858 | collocated MV buffer start |
| later independent slices: HEVC_MPRED_MV_RPTR | 0xC858 | collocated MV buffer + slice address × MV_MEM_UNIT |
| HEVC_MPRED_MV_RD_END_ADDR | 0xC988 | collocated MV buffer + LCUs × MV_MEM_UNIT |

4. **SAO and deblocking:**

| Register | DOS offset | Value |
|---|---|---|
| HEVC_SAO_CTRL0 | 0xD804 | [3:0] = log2 LCU size |
| HEVC_SAO_PIC_SIZE | 0xD814 | w \| h<<16 |
| HEVC_SAO_PIC_SIZE_LCU | 0xD818 | as for MPRED |
| HEVC_SAO_CTRL5 | 0xD88C | compressed modes: [23:16] = 0 for double-write mode 1, else 0xFF; [9] = memory-saving; [7] = workaround (0). Mode 0x10: [23:16] = 0 |
| HEVC_CM_BODY_START_ADDR | 0xD898 (word 0x3626) | compressed body address (not in mode 0x10) |
| HEVC_SAO_Y_START_ADDR / HEVC_SAO_Y_WPTR | 0xD82C / 0xD83C | double-write Y address (when double write is on) |
| HEVC_SAO_C_START_ADDR / HEVC_SAO_C_WPTR | 0xD834 / 0xD840 | double-write CbCr address |
| HEVC_SAO_Y_LENGTH / HEVC_SAO_C_LENGTH | 0xD830 / 0xD838 | 2C / C (written in every mode) |
| HEVC_SAO_CTRL1 | 0xD808 | [13:12] = memory map mode (0 linear, 1 32×32, 2 64×32; vendor default 0). [11:4] = 0xFF ("big-endian per 64-bit"; default `endian` = 0xFF0). [1] = 1 disables double write (mode 0). [0] = 1 disables compression (mode 0x10) |
| HEVCD_IPP_AXIIF_CONFIG | 0xD02C | [5:4] = memory map mode; [3:0] = 0xF (vendor comment: matters only in double-write-only mode) |
| HEVC_DBLK_CFG9 | 0xD424 | [0] loop filter across tiles (if tiles); [1] across slices; [2] deblocking disabled; [6:3] beta_offset_div2; [10:7] tc_offset_div2. Slice values if override is enabled and used, else PPS values |
| HEVC_SAO_CTRL0 | 0xD804 | also [8] across tiles, [9] across slices (same choice as CFG9 bits 0 and 1) |

**Clock gating:** the vendor never forces any decoder clock on in normal use. A debug flag sets "clock forced on" bits: IQIT_CLK_RST_CTRL bits 2:1, DBLK_CFG0 bits 2:1 and 29:16, SAO_CTRL1 bit 2, MPRED_CTRL1 bit 24, STREAM_CONTROL / PARSER_CORE_CONTROL / PARSER_INT_CONTROL bit 15, CABAC_CONTROL bit 13, PARSER_IF_CONTROL bits 6, 3, 1, IPP_DYNCLKGATE_CONFIG all bits, MCRCC_CTL1 bit 3.

### 5.10 Output modes ("double write")

The module parameter `double_write_mode` defaults to **0**.

| double_write_mode | Reference frames | Output | SAO_CTRL1 [1:0] | Display path |
|---|---|---|---|---|
| 0 (default) | compressed | compressed only | 0b10 | vframe marked compressed. The VPU's AFBC decoder reads body and header (below) |
| 1 | compressed | compressed + full-size uncompressed 4:2:0 copy | 0b00 | vframe marked NV21 |
| 2 | compressed | compressed + 1/4 × 1/4 copy (SAO_CTRL5 [23:16] = 0xFF) | 0b00 | NV21, reported size w/4 × h/4. The buffer is still sized for full resolution |
| 0x10 | uncompressed (the MC reads uncompressed references: DECOMP_CTL1 bit 31) | uncompressed only | 0b01 | NV21 |

**The display side:**
- **Uncompressed modes:** the vendor sets up two DMC canvases per picture for the VPU only: Y at index 128 + 2i, CbCr at 129 + 2i.
  - Width = picture width (÷4 in mode 2), aligned to 32 for linear memory (64 for block modes). Height aligned to 32.
  - Canvas endian field 7.
  - Using the same 32-aligned width as the output line stride is inferred (**UNVERIFIED**). The mainline family also sizes NV12 output with 32-aligned width and height.
- **Chroma order:** the vendor labels the uncompressed copy NV21 for its display path. With the same SAO_CTRL1 endian value 0xFF0, the mainline family hands the output to V4L2 as NV12. Which byte order the memory actually holds, CbCr or CrCb, is **UNVERIFIED** here.
- **Compressed output to the display:** canvas0 = body + body size (the header) and canvas1 = body; bit depth from the RPM, plus a memory-saving flag. Reading `AFBC_BODY_BADDR` (VCBUS) << 4 is how the vendor finds the buffer on screen on GXBB.
- **Advertised profile:** the GXBB decoder registers the profile string "4k, 9bit, 10bit, dwrite, compressed".
- **For a driver that hands frames to software:** mode 1 or mode 0x10 gives an uncompressed copy. Mode 0x10 avoids the compressed buffers entirely, but the vendor never enabled it by default, and the microcode meant for it (`vh265_mc_dw`) is missing from the tree. Whether mode 0x10 works with `vh265_mc` on GXBB is **UNVERIFIED**.

### 5.11 Output order and end of stream

- **When a picture becomes ready:** at each new picture (and at an IDR, by flushing everything), the host marks the previous picture "to output".
- **Which picture goes out:** the lowest POC among pictures marked for output. It goes out once more pictures wait than that picture's sps_num_reorder_pics, or when 15 are waiting, or on a flush.
- **Completeness check:** before each new picture the host compares HEVC_PARSER_LCU_START (DOS + 0xC490) & 0xFFFFFF with the previous picture's LCU count − 1. If it is lower, the previous picture is marked erroneous. This register is the only progress indicator in the protocol.
- **End of stream:** an EOS NAL (type 36, NAL header bytes 0x48 0x01) produces status 9 (NAL_SEARCH_CTL bit 16 = 0). The host flushes every pending picture and resets its POC state.
- **No other end-of-stream mechanism:** the vendor has no padding scheme of its own for HEVC.

### 5.12 Errors and recovery

**Watchdog** (every 10 ms):
- If the stream isn't empty, the level is above 0x200, and nothing is displayed for 30 ticks, set error state 1 and re-raise the IRQ.
- That makes the handler switch to manual NAL search skipping n NALs (NAL_SEARCH_CTL = (n<<4) | 1, with n = 6 by default) and answer 0xFF.
- After 10 more stuck ticks, run a full recovery.

**Full recovery (`hevc_recover`):**
1. Stop the AMRISC.
2. Copy HEVC_STREAM_RD_PTR into PARSER_VIDEO_RP, with PARSER_ES_CONTROL bit 0 set.
3. Save HEVC_STREAM_START_ADDR, END_ADDR, RD_PTR, WR_PTR, CONTROL and FIFO_CTL.
4. Recompute the 64-bit consumed-byte count for the new read position.
5. DOS_SW_RESET3 = 0x000ED918, then 0.
6. Restore the stream registers, including HEVC_SHIFT_BYTE_COUNT.
7. Rewrite the workspace registers.
8. Repeat §5.6 steps 2–18. The PARSER_VERSION check is not repeated. The Android M driver also repeats step 19.
9. WAIT_FLAG = 1, MBOX1_CLR = 1, MBOX1_MASK = 1, PSCALE_CTRL = 0. Clear PARSER_ES_CONTROL bit 0. DEBUG_REG1 = 0. NAL_SEARCH_CTL = 0xC. DECODE_STOP_POS = 0.
10. Rebuild the ANC2AXI table.
11. Start the AMRISC.
12. Clear WAIT_FLAG bit 1, write SCRATCH_0 = 0xFF, kick.

**Microcode debug messages:** if DEBUG_REG1 is non-zero at an interrupt, it is a debug message from the microcode (value in DEBUG_REG2), or a finished LMEM dump when bit 16 is set. The host prints it, clears DEBUG_REG1 and returns without other processing. This happens only when the debug flag was set at init.

**Android M differences on the same protocol:**
- At EOS it answers 0xF0 and kicks.
- It waits for the status register to leave 5 before stopping.
- It writes DECOMP_CTL1 bit 31 again after recovery in mode 0x10.
- It changes buffer bookkeeping.

### 5.13 Minimal sequence for one IDR picture (vendor order, default settings)

1. Gates on (§1.1); power-up (§1.3).
2. Stream ring registers (§3, `stbuf_init`).
3. Microcode load (§2).
4. Workspace registers (§4), then the one-time setup (§5.6).
5. Register the IRQ handler on SPI 44 (§6). The mailbox mask is already on.
6. Pulse the AMRISC reset: three reads of DOS_SW_RESET3, write 0x1800, write 0, three reads.
7. HEVC_MPSR = 1. The microcode now runs.
8. Hook the parser (§3, `esparser_init`) and push VPS + SPS + PPS + the IDR slice NAL(s), then an EOS NAL (00 00 01 48 01).
   - How much data must follow to push the last NAL through the parser and stream FIFO is **UNVERIFIED** for HEVC. The VDEC_1 paths in base §5.9 and the H.264 document §10 append padding.
9. Status 8 arrives (first slice).
   - Read the RPM: width (idx 23), height (idx 24), LCU size (idx 25/26), bit depth (idx 60), reorder count (idx 33).
   - Allocate the picture buffers and program parts B and C of §5.9.
   - Then (the vendor raises its own IRQ here) process the header: POC 0; no references; MCRCC off; MPRED with slice type 2; SAO/DBLK; output address.
   - WAIT_FLAG |= 2, SCRATCH_0 = 5, HEVC_MCPU_INTR_REQ = 4.
10. For each further slice segment of the picture: status 8 with slice_segment_address ≠ 0. Same processing without part D. Answer 5.
11. **Picture complete:** status 9 with SCRATCH_J = 36 (EOS) means the IDR is done. Check HEVC_PARSER_LCU_START & 0xFFFFFF = LCUs − 1 (**UNVERIFIED** as a completion signal). Read the output: the double-write planes, or the compressed body and header.
12. Stop (§1.5), then power down.

---

## 6. Interrupts

**The C2 vendor device tree:** node `vdec`, `interrupts = <0 3 1  0 23 1  0 32 1  0 43 1  0 44 1  0 45 1>`, names "vsync", "demux", "parser", "mailbox_0", "mailbox_1", "mailbox_2".
- All are SPIs with flag 1 (edge rising).
- So mailbox_0 = SPI 43 (INTID 75), mailbox_1 = SPI 44 (INTID 76), mailbox_2 = SPI 45 (INTID 77), parser = SPI 32 (INTID 64). The vendor header agrees: INT_DOS_MAILBOX_1 = 32 + 44.

**Which line HEVC uses:** vh265 requests "mailbox_1" (VDEC_IRQ_1, SPI 44) as a shared IRQ. VDEC_1's decoders use the same line. Only one decoder runs at a time (`SUPPORT_VCODEC_NUM` = 1).

**HEVC mailbox registers** (DOS byte offsets):

| Register | Offset | Use |
|---|---|---|
| HEVC_ASSIST_MBOX0_IRQ_REG / CLR_REG / MASK | 0xC1C0 / 0xC1C4 / 0xC1C8 | not used by vh265 |
| **HEVC_ASSIST_MBOX1_IRQ_REG** | 0xC1D0 | writing 1 raises mailbox 1. The microcode uses mailbox 1 to signal the host; the host also writes it to re-enter its own handler (buffer returned, deferred allocation done, watchdog) |
| **HEVC_ASSIST_MBOX1_CLR_REG** | 0xC1D4 | write 1 to clear. Done at init and recovery only |
| **HEVC_ASSIST_MBOX1_MASK** | 0xC1D8 | 1 = enabled (init), 0 = disabled (stop) |
| HEVC_ASSIST_MBOX2_IRQ_REG | 0xC1E0 | not used |
| HEVC_MCPU_INTR_REQ | 0xCC14 | host → AMRISC: 4 = main request |
| HEVC_MCPU_INTR_MSK | 0xCC10 | not written by the host |

**Acknowledge:** the vendor's handler does not clear mailbox 1. Either the microcode clears it when it is kicked, or edge triggering makes clearing unnecessary; which is **UNVERIFIED**. The mainline VDEC_1 code clears its mailbox in the handler (base §7). Clearing it in a new driver is harmless as long as it happens before the kick.

That mailbox_0 and mailbox_2 (SPI 43/45) map to HEVC MBOX0/MBOX2 rather than to VDEC_1/VDEC2 mailboxes is **UNVERIFIED**. On M8 the vendor used mailbox 0 for VDEC2.

---

## 7. Vendor protocol versus the mainline family (brief)

Compared with the out-of-tree `codec_hevc.c` (Maxime Jourdan's 2018 driver as carried by LibreELEC), which loads linux-firmware `gxl_hevc.bin`, including on GXBB in its platform table. The companion document covers it in depth.

**Same in both:**
- IMEM DMA: 0x1000 words, 0x00078000.
- Status values 0–9 and 0xFF; the kick (4 to HEVC_MCPU_INTR_REQ); mailbox 1.
- WAIT_FLAG bit 1 = decode this slice.
- The parser command table and the three SKIP values.
- HEVC_PARSER_INT_CONTROL (the vendor's 2<<26 is the mainline's BIT(27)) and HEVC_SHIFT_CONTROL = 0xE5.
- The order and sizes of the workspace regions from ipp up to dblk_para (with the vendor's 4K layout).
- SAO_CTRL1 [11:4] = 0xFF; IPP_AXIIF_CONFIG [3:0] = 0xF.
- The 4-halfword reversal of the RPM.

| Item | Vendor (GXBB, vh265_mc 2015) | Mainline family (gxl_hevc.bin) |
|---|---|---|
| SCRATCH_H | DEBUG_REG2 (read) | HEVC_DECODE_MODE2 (write 0) |
| SCRATCH_J | CUR_NAL_UNIT_TYPE (read with status 9) | HEVC_DECODE_MODE (write 0 = single) |
| SCRATCH_L / M / N | swap test 0 / picture begin 0 / picture count 0xFFFFFFFF | AUX buffer address / AUX size / decode size (0) |
| SCRATCH_7 | stream swap buffer | swap buffer, or the MMU map on G12A |
| AUX (SEI) buffer | none | 16 KiB |
| Statuses the host handles | 8 and 9 | 8 and 0xB (0xA–0xD defined) |
| Initial NAL_SEARCH_CTL | 0x8 | 0xC |
| RPM location | separate 256-byte buffer | inside the workspace |
| RPM layout | tile_width[4], tile_height[4] | tile_width[8], tile_height[8]. Every field from index 42 on is 8 halfwords later: misc_flag0 54, m_nalUnitType 63, bit_depth 68, modification_list 72, conformance window 104. Extra fields at the end (video signal, colour, aspect) |
| Workspace after dblk_para | dblk_data 0x40000, then mpred | dblk_data 0x80000, dblk_data2 0x80000, mmu_vbh 0x5000, then mpred |
| MV buffers | 16 × 0x120000 | 24 × 0x120000 |
| LMEM dump | 0x800 | 0xA00 |
| HEVC clock | 500 MHz at power-up, then 100–648 MHz by resolution × fps | fixed 666.67 MHz |
| Revision-A self-test | yes (§1.4) | no |

The RPM layout difference matters most: a host written for one microcode misreads every field from misc_flag0 on with the other.

---

## 8. Open questions

1. Does the 2015 vendor microcode, or linux-firmware's `gxl_hevc.bin`, run on this board's GXBB revision C? The mainline family claims `gxl_hevc.bin` does. The vendor image is the known-good one for this SoC, but it is not redistributable.
2. Mailbox acknowledge: does the microcode clear HEVC_ASSIST_MBOX1 itself? Is a host-side clear in the handler harmful if it races with the microcode's next post? (§6)
3. In automatic header mode, is status 9 really reported only for EOS (§5.7)? Does the microcode wait for an answer after an EOS report? The 3.14 driver gives none; the Android M driver answers 0xF0.
4. Chroma order and stride of the double-write output: NV12 or NV21 bytes with SAO_CTRL1 [11:4] = 0xFF, and a stride of ALIGN(width, 32)? (§5.10)
5. Does double-write-only mode (0x10) work with `vh265_mc`, given that a dedicated `vh265_mc_dw` once existed? Mode 1 is the safer choice; it costs a compressed copy per picture (3 MiB at 1080p).
6. A reliable "picture decoded" signal: is HEVC_PARSER_LCU_START = LCUs − 1 dependable, or must the host wait for the next slice header or an EOS? (§5.11)
7. Does the HEVC block need a DMC_REQ_CTRL bit on GXBB (the vendor sets bit 13 only for VDEC_1), or the DMC_AM5_CHAN_CTRL urgency write? (§1.3)
8. Meaning of DOS_SW_RESET3 bits 11/12 and of HEVC_IMEM_DMA_CTRL bits 18:16 (assumed as for VDEC_1).
9. Do the VDEC-mirror registers that `amhevc_disable` polls (HEVC_MDEC_PIC_DC_STATUS, HEVC_DBLK_STATUS, HEVC_DCAC_DMA_CTRL) exist on GXBB's HEVC block? (§1.5)
10. How much trailing data is needed for the parser to push the final NAL into the HEVC ring (§5.13 step 8)?
11. The meaning of HEVC_WAIT_FLAG bit 0, and of the unnamed HEVC_PARSER_INT_CONTROL bits 31:29 and 27:26.
12. Is m_temporalId really TemporalId + 1 (§5.8)?

---

## 9. Register reference (vendor word index → DOS byte offset)

| Register | Word | DOS + | Register | Word | DOS + |
|---|---|---|---|---|---|
| HEVC_ASSIST_MBOX0_IRQ_REG | 0x3070 | 0xC1C0 | HEVC_MPRED_CTRL0 | 0x3201 | 0xC804 |
| HEVC_ASSIST_MBOX1_IRQ_REG | 0x3074 | 0xC1D0 | HEVC_MPRED_CTRL1 | 0x3202 | 0xC808 |
| HEVC_ASSIST_MBOX1_CLR_REG | 0x3075 | 0xC1D4 | HEVC_MPRED_PIC_SIZE | 0x3205 | 0xC814 |
| HEVC_ASSIST_MBOX1_MASK | 0x3076 | 0xC1D8 | HEVC_MPRED_PIC_SIZE_LCU | 0x3206 | 0xC818 |
| HEVC_ASSIST_SCRATCH_0 … _N | 0x30C0 … 0x30D7 | 0xC300 … 0xC35C | HEVC_MPRED_TILE_START | 0x3207 | 0xC81C |
| HEVC_PARSER_VERSION | 0x3100 | 0xC400 | HEVC_MPRED_TILE_SIZE_LCU | 0x3208 | 0xC820 |
| HEVC_STREAM_CONTROL | 0x3101 | 0xC404 | HEVC_MPRED_REF_NUM | 0x3209 | 0xC824 |
| HEVC_STREAM_START_ADDR | 0x3102 | 0xC408 | HEVC_MPRED_LT_REF | 0x320A | 0xC828 |
| HEVC_STREAM_END_ADDR | 0x3103 | 0xC40C | HEVC_MPRED_REF_EN_L0 / L1 | 0x320C / 0x320D | 0xC830 / 0xC834 |
| HEVC_STREAM_WR_PTR | 0x3104 | 0xC410 | HEVC_MPRED_ABV_START_ADDR | 0x3212 | 0xC848 |
| HEVC_STREAM_RD_PTR | 0x3105 | 0xC414 | HEVC_MPRED_MV_WR_START_ADDR | 0x3213 | 0xC84C |
| HEVC_STREAM_LEVEL | 0x3106 | 0xC418 | HEVC_MPRED_MV_RD_START_ADDR | 0x3214 | 0xC850 |
| HEVC_STREAM_FIFO_CTL | 0x3107 | 0xC41C | HEVC_MPRED_MV_WPTR / RPTR | 0x3215 / 0x3216 | 0xC854 / 0xC858 |
| HEVC_SHIFT_CONTROL | 0x3108 | 0xC420 | HEVC_MPRED_MV_WR/RD_ROW_JUMP | 0x3217 / 0x3218 | 0xC85C / 0xC860 |
| HEVC_SHIFT_STARTCODE | 0x3109 | 0xC424 | HEVC_MPRED_CURR_LCU | 0x3219 | 0xC864 |
| HEVC_SHIFT_EMULATECODE | 0x310A | 0xC428 | HEVC_MPRED_L0_REF00..15_POC | 0x3220–0x322F | 0xC880–0xC8BC |
| HEVC_SHIFT_STATUS | 0x310B | 0xC42C | HEVC_MPRED_L1_REF00..15_POC | 0x3230–0x323F | 0xC8C0–0xC8FC |
| HEVC_SHIFT_BYTE_COUNT | 0x310D | 0xC434 | HEVC_MPRED_CUR_POC / COL_POC | 0x3260 / 0x3261 | 0xC980 / 0xC984 |
| HEVC_CABAC_CONTROL | 0x3110 | 0xC440 | HEVC_MPRED_MV_RD_END_ADDR | 0x3262 | 0xC988 |
| HEVC_PARSER_CMD_WRITE | 0x3112 | 0xC448 | HEVC_MPSR | 0x3301 | 0xCC04 |
| HEVC_PARSER_CORE_CONTROL | 0x3113 | 0xC44C | HEVC_MCPU_INTR_MSK / REQ | 0x3304 / 0x3305 | 0xCC10 / 0xCC14 |
| HEVC_PARSER_INT_CONTROL | 0x3120 | 0xC480 | HEVC_CPSR | 0x3321 | 0xCC84 |
| HEVC_PARSER_INT_STATUS | 0x3121 | 0xC484 | HEVC_IMEM_DMA_CTRL / ADR / COUNT | 0x3340–0x3342 | 0xCD00–0xCD08 |
| HEVC_PARSER_IF_CONTROL | 0x3122 | 0xC488 | HEVCD_IPP_TOP_CNTL | 0x3400 | 0xD000 |
| HEVC_PARSER_LCU_START | 0x3124 | 0xC490 | HEVCD_IPP_LINEBUFF_BASE | 0x3409 | 0xD024 |
| HEVC_PARSER_CMD_SKIP_0/1/2 | 0x3128–0x312A | 0xC4A0–0xC4A8 | HEVCD_IPP_AXIIF_CONFIG | 0x340B | 0xD02C |
| HEVC_DBLK_CFG0/1/2 | 0x3500–0x3502 | 0xD400–0xD408 | HEVCD_IPP_DYNCLKGATE_CONFIG | 0x3420 | 0xD080 |
| HEVC_DBLK_CFG4/5 | 0x3504 / 0x3505 | 0xD410 / 0xD414 | HEVCD_IPP_DBG_SEL / DATA | 0x3430 / 0x3431 | 0xD0C0 / 0xD0C4 |
| HEVC_DBLK_CFG9 | 0x3509 | 0xD424 | HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR | 0x3460 | 0xD180 |
| HEVC_SAO_CTRL0 / CTRL1 | 0x3601 / 0x3602 | 0xD804 / 0xD808 | HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR | 0x3461 | 0xD184 |
| HEVC_SAO_PIC_SIZE / _LCU | 0x3605 / 0x3606 | 0xD814 / 0xD818 | HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR | 0x34C0 | 0xD300 |
| HEVC_SAO_Y_START_ADDR / Y_LENGTH | 0x360B / 0x360C | 0xD82C / 0xD830 | HEVCD_MPP_ANC_CANVAS_DATA_ADDR | 0x34C1 | 0xD304 |
| HEVC_SAO_C_START_ADDR / C_LENGTH | 0x360D / 0x360E | 0xD834 / 0xD838 | HEVCD_MPP_DECOMP_CTL1 / CTL2 | 0x34C2 / 0x34C3 | 0xD308 / 0xD30C |
| HEVC_SAO_Y_WPTR / C_WPTR | 0x360F / 0x3610 | 0xD83C / 0xD840 | HEVCD_MCRCC_CTL1/2/3 | 0x34F0–0x34F2 | 0xD3C0–0xD3C8 |
| HEVC_SAO_CTRL5 | 0x3623 | 0xD88C | HEVC_IQIT_CLK_RST_CTRL | 0x3700 | 0xDC00 |
| HEVC_CM_BODY_START_ADDR | 0x3626 | 0xD898 | HEVC_IQIT_SCALELUT_WR_ADDR / DATA | 0x3702 / 0x3704 | 0xDC08 / 0xDC10 |
| HEVC_CM_BODY_LENGTH | 0x3627 | 0xD89C | HEVC_PSCALE_CTRL | 0x3911 | 0xE444 |
| HEVC_CM_HEADER_LENGTH | 0x3629 | 0xD8A4 | HEVC_DBLK_CTRL / STATUS (mirror) | 0x3951 / 0x3953 | 0xE544 / 0xE54C |
| HEVC_CM_HEADER_OFFSET | 0x362B | 0xD8AC | HEVC_MDEC_PIC_DC_CTRL / STATUS (mirror) | 0x398E / 0x398F | 0xE638 / 0xE63C |
| DOS_GEN_CTRL0 | 0x3F02 | 0xFC08 | HEVC_DCAC_DMA_CTRL (mirror) | 0x3E12 | 0xF848 |
| DOS_MEM_PD_HEVC | 0x3F33 | 0xFCCC | DOS_SW_RESET3 | 0x3F34 | 0xFCD0 |
| DOS_GCLK_EN3 | 0x3F35 | 0xFCD4 | | | |

**Outside DOS:**

| Register | Address |
|---|---|
| AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 |
| AO_RTI_GEN_PWR_ISO0 | 0xC81000EC |
| HHI_GCLK_MPEG0 / MPEG1 | 0xC883C140 / 0xC883C144 |
| HHI_VDEC2_CLK_CNTL | 0xC883C1E4 |
| HHI_VDEC4_CLK_CNTL | 0xC883C1EC |
| DMC_REQ_CTRL | 0xC8838000 |
| DMC_AM5_CHAN_CTRL | 0xC8838248 |
| PARSER_ES_CONTROL | 0xC110A5DC |
| PARSER_VIDEO_START_PTR / END_PTR / WP / RP / HOLE | 0xC110A600 / 604 / 608 / 60C / 610 |

The HEVC_CM_* registers are defined locally in vh265.c, not in the vendor's hevc_regs.h.

---

## Sources

All from https://github.com/hardkernel/linux, branch `odroidc2-3.14.y` at c3e4c730feb1750940971cae9ca1da3ca50f1d56 unless noted. Raw URLs are https://raw.githubusercontent.com/hardkernel/linux/c3e4c730feb1750940971cae9ca1da3ca50f1d56/ + path.

**Driver:**
- drivers/amlogic/amports/vh265.c
- drivers/amlogic/amports/vdec.c and vdec.h
- drivers/amlogic/amports/amvdec.c
- drivers/amlogic/amports/esparser.c
- drivers/amlogic/amports/streambuf.c
- drivers/amlogic/amports/amstream.c
- drivers/amlogic/amports/amports_gate.c
- drivers/amlogic/amports/vdec_reg.h

**arch/ (clocks, firmware, register access):**
- drivers/amlogic/amports/arch/clkgx.c, clk.c, clk.h
- drivers/amlogic/amports/arch/firmware.c, firmware.h, chips.c
- drivers/amlogic/amports/arch/register_ops.c, register_ops.h, register_ops_m8.c, register_map.c
- drivers/amlogic/amports/arch/regs/hevc_regs.h, dos_regs.h, ao_regs.h, hhi_regs.h, parser_regs.h, dmc_regs.h

**Microcode:**
- drivers/amlogic/amports/arch/ucode/h265/vh265_mc.c, arch/ucode/firmware_def.h

**Device tree and bindings:**
- arch/arm64/boot/dts/meson64_odroidc2.dts
- include/dt-bindings/reset/aml_gxbb.h

**Android M branch:**
- https://raw.githubusercontent.com/hardkernel/linux/18d416162ec7dfc0f582dd9b77a57112e3307376/drivers/amlogic/amports/vh265.c
- https://raw.githubusercontent.com/hardkernel/linux/18d416162ec7dfc0f582dd9b77a57112e3307376/drivers/amlogic/amports/arch/ucode/h265/vh265_mc.c

**Commit history through the GitHub API:** fa0377cba2c3, 02c48d59fd36, 2d5875f87287, b029e7062877.

**For §7 only:** the out-of-tree `codec_hevc.c`, `codec_hevc_common.c` and `vdec_hevc.c` (Maxime Jourdan lineage, as carried by LibreELEC), the copy used for the companion mainline-family document.

**From this repository:** logs/m9-sep-1.log, for the board's SoC revision line.
