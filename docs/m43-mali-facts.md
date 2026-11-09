<!--
Copyright 2026, haiku-odroid-c2 port authors.
Distributed under the terms of the MIT License.
-->

# M43: Mali-450 MP3 (S905 / GXBB) for a lima-compatible kernel driver, hardware facts

> Facts read from Linux's lima DRM driver (drivers/gpu/drm/lima and include/uapi/drm/lima_drm.h, torvalds/linux master at cbbddb6b08cc, fetched 2026-10-31; the last change to the lima directory is ae72b9c39d28, 2026-09-30), the GX device tree, the GXBB clock driver, a few DRM core helpers, the board's own U-Boot DTB, and Mesa 22.0.5's lima Gallium driver (MIT, the copy in `deps/build/mesa-22.0.5`). No code, macros or comments were copied.

This is the input for a clean-room, MIT-licensed Haiku kernel driver that Mesa's lima userland can run on. It lists register offsets, bit meanings, sequences, values, timeouts and the uAPI's structures and semantics, in tables and short prose. Where a source gives no meaning for a value, or where a meaning is inferred from names or behaviour, the text says **UNVERIFIED**.

**Licences of the sources** (SPDX lines, exact):

| File | SPDX-License-Identifier |
|---|---|
| lima_bcast.c/.h, lima_ctx.c/.h, lima_device.c/.h, lima_dlbu.c/.h, lima_drv.c/.h, lima_dump.h, lima_gem.c/.h, lima_gp.c/.h, lima_l2_cache.c/.h, lima_mmu.c/.h, lima_pmu.c/.h, lima_pp.c/.h, lima_sched.c/.h, lima_vm.c/.h | `GPL-2.0 OR MIT` (dual) |
| lima_regs.h | `GPL-2.0` only. Its header says the register definitions were collected from ARM's Utgard kernel driver. |
| lima_devfreq.c | `GPL-2.0` only |
| include/uapi/drm/lima_drm.h | `(GPL-2.0 WITH Linux-syscall-note) OR MIT` |
| meson-gx.dtsi, meson-gxbb.dtsi, meson-gx-mali450.dtsi | `(GPL-2.0+ OR MIT)` |
| drivers/clk/meson/gxbb.c, gxbb-clkc.h | `GPL-2.0` |
| drm_timeout.c | `MIT`; drm_gem.c has an MIT-style permission notice and no SPDX line; drm_gem_shmem_helper.c is `GPL-2.0` |

So most lima code may be reused under MIT. lima_regs.h and lima_devfreq.c may not. This document is written from all of them as facts only either way. Register names below follow the ARM/lima naming so they can be matched against other sources. The tables are laid out afresh, not copied from lima_regs.h.

---

## 0. Conventions and address map

- All registers are 32 bits wide. lima uses plain 32-bit MMIO loads and stores (writel/readl, so with barriers).
- Offsets are byte offsets from the block's base. "Block base" = GPU base + the block offset in §1.
- "Write" means a full 32-bit store. lima never does read-modify-write on the GPU's registers, except for the BCAST mask (§2.7).
- "VA" is a GPU virtual address (32 bits, translated by the block's MMU). "Phys" is a CPU physical address.
- INTID = GIC SPI + 32. The port's FDT code already adds 32, so Haiku interrupt number = INTID (as in m27-irq-uart-i2c-facts.md).

### 0.1 Where the GPU is on GXBB

From meson-gx.dtsi, meson-gx-mali450.dtsi and meson-gxbb.dtsi. The board's U-Boot DTB (`u-boot/u-boot.dtb`, U-Boot v2026.10, which `bootefi` hands to Haiku) has the same node.

| Item | Value |
|---|---|
| Compatible | `"amlogic,meson-gxbb-mali", "arm,mali-450"` (lima matches `arm,mali-450`) |
| Register window | APB bus 0xD0000000 + 0xC0000 = **0xD00C0000**, size **0x40000** (256 KiB) |
| Clocks | `bus` = CLKID_CLK81 (12), `core` = CLKID_MALI (106) |
| Assigned rate | GP0_PLL (clock 9) set to 744,000,000 Hz |
| Resets | none in the GXBB node (the GXL node has none either) |
| Regulator | no `mali-supply` |
| Power domain | none |
| DMA coherence | no `dma-coherent` property: the GPU does not snoop CPU caches |
| OPP table | 125, 250, 285.714285, 400, 500, 666.666666, 744 MHz, all at 950 mV |

Interrupts. All are level-high, and each block has its own line:

| DT name | SPI | INTID | Source |
|---|---|---|---|
| gp | 160 | 192 | geometry processor |
| gpmmu | 161 | 193 | GP's MMU |
| pp | 162 | 194 | PP broadcast (Mali-450 only) |
| pmu | 163 | 195 | power management unit |
| pp0 | 164 | 196 | pixel processor 0 |
| ppmmu0 | 165 | 197 | PP0's MMU |
| pp1 | 166 | 198 | pixel processor 1 |
| ppmmu1 | 167 | 199 | PP1's MMU |
| pp2 | 168 | 200 | pixel processor 2 |
| ppmmu2 | 169 | 201 | PP2's MMU |

**lima decides which optional blocks exist from these interrupt names.** For each optional block that has an interrupt name, it looks the name up. If the DT does not name it, lima skips the block without touching its registers. On GXBB this gives exactly 3 PPs (MP3) and a PMU. lima does not read a PP count from the hardware. Whether a hardware register reports it is **UNVERIFIED**.

### 0.2 Clocks, reset and power on GXBB

lima itself only calls "enable the `bus` clock, enable the `core` clock, deassert the resets if there are any, enable the regulator if there is one". On GXBB that means:

- **CLK81** is a gate in HHI_MPEG_CLK_CNTL (0xC883C174) bit 7. Linux marks it critical, so it is always on.
- **The Mali clock** is HHI_MALI_CLK_CNTL at HHI + 0x1B0 = **0xC883C1B0**:

| Bits | Field | Meaning |
|---|---|---|
| [6:0] | mali_0 divider | divisor − 1 |
| [8] | mali_0 gate | 1 = on |
| [11:9] | mali_0 source | see below |
| [22:16] | mali_1 divider | divisor − 1 |
| [24] | mali_1 gate | 1 = on |
| [27:25] | mali_1 source | see below |
| [31] | final mux | 0 = mali_0, 1 = mali_1 |

  - Source values: 0 = xtal (24 MHz), 1 = gp0_pll, 2 = mpll2, 3 = mpll1, 4 = fclk_div7, 5 = fclk_div4, 6 = fclk_div3, 7 = fclk_div5.
  - The two branches exist so the rate can change without a glitch. Linux reprograms the branch that is not selected (source, divider, gate) and then flips bit 31.
  - Linux never lets the Mali mux change its parent's rate. Its reason: every GPU rate comes from the fclk_* clocks plus one GP0 setting, and the MPLLs belong to audio.
  - With the fixed PLL at its nominal 2 GHz, fclk_div3/4/5/7 are 666.67, 500, 400 and 285.71 MHz. fclk_div3's gate (HHI_MPLL_CNTL6 0xC883C294 bit 28) is critical in Linux. fclk_div4/5/7 are bits 29/30/31 of the same register and are **not** critical, so a driver that picks them must make sure their gate is on.
  - 744 MHz needs GP0. In gxbb.c, HHI_GP0_PLL_CNTL (HHI + 0x40) has M [8:0], N [13:9], OD [17:16] (power-of-two divider), reset bit 29, enable bit 30 and lock bit 31. Linux's table allows M = 32..62 with N = 1, and it writes CNTL2/3/4 = 0x69C80000 / 0x0A5590C4 / 0x0000500D at init. M = 62, N = 1, OD = 1 (24 × 62 / 2) gives 744 MHz; that is derived here, **UNVERIFIED**.
  - Which source and rate U-Boot leaves in HHI_MALI_CLK_CNTL on this board is **UNVERIFIED** (read it on the board).
- **Reset lines**: the GXBB reset binding has RESET_MALI_CAPB3 = 20 and RESET_MALI = 78. With 32 IDs per register (as in m26-vdec-facts.md §3.2) those are RESET0 (0xC1104404) bit 20 and RESET2 (0xC110440C) bit 14 for pulses, and level registers 0xC1104480 bit 20 and 0xC1104488 bit 14 (active low). lima on GXBB does not touch them, because the DT lists none. Whether the boot firmware leaves them released is **UNVERIFIED**.
- **Power**: no SoC power domain. The GPU's own PMU (§2.5) powers its internal domains.

### 0.3 Memory

- The GPU addresses 32-bit physical memory. lima sets a 32-bit coherent DMA mask and allocates buffer pages with GFP_DMA32 (below 4 GiB). The C2's 2 GiB of RAM is all below that.
- lima allocates the page directory, the page tables and the DLBU page as write-combined DMA memory (uncached for the CPU).
- Buffer objects are mapped write-combined into user space (`map_wc`). Their pages are DMA-mapped for the GPU through the streaming DMA API.
- Linux's DMA allocator returns zeroed memory. lima also asks for zeroed page directories and tables.

---

## 1. Block layout (Mali-450 offsets, as lima uses them)

GPU base 0xD00C0000. "Present" means present on the S905 (Mali-450 MP3). "Needed" means lima fails the probe on a Mali-450 without it.

| Block | Offset | Phys (GXBB) | Present | Needed | Interrupt | Notes |
|---|---|---|---|---|---|---|
| GP | 0x00000 | 0xD00C0000 | yes | yes | gp | geometry processor |
| L2 cache 1 | 0x01000 | 0xD00C1000 | yes | yes | — | **PP0–PP3's L2** on Mali-450 |
| PMU | 0x02000 | 0xD00C2000 | yes | no | pmu (presence only) | |
| GP MMU | 0x03000 | 0xD00C3000 | yes | yes | gpmmu | |
| PP MMU 0 | 0x04000 | 0xD00C4000 | yes | yes | ppmmu0 | |
| PP MMU 1 | 0x05000 | 0xD00C5000 | yes | no | ppmmu1 | |
| PP MMU 2 | 0x06000 | 0xD00C6000 | yes | no | ppmmu2 | |
| PP MMU 3 | 0x07000 | — | no | no | ppmmu3 | |
| PP0 | 0x08000 | 0xD00C8000 | yes | yes | pp0 | |
| PP1 | 0x0A000 | 0xD00CA000 | yes | no | pp1 | |
| PP2 | 0x0C000 | 0xD00CC000 | yes | no | pp2 | |
| PP3 | 0x0E000 | — | no | no | pp3 | |
| L2 cache 0 | 0x10000 | 0xD00D0000 | yes | yes | — | **the GP's L2** on Mali-450 |
| L2 cache 2 | 0x11000 | — | no | no | — | only with PP4–PP7 |
| BCAST | 0x13000 | 0xD00D3000 | yes | yes | — | broadcast unit |
| DLBU | 0x14000 | 0xD00D4000 | yes | yes | — | dynamic load-balancing unit |
| PP MMU broadcast | 0x15000 | 0xD00D5000 | yes | yes | — | MMU-shaped window that writes to several PP MMUs |
| PP broadcast | 0x16000 | 0xD00D6000 | yes | yes | pp | PP-shaped window that writes to several PPs |
| PP MMU 4–7 | 0x1C000, 0x1D000, 0x1E000, 0x1F000 | — | no | no | ppmmu4–7 | |
| PP4–PP7 | 0x28000, 0x2A000, 0x2C000, 0x2E000 | — | no | no | pp4–7 | |

- So a Mali-450 MP3 has **two L2 caches**: one at 0x10000 for the GP and one at 0x01000 for PP0–PP3. lima pairs PP n with the L2 at 0x01000 for n = 0..3 and with the one at 0x11000 for n = 4..7. lima only initialises the third L2 when one of PP4–PP7 exists.
- For comparison, Mali-400 has a single L2 at 0x01000 shared by GP and PPs, and no BCAST, DLBU or broadcast windows. The other offsets are the same.
- Block sizes are not given by lima. A PP uses at least 0x0000–0x10B4 (frame registers in the first 4 KiB, management in the second). The MMU, L2, PMU, DLBU and BCAST registers all lie below 0x30. Exact decode sizes are **UNVERIFIED**.
- lima pairs PP n with PP MMU n.

---

## 2. Registers per block

### 2.1 GP (geometry processor), base +0x00000

| Offset | Name | Use |
|---|---|---|
| 0x00 | VSCL_START_ADDR | frame word 0: vertex shader command list start (VA) |
| 0x04 | VSCL_END_ADDR | frame word 1: its end (VA, exclusive) |
| 0x08 | PLBUCL_START_ADDR | frame word 2: PLBU (polygon list builder) command list start |
| 0x0C | PLBUCL_END_ADDR | frame word 3: its end |
| 0x10 | PLBU_ALLOC_START_ADDR | frame word 4: tile heap start (VA) |
| 0x14 | PLBU_ALLOC_END_ADDR | frame word 5: tile heap end (VA) |
| 0x20 | CMD | commands (bits below) |
| 0x24 | INT_RAWSTAT | raw interrupt bits |
| 0x28 | INT_CLEAR | write 1s to clear |
| 0x2C | INT_MASK | 1 = enabled |
| 0x30 | INT_STAT | masked status (what lima's handler reads) |
| 0x34 | WRITE_BOUND_LOW | not used by lima |
| 0x3C / 0x40 | PERF_CNT_0/1_ENABLE | not used |
| 0x44 / 0x48 | PERF_CNT_0/1_SRC | not used |
| 0x4C / 0x50 | PERF_CNT_0/1_VALUE | not used |
| 0x54 | PERF_CNT_0_LIMIT | used only as a probe during hard reset (§2.1.3) |
| 0x68 | STATUS | bits below |
| 0x6C | VERSION | product and revision |
| 0x80 | VSCL_START_ADDR_READ | not used |
| 0x84 | PLBCL_START_ADDR_READ | not used |
| 0x94 | CONTR_AXI_BUS_ERROR_STAT | not used |

CMD bits:

| Bit | Name | lima use |
|---|---|---|
| 0 | START_VS | start the vertex shader list |
| 1 | START_PLBU | start the PLBU list |
| 4 | UPDATE_PLBU_ALLOC | load the heap start/end registers; written before every start and after heap growth |
| 5 | RESET | hard reset (after STOP_BUS) |
| 6 | FORCE_HANG | not used |
| 9 | STOP_BUS | stop bus traffic before a hard reset |
| 10 | SOFT_RESET | soft reset |

STATUS bits: 1 = VS active, 2 = bus stopped, 3 = PLBU active, 6 = bus error, 8 = write-bound error.

Interrupt bits (the same layout in RAWSTAT, CLEAR, MASK and STAT):

| Bit | Name | Group |
|---|---|---|
| 0 | VS_END_CMD_LST | used (done) |
| 1 | PLBU_END_CMD_LST | used (done) |
| 2 | PLBU_OUT_OF_MEM | error (recoverable, §4.3) |
| 3 | VS_SEM_IRQ | — |
| 4 | PLBU_SEM_IRQ | — |
| 5 | HANG | — (not enabled) |
| 6 | FORCE_HANG | error |
| 7 / 8 | PERF_CNT_0/1_LIMIT | — |
| 9 | WRITE_BOUND_ERR | error |
| 10 | SYNC_ERROR | error |
| 11 | AXI_BUS_ERROR | error |
| 12 | AXI_BUS_STOPPED | — |
| 13 | VS_INVALID_CMD | error |
| 14 | PLB_INVALID_CMD | error |
| 19 | RESET_COMPLETED | soft-reset completion |
| 20 | SEMAPHORE_UNDERFLOW | error |
| 21 | SEMAPHORE_OVERFLOW | error |
| 22 | PTR_ARRAY_OUT_OF_BOUNDS | error |

- "All" = **0x00787FFF** (bits 0–14 and 19–22).
- "Error" = **0x00706E44** (bits 2, 6, 9, 10, 11, 13, 14, 20, 21, 22).
- "Used" = error | bits 0, 1 = **0x00706E47**. This is the value lima keeps in INT_MASK while the GP is idle or running.

#### 2.1.1 Identify

VERSION (0x6C): [31:16] product, [15:8] major, [7:0] minor. Products: 0x0A07 Mali-200, 0x0C07 Mali-300, 0x0B07 Mali-400, **0x0D07 Mali-450**. lima stores the whole register for GET_PARAM (§5.2). The revision on the S905 is **UNVERIFIED** (read it on the board).

#### 2.1.2 Soft reset (used at init, at resume, and after every finished job)

1. INT_MASK = 0.
2. INT_CLEAR = 0x00080000 (RESET_COMPLETED).
3. CMD = 0x400 (SOFT_RESET).
4. Poll INT_RAWSTAT until bit 19 is set. lima polls without sleeping, for up to 100 µs.
5. INT_CLEAR = 0x00787FFF. INT_MASK = 0x00706E47.

At init and resume lima does all five steps at once. After a job it issues steps 1–3 when the job completes and does steps 4–5 just before the next job's registers are written.

#### 2.1.3 Hard reset (after an error or a timeout)

1. PERF_CNT_0_LIMIT = 0xC0FFE000. INT_MASK = 0.
2. CMD = 0x200 (STOP_BUS). Poll STATUS bit 2 (bus stopped), sleeping 3–10 µs between reads, for up to 100 µs.
3. CMD = 0x20 (RESET). Poll by writing 0xC01A0000 to PERF_CNT_0_LIMIT and reading it back until it reads 0xC01A0000 (same sleeps, 100 µs).
4. PERF_CNT_0_LIMIT = 0. INT_CLEAR = 0x00787FFF. INT_MASK = 0x00706E47.
5. Forget any pending soft reset.

The probe value only matters as a pattern. The idea that register writes do not stick until the reset ends is inferred, **UNVERIFIED**.

### 2.2 PP (pixel processor), base +0x08000 / +0x0A000 / +0x0C000

The first 4 KiB holds the per-job "frame" registers and three write-back units. The second 4 KiB holds management registers.

#### Frame registers (0x000–0x058, 23 words, written from the frame struct)

lima names only five of them. The rest of the names come from Mesa's `lima_pp_frame_reg`, as are the uses.

| Offset | Word | lima name | Mesa name | Mesa 22.0.5 value / kernel handling |
|---|---|---|---|---|
| 0x00 | 0 | FRAME | plbu_array_address | **kernel overwrites**: per-PP address, or the DLBU VA 0xFFF00000 (§4.4) |
| 0x04 | 1 | RSW | render_address | VA of the render state words |
| 0x08 | 2 | — | unused_0 | 0 |
| 0x0C | 3 | — | flags | 0x02, plus 0x01 for an fp16 colour buffer |
| 0x10 | 4 | — | clear_value_depth | |
| 0x14 | 5 | — | clear_value_stencil | |
| 0x18–0x24 | 6–9 | — | clear_value_color, _1, _2, _3 | |
| 0x28 | 10 | — | width | framebuffer width − 1 |
| 0x2C | 11 | — | height | framebuffer height − 1 |
| 0x30 | 12 | STACK | fragment_stack_address | **kernel overwrites per PP** from `fragment_stack_address[i]` |
| 0x34 | 13 | STACK_SIZE | fragment_stack_size | (size << 16) \| size, in Mesa's units |
| 0x38, 0x3C | 14, 15 | — | unused_1, unused_2 | 0 |
| 0x40 | 16 | ORIGIN_OFFSET_X | one | Mesa writes 1. The two names disagree; the meaning is **UNVERIFIED** |
| 0x44 | 17 | — | supersampled_height | height × 2 − 1 |
| 0x48 | 18 | — | dubya | 0x77 |
| 0x4C | 19 | — | onscreen | 1 |
| 0x50 | 20 | — | blocking | (shift_min << 28) \| (shift_h << 16) \| shift_w |
| 0x54 | 21 | — | scale | 0xE0C |
| 0x58 | 22 | — | channel_layout | 0x8888 |

#### Write-back units: WB0 at 0x100, WB1 at 0x200, WB2 at 0x300 (12 words each)

| Word (offset in unit) | lima name | Mesa name | Mesa use |
|---|---|---|---|
| 0 (0x00) | SOURCE_SELECT | type | 0 = unused (zeroed struct), 1 = depth/stencil, 2 = colour |
| 1 (0x04) | SOURCE_ADDR | address | VA of the target surface |
| 2 (0x08) | — | pixel_format | |
| 3 (0x0C) | — | downsample_factor | |
| 4 (0x10) | — | pixel_layout | 0 = linear, 2 = 16×16 block-interleaved |
| 5 (0x14) | — | pitch | tiled width, or stride / 8 for linear |
| 6 (0x18) | — | mrt_bits | 0x4 to swap R and B |
| 7 (0x1C) | — | mrt_pitch | |
| 8 (0x20) | — | zero | |
| 9–11 (0x24–0x2C) | — | unused | |

The kernel does not interpret any WB word. It writes all 36.

#### Management registers (0x1000–0x10B4)

| Offset | Name | Use |
|---|---|---|
| 0x1000 | VERSION | product and revision |
| 0x1004 | CURRENT_REND_LIST_ADDR | not used |
| 0x1008 | STATUS | bit 0 = rendering active, bit 4 = bus stopped |
| 0x100C | CTRL | commands (bits below) |
| 0x1020 | INT_RAWSTAT | raw bits; lima also writes it during soft reset (§2.2.2) |
| 0x1024 | INT_CLEAR | write 1s to clear |
| 0x1028 | INT_MASK | 1 = enabled |
| 0x102C | INT_STATUS | masked status (what lima reads) |
| 0x1044 | WRITE_BOUNDARY_LOW | not used |
| 0x1050 | BUS_ERROR_STATUS | not used |
| 0x1080 / 0x1084 / 0x1088 / 0x108C | PERF_CNT_0 ENABLE / SRC / LIMIT / VALUE | LIMIT used as the hard-reset probe |
| 0x10A0 / 0x10A4 / 0x10A8 / 0x10AC | PERF_CNT_1 ENABLE / SRC / LIMIT / VALUE | not used |
| 0x10B0 / 0x10B4 | PERFMON_CONTR / PERFMON_BASE | not used |

CTRL bits: 0 = STOP_BUS, 3 = FLUSH_CACHES (not used by lima), 5 = FORCE_RESET (hard reset), 6 = START_RENDERING, 7 = SOFT_RESET.

Interrupt bits:

| Bit | Name | Group |
|---|---|---|
| 0 | END_OF_FRAME | used (done) |
| 1 | END_OF_TILE | — |
| 2 | HANG | — (not enabled) |
| 3 | FORCE_HANG | error |
| 4 | BUS_ERROR | error |
| 5 | BUS_STOP | — |
| 6 / 7 | CNT_0/1_LIMIT | — |
| 8 | WRITE_BOUNDARY_ERROR | error |
| 9 | INVALID_PLIST_COMMAND | error |
| 10 | CALL_STACK_UNDERFLOW | error |
| 11 | CALL_STACK_OVERFLOW | error |
| 12 | RESET_COMPLETED | soft-reset completion |

All = **0x1FFF**, error = **0x0F18**, used = **0x0F19**.

#### 2.2.1 Identify

VERSION (0x1000): [31:16] product, [15:8] major, [7:0] minor. Products: 0xC807 Mali-200, 0xCE07 Mali-300, 0xCD07 Mali-400, **0xCF07 Mali-450**. lima keeps the value read from the last PP it initialised.

#### 2.2.2 Soft reset

1. INT_MASK = 0.
2. **INT_RAWSTAT** (not INT_CLEAR) = 0x1FFF.
3. CTRL = 0x80 (SOFT_RESET).
4. Poll until STATUS bit 0 is clear **and** INT_RAWSTAT reads exactly 0x1000 (only RESET_COMPLETED). lima polls without sleeping, for up to 100 µs.
5. INT_CLEAR = 0x1FFF. INT_MASK = 0x0F19.

Since step 4 needs every other raw bit at 0, writing 1s to INT_RAWSTAT probably clears them. That is **UNVERIFIED**.

As with the GP, lima issues steps 1–3 when a job completes and finishes steps 4–5 before the next job. On Mali-450 it issues steps 1–3 through the PP broadcast window (§2.8) and then polls each PP through its own window.

#### 2.2.3 Hard reset

The same as the GP's (§2.1.3), with PP registers:

1. PERF_CNT_0_LIMIT (0x1088) = 0xC0FFE000. INT_MASK = 0.
2. CTRL = 0x01 (STOP_BUS). Poll STATUS bit 4 (3–10 µs sleeps, 100 µs).
3. CTRL = 0x20 (FORCE_RESET). Poll by writing 0xC01A0000 to 0x1088 until it reads back (100 µs).
4. 0x1088 = 0. INT_CLEAR = 0x1FFF. INT_MASK = 0x0F19.

lima hard-resets each PP through its own window, never through the broadcast window.

### 2.3 MMU (GP MMU, PP MMU 0–2; the same layout)

| Offset | Name | Use |
|---|---|---|
| 0x00 | DTE_ADDR | page directory phys address (4 KiB aligned) |
| 0x04 | STATUS | bits below |
| 0x08 | COMMAND | write a command value (not a bit mask) |
| 0x0C | PAGE_FAULT_ADDR | the faulting VA |
| 0x10 | ZAP_ONE_LINE | not used by lima |
| 0x14 | INT_RAWSTAT | |
| 0x18 | INT_CLEAR | |
| 0x1C | INT_MASK | |
| 0x20 | INT_STATUS | masked status |

STATUS bits:

| Bit | Meaning |
|---|---|
| 0 | paging enabled |
| 1 | page fault active |
| 2 | stall active |
| 3 | idle |
| 4 | replay buffer empty |
| 5 | the page fault was a write |
| [10:6] | bus ID of the faulting master |
| 31 | stall not active |

COMMAND values: 0 = enable paging, 1 = disable paging, 2 = enable stall, 3 = disable stall, 4 = zap cache (flush the TLB), 5 = page fault done, 6 = hard reset. lima uses 0, 2, 3, 4 and 6. It never uses 1 or 5: it recovers from a page fault by hard-resetting the MMU instead.

Interrupt bits: 0 = page fault, 1 = read bus error. lima's INT_MASK value is **0x3**.

- **Identify / probe**: write 0xCAFEBABE to DTE_ADDR and read it back. A working MMU returns **0xCAFEB000**: the low 12 bits read as 0. lima fails the probe otherwise. There is no MMU version register in lima.
- lima's MMU interrupt handler decodes "write" and "bus ID" from the INT_STATUS value, using the STATUS register's bit positions. Taking them from STATUS (0x04) is probably what was meant. **UNVERIFIED**.
- The MMU sequences are in §3.2.

### 2.4 L2 cache (0x10000 for the GP, 0x01000 for the PPs)

| Offset | Name | Use |
|---|---|---|
| 0x04 | SIZE | geometry (below) |
| 0x08 | STATUS | bit 0 = command busy, bit 1 = data busy |
| 0x10 | COMMAND | bit 0 = clear all (flush and invalidate everything) |
| 0x14 | CLEAR_PAGE | not used |
| 0x18 | MAX_READS | outstanding-read limit |
| 0x1C | ENABLE | bit 0 = access, bit 1 = read allocate |
| 0x20 / 0x24 | PERFCNT_SRC0 / VAL0 | not used |
| 0x28 / 0x2C | PERFCNT_SRC1 / VAL1 | not used |

- **SIZE** decode (each field is a log2): [7:0] line size in bytes, [15:8] number of ways, [23:16] total size in bytes, [31:24] external bus width in bits. lima prints size = 2^([23:16] − 10) KiB. The values on the S905 are **UNVERIFIED** (read them on the board).
- Offset 0x00 is not named by lima. A version register there is **UNVERIFIED**.
- **Flush** ("clear all"): COMMAND = 1, then poll STATUS until bit 0 is clear. lima polls without sleeping, for up to 1000 µs, under a per-cache lock.
- **Init and resume**: flush, then ENABLE = **0x3** (access + read allocate), then MAX_READS = **0x1C** (28).
- lima flushes the pipe's L2 caches **before every job** (§4.1).

### 2.5 PMU (power management unit), base +0x02000

| Offset | Name | Use |
|---|---|---|
| 0x00 | POWER_UP | write a domain mask to power those domains up |
| 0x04 | POWER_DOWN | write a domain mask to power them down |
| 0x08 | STATUS | one bit per domain: **1 = off, 0 = on** |
| 0x0C | INT_MASK | lima writes 0 (it polls) |
| 0x10 | INT_RAWSTAT | bit 0 = command done |
| 0x18 | INT_CLEAR | bit 0 = clear command done |
| 0x1C | SW_DELAY | power-switch delay |

Domain bits on **Mali-450**: bit 0 = GP, bit 1 = PP0, bit 2 = PP1–PP3, bit 3 = PP4–PP7. Each domain brings up its own L2 by itself, and the PPs cannot be powered one by one. For the S905 the mask is **0x7**. (On Mali-400: bit 0 = GP, bit 1 = L2, bit 2 + n = PP n.)

- **Init and resume** (lima does this first, before any other block):
  1. INT_MASK = 0.
  2. SW_DELAY = **0xFFFF**. lima's reason: lower values make the GPU unstable at high clock rates.
  3. Read STATUS. If it is not 0, write that same value to POWER_UP, poll INT_RAWSTAT bit 0 (100 µs between reads, for up to 100 ms), then write INT_CLEAR = 1.
- **Suspend and remove**: mask = 0x7 (from the PPs present). Write POWER_DOWN = (~STATUS) & mask, the domains that are on. On Mali-450 lima then waits for INT_RAWSTAT bit 0 as above and clears it. (On Mali-400 it skips the wait, because no interrupt comes when everything goes off.)
- lima only uses the `pmu` interrupt to decide that the PMU exists. It never requests that interrupt.
- What STATUS reads after U-Boot on this board is **UNVERIFIED**.

### 2.6 DLBU (dynamic load-balancing unit), base +0x14000

| Offset | Name | lima writes |
|---|---|---|
| 0x00 | MASTER_TLLIST_PHYS_ADDR | at init and resume: phys address of the 4 KiB DLBU page, **OR 1** |
| 0x04 | MASTER_TLLIST_VADDR | at init and resume: **0xFFF00000** (the page's fixed VA, §3.3) |
| 0x08 | TLLIST_VBASEADDR | per job: `dlbu_regs[0]` |
| 0x0C | FB_DIM | per job: `dlbu_regs[1]` |
| 0x10 | TLLIST_CONF | per job: `dlbu_regs[2]` |
| 0x14 | START_TILE_POS | per job: `dlbu_regs[3]` |
| 0x18 | PP_ENABLE_MASK | per job: one bit per PP taking part (bit n = PP n), or 0 when the job does not use the DLBU |

- The meaning of bit 0 in MASTER_TLLIST_PHYS_ADDR is **UNVERIFIED** (perhaps "valid" or "enable").
- What the hardware writes into or reads from the DLBU page is **UNVERIFIED**. The CPU never touches it after allocation.
- The values Mesa 22.0.5 puts in `dlbu_regs` are in §5.9.

### 2.7 BCAST (broadcast unit), base +0x13000

| Offset | Name | lima writes |
|---|---|---|
| 0x00 | BROADCAST_MASK | init, resume and after errors: (present PPs) << 16 = **0x00070000**. Per job: keep [31:16], set [15:0] to the PPs of the job (read-modify-write). |
| 0x04 | INTERRUPT_MASK | init, resume and after errors: present PPs = **0x7** |

- When a timeout handler masks interrupts, lima writes 0 to both registers. After the hard reset it restores the init values.
- Inference, **UNVERIFIED**: bits [7:0] pick which PPs receive writes made to the PP broadcast window, and bits [23:16] pick which PP MMUs receive writes to the PP MMU broadcast window. lima's usage fits this. It switches page tables through the MMU window before a job sets bits [7:0], so only the upper half can be routing those writes.
- Inference, **UNVERIFIED**: INTERRUPT_MASK routes the selected PPs' interrupts to the broadcast `pp` line. lima's completion counting is only correct if, in this mode, the per-PP lines stay quiet (§4.6).

### 2.8 The two broadcast windows

- **PP broadcast, +0x16000**: a PP-shaped register window. lima writes the frame registers, the WB registers, CTRL = START_RENDERING and CTRL = SOFT_RESET through it, so they reach every PP selected in BROADCAST_MASK [7:0]. It never reads status through it: lima reads each PP's STATUS and INT_STATUS through its own window. It registers its handler on the `pp` interrupt and writes nothing at init.
- **PP MMU broadcast, +0x15000**: an MMU-shaped window. lima uses it to switch page tables (stall, DTE_ADDR, zap, unstall) and for page-fault recovery on the PP side. At init and resume lima skips it: each PP MMU is set up through its own window.
- lima polls MMU STATUS **through the MMU broadcast window** (the stall wait, the paging-enabled wait, the page-fault check). What a read through a broadcast window returns (one MMU, an AND or an OR of all) is **UNVERIFIED**.

---

## 3. MMU page tables and the VM

### 3.1 Format

- Two levels. The VA is 32 bits:
  - VA[31:22] indexes the page directory (1024 entries × 4 bytes = one 4 KiB page);
  - VA[21:12] indexes a page table (1024 entries, one 4 KiB page);
  - VA[11:0] is the byte offset. The page size is **4 KiB**, the only one lima uses. Larger pages are **UNVERIFIED**.
- **PDE**: [31:12] = phys address of the page table, bit 0 = present. lima writes `PT phys | 0x1`.
- **PTE**: [31:12] = phys page address, [8:0] = flags:

| Bit | Flag |
|---|---|
| 0 | present |
| 1 | read permission |
| 2 | write permission |
| 3 | override cache attributes |
| 4 | write cacheable |
| 5 | write allocate |
| 6 | write bufferable |
| 7 | read cacheable |
| 8 | read allocate |

- lima maps every page with **0x1DF** (all of these except write allocate). An uncached value 0x007 (present + read + write) is defined but never used.
- An empty entry is 0. lima clears PTEs on unmap and never removes page tables until the whole VM is freed.
- DTE_ADDR holds the page directory's phys address. The low 12 bits read as 0.

### 3.2 MMU sequences

lima waits for every MMU command without sleeping, for up to 100 µs. If a wait times out, it logs the failure; only init and resume stop on it.

**Init / resume** (each real MMU, through its own window; the broadcast MMU is skipped):

1. (Init only) the DTE_ADDR write test of §2.3.
2. COMMAND = 6 (hard reset). lima writes it twice: once bare, then once more followed by the wait. Poll until DTE_ADDR reads 0.
3. INT_MASK = 0x3.
4. DTE_ADDR = phys address of the device's "empty" page directory (§3.3).
5. COMMAND = 0 (enable paging). Poll until STATUS bit 0 is set.

**Switch to a job's page directory** (at the start of every job; for the PP side on Mali-450 through the MMU broadcast window only):

1. COMMAND = 2 (enable stall). Poll until STATUS bit 2 is set.
2. DTE_ADDR = phys address of the job's page directory.
3. COMMAND = 4 (zap cache). No wait.
4. COMMAND = 3 (disable stall). Poll until STATUS bit 2 is clear.

**Flush the TLB only**: COMMAND = 4. lima uses it before resuming after heap growth (§4.3).

**Page-fault interrupt**:

1. Read INT_STATUS. 0 means "not mine" (the line may be shared).
2. If bit 0 is set, read PAGE_FAULT_ADDR (for the log).
3. INT_MASK = 0. INT_CLEAR = the status read in step 1.
4. Mark the pipe as failed (§4.7).

**Recovery after a page fault** (in the timeout/error path, after the GP/PP hard reset; on the PP side through the broadcast MMU window): read STATUS. If bit 1 (page fault active) is set:

1. INT_MASK = 0.
2. DTE_ADDR = 0xCAFEBABE.
3. COMMAND = 6, then poll until DTE_ADDR reads 0.
4. INT_MASK = 0x3.
5. DTE_ADDR = the empty page directory.
6. COMMAND = 0, then poll STATUS bit 0.

### 3.3 lima's VM layout

- **One VM (page directory) per open file**, plus a device-wide "empty" VM that the MMUs hold while idle. Every context of a file shares the file's VM.
- **VA range for buffers on Mali-450: 0x00000000 up to 0xFFF00000** (exclusive). The range starts at 0, so a buffer can get VA 0. (Mali-400: 0x00000000 up to 0x1_0000_0000.)
- **The reserved region is 0xFFF00000–0xFFFFFFFF.** Its first page, **VA 0xFFF00000**, maps the DLBU page in every VM, the empty VM included, with flags 0x1DF. That is PDE index 0x3FF, PTE index 0x300. The rest of the reserved region stays unmapped.
- lima allocates the DLBU page once per device: 4 KiB, write-combined, phys below 4 GiB. It does this at probe, before the page tables or any register access.
- A buffer gets its VA when it gets a handle in a file (create or import). The VA covers the buffer's full size (page-rounded), and it is mapped page by page. VAs come from a range allocator with no alignment beyond the page size.
- Page tables are lima's choice, not the hardware's. lima allocates them 8 at a time (a 32 KiB contiguous block covering 32 MiB of VA, which fills 8 consecutive PDEs). A driver may allocate one page table per PDE instead.
- When a buffer's last user in a VM goes away, lima clears its PTEs. It does not zap the TLB then, because the next job's switch (§3.2) zaps anyway.
- A buffer stays mapped for as long as a job that uses it is queued or running, because the submit takes an extra reference on the mapping.

---

## 4. Job submission

### 4.1 Common to both pipes

- There are two independent pipes, **GP** and **PP**, each with its own scheduler. Each runs **one job at a time**. GP and PP jobs can run at the same time. The order between a GP job and the PP job that reads its output comes from buffer fences or sync objects (§5.5).
- Per job, before touching the processor, lima:
  1. flushes the pipe's L2 caches (GP pipe: the L2 at 0x10000; PP pipe on Mali-450: the L2 at 0x01000). lima's reason: without this the GP/PP hang or page-fault after a while, and its authors did not find out why;
  2. switches the pipe's MMUs to the job's VM (§3.2). The GP pipe uses the GP MMU. The PP pipe on Mali-450 uses only the MMU broadcast window;
  3. clears the pipe's error flag and calls the job's run step (§4.2, §4.4).
- After a job completes without error, lima starts the soft reset (§2.1.2, §2.2.2) and signals the job's fence. The next job finishes that reset before it writes registers.

### 4.2 GP job

**Validate** (at submit; failure → EINVAL):

- start ≤ end for each of the three pairs (words 0/1, 2/3, 4/5);
- the job must not have both lists empty (word 0 = word 1 **and** word 2 = word 3).

**Run**:

1. **Heap fix-up**: if one of the job's buffers is a heap buffer (§5.3) whose VA equals word 4, the kernel sets word 5 = word 4 + that buffer's current backed size. It marks the job recoverable and remembers the heap.
2. cmd = START_VS (bit 0) if word 0 ≠ word 1, plus START_PLBU (bit 1) if word 2 ≠ word 3.
3. Finish any pending soft reset (§2.1.2 steps 4–5).
4. Write the six words to 0x00, 0x04, …, 0x14 in order.
5. CMD = 0x10 (UPDATE_PLBU_ALLOC).
6. CMD = cmd.

**Interrupt** (`gp` line):

1. Read INT_STAT (0x30) and STATUS (0x68). If INT_STAT is 0 → not mine.
2. If INT_STAT has any error bit:
   - if PLBU_OUT_OF_MEM is the **only** error bit, this is a heap-full event. The job keeps its recoverable mark;
   - any other error marks the job not recoverable;
   - either way: INT_MASK = 0, the pipe is in error, the job is over.
3. Otherwise the job is done when INT_STAT has VS_END_CMD_LST or PLBU_END_CMD_LST **and** STATUS shows neither VS active (bit 1) nor PLBU active (bit 3). So a job with both lists ends on the second end interrupt.
4. INT_CLEAR = the INT_STAT value.
5. If the job is over, hand it to the completion logic (§4.7).

### 4.3 Heap buffers and PLBU out-of-memory

- A heap buffer (LIMA_BO_FLAG_HEAP) reserves VA for its full size but starts with only **8 pages (32 KiB)** of memory (module parameter heap_init_nr_pages), mapped at its start. The size is only an upper bound.
- On an out-of-memory interrupt in a recoverable job, lima runs a recovery step outside the interrupt:
  1. Flush the GP pipe's L2 (0x10000). COMMAND = 4 (zap) on the GP MMU.
  2. If (word 5 − word 4) equals the heap's backed size, **double** the backed size, capped at the buffer size. Allocate the new pages and map them right after the old ones. If the heap is already at full size (ENOSPC) or the allocation fails, the job fails (§4.7).
  3. INT_MASK = 0x00706E47.
  4. PLBU_ALLOC_START_ADDR = the old end (old word 5): the GP resumes where it stopped.
  5. Word 5 = word 4 + the new backed size. PLBU_ALLOC_END_ADDR = word 5.
  6. CMD = 0x10 (UPDATE_PLBU_ALLOC). No start command: the GP continues by itself.
- Heap buffers cannot be mapped into user space, vmapped or pinned (EINVAL).
- If a GP MMU page fault happens in a recoverable job, lima's code also takes this recovery path, because the job is still marked recoverable. That is lima's behaviour, not a documented intent.

### 4.4 PP job on Mali-450 (broadcast)

**Validate**: num_pp must be 1..(number of PPs), and `_pad` must be 0.

**Run** (PP n for n = 0..num_pp−1):

1. Clear the per-job "done" bitmap. pending = num_pp.
2. **If use_dlbu**:
   1. DLBU PP_ENABLE_MASK = bits 0..num_pp−1.
   2. Word 0 (FRAME) = **0xFFF00000**.
   3. DLBU 0x08, 0x0C, 0x10, 0x14 = `dlbu_regs[0..3]`.

   **Otherwise**: DLBU PP_ENABLE_MASK = 0.
3. BCAST BROADCAST_MASK = (current value & 0xFFFF0000) | bits 0..num_pp−1.
4. Finish the pending soft reset. lima polls each PP of **this** job through its own window, then clears and unmasks it.
5. Through the **PP broadcast** window: write the 23 frame words to 0x000–0x058, then the 36 WB words to 0x100–0x12C, 0x200–0x22C and 0x300–0x32C.
6. For each PP n, through **its own** window:
   1. STACK (0x30) = `fragment_stack_address[n]`;
   2. if not use_dlbu: FRAME (0x00) = `plbu_array_address[n]`.
7. Through the PP broadcast window: CTRL = 0x40 (START_RENDERING). All selected PPs start together.

- In DLBU mode every PP fetches its tile list from VA 0xFFF00000, which the DLBU serves. The DLBU spreads the tiles across the PPs.
- In the per-PP mode user space has split the work into one PLBU array per PP.
- When Mesa chooses which mode is in §5.9.
- Note: step 4 polls only the PPs of the new job. If the previous job used more PPs, the extra PPs keep INT_MASK = 0 until a later job polls them. Mesa 22.0.5 always uses every PP, so this does not arise there.

### 4.5 PP job on Mali-400 (per core, for comparison)

For each PP n: copy the frame, set word 0 = `plbu_array_address[n]` and word 12 = `fragment_stack_address[n]`, finish that PP's soft reset, write the 23 + 36 words to PP n, and write CTRL = START_RENDERING to PP n. Each PP interrupts on its own line. A driver for the S905 only needs §4.4.

### 4.6 PP completion

**Broadcast interrupt (`pp` line, Mali-450)**: if no job is current → not mine. Otherwise, for each PP n of the job that is not yet done:

1. Read STATUS **first**, then INT_STATUS. The order guards against a status change between the two reads.
2. If INT_STATUS ≠ 0:
   - if any error bit is set: read STATUS for the log, mark the pipe in error, INT_MASK = 0;
   - INT_CLEAR = INT_STATUS;
   - mark PP n done.
3. If INT_STATUS = 0 and STATUS bit 0 (rendering active) is set: skip PP n for now.
4. If INT_STATUS = 0 and rendering is not active: mark PP n done.
5. Each PP marked done decrements pending. When pending reaches 0 the job is over (§4.7).

**Per-PP interrupts (`pp0`..`pp2`)**: lima registers a handler on these too, even on Mali-450. It reads INT_STATUS (0 → not mine), handles errors as above, clears, and decrements pending. It does not set the done bitmap. Both handlers counting the same PP would break the count, so lima relies on the per-PP lines not firing in broadcast mode (§2.7, **UNVERIFIED**).

### 4.7 Errors, timeouts and recovery

- **Job over, no error**: start the soft reset, signal the fence, drop the runtime-PM reference.
- **Job over with error, recoverable** (GP heap full): the recovery step of §4.3.
- **Job over with error, not recoverable**, or a recovery that failed: the pipe is faulted, and the scheduler immediately runs the timeout handler below.
- **MMU fault**: the MMU handler (§3.2) marks the pipe in error. On the GP side the job is over at once. On the PP side the fault counts as one PP finishing (pending − 1).
- **Timeout handler** (after 10 s, or at once on a fault):
  1. If the fence is already signalled, the timeout was spurious: stop here.
  2. Wait for in-flight interrupt handlers of the pipe's processors (and of the PP broadcast line), then check the fence again. Signalled now means only late interrupt handling: stop here.
  3. Mask the processors' interrupts: GP INT_MASK = 0; on the PP side every PP's INT_MASK = 0, and BCAST BROADCAST_MASK = 0 and INTERRUPT_MASK = 0.
  4. Stop the scheduler. Count a hang against the job. The hang limit is 0 by default, so the job is dropped and its fence completes with an error. That last part is the DRM scheduler's behaviour, not code in the lima files.
  5. Hard reset: the GP (§2.1.3), or every PP one by one (§2.2.3) followed by BCAST re-init (0x00070000 / 0x7).
  6. Page-fault recovery on the pipe's MMU(s) (§3.2).
  7. Drop the current VM and job. Release the runtime-PM reference.
  8. Restart the scheduler, which resubmits the queued jobs.

### 4.8 Timeouts and delays lima uses

| What | Poll interval | Limit |
|---|---|---|
| PMU command done (INT_RAWSTAT bit 0) | 100 µs | 100 ms |
| MMU commands (hard reset, enable paging, stall, unstall) | none (busy) | 100 µs |
| GP soft reset (RAWSTAT bit 19) | none | 100 µs |
| PP soft reset (STATUS bit 0 clear and RAWSTAT = 0x1000) | none | 100 µs |
| GP/PP bus stop, GP/PP hard reset | 3–10 µs sleeps | 100 µs each |
| L2 flush (STATUS bit 0 clear) | none | 1000 µs |
| Job timeout | — | **10,000 ms** (module parameter sched_timeout_ms; 10 s when ≤ 0) |
| Runtime-PM autosuspend after idle | — | 200 ms |
| devfreq load sampling (GPL file; simple_ondemand, up 30 %, down-differential 5 %) | 50 ms | — |

---

## 5. The lima uAPI (lima_drm.h)

Driver name "lima", version **1.1.0**. 1.1 added heap buffers. Features: render node, GEM, sync objects. All seven ioctls are allowed on the render node.

### 5.1 Ioctls

Number = DRM_COMMAND_BASE (0x40) + index. Linux's encoding is (dir << 30) | (size << 16) | ('d' << 8) | number, with dir: 1 = write (user → kernel), 2 = read, 3 = both.

| Index | Name | Dir | Struct (size) | Linux request value |
|---|---|---|---|---|
| 0x00 | GET_PARAM | read/write | drm_lima_get_param (16) | 0xC0106440 |
| 0x01 | GEM_CREATE | read/write | drm_lima_gem_create (16) | 0xC0106441 |
| 0x02 | GEM_INFO | read/write | drm_lima_gem_info (16) | 0xC0106442 |
| 0x03 | GEM_SUBMIT | write | drm_lima_gem_submit (48) | 0x40306443 |
| 0x04 | GEM_WAIT | write | drm_lima_gem_wait (16) | 0x40106444 |
| 0x05 | CTX_CREATE | read | drm_lima_ctx_create (8) | 0x80086445 |
| 0x06 | CTX_FREE | write | drm_lima_ctx_free (8) | 0x40086446 |

Sizes and request values were checked by compiling against the header in `deps/build/mesa-22.0.5/include/drm-uapi/lima_drm.h`, which is identical to the kernel's.

### 5.2 GET_PARAM

Fields: `param` u32 (in), `pad` u32 (must be 0, else EINVAL), `value` u64 (out, at offset 8).

| param | Value | Returns |
|---|---|---|
| 0 | GPU_ID | 0 = unknown, 1 = Mali-400, **2 = Mali-450** |
| 1 | NUM_PP | number of PPs in the PP pipe (**3** here) |
| 2 | GP_VERSION | the raw GP VERSION register (0x6C) |
| 3 | PP_VERSION | the raw PP VERSION register (0x1000) |

Any other param → EINVAL. Mesa 22.0.5 asks for GPU_ID and NUM_PP only.

### 5.3 GEM_CREATE

Fields: `size` u32 (in), `flags` u32 (in), `handle` u32 (out), `pad` u32 (must be 0).

- EINVAL if pad ≠ 0, if any flag other than bit 0 is set, or if size = 0.
- The size is rounded up to whole pages.
- Pages come from below 4 GiB.
- **flags bit 0, LIMA_BO_FLAG_HEAP**: a growable heap (§4.3). Only 32 KiB is backed at first, and it cannot be mmapped, vmapped or pinned.
- Without the flag: every page is allocated and DMA-mapped at create time.
- Creating the handle also maps the buffer into the caller's VM (§3.3).
- Mesa 22.0.5 makes its GP tile heaps 16 MiB with the flag when the driver version is above 1.0, and 1 MiB without the flag otherwise.

### 5.4 GEM_INFO

Fields: `handle` u32 (in), `va` u32 (out), `offset` u64 (out, at offset 8).

- `va` = the buffer's GPU VA in the caller's VM.
- `offset` = the fake offset to pass to mmap() on the DRM file to map the buffer for the CPU (write-combined).
- ENOENT for an unknown handle.

### 5.5 GEM_SUBMIT

| Offset | Field | Type | Meaning |
|---|---|---|---|
| 0 | ctx | u32 | context ID from CTX_CREATE |
| 4 | pipe | u32 | 0 = GP, 1 = PP |
| 8 | nr_bos | u32 | number of entries in `bos` |
| 12 | frame_size | u32 | size of `frame` in bytes |
| 16 | bos | u64 | user pointer to an array of drm_lima_gem_submit_bo |
| 24 | frame | u64 | user pointer to the GP or PP frame (§5.8) |
| 32 | flags | u32 | bit 0 = LIMA_SUBMIT_FLAG_EXPLICIT_FENCE |
| 36 | out_sync | u32 | sync object to receive the job's completion fence (0 = none) |
| 40 | in_sync[2] | u32 × 2 | sync objects to wait on before the job runs (0 = unused) |

drm_lima_gem_submit_bo (8 bytes): `handle` u32, `flags` u32. Flag 0x01 = READ, 0x02 = WRITE. The kernel only looks at WRITE.

What the kernel does, in order:

1. EINVAL if pipe ≥ 2, nr_bos = 0, a flag other than bit 0 is set, or frame_size ≠ the pipe's frame size. The pipe's frame size is 24 for GP, and 312 for PP on Mali-450 (272 on Mali-400).
2. Copy the BO array and the frame (EFAULT on failure). Validate the frame (§4.2, §4.4).
3. Look up the context (ENOENT).
4. Look up each handle (ENOENT). Take a reference on its VM mapping; a buffer not mapped in this file's VM → ENOENT.
5. Lock all the buffers' reservation objects.
6. Create the job. Add in_sync[0] and in_sync[1] as dependencies when they are not 0.
7. For each buffer, unless EXPLICIT_FENCE is set, add implicit dependencies on the buffer's existing fences: a writer waits for all earlier readers and writers, a reader waits for earlier writers.
8. Queue the job on the context's entity for that pipe.
9. Add the job's fence to every buffer, as a write fence if WRITE is set, else as a read fence.
10. Unlock. If out_sync ≠ 0, replace that sync object's fence with the job's fence.

An unknown out_sync handle gives ENOENT; lima checks it before it looks up the buffers. An unknown in_sync handle also gives ENOENT.

Mesa 22.0.5 always passes out_sync (one sync object per pipe per context), uses in_sync[0] only when importing a sync file, never sets EXPLICIT_FENCE, and waits for jobs with the sync object wait ioctl.

### 5.6 GEM_WAIT

Fields: `handle` u32, `op` u32, `timeout_ns` s64 (at offset 8).

- op bit 0 = READ (the CPU wants to read), bit 1 = WRITE (the CPU wants to write). Other bits → EINVAL. op = 0 returns 0 at once.
- WRITE waits for every GPU fence on the buffer. READ alone waits only for GPU writers.
- timeout_ns is an **absolute** time on the monotonic clock. 0, or a time already past, means "poll".
- Results:
  - done → 0;
  - a real wait that timed out → ETIMEDOUT;
  - a poll (timeout 0 or already past) that found the buffer busy → EBUSY;
  - unknown handle → EINVAL (from the DRM core helper);
  - interrupted → ERESTARTSYS.

### 5.7 CTX_CREATE / CTX_FREE

- CTX_CREATE: `id` u32 (out), `_pad` u32 (must be 0, else EINVAL).
  - A context holds one scheduler entity per pipe, at normal priority.
  - IDs are allocated per file from 0 upward, as 32-bit values.
  - lima records the creating process's PID and name, for error dumps only.
- CTX_FREE: `id` u32 (in), `_pad` u32 (must be 0). An unknown id → EINVAL. The context lives on until its queued jobs drop their references.
- Closing the file frees all its contexts and then its VM.

### 5.8 Frame layouts

**drm_lima_gp_frame** (24 bytes): `frame[6]`, written to GP 0x00–0x14 (§4.2). Mesa's names, in order: vs_cmd_start, vs_cmd_end, plbu_cmd_start, plbu_cmd_end, tile_heap_start, tile_heap_end.

**drm_lima_m450_pp_frame** (312 bytes):

| Offset | Field | Words | Kernel use |
|---|---|---|---|
| 0 | frame[23] | 23 | PP 0x000–0x058 through the broadcast window. Word 0 is replaced by 0xFFF00000 in DLBU mode, and words 0 and 12 by per-PP values (§4.4) |
| 92 | num_pp | 1 | PPs to use, 1..NUM_PP; PPs 0..num_pp−1 are used |
| 96 | wb[36] | 36 | WB0 (words 0–11) → 0x100, WB1 → 0x200, WB2 → 0x300 |
| 240 | use_dlbu | 1 | non-zero = DLBU mode |
| 244 | _pad | 1 | must be 0 |
| 248 | plbu_array_address[8] **or** dlbu_regs[4] (a union) | 8 | per-PP mode: PP n's FRAME register. DLBU mode: words 0–3 go to DLBU 0x08–0x14 |
| 280 | fragment_stack_address[8] | 8 | PP n's STACK register (0x30) |

**drm_lima_m400_pp_frame** (272 bytes, for reference): frame[23] at 0, num_pp at 92, wb[36] at 96, plbu_array_address[4] at 240, fragment_stack_address[4] at 256.

### 5.9 What Mesa 22.0.5 puts in a Mali-450 PP frame

- Full-screen damage (no partial damage region) → **DLBU mode**:
  - `dlbu_regs[0]` = VA of the PLB buffer;
  - `[1]` = ((tiled_h − 1) << 16) | (tiled_w − 1);
  - `[2]` = (s << 28) | (shift_h << 16) | shift_w, with s = log2(512) − 7 = 2 for Mesa's 512-byte PLB blocks;
  - `[3]` = ((tiled_h − 1) << 24) | ((tiled_w − 1) << 16).
  - tiled_w/h are the framebuffer size in 16×16 tiles. shift_w/h are the halvings Mesa applies until the block count fits its PLB limit (4096 blocks on Mali-450).
- Partial damage → **per-PP mode**: `plbu_array_address[n]` = VA of PP n's slice of a PLBU stream that Mesa builds on the CPU.
- In both modes: `fragment_stack_address[n]` = stack base + n × (stack size × 0x400), and num_pp = NUM_PP.

### 5.10 Other DRM interfaces Mesa's lima userland uses

These are not lima-specific, but a Haiku back end must provide equivalents (facts from Mesa 22.0.5):

- driver version (to enable heaps);
- GEM_CLOSE;
- mmap at GEM_INFO's offset;
- GEM_FLINK and GEM_OPEN (global names);
- PRIME handle ↔ fd (dma-buf);
- sync objects: create (signalled), destroy, wait, import and export as a sync file;
- device info: the platform compatible string. Mesa uses it only to give `allwinner,sun50i-h5-mali` a smaller PLB limit.

---

## 6. Probe, resume and suspend: what lima does, in order

### 6.1 Probe (Mali-450, as on the S905)

1. Set the DMA mask to 32 bits.
2. Get and enable the `bus` and `core` clocks. Deassert the resets (none on GXBB). Enable the optional `mali` regulator (none on GXBB).
3. Allocate the DLBU page (4 KiB, write-combined).
4. Create the empty VM: a zeroed page directory, and a page-table block holding the DLBU page at VA 0xFFF00000.
5. Map the register window.
6. Initialise the blocks in this order. The order is lima's internal block numbering, not the address order:
   1. **PMU**: §2.5 power-up.
   2. **GP MMU, PP MMU 0, 1, 2**: DTE test, request the interrupt, §3.2 init.
   3. **GP**: read VERSION, soft reset (§2.1.2), request the interrupt, store VERSION.
   4. **PP0, PP1, PP2**: read VERSION, soft reset (§2.2.2), request the interrupt, store VERSION.
   5. **L2 0** (0x10000), **L2 1** (0x01000): read SIZE, flush, ENABLE = 0x3, MAX_READS = 0x1C. L2 2 is skipped (no PP4–PP7).
   6. **DLBU**: phys | 1 and 0xFFF00000 (§2.6).
   7. **BCAST**: 0x00070000 and 0x7 (§2.7).
   8. **PP broadcast**: request the `pp` interrupt.
   9. **PP MMU broadcast**: nothing.
7. Build the pipes:
   - GP pipe = {L2 0, GP MMU, GP};
   - PP pipe = {L2 1, PP MMU 0–2, PP0–2}, plus the broadcast PP and broadcast MMU windows. A PP joins only when the PP, its MMU and its L2 all exist.
8. Enable runtime PM (200 ms autosuspend) and register the device.

### 6.2 Resume and suspend (runtime PM)

- **Resume**: enable the clocks (and regulator). Then each block's resume, in the same order as §6.1:
  - PMU power-up;
  - MMU init (not the broadcast MMU);
  - GP/PP soft reset;
  - L2 flush + enable + MAX_READS;
  - DLBU registers;
  - BCAST masks;
  - PP broadcast: forget any pending soft reset.
- **Suspend**: refused (EBUSY) while either pipe has a job. Otherwise the blocks' suspend steps run in reverse order; only the PMU's does anything (POWER_DOWN with the domains that are on, §2.5). Then the regulator goes off, the resets are asserted, and the `core` and `bus` clocks go off, in that order.

### 6.3 Minimal bring-up for a Haiku driver (derived from the above)

1. Mali clock on (§0.2); CLK81 is always on.
2. PMU: INT_MASK = 0, SW_DELAY = 0xFFFF, power up whatever STATUS reports off, wait.
3. One 4 KiB DLBU page and one empty page directory with a page table mapping it at 0xFFF00000 (PDE 0x3FF, PTE 0x300, flags 0x1DF). All of it physically contiguous per page, below 4 GiB, and uncached or write-combined for the CPU.
4. The four MMUs: DTE test, hard reset, INT_MASK = 0x3, DTE = empty directory, enable paging.
5. GP and PP0–2 soft reset.
6. Both L2s: flush, ENABLE = 0x3, MAX_READS = 0x1C.
7. DLBU: page phys | 1, VA 0xFFF00000. BCAST: 0x00070000 / 0x7.
8. Interrupts: gp (192), gpmmu (193), pp (194), ppmmu0–2 (197, 199, 201). lima also hooks pp0–2 (196, 198, 200) but expects silence from them in broadcast mode. The pmu line (195) is not needed.
9. Per job: flush the pipe's L2, switch the MMU(s) (§3.2), write the frame, start (§4.2, §4.4). On completion, start the soft reset.

---

## 7. Register reference (absolute addresses on the S905)

| Block | Phys base | Registers used by lima (offset: name) |
|---|---|---|
| GP | 0xD00C0000 | 0x00–0x14 frame, 0x20 CMD, 0x24 INT_RAWSTAT, 0x28 INT_CLEAR, 0x2C INT_MASK, 0x30 INT_STAT, 0x54 PERF_CNT_0_LIMIT, 0x68 STATUS, 0x6C VERSION |
| L2 (PP) | 0xD00C1000 | 0x04 SIZE, 0x08 STATUS, 0x10 COMMAND, 0x18 MAX_READS, 0x1C ENABLE |
| PMU | 0xD00C2000 | 0x00 POWER_UP, 0x04 POWER_DOWN, 0x08 STATUS, 0x0C INT_MASK, 0x10 INT_RAWSTAT, 0x18 INT_CLEAR, 0x1C SW_DELAY |
| GP MMU | 0xD00C3000 | 0x00 DTE_ADDR, 0x04 STATUS, 0x08 COMMAND, 0x0C PAGE_FAULT_ADDR, 0x18 INT_CLEAR, 0x1C INT_MASK, 0x20 INT_STATUS |
| PP MMU 0/1/2 | 0xD00C4000 / 0xD00C5000 / 0xD00C6000 | as GP MMU |
| PP0/1/2 | 0xD00C8000 / 0xD00CA000 / 0xD00CC000 | 0x000–0x058 frame, 0x100/0x200/0x300 WB, 0x1000 VERSION, 0x1008 STATUS, 0x100C CTRL, 0x1020 INT_RAWSTAT, 0x1024 INT_CLEAR, 0x1028 INT_MASK, 0x102C INT_STATUS, 0x1088 PERF_CNT_0_LIMIT |
| L2 (GP) | 0xD00D0000 | as L2 (PP) |
| BCAST | 0xD00D3000 | 0x00 BROADCAST_MASK, 0x04 INTERRUPT_MASK |
| DLBU | 0xD00D4000 | 0x00–0x18 (§2.6) |
| PP MMU broadcast | 0xD00D5000 | 0x00 DTE_ADDR, 0x04 STATUS, 0x08 COMMAND, 0x1C INT_MASK |
| PP broadcast | 0xD00D6000 | 0x000–0x058, 0x100–0x32C, 0x100C CTRL (START_RENDERING, SOFT_RESET); also INT_MASK / INT_RAWSTAT writes in the soft-reset start |
| HHI_MALI_CLK_CNTL | 0xC883C1B0 | §0.2 |
| HHI_GP0_PLL_CNTL | 0xC883C040 | §0.2 |
| HHI_MPLL_CNTL6 | 0xC883C294 | fclk_div3/4/5/7 gates, bits 28/29/30/31 |

---

## 8. Open questions (UNVERIFIED items, collected)

1. The GP/PP VERSION values (revision) and the two L2 SIZE values on this S905.
2. Whether a hardware register reports the number of PPs. lima takes it from the DT interrupt names.
3. What PMU STATUS reads after U-Boot, and whether U-Boot leaves the Mali clock running (HHI_MALI_CLK_CNTL source, divider, mux).
4. Whether RESET0 bit 20 and RESET2 bit 14 (the Mali reset lines) are released at boot. The GXBB DT does not list them.
5. BCAST BROADCAST_MASK: whether [23:16] really selects the MMU-broadcast targets and [7:0] the PP-broadcast targets. And INTERRUPT_MASK: whether it moves the PP interrupts from the pp0..pp2 lines to the `pp` line (§2.7, §4.6).
6. What a read through the PP MMU broadcast window returns (lima polls STATUS through it).
7. The DLBU: the meaning of bit 0 in MASTER_TLLIST_PHYS_ADDR, what the DLBU writes into its page, and the exact field layouts of FB_DIM, TLLIST_CONF and START_TILE_POS beyond Mesa's formulas.
8. Writing 0x1FFF to PP INT_RAWSTAT during a soft reset: does it clear the raw bits?
9. The meaning of PP frame word 16 (0x40): lima says ORIGIN_OFFSET_X, Mesa writes 1 and calls it "one". The meanings of words 3 and 17–22 beyond Mesa's names.
10. Cache coherence between the pipes on Mali-450. The GP writes the PLB and heap through the L2 at 0x10000, and the PPs read them through the L2 at 0x01000. lima flushes only the next job's own pipe L2 before that job runs, and its own notes leave open whether the PP should wait for a GP L2 flush. Whether PTE flag 0x1DF (no write-allocate) makes this safe is unknown.
11. Whether the Utgard MMU supports pages larger than 4 KiB (lima uses only 4 KiB).
12. Whether the MMU stall command succeeds while a page fault is active (lima does not handle that case; it hard-resets the MMU in the error path).
13. The "write" and "bus ID" fields in the MMU page-fault log: lima decodes them from INT_STATUS, though they are STATUS register bits.
14. Exact decode sizes of each block's register window.
15. Whether 744 MHz (GP0) is stable on this board, or whether a fixed fclk_div rate (500 or 666.67 MHz) is the better start.

---

## Sources

Downloaded 2026-10-31 from torvalds/linux master (cbbddb6b08cc at fetch time), raw from GitHub:

- https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/lima/ : lima_bcast.c, lima_bcast.h, lima_ctx.c, lima_ctx.h, lima_devfreq.c, lima_device.c, lima_device.h, lima_dlbu.c, lima_dlbu.h, lima_drv.c, lima_drv.h, lima_dump.h, lima_gem.c, lima_gem.h, lima_gp.c, lima_gp.h, lima_l2_cache.c, lima_l2_cache.h, lima_mmu.c, lima_mmu.h, lima_pmu.c, lima_pmu.h, lima_pp.c, lima_pp.h, lima_regs.h, lima_sched.c, lima_sched.h, lima_vm.c, lima_vm.h
- https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/drm/lima_drm.h
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gx-mali450.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxbb.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gx.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxl-mali.dtsi
- https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/gpu/arm,mali-utgard.yaml
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/meson/gxbb.c
- https://raw.githubusercontent.com/torvalds/linux/master/include/dt-bindings/clock/gxbb-clkc.h
- https://raw.githubusercontent.com/torvalds/linux/master/include/dt-bindings/reset/amlogic,meson-gxbb-reset.h
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/drm_timeout.c, drm_gem.c, drm_gem_shmem_helper.c (only for GEM_WAIT's timeout and result semantics and page rounding)
- Last change to drivers/gpu/drm/lima: ae72b9c39d28, 2026-09-30, from the git.kernel.org atom feed for that path.

Local:

- `deps/build/mesa-22.0.5/src/gallium/drivers/lima/` (MIT): lima_gpu.h (frame register names), lima_job.c (frame contents, DLBU choice), lima_context.c (heap sizes), lima_screen.c (parameters, PLB limits), lima_bo.c (ioctls used).
- `deps/build/mesa-22.0.5/include/drm-uapi/lima_drm.h`: identical to the kernel's.
- `u-boot/u-boot.dtb` (U-Boot v2026.10): the board's Mali node and OPP table, decompiled with dtc.
- `docs/m26-vdec-facts.md` §3.2: the reset controller's register mapping.
