# M26: S905 (GXBB) VDEC_1 MPEG-1/2 decoding, hardware facts

> Facts read from Linux's GPL drivers/staging/media/meson/vdec (mainline master at commit abf8c33470f7, 2026-10-30; the last change to that directory is b3a805dd5a83, committed 2026-10-04) and the GX device tree and clock/reset drivers; no code copied.

This is the input for a clean-room, MIT-licensed VDEC driver for Haiku. It lists register names, offsets, values, ordering, timing and data layouts, in tables and short prose. Where Linux gives no meaning for a value, or where a meaning is inferred from names or behaviour, the text says **UNVERIFIED**.

---

## 0. Conventions and address map

- All registers are 32 bits wide and Linux accesses them with 32-bit loads and stores. It uses relaxed (barrier-free) MMIO, so the only ordering is program order to one device.
- **DOS offsets in this document are byte offsets from the DOS base**, the way Linux's dos_regs.h numbers them. Amlogic's vendor headers and the datasheet number DOS registers by 32-bit word index, which is the byte offset divided by 4. §9 gives both.
- ESPARSER offsets are byte offsets from 0xC110A580. That block is part of CBUS, at CBUS word index 0x2960 + offset/4.
- "Set bits" and "clear bits" mean read-modify-write. "Write" means a full 32-bit store.

| Block | Physical base | Size | Where it comes from |
|---|---|---|---|
| DOS (decoder registers) | 0xC882_0000 | 64 KiB | vdec node, reg "dos" |
| ESPARSER | 0xC110_A580 | 0xE4 | vdec node, reg "esparser" |
| AO sysctrl (syscon) | 0xC810_0000 | 0x100 | `sysctrl_AO` at aobus 0xC8100000 + 0 |
| HHI (clock registers) | 0xC883_C000 | 0x400 | hiubus `sysctrl` |
| Reset controller | 0xC110_4404 | 0x9C | cbus 0xC1100000 + 0x4404, "amlogic,meson-gxbb-reset" |
| DMC canvas LUT | 0xC883_8048 | 0x14 | dmcbus 0xC8838000 + 0x48 |
| IRQ "vdec" | GIC SPI 44 (INTID 76) | edge rising | firmware-to-host mailbox 1 |
| IRQ "esparser" | GIC SPI 32 (INTID 64) | edge rising | parser start-code interrupt |

DMA: the GX device tree does not mark the decoder `dma-coherent`. Linux gives the decoder non-cacheable memory for its own buffers (firmware staging, VIFIFO, workspace) and does cache maintenance on the bitstream and picture buffers. Every address written to a register is a 32-bit physical address. GXBB has no IOMMU in this path.

---

## 1. Power-up and power-down of VDEC_1 (GXBB)

### 1.1 Clock gates

| DT clock name | Clock ID (gxbb-clkc.h) | Register | Phys address | Bit | Parent |
|---|---|---|---|---|---|
| `dos_parser` | CLKID_DOS_PARSER = 54 | HHI_GCLK_MPEG1 (HHI + 0x144) | 0xC883C144 | 25 | clk81 |
| `dos` | CLKID_DOS = 17 | HHI_GCLK_MPEG0 (HHI + 0x140) | 0xC883C140 | 1 | clk81 |
| `vdec_1` | CLKID_VDEC_1 = 153 | HHI_VDEC_CLK_CNTL (HHI + 0x1E0) | 0xC883C1E0 | 8 | vdec_1_div (see 1.2) |
| `vdec_hevc` | CLKID_VDEC_HEVC = 156 | HHI_VDEC2_CLK_CNTL (HHI + 0x1E4) | 0xC883C1E4 | 24 | listed in the GXBB DT and fetched at probe, but **never enabled** on the VDEC_1 path |

For all of these gates, 1 means the clock runs. Linux turns on `dos_parser` before `dos`, and turns them off in the reverse order.

### 1.2 HHI_VDEC_CLK_CNTL (0xC883C1E0): VDEC_1 clock

| Bits | Field | Meaning |
|---|---|---|
| [6:0] | divider | The divisor minus 1 (0 means divide by 1) |
| [8] | gate | 1 = VDEC_1 clock on |
| [10:9] | source | 0 = fclk_div4, 1 = fclk_div3, 2 = fclk_div5, 3 = fclk_div7 |

- With the fixed PLL at its nominal 2 GHz, the sources are 500, 666.67, 400 and 285.7 MHz. The boot firmware sets the fixed PLL and Linux treats it as read-only. On this board, read HHI_MPLL_CNTL to confirm it (**UNVERIFIED** here).
- Linux asks for 666,666,666 Hz, which its comment calls "the maximum available". The result is source 1 (fclk_div3), divider 0 and gate 1, so the low 11 bits read **0x300**. Linux changes only these fields and leaves the other bits alone.
- Order: Linux programs the source and divider while the gate is still off, then sets bit 8.
- The fclk_div3 gate (HHI_MPLL_CNTL6 at 0xC883C294, bit 28) is marked critical in Linux, so it is always on and nothing needs doing.

### 1.3 Power-up order (GXBB; GXL, GXM and G12A take the same path)

| # | Register | Address | Action | Value | Note |
|---|---|---|---|---|---|
| 1 | HHI_GCLK_MPEG1 | 0xC883C144 | set bit 25 | | DOS_PARSER clock |
| 2 | HHI_GCLK_MPEG0 | 0xC883C140 | set bit 1 | | DOS clock |
| 3 | HHI_VDEC_CLK_CNTL | 0xC883C1E0 | fields [10:9]=1, [6:0]=0 | | 666.67 MHz |
| 4 | HHI_VDEC_CLK_CNTL | 0xC883C1E0 | set bit 8 | | VDEC_1 clock on |
| 5 | AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 | clear bits 3:2 | mask 0x0000000C | VDEC_1 power on (read-modify-write) |
| 6 | wait | | 10–20 µs | | the only delay in power-up |
| 7 | DOS_SW_RESET0 | DOS + 0xFC00 | write | 0xFFFFFFFC | asserts every reset except bits 1:0 |
| 8 | DOS_SW_RESET0 | DOS + 0xFC00 | write | 0x00000000 | releases them |
| 9 | DOS_GCLK_EN0 | DOS + 0xFC04 | write | 0x000003FF | DOS internal clock gates |
| 10 | DOS_MEM_PD_VDEC | DOS + 0xFCC0 | write | 0x00000000 | powers all VDEC memories |
| 11 | AO_RTI_GEN_PWR_ISO0 | 0xC81000EC | write (whole register) | 0x00000000 | removes isolation |
| 12 | DOS_VDEC_MCRCC_STALL_CTRL | DOS + 0xFD00 | write | 0x00000000 | Linux's comment calls this "reset DOS top registers" |
| 13 | GCLK_EN | DOS + 0x260C | write | 0x000003FF | decoder clock gates |
| 14 | MDEC_PIC_DC_CTRL | DOS + 0x2638 | clear bit 31 | | meaning **UNVERIFIED** |

After this come the VIFIFO setup (§4), the firmware load (§2), the codec start (§5.4), the IRQ enable (§7), the output-format bit and the processor start (§8). Nothing is polled during power-up.

Notes:
- SLEEP0 uses bits 3:2 and ISO0 uses bits 7:6, as the power-down value 0xC0 shows. They are different bit positions.
- Linux writes the whole ISO0 register: 0 at power-up and 0xC0 at power-down. That also clears or sets every other bit in it. Which other blocks (HEVC, HCODEC) own bits there is **UNVERIFIED**. A read-modify-write of bits 7:6 alone is the conservative equivalent; that it is enough is inferred from the 0xC0 value (**UNVERIFIED**).
- SM1 uses bit 1 in both SLEEP0 and ISO0. That does not apply to GXBB.
- The DT's `meson-gxbb-pwrc` power controller is not used. The vdec driver writes the AO registers itself.
- What individual DOS_SW_RESET0 bits mean is **UNVERIFIED**.

### 1.4 Power-down order

Linux first waits until no vdec IRQ has arrived for 50 ms (see §8). Then:

| # | Register | Address | Action | Value |
|---|---|---|---|---|
| 1 | MPSR | DOS + 0x0C04 | write | 0 (stops the AMRISC) |
| 2 | CPSR | DOS + 0x0C84 | write | 0 |
| 3 | ASSIST_MBOX1_MASK | DOS + 0x01D8 | write | 0 (vdec IRQ off) |
| 4 | DOS_SW_RESET0 | DOS + 0xFC00 | write | 0x00001800 (bits 12 and 11) |
| 5 | DOS_SW_RESET0 | DOS + 0xFC00 | write | 0 |
| 6 | DOS_SW_RESET0 | DOS + 0xFC00 | read | read-back to flush posted writes |
| 7 | AO_RTI_GEN_PWR_ISO0 | 0xC81000EC | write (whole register) | 0x000000C0 (isolation on) |
| 8 | DOS_MEM_PD_VDEC | DOS + 0xFCC0 | write | 0xFFFFFFFF (memories off) |
| 9 | AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 | set bits 3:2 | VDEC_1 power off |
| 10 | (codec) | | free the MPEG workspace | |
| 11 | HHI_VDEC_CLK_CNTL | 0xC883C1E0 | clear bit 8 | |
| 12 | HHI_GCLK_MPEG0 | 0xC883C140 | clear bit 1 | |
| 13 | HHI_GCLK_MPEG1 | 0xC883C144 | clear bit 25 | |

The ESPARSER is neither stopped nor masked at power-down. It is reset at the next power-up.

---

## 2. Firmware load

**Which file:** GXBB uses `meson/vdec/gxl_mpeg12.bin` for both MPEG-1 and MPEG-2. The GXBB format table names it, and every GX/G12/SM1 table uses the same file; no gxbb_mpeg12 exists. The copy in this repo, `firmware/meson/vdec/gxl_mpeg12.bin`, is 16,384 bytes (md5 01607e4e6b3ecd98fb65b30302baebb9), identical to the host's linux-firmware copy.

**Size:** the AMRISC instruction memory takes exactly 16 KiB, which is 4096 32-bit words. Linux rejects a file shorter than 16 KiB. It loads the first 16 KiB, and anything beyond that would go to a codec-specific "extended firmware" hook. MPEG-1/2 has no such hook, and the file is exactly 16 KiB anyway.

**Staging buffer:**
- 16 KiB, physically contiguous, 32-bit physical address.
- The CPU copies the file bytes in unchanged; the CPU does no byte swapping.
- The device must see the data. Linux uses uncached coherent memory, so a cached buffer needs a cache clean before the DMA.
- Alignment: Linux's allocation is at least page aligned (in practice 16 KiB aligned). What the DMA engine itself needs is **UNVERIFIED**.
- Linux frees the buffer as soon as the DMA finishes, because the AMRISC runs from its internal memory.
- The firmware is loaded again at every session start, because power-down loses the instruction memory (inferred, **UNVERIFIED**).

**Sequence.** It runs after the power-up of §1.3 and the VIFIFO setup of §4.

| # | Register | Offset | Action | Value |
|---|---|---|---|---|
| 1 | MPSR | 0x0C04 | write | 0 (main AMRISC stopped) |
| 2 | CPSR | 0x0C84 | write | 0 (co-processor stopped) |
| 3 | MDEC_PIC_DC_CTRL | 0x2638 | clear bit 31 | |
| 4 | IMEM_DMA_ADR | 0x0D04 | write | physical address of the staging buffer |
| 5 | IMEM_DMA_COUNT | 0x0D08 | write | 0x1000 (length in 32-bit words) |
| 6 | IMEM_DMA_CTRL | 0x0D00 | write | 0x00078000: bit 15 starts the DMA (and reads 1 while busy); bits 18:16 = 7 |
| 7 | IMEM_DMA_CTRL | 0x0D00 | poll | until bit 15 reads 0 |

- Linux polls with about 1000 back-to-back reads and no delay. If bit 15 is still set, it reports "DMA hang" and fails the start. How long the transfer really takes is **UNVERIFIED**; a time-based timeout of a few ms is safe.
- The meaning of bits 18:16 = 7 is **UNVERIFIED**. The same value 7 is the 64-bit byte-swap code in the canvas endian field and in the parser's fetch command.
- Stopping MPSR and CPSR is all that is done to the AMRISC before the load. Linux does no separate processor reset; the DOS_SW_RESET0 pulse in power-up comes before.
- Afterwards the AMRISC is started by **MPSR = 1**, only once the codec registers, canvases, IRQ mask and output format are set (§8 steps 6–9). CPSR is never set to 1.

---

## 3. ESPARSER (elementary-stream parser)

### 3.1 Registers and fields used

| Register | Offset | Fields / use |
|---|---|---|
| PARSER_CONTROL | 0x00 | bit 0 ES_PARSER_START; bit 1 ES_SEARCH; bit 5 ES_WRITE; bits [31:8] ES_PACK_SIZE (byte count; field width **UNVERIFIED**) |
| PARSER_FETCH_ADDR | 0x04 | physical address of the source buffer |
| PARSER_FETCH_CMD | 0x08 | low bits: fetch length in bytes; from bit 27: fetch endian, value 7 (field widths **UNVERIFIED**; Linux defines the bit-27 constant under SEARCH_MASK but uses it here) |
| PARSER_CONFIG | 0x14 | from bit 0: PS_CFG_MAX_FETCH_CYCLE; bits 11:10: PS_CFG_STARTCODE_WID_24; from bit 12: PS_CFG_MAX_ES_WR_CYCLE; from bit 16: PS_CFG_PFIFO_EMPTY_CNT (field widths **UNVERIFIED**) |
| PFIFO_WR_PTR | 0x18 | the parser's internal FIFO write pointer; Linux zeroes it |
| PFIFO_RD_PTR | 0x1C | the parser's internal FIFO read pointer; Linux zeroes it |
| PARSER_SEARCH_PATTERN | 0x24 | 0x00000100 |
| PARSER_SEARCH_MASK | 0x28 | 0xFFFFFF00 (together with the pattern: any 00 00 01 xx) |
| PARSER_INT_ENABLE | 0x2C | the host enable field starts at bit 8; Linux writes 0x100 (start-code-found to the host) |
| PARSER_INT_STATUS | 0x30 | bit 0 = start code found. Write 1 to clear |
| PARSER_ES_CONTROL | 0x5C | Linux clears bit 0; meaning **UNVERIFIED** |
| PARSER_VIDEO_START_PTR | 0x80 | VIFIFO start (physical), as the parser sees it |
| PARSER_VIDEO_END_PTR | 0x84 | VIFIFO end (physical) |
| PARSER_VIDEO_WP | 0x88 | where the parser is writing in the VIFIFO now (physical; Linux only reads it) |
| PARSER_VIDEO_HOLE | 0x90 | Linux reads it and counts it as VIFIFO usage; meaning **UNVERIFIED** |

### 3.2 Parser reset

- In the GXBB reset binding, RESET_PARSER is reset ID 40. With 32 IDs per register that is register 1, bit 8: **RESET1 at 0xC1104408, bit 8**.
- Linux resets the parser by writing 0x00000100 to that register once. The bit is a self-clearing pulse: no read-modify-write, no release write, no delay.
- The same controller also has active-low level registers at base + 0x7C (for this ID, 0xC1104484 bit 8). The pulse reset does not use them.
- Linux never touches RESET_DOS_RESET (ID 2) or the other RESET_PARSER_* IDs (71–74).

### 3.3 Parser init sequence

Linux runs this **after** VDEC_1 is started (after MPSR = 1).

| # | Register | Action / value |
|---|---|---|
| 1 | RESET1 (0xC1104408) | write 0x100 (parser reset pulse) |
| 2 | PARSER_CONFIG | write 0x000A1010 (PFIFO_EMPTY_CNT 10, MAX_ES_WR_CYCLE 1, MAX_FETCH_CYCLE 16) |
| 3 | PFIFO_RD_PTR, then PFIFO_WR_PTR | write 0, 0 |
| 4 | PARSER_SEARCH_PATTERN | write 0x00000100 |
| 5 | PARSER_SEARCH_MASK | write 0xFFFFFF00 |
| 6 | PARSER_CONFIG | write 0x000A1810 (same as step 2, plus STARTCODE_WID_24 = 2) |
| 7 | PARSER_CONTROL | write 0x00000003 (search + start, idle) |
| 8 | PARSER_VIDEO_START_PTR | write the VIFIFO physical base |
| 9 | PARSER_VIDEO_END_PTR | write VIFIFO base + size − 8 |
| 10 | PARSER_ES_CONTROL | clear bit 0 |
| 11 | DOS_GEN_CTRL0 (DOS + 0xFC08) | write 0. This selects VDEC_1 as the VIFIFO consumer: the HEVC path writes 3<<1 here, with a comment that the field selects the VIFIFO read pointer for HEVC |
| 12 | VLD_MEM_VIFIFO_BUF_CNTL (DOS + 0x3120) | write 1, then clear bit 0. The register ends at 0, so manual write-pointer mode (bit 1) is now off and the VIFIFO write pointer follows the parser |
| 13 | PARSER_INT_STATUS | write 0xFFFF (clear everything) |
| 14 | PARSER_INT_ENABLE | write 0x00000100 |

### 3.4 Submitting a buffer of bitstream

**Preconditions:**
- The buffer is physically contiguous, has a 32-bit physical address, and the CPU's writes are cleaned to RAM.
- Its capacity is at least the padded length plus 512 bytes.
- Linux gives page-aligned buffers; the alignment the parser needs is **UNVERIFIED**.

**Linux submits one buffer at a time**, strictly in order, and waits for each before the next.

1. **Pad.** If the payload is shorter than 4096 bytes, fill zeros up to 4096. Linux's comment says the VDEC raises no interrupts otherwise. Call the padded length N.
2. **Trailer.** Bytes N to N+511 are `00 00 01 FF` followed by 508 zero bytes. This marker is what makes the parser interrupt fire.
3. PFIFO_RD_PTR = 0, then PFIFO_WR_PTR = 0.
4. PARSER_CONTROL = (N << 8) | 0x23. That is ES_WRITE, ES_SEARCH and ES_PARSER_START, with the pack size N.
5. PARSER_FETCH_ADDR = physical address of the buffer.
6. PARSER_FETCH_CMD = 0x38000000 | (N + 512). That is endian 7 at bit 27, with a fetch length that includes the 512-byte trailer.
7. Wait for the "search done" signal from the parser IRQ (§3.5). Linux allows up to 200 ms.

If the buffer is too small to hold the trailer, Linux only warns and still fetches N + 512 bytes. In that case the marker is missing and the wait times out.

**Why this works (inferred, UNVERIFIED).** The search pattern matches every MPEG start code, yet the interrupt does not fire on the start codes inside the payload. The reading that fits the register names and the behaviour is:
- With ES_WRITE and a pack size of N, the parser copies N bytes into the VIFIFO, then start-code-searches the rest of the fetch.
- The `00 00 01 FF` in the trailer is therefore the first start code it sees, so "start code found" means "all N bytes are in the VIFIFO".
- Whether any trailer bytes reach the VIFIFO is also **UNVERIFIED**. 0x000001FF lies in MPEG's system start-code range, so a decoder would skip it.

### 3.5 Parser interrupt and the search-done handshake

The ESPARSER IRQ is SPI 32; Linux requests it as a shared line. Linux handles it in this order:
1. Read PARSER_INT_STATUS.
2. Write the same value back to PARSER_INT_STATUS. This is the acknowledge.
3. If bit 0 (start code found) is set: PFIFO_RD_PTR = 0, PFIFO_WR_PTR = 0, then set the "search done" flag and wake the submitter.

On the submitter side:
- If the 200 ms wait times out or is interrupted, Linux writes PARSER_FETCH_CMD = 0 to abort the fetch and drops the buffer as an error.
- Linux clears its done flag after writing FETCH_CMD. That is a software race, not a hardware rule: arm the wait before writing FETCH_CMD.

### 3.6 Flow control (VIFIFO free space)

- Before submitting a payload of P bytes (the unpadded size), Linux computes:
  - usage = VLD_MEM_VIFIFO_LEVEL + PARSER_VIDEO_HOLE + 6144. The 6144 bytes are a margin its comment calls a "6 KiB internal fifo".
  - free = VIFIFO size − usage.
- It submits only if free ≥ P. If usage exceeds the VIFIFO size, it warns and treats free as 0.
- When there is not enough space, Linux tries again after each decoded frame (decoding drains the VIFIFO) and each time a new input buffer is queued.

### 3.7 Bitstream offsets (for timestamps)

Before each submission, Linux records where the buffer will land:
- offset = PARSER_VIDEO_WP − VIFIFO base.
- If this offset is below the previous one, the ring has wrapped and a wrap counter goes up.
- The stored value is offset + wraps × VIFIFO size, kept with the input buffer's timestamp. §5.6 says how it is matched.

---

## 4. VIFIFO (video input FIFO in DDR)

**Buffer:**
- A ring of **16 MiB** in Linux, one physically contiguous block with a 32-bit physical address.
- The CPU never reads or writes it: the parser writes it and the VLD reads it.
- Linux allocates it from CMA, which with a default kernel config means at least 1 MiB alignment. The hardware's own requirement is **UNVERIFIED**; the end pointer at base + size − 8 suggests 8-byte granularity.

**Setup.** It runs inside power-up, after §1.3 step 14 and before the firmware load.

| # | Register | Offset | Action | Value |
|---|---|---|---|---|
| 1 | VLD_MEM_VIFIFO_CONTROL | 0x3110 | write | 0 |
| 2 | VLD_MEM_VIFIFO_WRAP_COUNT | 0x3144 | write | 0 |
| 3 | POWER_CTL_VLD | 0x3020 | write | 0x10 (bit 4; meaning **UNVERIFIED**) |
| 4 | VLD_MEM_VIFIFO_START_PTR | 0x3100 | write | base |
| 5 | VLD_MEM_VIFIFO_CURR_PTR | 0x3104 | write | base |
| 6 | VLD_MEM_VIFIFO_END_PTR | 0x3108 | write | base + size − 8 |
| 7 | VLD_MEM_VIFIFO_CONTROL | 0x3110 | set bit 0, then clear bit 0 | pulse (presumably latches the pointers; **UNVERIFIED**) |
| 8 | VLD_MEM_VIFIFO_BUF_CNTL | 0x3120 | write | 0x2 (bit 1 MEM_BUFCTRL_MANUAL) |
| 9 | VLD_MEM_VIFIFO_WP | 0x3114 | write | base |
| 10 | VLD_MEM_VIFIFO_BUF_CNTL | 0x3120 | set bit 0, then clear bit 0 | pulse (presumably loads the manual WP; **UNVERIFIED**) |
| 11 | VLD_MEM_VIFIFO_CONTROL | 0x3110 | set bits | 0x00110406: field at bit 16 (MEM_FIFO_CNT) = 0x11, bit 10 MEM_FILL_ON_LEVEL, bit 2 MEM_CTRL_EMPTY_EN, bit 1 MEM_CTRL_FILL_EN |

Then, in §3.3 steps 11–12, DOS_GEN_CTRL0 is set to 0 and BUF_CNTL ends at 0, which hands the write pointer to the parser.

**Pointer handling:**
- **Write side:** after the handover, the ESPARSER advances the write pointer. The host reads the parser's view of it from PARSER_VIDEO_WP.
- **Read side:** Linux never reads or writes VLD_MEM_VIFIFO_RP. The VLD, under firmware control, owns it. CURR_PTR is only initialised.
- **Level:** VLD_MEM_VIFIFO_LEVEL is the amount of data in the FIFO. Linux treats it as bytes.
- **Wrap:** the hardware wraps from END back to START. This is inferred from Linux's wrap detection on PARSER_VIDEO_WP. WRAP_COUNT is zeroed at start and never read.
- The parser's PARSER_VIDEO_START_PTR and END_PTR hold the same values as the VLD's START_PTR and END_PTR.

---

## 5. MPEG-1/2 codec

### 5.1 Firmware

`gxl_mpeg12.bin` (§2). Nothing in the register setup distinguishes MPEG-1 from MPEG-2: MPEG1_2_REG is written 0 for both. That the firmware detects the variant from the stream is **UNVERIFIED**.

### 5.2 Workspace

- 128 KiB, physically contiguous, 32-bit physical address, visible to the device (uncached in Linux).
- Linux's allocator returns zeroed memory. Whether the firmware needs it zeroed is **UNVERIFIED**.
- Its address goes into **MREG_CO_MV_START = AV_SCRATCH_B (0x272C) as base + 0x1400 (5 KiB)**. Linux's comment says the firmware subtracts 5 KiB from the value it is given. How much of the 128 KiB the firmware uses is **UNVERIFIED**.

### 5.3 Firmware interface registers (Linux's names for the scratch registers)

| Register | Offset | Linux name | Direction | Use |
|---|---|---|---|---|
| AV_SCRATCH_0..3 | 0x2700–0x270C | (canvas table) | host → fw | canvas word for buffers 0–3 (§5.5) |
| AV_SCRATCH_4 | 0x2710 | MREG_SEQ_INFO | host → fw at start, then fw → host | canvas word for buffer 4 at start; later the sequence info, bits [3:0] = aspect code |
| AV_SCRATCH_5 | 0x2714 | MREG_PIC_INFO | host → fw at start, then fw → host | canvas word for buffer 5; later the picture flags |
| AV_SCRATCH_6 | 0x2718 | MREG_PIC_WIDTH | host → fw at start, then fw → host | canvas word for buffer 6; later the width (never read by Linux) |
| AV_SCRATCH_7 | 0x271C | MREG_PIC_HEIGHT | host → fw at start, then fw → host | canvas word for buffer 7; later the height (never read by Linux) |
| AV_SCRATCH_8 | 0x2720 | MREG_BUFFERIN | host → fw | buffer handed back for reuse (index + 1); the firmware sets it to 0 once taken |
| AV_SCRATCH_9 | 0x2724 | MREG_BUFFEROUT | fw → host | the decoded buffer report; the host writes 0 after handling it |
| AV_SCRATCH_A | 0x2728 | MREG_CMD | host → fw | (width << 16) \| height at start |
| AV_SCRATCH_B | 0x272C | MREG_CO_MV_START | host → fw | workspace base + 0x1400 |
| AV_SCRATCH_C | 0x2730 | MREG_ERROR_COUNT | host → fw / fw → host | written 0 at start, never read |
| AV_SCRATCH_D | 0x2734 | MREG_FRAME_OFFSET | fw → host | VIFIFO offset of the reported picture |
| AV_SCRATCH_E | 0x2738 | MREG_WAIT_BUFFER | ? | written 0 at start, never read (meaning **UNVERIFIED**) |
| AV_SCRATCH_F | 0x273C | MREG_FATAL_ERROR | fw → host | written 0 at start; 1 = fatal error |

AV_SCRATCH_4 to 7 do two jobs. They carry the canvas words for buffers 4–7 when the firmware starts, and the firmware later reports sequence and picture data in them. So the firmware reads the canvas table once at start (inferred, **UNVERIFIED**), and Linux never rewrites canvases after start.

### 5.4 Codec start sequence

This runs after the firmware load and before the IRQ enable and MPSR = 1.

| # | Register | Offset | Value |
|---|---|---|---|
| 1 | (allocate the workspace, §5.2) | | |
| 2 | AV_SCRATCH_0 … AV_SCRATCH_7 | 0x2700–0x271C | one canvas word per output buffer (§5.5); the canvases are programmed in the DMC LUT at the same time |
| 3 | POWER_CTL_VLD | 0x3020 | 0x10 (written again; it was already set in §4) |
| 4 | AV_SCRATCH_B (MREG_CO_MV_START) | 0x272C | workspace physical + 0x1400 |
| 5 | MPEG1_2_REG | 0x3004 | 0 |
| 6 | PSCALE_CTRL | 0x2444 | 0 |
| 7 | PIC_HEAD_INFO | 0x300C | 0x380 |
| 8 | M4_CONTROL_REG | 0x30A4 | 0 |
| 9 | AV_SCRATCH_8 (MREG_BUFFERIN) | 0x2720 | 0 |
| 10 | AV_SCRATCH_9 (MREG_BUFFEROUT) | 0x2724 | 0 |
| 11 | AV_SCRATCH_A (MREG_CMD) | 0x2728 | (W << 16) \| H, the unaligned display size the session was set up with |
| 12 | AV_SCRATCH_C (MREG_ERROR_COUNT) | 0x2730 | 0 |
| 13 | AV_SCRATCH_F (MREG_FATAL_ERROR) | 0x273C | 0 |
| 14 | AV_SCRATCH_E (MREG_WAIT_BUFFER) | 0x2738 | 0 |

What the values in steps 5–8 and 11 mean is **UNVERIFIED**; only the register names are known.

### 5.5 Output buffers and canvases

**Count:**
- Exactly **8** output buffers. Linux's MPEG-1/2 table has both the minimum and the maximum set to 8.
- All 8 are given to the firmware before it starts. The firmware treats all of them as free at start and decodes into any free one.

**Firmware index i (0–7) corresponds to AV_SCRATCH_i.** Each canvas word is laid out as:

| Bits | Content |
|---|---|
| [7:0] | Y canvas index |
| [15:8] | U canvas index (NV12: the UV canvas) |
| [23:16] | V canvas index (NV12: the same UV canvas again) |
| [31:24] | 0 |

**Canvases:**
- NV12 takes 2 canvases per buffer, so 16 LUT entries in total. 3-plane YUV420 takes 3 per buffer.
- Linux takes the lowest free indexes from its shared canvas allocator, so any free, unique indexes work. Whether some index range is reserved or forbidden for the decoder is **UNVERIFIED**. They must not collide with the indexes the display side uses.
- The canvases must be programmed in the LUT before MPSR = 1.

**Output format select:** MDEC_PIC_DC_CTRL (0x2638) bit 17 = 1 selects NV12 (two-plane, interleaved chroma); 0 selects three-plane. Linux sets it after the codec start and IRQ enable, just before MPSR = 1.

### 5.6 How the firmware reports a decoded frame (vdec threaded handler)

| # | Action |
|---|---|
| 1 | ASSIST_MBOX1_CLR_REG (0x01D4) ← 1. This acknowledges the mailbox interrupt. |
| 2 | Read AV_SCRATCH_F (MREG_FATAL_ERROR). If it is **1**: fatal error. Linux abandons the session; no more register writes. |
| 3 | Read AV_SCRATCH_9 (MREG_BUFFEROUT). If **0**, there is nothing to report: stop here and do not write BUFFEROUT. |
| 4 | If bits 23:17 of BUFFEROUT are all 1 (BUFFEROUT AND 0x00FE0000 equals 0x00FE0000), Linux delivers no frame and goes straight to step 9. Its comment says the meaning is unclear (**UNVERIFIED**). |
| 5 | Read AV_SCRATCH_5 (MREG_PIC_INFO). **Bit 15** set = progressive frame. If bit 15 is clear the frame is interlaced, and **bit 13** set = top field first, clear = bottom field first. Other bits **UNVERIFIED**. |
| 6 | Read AV_SCRATCH_4 (MREG_SEQ_INFO). **Bits [3:0]** are the sequence header's aspect code: 2 = 4:3, 3 = 16:9, 4 = 2.21:1, anything else = square pixels. Linux turns the display aspect into a pixel aspect using the session width and height. |
| 7 | Buffer index = (BUFFEROUT bits [3:0]) − 1, kept to 3 bits. The field holds a **1-based** index, 1–8. |
| 8 | Read AV_SCRATCH_D (MREG_FRAME_OFFSET), the VIFIFO offset of this picture. Linux picks the last submitted input buffer whose recorded offset (§3.7) is ≤ this value and gives its timestamp to the frame. Records not matched after 32 tries are dropped. |
| 9 | AV_SCRATCH_9 ← 0. The report has been consumed; the firmware can post the next one. |

- An interlaced frame arrives as one buffer with both fields woven together.
- Linux hands frames out in interrupt order and does no reordering. That this order is display order is **UNVERIFIED**.
- Whether MREG_FRAME_OFFSET counts cumulatively, as Linux's wrap-adjusted offsets assume, or wraps with the ring is **UNVERIFIED**. If it wraps, Linux's matching fails after 16 MiB of stream.

### 5.7 Handing a buffer back to the firmware

- A buffer reported in §5.6 stays with the host until it is handed back.
- To hand back buffer k: wait until AV_SCRATCH_8 (MREG_BUFFERIN) reads 0, then write **k + 1** to it. Only one hand-back can be pending at a time.
- Linux checks from a kernel thread every 5–10 ms.
- Linux writes the capture buffer's own index + 1. That is only correct because its firmware index equals its buffer index (the buffers are mapped in queue order at start). Use the firmware index + 1.
- What the firmware does when all 8 buffers are with the host is **UNVERIFIED**. It presumably waits, and MREG_WAIT_BUFFER may flag this.

### 5.8 Resolution and sequence handling

- **Linux's MPEG-1/2 path does not detect the resolution.**
  - It never reads AV_SCRATCH_6/7 (MREG_PIC_WIDTH/HEIGHT).
  - The MPEG formats lack the dynamic-resolution flag, so there is no source-change event.
  - The application sets width and height before start, and those values size the canvases and go into MREG_CMD.
- The bit layout of AV_SCRATCH_6/7, and when they become valid, is **UNVERIFIED**. They are presumably plain pixel counts after a sequence header.
- A Haiku driver can take the size from the stream's sequence header (start code 00 00 01 B3: 12-bit horizontal size, then 12-bit vertical size) before powering up.
- Linux does not handle a resolution change mid-stream; what the hardware does then is **UNVERIFIED**.
- Linux's limits for MPEG-1/2 are at most 1920×1080, and format negotiation clamps to at least 256×144. These are driver policy and may not be hardware limits.

### 5.9 End of stream

- To flush, Linux submits one more parser buffer of **1024 bytes**: `00 00 01 B7` (MPEG sequence_end_code) followed by 1020 zero bytes.
  - It goes through the same path as §3.4 with N = 1024 and a fetch of 1536 bytes.
  - It gets **no** 4 KiB padding and **no** `00 00 01 FF` trailer; the 512 extra bytes are zeros.
- Linux ignores the parser wait result for this buffer. Whether the parser IRQ fires at all is **UNVERIFIED**; with no marker it may simply time out after 200 ms.
- Linux then waits until 50 ms pass with no vdec IRQ.
- The purpose, per vdec.h, is to flush: the firmware outputs the pictures it still holds, such as the last reference picture (inferred, **UNVERIFIED**).
- Linux tags a frame as the last one when a stop was requested and at most one submitted input buffer is still unmatched.

### 5.10 Errors

- AV_SCRATCH_F = 1 is fatal. Linux abandons the session and does not try to recover. AV_SCRATCH_C (error count) is never read.
- A parser timeout drops that input buffer (PARSER_FETCH_CMD ← 0). Decoding continues with the next one.

---

## 6. Output frame layout (NV12)

Let W32 = W rounded up to a multiple of 32, and H32 = H rounded up to a multiple of 32.

| Plane | Stride (bytes) | Lines | Bytes the decoder fills | Linux buffer size |
|---|---|---|---|---|
| Y | W32 | H32 | W32 × H32 | W32 × H32 rounded up to 64 KiB |
| UV (Cb, Cr interleaved, Cb first) | W32 | H32 / 2 | W32 × H32 / 2 | half the Y buffer size |

- The visible picture is the top-left W × H of each plane.
- The 64 KiB round-up is Linux policy. Before commit b3a805dd5a83 (Sep/Oct 2026), Linux sized the planes from the unaligned W × H and overran the buffers when W was not a multiple of 32.
- Linux puts each plane in its own contiguous buffer. The two canvases are independent, so one allocation with UV placed after Y also works. The canvas address field is in 8-byte units; whether any alignment beyond 8 bytes is needed is **UNVERIFIED** (Linux's buffers are page aligned).

**Canvas settings the decoder canvases use:**

| Canvas | Address | Width (bytes) | Height (lines) | Wrap | Block mode | Endian |
|---|---|---|---|---|---|---|
| Y | Y plane physical | W32 | H32 | 0 (none) | 0 (linear) | 7 (64-bit swap) |
| UV | UV plane physical | W32 | H32 / 2 | 0 | 0 | 7 |

- The endian field only works on the GX-style "amlogic,canvas" LUT; Linux refuses a non-zero endian value on Meson8. GXBB uses the GX style.
- For 3-plane YUV420 (not needed here), U and V each use W32/2 × H32/2.

**DMC canvas LUT layout** (for reference; the driver programs these already):

| Register | Address | Fields |
|---|---|---|
| DMC_CAV_LUT_DATAL | 0xC8838048 | [28:0] address >> 3; [31:29] low 3 bits of (width / 8) |
| DMC_CAV_LUT_DATAH | 0xC883804C | [8:0] rest of (width / 8); [21:9] height in lines; [23:22] wrap; [25:24] block mode; [29:26] endian |
| DMC_CAV_LUT_ADDR | 0xC8838050 | [7:0] canvas index; bit 9 = write strobe |

Linux reads DATAH back after each LUT write so that the write is flushed.

**Examples:**

| Picture | Stride | Y lines | Y bytes (Linux) | UV lines | UV bytes (Linux) |
|---|---|---|---|---|---|
| 352×288 | 352 | 288 | 0x20000 (131,072) | 144 | 0x10000 |
| 720×480 | 736 | 480 | 0x60000 (393,216) | 240 | 0x30000 |
| 720×576 | 736 | 576 | 0x70000 (458,752) | 288 | 0x38000 |
| 1280×720 | 1280 | 736 | 0xF0000 (983,040) | 368 | 0x78000 |
| 1920×1080 | 1920 | 1088 | 0x200000 (2,097,152) | 544 | 0x100000 |

---

## 7. The vdec interrupt in general

**Source:** SPI 44 ("vdec"), raised when the firmware posts to **mailbox 1** (ASSIST_MBOX1). There is one line for VDEC_1 and HEVC together, and Linux runs only one session at a time.

**Registers:**

| Operation | Register | Offset | Value |
|---|---|---|---|
| Clear a stale request before enabling | ASSIST_MBOX1_CLR_REG | 0x01D4 | write 1 |
| Enable | ASSIST_MBOX1_MASK | 0x01D8 | write 1 |
| Acknowledge (first thing in the handler) | ASSIST_MBOX1_CLR_REG | 0x01D4 | write 1 |
| Disable (at stop) | ASSIST_MBOX1_MASK | 0x01D8 | write 0 |

**No status register is read for this IRQ.** The reason for the interrupt is in the scratch registers: AV_SCRATCH_9 (frame ready) and AV_SCRATCH_F (fatal error).

**Linux's split:**
- The hard handler only records the time of the last IRQ, which the 50 ms inactivity wait uses, and defers to the thread.
- The threaded handler runs §5.6.
- The line is "oneshot": it stays masked until the threaded handler returns.

VDEC_ASSIST_AMR1_INT8 (0x00B4) and MCPU_INTR_MSK (0x0C10) are defined in dos_regs.h but **not used** on this path.

---

## 8. Session start and stop, and what is GXBB-specific

### Start (in Linux's order)

| # | Step |
|---|---|
| 1 | Allocate the 16 MiB VIFIFO (§4) |
| 2 | DOS_PARSER clock on, then DOS clock on (§1.3 steps 1–2) |
| 3 | VDEC_1 clock, AO power, DOS reset, memories, isolation, top registers (§1.3 steps 3–14) |
| 4 | VIFIFO registers (§4) |
| 5 | Firmware load (§2) |
| 6 | Codec start: workspace, canvases, codec registers (§5.4) |
| 7 | ASSIST_MBOX1_CLR_REG ← 1, then ASSIST_MBOX1_MASK ← 1 |
| 8 | MDEC_PIC_DC_CTRL: set bit 17 (NV12) |
| 9 | **MPSR ← 1** (AMRISC runs) |
| 10 | Wait 10–20 µs (Linux: "let the firmware settle") |
| 11 | ESPARSER init (§3.3), including DOS_GEN_CTRL0 ← 0 and the BUF_CNTL handover |
| 12 | Start feeding the bitstream (§3.4) and start the buffer hand-back polling (§5.7) |

### Stop

| # | Step |
|---|---|
| 1 | Stop feeding; stop the hand-back polling |
| 2 | Wait until 50 ms pass with no vdec IRQ (Linux polls every 25 ms). For a clean end, send EOS first (§5.9) |
| 3 | Power-down (§1.4), which also frees the workspace and turns the clocks off |
| 4 | Free the canvas indexes and the VIFIFO |

### GXBB versus other SoCs

- vdec_1 has only one SoC branch: SM1 uses bit 1 in SLEEP0 and ISO0. **GXBB takes the same path as GXL, GXM and G12A.** Nothing in the VDEC_1 or MPEG-1/2 path differs between GXBB and GXL.
- The GXBB format table has H.264 (`gxbb_h264.bin`), MPEG-1 and MPEG-2 (both `gxl_mpeg12.bin`). It has no VP9 or HEVC. The MPEG entries are the same as GXL's (8 buffers, at most 1920×1080, NV12 or 3-plane output).
- DT: compatible "amlogic,gxbb-vdec", "amlogic,gx-vdec". Clocks DOS_PARSER, DOS, VDEC_1 and VDEC_HEVC; reset RESET_PARSER, named "esparser".
- **Risk:** mainline commit 20aa934ace69 (June 2026, a vdec fix) says in passing that some distributions patch MPEG-2 support out of this driver because it has been broken for a while. It does not give the cause or the SoCs (**UNVERIFIED**). Expect to debug MPEG-2 against a reference decoder rather than assume Linux's sequence is known-good on GXBB.

---

## 9. Register reference

### 9.1 DOS registers on this path (base 0xC8820000)

| Name | Byte offset | Word index | Physical | Used in |
|---|---|---|---|---|
| ASSIST_MBOX1_CLR_REG | 0x01D4 | 0x0075 | 0xC88201D4 | §7 |
| ASSIST_MBOX1_MASK | 0x01D8 | 0x0076 | 0xC88201D8 | §7, §1.4 |
| MPSR | 0x0C04 | 0x0301 | 0xC8820C04 | §2, §8, §1.4 |
| CPSR | 0x0C84 | 0x0321 | 0xC8820C84 | §2, §1.4 |
| IMEM_DMA_CTRL | 0x0D00 | 0x0340 | 0xC8820D00 | §2 |
| IMEM_DMA_ADR | 0x0D04 | 0x0341 | 0xC8820D04 | §2 |
| IMEM_DMA_COUNT | 0x0D08 | 0x0342 | 0xC8820D08 | §2 |
| PSCALE_CTRL | 0x2444 | 0x0911 | 0xC8822444 | §5.4 |
| GCLK_EN | 0x260C | 0x0983 | 0xC882260C | §1.3 |
| MDEC_PIC_DC_CTRL | 0x2638 | 0x098E | 0xC8822638 | §1.3, §2, §5.5 |
| AV_SCRATCH_0 … _7 | 0x2700 … 0x271C | 0x09C0 … 0x09C7 | 0xC8822700 … | §5.3–5.6 |
| AV_SCRATCH_8 (MREG_BUFFERIN) | 0x2720 | 0x09C8 | 0xC8822720 | §5.7 |
| AV_SCRATCH_9 (MREG_BUFFEROUT) | 0x2724 | 0x09C9 | 0xC8822724 | §5.6 |
| AV_SCRATCH_A (MREG_CMD) | 0x2728 | 0x09CA | 0xC8822728 | §5.4 |
| AV_SCRATCH_B (MREG_CO_MV_START) | 0x272C | 0x09CB | 0xC882272C | §5.4 |
| AV_SCRATCH_C (MREG_ERROR_COUNT) | 0x2730 | 0x09CC | 0xC8822730 | §5.4 |
| AV_SCRATCH_D (MREG_FRAME_OFFSET) | 0x2734 | 0x09CD | 0xC8822734 | §5.6 |
| AV_SCRATCH_E (MREG_WAIT_BUFFER) | 0x2738 | 0x09CE | 0xC8822738 | §5.4 |
| AV_SCRATCH_F (MREG_FATAL_ERROR) | 0x273C | 0x09CF | 0xC882273C | §5.4, §5.6 |
| MPEG1_2_REG | 0x3004 | 0x0C01 | 0xC8823004 | §5.4 |
| PIC_HEAD_INFO | 0x300C | 0x0C03 | 0xC882300C | §5.4 |
| POWER_CTL_VLD | 0x3020 | 0x0C08 | 0xC8823020 | §4, §5.4 |
| M4_CONTROL_REG | 0x30A4 | 0x0C29 | 0xC88230A4 | §5.4 |
| VLD_MEM_VIFIFO_START_PTR | 0x3100 | 0x0C40 | 0xC8823100 | §4 |
| VLD_MEM_VIFIFO_CURR_PTR | 0x3104 | 0x0C41 | 0xC8823104 | §4 |
| VLD_MEM_VIFIFO_END_PTR | 0x3108 | 0x0C42 | 0xC8823108 | §4 |
| VLD_MEM_VIFIFO_CONTROL | 0x3110 | 0x0C44 | 0xC8823110 | §4 |
| VLD_MEM_VIFIFO_WP | 0x3114 | 0x0C45 | 0xC8823114 | §4 |
| VLD_MEM_VIFIFO_RP | 0x3118 | 0x0C46 | 0xC8823118 | (defined; Linux never touches it) |
| VLD_MEM_VIFIFO_LEVEL | 0x311C | 0x0C47 | 0xC882311C | §3.6 |
| VLD_MEM_VIFIFO_BUF_CNTL | 0x3120 | 0x0C48 | 0xC8823120 | §4, §3.3 |
| VLD_MEM_VIFIFO_WRAP_COUNT | 0x3144 | 0x0C51 | 0xC8823144 | §4 |
| DOS_SW_RESET0 | 0xFC00 | 0x3F00 | 0xC882FC00 | §1.3, §1.4 |
| DOS_GCLK_EN0 | 0xFC04 | 0x3F01 | 0xC882FC04 | §1.3 |
| DOS_GEN_CTRL0 | 0xFC08 | 0x3F02 | 0xC882FC08 | §3.3 |
| DOS_MEM_PD_VDEC | 0xFCC0 | 0x3F30 | 0xC882FCC0 | §1.3, §1.4 |
| DOS_VDEC_MCRCC_STALL_CTRL | 0xFD00 | 0x3F40 | 0xC882FD00 | §1.3 |

AV_SCRATCH_G to _L (0x2740–0x2754, words 0x09D0–0x09D5) exist but are not used by MPEG-1/2.

**Defined in dos_regs.h but not on the MPEG-1/2 path** (used by H.264, HEVC or nothing):

| Name | Byte offset | Word index |
|---|---|---|
| VDEC_ASSIST_AMR1_INT8 | 0x00B4 | 0x002D |
| MCPU_INTR_MSK | 0x0C10 | 0x0304 |
| LMEM_DMA_CTRL | 0x0D40 | 0x0350 |
| MC_STATUS0 | 0x2424 | 0x0909 |
| MC_CTRL1 | 0x242C | 0x090B |
| PSCALE_RST | 0x2440 | 0x0910 |
| PSCALE_BMEM_ADDR | 0x247C | 0x091F |
| PSCALE_BMEM_DAT | 0x2480 | 0x0920 |
| DBLK_CTRL | 0x2544 | 0x0951 |
| DBLK_STATUS | 0x254C | 0x0953 |
| MDEC_PIC_DC_STATUS | 0x263C | 0x098F |
| ANC0_CANVAS_ADDR | 0x2640 | 0x0990 |
| MDEC_PIC_DC_THRESH | 0x26E0 | 0x09B8 |
| DCAC_DMA_CTRL | 0x3848 | 0x0E12 |
| DOS_MEM_PD_HEVC | 0xFCCC | 0x3F33 |
| DOS_SW_RESET3 | 0xFCD0 | 0x3F34 |
| DOS_GCLK_EN3 | 0xFCD4 | 0x3F35 |

### 9.2 ESPARSER registers (base 0xC110A580)

| Name | Byte offset | Physical | CBUS word |
|---|---|---|---|
| PARSER_CONTROL | 0x00 | 0xC110A580 | 0x2960 |
| PARSER_FETCH_ADDR | 0x04 | 0xC110A584 | 0x2961 |
| PARSER_FETCH_CMD | 0x08 | 0xC110A588 | 0x2962 |
| PARSER_CONFIG | 0x14 | 0xC110A594 | 0x2965 |
| PFIFO_WR_PTR | 0x18 | 0xC110A598 | 0x2966 |
| PFIFO_RD_PTR | 0x1C | 0xC110A59C | 0x2967 |
| PARSER_SEARCH_PATTERN | 0x24 | 0xC110A5A4 | 0x2969 |
| PARSER_SEARCH_MASK | 0x28 | 0xC110A5A8 | 0x296A |
| PARSER_INT_ENABLE | 0x2C | 0xC110A5AC | 0x296B |
| PARSER_INT_STATUS | 0x30 | 0xC110A5B0 | 0x296C |
| PARSER_ES_CONTROL | 0x5C | 0xC110A5DC | 0x2977 |
| PARSER_VIDEO_START_PTR | 0x80 | 0xC110A600 | 0x2980 |
| PARSER_VIDEO_END_PTR | 0x84 | 0xC110A604 | 0x2981 |
| PARSER_VIDEO_WP | 0x88 | 0xC110A608 | 0x2982 |
| PARSER_VIDEO_HOLE | 0x90 | 0xC110A610 | 0x2984 |

### 9.3 Registers outside DOS and the parser

| Name | Physical | Use |
|---|---|---|
| AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 (AO + 0xE8) | bits 3:2 = VDEC_1 power off (1) / on (0) |
| AO_RTI_GEN_PWR_ISO0 | 0xC81000EC (AO + 0xEC) | bits 7:6 = VDEC_1 isolation; Linux writes the whole register (0 / 0xC0) |
| HHI_GCLK_MPEG0 | 0xC883C140 | bit 1 = DOS clock |
| HHI_GCLK_MPEG1 | 0xC883C144 | bit 25 = DOS_PARSER clock |
| HHI_VDEC_CLK_CNTL | 0xC883C1E0 | [10:9] source, [8] gate, [6:0] divider − 1 |
| HHI_VDEC2_CLK_CNTL | 0xC883C1E4 | HEVC clock (not used here) |
| HHI_MPLL_CNTL / CNTL6 | 0xC883C280 / 0xC883C294 | fixed PLL (read-only) / fclk_div3 gate bit 28 (always on) |
| RESET1 (pulse) | 0xC1104408 | bit 8 = parser reset |
| DMC_CAV_LUT_DATAL / DATAH / ADDR | 0xC8838048 / 0xC883804C / 0xC8838050 | canvas LUT |

---

## 10. Open questions (UNVERIFIED items, collected)

1. Meaning of IMEM_DMA_CTRL bits 18:16 = 7; how long the 16 KiB load takes; the alignment the IMEM DMA needs.
2. Meaning of MDEC_PIC_DC_CTRL bit 31 (cleared before load), POWER_CTL_VLD bit 4, PIC_HEAD_INFO = 0x380, MPEG1_2_REG, M4_CONTROL_REG, PSCALE_CTRL = 0 and MREG_CMD = (W<<16)|H.
3. Meaning of the individual DOS_SW_RESET0 bits (0xFFFFFFFC at power-up, bits 12:11 at power-down).
4. Which other blocks own bits in AO_RTI_GEN_PWR_ISO0, and whether a read-modify-write of bits 7:6 is equivalent to Linux's whole-register writes.
5. The VIFIFO_CONTROL bit-0 pulse, the BUF_CNTL bit-0 pulse, the MEM_FIFO_CNT field = 0x11, and the VIFIFO's alignment and size limits.
6. ESPARSER field widths (pack size, fetch length, endian, config fields), and the alignment and maximum length of a source buffer.
7. The pack-size / search interpretation of §3.4, and whether trailer bytes reach the VIFIFO.
8. PARSER_ES_CONTROL bit 0, PARSER_VIDEO_HOLE, and the 6 KiB internal-FIFO margin.
9. Whether the parser interrupt fires for Linux's EOS buffer, which has no 00 00 01 FF trailer.
10. MREG_BUFFEROUT: the meaning of bits 23:17 all set, and of the other bits.
11. MREG_PIC_INFO bits other than 15 and 13. The layout of MREG_PIC_WIDTH/HEIGHT and when they are valid. MREG_WAIT_BUFFER. MREG_ERROR_COUNT.
12. Whether MREG_FRAME_OFFSET is cumulative or wraps with the 16 MiB ring.
13. Whether frames come out in display order, and what the firmware does when all 8 buffers are held by the host.
14. Whether the workspace must be zeroed, and how much of the 128 KiB (minus the 5 KiB offset) is used.
15. Whether any canvas index range is reserved for or forbidden to the decoder.
16. Behaviour on a resolution change mid-stream.
17. The cause of the "MPEG-2 broken" remark in commit 20aa934ace69, and whether it affects GXBB.
18. The actual fixed-PLL rate on this board (nominally 2 GHz, which makes fclk_div3 666.67 MHz).

---

## Sources

All files are from torvalds/linux master as of 2026-10-30 (HEAD abf8c33470f7), fetched raw from GitHub:

- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_1.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_hevc.c (only for the DOS_GEN_CTRL0 consumer-select comment)
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/esparser.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/esparser.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/codec_mpeg12.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_helpers.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_helpers.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/vdec_platform.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/staging/media/meson/vdec/dos_regs.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/soc/amlogic/meson-canvas.c
- https://raw.githubusercontent.com/torvalds/linux/master/include/linux/soc/amlogic/meson-canvas.h
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gx.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxbb.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/include/dt-bindings/clock/gxbb-clkc.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/meson/gxbb.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/meson/meson-clkc-utils.h
- https://raw.githubusercontent.com/torvalds/linux/master/include/dt-bindings/reset/amlogic,meson-gxbb-reset.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/reset/amlogic/reset-meson.c (the reset driver now lives here, not in drivers/reset/reset-meson.c)
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/reset/amlogic/reset-meson-common.c
- Commit messages: b3a805dd5a83 (plane sizing from the aligned canvas) and 20aa934ace69 (MPEG-2 fallback fix), via https://api.github.com/repos/torvalds/linux/commits/
- Firmware: local `firmware/meson/vdec/gxl_mpeg12.bin`, compared with `/usr/lib/firmware/meson/vdec/gxl_mpeg12.bin.zst` (linux-firmware)


---

## 10. Found on the board (ODROID-C2, Haiku meson_vdec, 2026-10-30)

These come from running the sequences above on this board; they are observations, not Linux facts.

- **Everything above works on GXBB as described:**
  - power-up;
  - the firmware load: IMEM DMA done within the poll;
  - the VIFIFO, at 8 MiB and page aligned (16 MiB and 1 MiB alignment are not needed);
  - the parser with its `00 00 01 FF` marker after the data. The interrupt fires once per submission, and no trailer bytes reach the decoder: no corruption at any of 177 write boundaries;
  - eight NV12 frames with canvases 0x80–0x8F, endian 7;
  - the mailbox report and the hand-back.
- **Order:** frames come out in display order (§5.6's open question).
- **PIC_INFO (AV_SCRATCH_5), observed values:** 0x9000 on I pictures, 0x19000 on P, 0x29000 on B. Bits 17:16 look like the coding type − 1, and bit 15 is progressive, as in §5.6.
- **AV_SCRATCH_E (MREG_WAIT_BUFFER):** reads 1 during 1080p decoding with a slow consumer, and 0 at 480p. It is not a reliable "starved for frames" signal.
- **End of stream:**
  - Sent while the firmware still had pictures in hand, the sequence end (§5.9) lost the picture not yet reported, always the third from the end.
  - Sent once the firmware is quiet with frames to decode into, every frame came out, except with a consumer slow to hand frames back at 1080p: then the flush returned one picture where it usually returns two, and one was still lost.
  - The flush reports carry PIC_INFO 0x9000 and the stream offset of the last normal report.
- **First session after boot:** the first picture's first macroblock had its top two 8×8 blocks wrong, saturated values, and the error carried through that GOP. Later sessions didn't show it. Neither a power cycle at init nor writing the canvases before the reset helped. It looks like inverse-quantizer or IDCT state that only a completed decode initialises.
- **Interrupts:** both are short pulses; with the GIC set to level, many arrive as spurious (INTID 1023). Polling covers them.
- **Quality and speed:** against FFmpeg, differences are within MPEG's IDCT tolerance: at most a few pixels by more than 2, with the usual drift along long P chains. Decoding at 1080p runs at well over 80 frames a second.
