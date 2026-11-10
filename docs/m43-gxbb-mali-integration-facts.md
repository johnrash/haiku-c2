<!--
Copyright 2026, haiku-odroid-c2 port authors.
Distributed under the terms of the MIT License.
-->

# M43: Mali-450 on the S905 (GXBB), SoC integration facts

> **Status:** Facts gathered before M43's driver. `meson_mali` runs the GPU at 666.67 MHz from fclk_div3; the firmware leaves HHI_MALI_CLK_CNTL at 0. Board values are in M43 stage 2.

> Facts read from Linux mainline (GPL; master at cbbddb6b08cc, fetched 2026-10-31/11-01 UTC), Hardkernel's vendor kernel `odroidc2-3.14.y` at c3e4c730feb1 (GPL), mainline U-Boot v2026.10 (the port's `u-boot/` tree; GPL), the S905 public datasheet V1.1.4, and the board's own device tree blob. No code, macros or comments were copied.

This is the input for a clean-room, MIT-licensed Haiku kernel driver for the ODROID-C2's Mali-450 MP3. It covers everything **outside** the GPU that the driver must do on GXBB: where the register window and the interrupts are, the clock, the resets, the power, and what Linux's lima driver does at probe. The GPU's own registers (PMU, L2 caches, GP, PP, MMUs, DLBU, broadcast unit) are in the companion document, `docs/m43-mali-facts.md`, called "the GPU document" below; this one points at them only where the two meet.

Anything not confirmed by a source, or where the sources disagree without a clear answer, is marked **UNVERIFIED**.

---

## 0. Conventions and address map

- All addresses are physical byte addresses. Registers are 32 bits wide.
- "HHI" is the HIU block at 0xC883_C000. The datasheet numbers HHI registers by word (address = 0xC883C000 + offset × 4); this document gives byte addresses, with the datasheet word offset where useful.
- CBUS is at 0xC110_0000. The reset controller sits in CBUS at 0xC110_4404.
- GIC-400: **INTID = SPI + 32**. The port's FDT bus adds the 32 itself when it reads `interrupts` (`fdt_module.cpp`, `GIC_INTERRUPT_BASE_SPI`), so the Haiku interrupt number is the INTID.
- "Set bits" and "clear bits" mean read-modify-write.
- "Linux" means mainline. "The vendor kernel" means Hardkernel's 3.14 kernel with Amlogic's Mali platform code (`drivers/gpu/arm/mali/platform/meson_bu/`).
- **The DT this board actually boots with** is U-Boot's control DTB (`u-boot/u-boot.dtb`, identical to `u-boot/dts/upstream/src/arm64/amlogic/meson-gxbb-odroidc2.dtb`, 44,144 bytes). `boot-scripts/boot-emmc.txt` passes it to Haiku with `bootefi ${kernel_addr_r} ${fdtcontroladdr}`. Its Mali node matches Linux master exactly (decompiled and compared).

| Block | Physical base | Size | Source |
|---|---|---|---|
| Mali-450 register window ("MALI APB") | 0xD00C_0000 | 0x40000 (256 KiB) | DT `gpu@c0000` in `apb@d0000000`; datasheet memory map 0xD00C0000–0xD00FFFFF |
| HHI (clocks) | 0xC883_C000 | 0x400 used | DT hiubus `sysctrl` |
| Reset controller | 0xC110_4404 | 0x9C | DT `reset-controller@4404`, "amlogic,meson-gxbb-reset" |
| AO sysctrl (power registers) | 0xC810_0000 | 0x100 | DT `sysctrl_AO` |
| DMC (DDR controller, non-secure regs) | 0xC883_8000 | 0x400 | datasheet ch. 32; DT `dmcbus` |

---

## 1. Register window and interrupts

### 1.1 Register window

- DT: `reg = <0x0 0xc0000 0x0 0x40000>` inside `apb@d0000000`, whose `ranges` map child 0 to parent 0xD000_0000 (size 0x200000). So the window is **0xD00C_0000, 0x40000 bytes**. The datasheet's memory map agrees: "MALI APB", 0xD00C0000–0xD00FFFFF.
- **Haiku trap:** the port's `fdt_device_get_reg()` returns `reg` exactly as written (0xC0000) and does **not** apply the parent bus's `ranges`. The meson_vdec driver doesn't notice because its node sits in `soc`, whose ranges are the identity. The Mali node is in `apb`, like the MMC controllers. `meson_gx_mmc.cpp` has a `translate_fdt_address()` helper that walks the ancestors' `ranges` for this reason. Use the same approach, or hard-code 0xD00C0000.
- DMA: the node has no `dma-coherent` property. lima sets a 32-bit coherent DMA mask. All of the C2's 2 GiB of RAM is below 4 GiB.

### 1.2 Interrupts

The DT lists ten lines, all `IRQ_TYPE_LEVEL_HIGH` (cell value 4), in this order:

| Index | Name | GIC SPI | INTID = Haiku irq | Trigger | Datasheet "A53 GIC bit" | lima on Mali-450 |
|---|---|---|---|---|---|---|
| 0 | gp | 160 | **192** | level, high | 192 mali_irq_gp | required |
| 1 | gpmmu | 161 | **193** | level, high | 193 mali_irq_gpmmu | required |
| 2 | pp | 162 | **194** | level, high | 194 mali_irq_pp (broadcast PP) | required |
| 3 | pmu | 163 | **195** | level, high | 195 mali_irq_pmu | optional |
| 4 | pp0 | 164 | **196** | level, high | 196 mali_irq_pp0 | required |
| 5 | ppmmu0 | 165 | **197** | level, high | 197 mali_irq_ppmmu0 | required |
| 6 | pp1 | 166 | **198** | level, high | 198 mali_irq_pp1 | optional |
| 7 | ppmmu1 | 167 | **199** | level, high | 199 mali_irq_ppmmu1 | optional |
| 8 | pp2 | 168 | **200** | level, high | 200 mali_irq_pp2 | optional |
| 9 | ppmmu2 | 169 | **201** | level, high | 201 mali_irq_ppmmu2 | optional |

- The datasheet's interrupt table agrees with DT + 32 for all ten. (The lower half of that table is off by 2 to 4 for other blocks, see `m27-irq-uart-i2c-facts.md` §1.1; this range is in the upper half, which agrees.)
- The datasheet also lists mali_irq_pp3..pp7 and ppmmu3..ppmmu7 at 202–211. The S905 has three PPs (MP3); the DT, the vendor DT (`num_of_pp = <3>`) and lima's probe use only pp0–pp2. Lines 202–211 can be left alone.
- The vendor DT uses the same ten SPIs (160–169, type 4) with names "IRQGP", "IRQGPMMU", "IRQPP", "IRQPMU", "IRQPP0", ….
- "required/optional" is lima's must-have flag for the IP behind the line (`lima_device.c` IP table, Mali-450 column). lima looks the lines up **by name**, never by position.
- **Haiku notes:**
  - The FDT module has no lookup by name. Read `interrupt-names` with `get_prop()`, find the index of each name, and call `get_interrupt(dev, index, NULL, &irq)`.
  - Haiku's GICv2 code makes every SPI level-sensitive at boot (`arch_int_gicv2.cpp`, ICFGR = 0). That is already right for these lines, so don't call `arch_int_configure_io_interrupt(…, B_EDGE_TRIGGERED, …)` as meson_vdec does for its pulse interrupts.
  - Level-triggered means each handler must clear the cause in the GPU (the unit's interrupt-clear register, see the GPU document) before it returns, or the line fires again at once.

---

## 2. Clocks

### 2.1 What the DT names

| Property | Value | Meaning |
|---|---|---|
| `clocks` / `clock-names` | `<&clkc CLKID_CLK81>` "bus", `<&clkc CLKID_MALI>` "core" | IDs 12 and 106 in `gxbb-clkc.h` |
| `assigned-clocks` / `assigned-clock-rates` | `<&clkc CLKID_GP0_PLL>` (ID 9) = **744,000,000** | GP0 PLL set to 744 MHz before lima's probe |
| `operating-points-v2` | the GPU OPP table (§2.5) | used by lima's devfreq |
| `resets`, `power-domains`, `mali-supply` | **absent** on GXBB and on the C2 board file | see §3, §4 |

- **"bus" is clk81 itself, not a gate.** clk81 is the gate at HHI_MPEG_CLK_CNTL (0xC883C174) bit 7, marked `CLK_IS_CRITICAL` in Linux, so it is always on. This board runs it at 166.67 MHz: the port's meson_i2c reports 166,666,666 Hz, a value it computes assuming a 2 GHz fixed PLL. **There is no Mali bit in HHI_GCLK_MPEG0/1/2 or HHI_GCLK_OTHER**: the datasheet's EE clock-gating table has no Mali entry, and Linux defines no Mali gate. Nothing to do for the bus clock.
- **"core" is the final Mali mux** ("mali", CLKID_MALI), described next.
- When lima probes, the driver core applies `assigned-clock-rates` (`of_clk_set_defaults()` in `drivers/base/platform.c`, before the driver's own probe). That sets GP0 to 744 MHz but does **not** select GP0 for the Mali.

### 2.2 HHI_MALI_CLK_CNTL: 0xC883_C1B0 (datasheet word 0x6C)

The register holds two identical clock branches and a select bit.

| Bits | Field | Linux clock | Meaning |
|---|---|---|---|
| 31 | final select | `mali` (mux) | **0 = mali_0 branch, 1 = mali_1 branch** |
| 30:28 | — | — | unused |
| 27:25 | mali_1 source | `mali_1_sel` | parent index 0–7 (§2.3) |
| 24 | mali_1 gate | `mali_1` | 1 = branch running |
| 23 | — | — | reserved |
| 22:16 | mali_1 divider | `mali_1_div` | divisor − 1 (0 = ÷1, 127 = ÷128) |
| 15:12 | — | — | reserved |
| 11:9 | mali_0 source | `mali_0_sel` | parent index 0–7 (§2.3) |
| 8 | mali_0 gate | `mali_0` | 1 = branch running |
| 7 | — | — | reserved |
| 6:0 | mali_0 divider | `mali_0_div` | divisor − 1 |

- Each half's fields are the other half's moved by 16 bits. Bit 15, the low-half twin of bit 31, is reserved.
- **Reset value: 0** for every field (datasheet). That means mali_0 selected, source xtal, ÷1, both gates off.
- **Bit 31:** the datasheet calls bits 31:28 "Reserved". Linux (`gxbb_mali`: mask 1, shift 31) and the vendor clock driver (`MUX(CLK_MALI, …, HHI_MALI_CLK_CNTL, 31, 1, …)`) both use bit 31 as the branch select. Two independent drivers agree, so the datasheet entry is incomplete.
- The datasheet's own field names (CLK_SEL 11:9 / 27:25, CLK_EN 8 / 24, CLK_DIV 6:0 / 22:16) match the table.
- The divider is a plain divider (no flags in `clk_regmap_div_data`), so the divisor is the field plus 1.

### 2.3 Parent select values (both halves) and their rates on GXBB

| Index | Linux parent | Vendor-kernel parent | Nominal rate | Gate that must be on |
|---|---|---|---|---|
| 0 | xtal | xtal | 24 MHz | — |
| 1 | gp0_pll | gp0_pll | programmable (§2.4); DT assigns 744 MHz | GP0 enable + lock (§2.4) |
| 2 | mpll2 | mpll_clk_out1 | set by audio | — |
| 3 | mpll1 | mpll_clk_out2 | set by audio | — |
| 4 | fclk_div7 | fclk_div7 | 285.714 MHz | HHI_MPLL_CNTL6 bit 31 |
| 5 | fclk_div4 | fclk_div4 | 500 MHz | HHI_MPLL_CNTL6 bit 29 |
| 6 | fclk_div3 | fclk_div3 | 666.667 MHz | HHI_MPLL_CNTL6 bit 28 (critical, always on) |
| 7 | fclk_div5 | fclk_div5 | 400 MHz | HHI_MPLL_CNTL6 bit 30 |

- The fclk rates assume the fixed PLL at its nominal 2 GHz. The boot firmware sets it and Linux only reads it (`meson_clk_pll_ro_ops`). On this board that is **UNVERIFIED**, as in `m26-vdec-facts.md` §1.2; HHI_MPLL_CNTL (0xC883C280) would confirm it.
- **Indices 2 and 3 disagree:** Linux lists mpll2 then mpll1, the vendor kernel mpll1 then mpll2 (**UNVERIFIED** which is right). Neither driver uses the MPLLs for the GPU: Linux's comment says every GPU rate comes from the fclk dividers plus one GP0 setting, and the MPLLs belong to audio. Don't use 2 or 3.
- **fclk gates.** All five gates are in HHI_MPLL_CNTL6 at 0xC883C294: bit 27 fclk_div2, 28 fclk_div3, 29 fclk_div4, 30 fclk_div5, 31 fclk_div7.
  - Linux marks only div2 and div3 critical. div4, div5 and div7 are enabled on demand when a consumer such as the Mali selects them, and Linux switches off unused gates late in boot.
  - Haiku never turns them off, but who turned them on at boot is unknown. **Set the bit for the chosen source before using it** (read-modify-write; no other Haiku driver writes this register today).
- What the boot firmware leaves in HHI_MALI_CLK_CNTL, GP0 and HHI_MPLL_CNTL6 is **UNVERIFIED**. Mainline U-Boot defines HHI_MALI_CLK_CNTL (`arch-meson/clock-gx.h`) but never writes it, and never touches GP0.

### 2.4 GP0 PLL (needed only for 744 MHz)

| Register | Address | Fields (GXBB) |
|---|---|---|
| HHI_GP0_PLL_CNTL | 0xC883C040 (word 0x10) | bit 31 LOCK (read-only), bit 30 ENABLE, bit 29 RESET, [17:16] OD, [13:9] N, [8:0] M |
| HHI_GP0_PLL_CNTL2 | 0xC883C044 | analog setup; Linux init value **0x69C80000** |
| HHI_GP0_PLL_CNTL3 | 0xC883C048 | analog setup; Linux init value **0x0A5590C4** |
| HHI_GP0_PLL_CNTL4 | 0xC883C04C | analog setup; Linux init value **0x0000500D** |

- **Output:** DCO = 24 MHz × M / N, and out = DCO >> OD (OD 0–3 gives ÷1, ÷2, ÷4, ÷8).
- **Linux's GXBB table:** N = 1 and M = 32…62, so the DCO runs from 768 to 1488 MHz. Linux rounds down.
- **744 MHz:** at ÷1 nothing fits (the lowest DCO is 768), so Linux picks ÷2 with M = 62: **M = 62, N = 1, OD = 1**. The vendor kernel's GP0 table has the same entry (744 MHz = 62, 1, 1).
- **CNTL5 and CNTL1** (0xC883C050, 0xC883C058) are GXL-only in Linux; GXBB does not write them.

**Linux's GP0 sequence on GXBB** (`clk-pll.c`, `gxbb_gp0_pll_dco`):

1. **Once, when the clock is registered** (unconditional; GXBB's GP0 has no "keep if already running" flag): RESET (bit 29) = 1; write CNTL2, CNTL3 and CNTL4 with the values above; RESET = 0. ENABLE is not touched.
2. **Set rate** (assigned-clock-rates, before lima's probe):
   - if ENABLE is set, first disable (RESET = 1, then ENABLE = 0);
   - write N, then M;
   - the separate `gp0_pll` divider clock writes OD;
   - if it was enabled, enable again (step 3).
3. **Enable** (only when a Mali branch that selects GP0 is switched on):
   - if already enabled, nothing;
   - otherwise RESET = 1, ENABLE = 1, RESET = 0, then poll LOCK (bit 31) every 20 µs for up to 100 ms.
4. **Disable:** RESET = 1, ENABLE = 0.

With the reserved bits at 0, the running 744 MHz value of HHI_GP0_PLL_CNTL is **0xC001023E** (lock bit included) and the written value is 0x4001023E.

- **Vendor kernel, for comparison:**
  - it writes CNTL = 0x6A000228 (ENABLE, RESET, M = 40, N = 1, plus bits 27 and 25), then the same three CNTL2–4 values;
  - it sets M, N and OD and clears RESET;
  - it waits 1 ms and checks LOCK.
  - Bits 27 and 25 are "Reserved" in the datasheet. What they do is **UNVERIFIED**; Linux never sets them.
- **Interlock:** Linux never changes GP0 while the GPU uses it. The branch muxes don't pass rate requests up to their parents, and the gate of a running branch blocks rate changes beneath it (§2.6). Do the same: program and lock GP0 before a branch selects it, and never touch GP0 while the active branch runs from it.
- The vendor kernel's fastest GPU rate on the C2 is 792 MHz from GP0 (M = 33, N = 1, OD = 0). Mainline stops at 744 MHz.

### 2.5 How Linux picks a rate

**OPP table** (`meson-gx-mali450.dtsi`, used by GXBB and GXL):

| OPP (Hz) | opp-microvolt | Source Linux ends up with (fixed PLL 2 GHz, MPLLs unused) |
|---|---|---|
| 125,000,000 | 950,000 | fclk_div4 ÷4 |
| 250,000,000 | 950,000 | fclk_div4 ÷2 |
| 285,714,285 | 950,000 | fclk_div7 ÷1 |
| 400,000,000 | 950,000 | fclk_div5 ÷1 |
| 500,000,000 | 950,000 | fclk_div4 ÷1 |
| 666,666,666 | 950,000 | fclk_div3 ÷1 |
| 744,000,000 | 950,000 | gp0_pll (744 MHz) ÷1 |

**How the clock framework gets there:**
- Each branch's divider asks its source mux for rate × divisor, trying divisors in turn.
- The mux picks the parent with the **fastest current rate not above** the request. The lowest index wins a tie. The mux never asks a parent to change its rate.
- The source column follows from that and the nominal rates. It would change only if an MPLL happened to give a closer rate (**UNVERIFIED** in general; the MPLLs are audio's and usually far off).

**Voltage:** the C2 DT has no `mali-supply`, so lima's OPP code runs without a regulator and ignores the 0.95 V. VDD_EE on the C2 has a PWM control net (`VDDEE_PWM`, schematic rev 0.2), but no Linux DT models it. Every OPP, 744 MHz included, runs at whatever VDD_EE the firmware left (**UNVERIFIED** value). The vendor DT listed 1150 (mV) for every GPU rate.

**lima's sequence** (`lima_drv.c`, `lima_device.c`, `lima_devfreq.c`):
1. Probe enables "bus", then "core" **as found**. It sets no rate, so the GPU's first register accesses run at whatever HHI_MALI_CLK_CNTL holds; from a reset value of 0 that is xtal, 24 MHz.
2. devfreq is set up from the OPP table with the `simple_ondemand` governor: 50 ms polling, up-threshold 30, down-differential 5. Its starting point is the current rate rounded to an OPP. The rounding direction is in the devfreq core, which was not read (**UNVERIFIED**).
3. From the first poll on, each change is a `clk_set_rate()` on "core", done glitch-free (§2.6).
4. Runtime PM autosuspends after 200 ms idle. Suspend switches "core" off (the active branch's gate) and "bus" off (a no-op, since clk81 is critical). Resume switches them back on.

**Vendor kernel:**
- It boots the GPU at 666 MHz (`def_clk = <4>` points at its 666 MHz fclk_div3 entry).
- Its DVFS table is 125/250 (fclk_div4), 285 (fclk_div7), 400 (fclk_div5), 500 (fclk_div4), 666 (fclk_div3) and 792 (gp0_pll).
- Its clock setup also forced the Mali to fclk_div5 (400 MHz) early in boot, before the GPU driver loaded.

### 2.6 Glitch-free switching between the two branches

Both kernels change the GPU clock the same way: **never reprogram the running branch; set up the idle one and flip bit 31.**

- **Linux** has no special code for this. It follows from the flags in `gxbb.c` plus the clock framework's ordering (`drivers/clk/clk.c`). The `gxbb.c` comment describes it as a glitch-free mux that the framework drives by switching to the inactive branch.
  - The `mali_0` and `mali_1` gates carry `CLK_SET_RATE_GATE`. While a gate is prepared, it and everything above it (its divider and source mux) are rate-protected.
  - A rate request on `mali` (the final mux, which passes requests to its parents) therefore can't be met by the running branch unless it already has the rate. The mux picks the idle branch.
  - Rate changes are applied parents first, so the idle branch's source and divider are written while its gate is still off.
  - Then the final mux changes parent. Because `mali` is on, the framework first switches on the new parent: the idle branch's gate, and below it GP0 if that is the source. Then it writes bit 31, then switches off the old branch's gate.
- **Vendor kernel** (`critical_clock_set`, the same steps written out):
  1. Pick the branch that bit 31 does not select.
  2. Set its source's rate, select that source and set its divider.
  3. Switch the branch on.
  4. `udelay(1)`.
  5. Flip bit 31.
  6. Switch the old branch off.
  - The build has `AML_CLK_LOCK_ERROR` defined, which keeps **both** gates on all the time and skips steps 3 and 6. So in that build the idle branch was reprogrammed while running, which Linux never does.
- **Both branches running at the flip:** both drivers have the new branch running before bit 31 changes and stop the old one only after. That a glitch-free mux needs both inputs clocking during the switch is the usual design, but it is **UNVERIFIED** for this one. Don't flip to a stopped branch, and don't stop the active branch before the flip.
- **Delay:** Linux has none between "gate on" and "flip"; the vendor has 1 µs. A 1 µs delay costs nothing.

### 2.7 Programming sequences for a Haiku driver

All writes are read-modify-writes of HHI_MALI_CLK_CNTL (0xC883C1B0) unless stated. Never change the active branch's fields.

**(a) Cold start at 500 MHz** (bring-up default)
1. HHI_MPLL_CNTL6 (0xC883C294): set bit 29 (fclk_div4 on).
2. If bit 31 = 0 and bit 8 = 0, mali_0 is selected and stopped, so there's nothing to glitch:
   - (i) write mali_0 source = 5, divider = 0, gate still 0;
   - (ii) set bit 8.
   - From the reset value the register then reads **0x00000B00**.
3. If the active branch is already running, use (b).
4. Only now touch GPU registers (the GPU document's power-up and reset).

At 666.67 MHz, use source 6 (fclk_div3, always on, nothing to enable): 0x00000D00. That rate needs no gate at all outside HHI_MALI_CLK_CNTL.

**(b) Change rate while the active branch runs** (the glitch-free path, §2.6). Let A be the active branch (bit 31) and I the other.
1. Make I's source available: set the fclk gate bit (§2.3), or have GP0 programmed and locked (§2.4).
2. Clear I's gate (bit 8 for mali_0, bit 24 for mali_1) if it is set.
3. Write I's source and divider.
4. Set I's gate.
5. Wait 1 µs (vendor; optional for Linux).
6. Write bit 31 to select I.
7. Clear A's gate.

- Example, from 500 MHz on mali_0 (0x00000B00) to 744 MHz on mali_1: 0x02000B00 → 0x03000B00 → (1 µs) → 0x83000B00 → **0x83000A00**.
- The GPU may be running jobs during this; Linux changes rates under load.

**(c) 744 MHz.** Program GP0 as in §2.4 (registration step 1, then M = 62, N = 1, OD = 1, then enable and wait for lock). Then select index 1 with divider 0 on the idle branch, using (b) or (a).

**(d) Stop.**
1. With the GPU idle and its internal domains powered down (GPU document), clear the active branch's gate. That is what lima's suspend does; it leaves bit 31 and the fields alone.
2. Optionally disable GP0 (RESET = 1, ENABLE = 0) if it was the source. Leave the fclk gates alone.

### 2.8 Per-OPP field values (one branch)

The low half is written into bits 15:0 for mali_0. Shift it left by 16 for mali_1, then set or clear bit 31 to choose the branch.

| OPP | Source index | Divider field | mali_0 bits [11:0] with gate | Same for mali_1, bits [27:16] |
|---|---|---|---|---|
| 125 MHz | 5 (fclk_div4) | 3 | 0x0B03 | 0x0B030000 |
| 250 MHz | 5 (fclk_div4) | 1 | 0x0B01 | 0x0B010000 |
| 285.7 MHz | 4 (fclk_div7) | 0 | 0x0900 | 0x09000000 |
| 400 MHz | 7 (fclk_div5) | 0 | 0x0F00 | 0x0F000000 |
| 500 MHz | 5 (fclk_div4) | 0 | 0x0B00 | 0x0B000000 |
| 666.7 MHz | 6 (fclk_div3) | 0 | 0x0D00 | 0x0D000000 |
| 744 MHz | 1 (gp0_pll at 744) | 0 | 0x0300 | 0x03000000 |

---

## 3. Resets

### 3.1 The controller

- `reset-meson.c` with the "amlogic,meson-gxbb-reset" parameters (shared with Meson8b and AXG): 256 IDs, **pulse registers at base + 0x00**, **level registers at base + 0x7C**, levels **active-low** (`level_low_reset = true`). Register stride 4, 32 IDs per register.
- ID n uses register n / 32, bit n % 32.
- **Pulse** (`reset`): write the single bit, with no read-modify-write and no release write. It self-clears, as the port already relies on for RESET1 (meson_vdec's parser reset).
- **Assert:** clear the bit in the level register (read-modify-write). **Deassert:** set it.
- Mainline U-Boot's reset driver uses the same 0x7C offset and the same polarity (assert clears).

### 3.2 The Mali bits

| ID (binding) | Name | Pulse register | Level register | Bit | Datasheet name |
|---|---|---|---|---|---|
| 20 | RESET_MALI_CAPB3 | RESET0, **0xC1104404** | **0xC1104480** | 20 | RESET_REGISTER bit 20 "Mali_capb3" |
| 78 | RESET_MALI | RESET2, **0xC110440C** | **0xC1104488** | 14 | RESET2_REGISTER bit 14 "MALI" |

- The datasheet documents the pulse registers and the watchdog masks (RESET0_MASK … at 0xC1104440 + 4n; a set bit there exempts a block from watchdog reset). It does not document the level registers. Those come from Linux and U-Boot.

### 3.3 What Linux does with them on GXBB: nothing

- The GXBB (and GXL) Mali node has **no `resets` property**. lima asks for an optional, shared reset array, gets none, and makes no reset call. **Linux never asserts, deasserts or pulses RESET_MALI or RESET_MALI_CAPB3 on GXBB.**
- The binding agrees: `arm,mali-utgard.yaml` requires `resets` only for some SoCs, Meson8 and Meson8b among them but not "amlogic,meson-gxbb-mali". For all compatibles other than hisilicon,hi6220-mali it allows **at most one** reset entry, so a GXBB node with both Mali resets would not even validate.
- For comparison:
  - Meson8 and Meson8b (also Mali-450) list `resets = <&reset RESET_MALI>`;
  - GXM (Mali-T820, Panfrost) lists both, RESET_MALI_CAPB3 then RESET_MALI;
  - the vendor GXBB DT has no reset either, and the vendor Mali platform files read here don't touch the reset controller.
- **lima's order when a reset is present** (Meson8, not GXBB): enable "bus", enable "core", then **deassert**. On suspend: **assert**, then "core" off, then "bus" off.
- **For Haiku on GXBB:** follow Linux and leave both bits alone. Reset the GPU through its own soft-reset registers (GPU document).
- **Inference, UNVERIFIED:** since Linux never deasserts them and lima works on GXBB, both level bits must already read 1 (out of reset) after boot. Reading 0xC1104480 bit 20 and 0xC1104488 bit 14 confirms it.
- **If a hard reset turns out to be needed** (for example to recover a wedged GPU), the Meson8 pattern is the documented one: clocks on, then a RESET2 bit 14 pulse, or assert and deassert through 0xC1104488. That this works and is safe on GXBB is **UNVERIFIED**, as is whether the CAPB3 reset (apparently the GPU's APB bus interface, judging by the name) is ever needed.

---

## 4. Power

### 4.1 What Linux does on GXBB: nothing outside the GPU

- **No power domain.** The GXBB power controller ("amlogic,meson-gxbb-pwrc", `meson-ee-pwrc.c`) has exactly two domains: VPU (AO SLEEP0 bits 8/9 plus VPU and HDMI memories) and Ethernet memory (HHI_MEM_PD_REG0 bits 3:2). The Mali node has no `power-domains`. The old `meson-gx-pwrc-vpu.c` no longer exists in master.
- **No memory power-down bits.** The Mali has none in HHI_MEM_PD_REG0 (0xC883C100). The datasheet lists bits 15:8 HDMI and 3:2 Ethernet; the rest are "Reserved" (defaults 0xFFFF, 0xF and 0x3). The pwrc gives no Mali bits there on GXBB either. AXG and G12A use other bits of that register for other blocks.
- **No AO sleep or isolation bits.** No GXBB code sets AO_RTI_GEN_PWR_SLEEP0/ISO0 for the GPU.
- **No regulator** on the C2 (§2.5).
- **Power inside the GPU** is the Mali's own PMU at window offset 0x2000 (**0xD00C2000**; the datasheet's "Mali Power UP/Down" registers at 0xD00C2000/0xD00C2004, and lima's PMU IP offset 0x2000). lima brings it up as part of its IP init. See the GPU document.
  - The datasheet (§18.4) gives three GPU-internal domains, GP + L2C_0, PP0 + L2C_1 and PP12, and a power-up order: GP first, then PP0, then PP12.
  - The vendor DT encodes the same in `pmu_domain_config = <0x1 0x2 0x4 0x4 0x0 0x0 0x0 0x0 0x0 0x1 0x2 0x0>` with `pmu_switch_delay = <0xffff>`.

### 4.2 Datasheet-only items (Linux ignores them)

| Register | Address | Datasheet says | Status |
|---|---|---|---|
| AO_RTI_GEN_PWR_ISO0 ("gen_pwr_iso_cntl0") | 0xC81000EC | bit 3 "GPU ISO OUT", bit 2 "GPU ISO IN" | **UNVERIFIED**. The same table marks bits 7:6 and 11:10 reserved, yet those are VDEC_1's and HEVC's isolation bits in Linux and the vendor kernel, so the table is unreliable. Linux's vdec_1 writes the whole register (0 at power-up, 0xC0 at power-down), which clears bits 3:2 as a side effect. The port's meson_vdec does read-modify-write of its own bits only and leaves 3:2 as the firmware set them. |
| AO_RTI_GEN_PWR_SLEEP_ACK0 ("gen_pwr_sleep_ack0") | 0xC81000F0 | bits 7:4 "MALI_PWRUP_ACK" for PP456, PP12, PP0 and GP; 1 = powered off | **UNVERIFIED**. Probably mirrors the GPU PMU's domain state; read-only, useful as a diagnostic. |
| AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 | no GPU bit; bits 5:0 "Reserved" with defaults 1 | Linux uses bits 3:2 (VDEC_1) and 7:6 (HEVC) here, so "reserved" again means "undocumented". |

**Recommendation:** read ISO0 once at driver init and log it. If bits 3:2 read non-zero and the GPU's registers don't respond, clearing them is the datasheet's suggestion (**UNVERIFIED**). Otherwise do nothing, as Linux does.

### 4.3 The DDR side

The GPU's two AXI masters reach DDR through DMC ports 1 ("Mali 0") and 2 ("Mali 1").

- **DMC_REQ_CTRL** at **0xC8838000**: bits 1 and 2 enable requests from those ports.
- **DMC_SOFT_RST** at 0xC8838004: bits 1 and 2 are the ports' n_clk-domain reset_n.
- **DMC_N_CLK_CTRL** at 0xC88380CC: bit 3 is "manual control for Mali n_clk".
- **DMC_SEC_AXI_PORT_CTRL**: per-port secure and non-secure read/write enables, bits 2–5 and 18–21 for the Mali ports. It is in the **secure** DMC block at 0xDA838400 + 0xE × 4.

The DDR set-up firmware owns all of these, and neither Linux nor the vendor GPU code touches them. That they are already enabled on this board is inferred from lima working with nothing more (**UNVERIFIED**). DMC_REQ_CTRL is in the non-secure block (the same 4 KiB page as the canvas LUT the port already maps), so it can be read as a diagnostic if the GPU's MMU or jobs never reach memory.

---

## 5. What lima's probe does, and what is Meson-specific

**Nothing in lima is Amlogic-specific.** It matches only "arm,mali-400" and "arm,mali-450". The "amlogic,meson-gxbb-mali" string only selects the DT binding rules (§3.3). Probe order (`lima_drv.c`, `lima_device.c`):

1. Allocate the DRM device.
2. DMA: 32-bit coherent mask; unlimited segment size.
3. **Clocks:** get "bus" and "core", enable "bus", then "core" (no rate set), then deassert the optional reset (none on GXBB).
4. **Regulator:** optional "mali" (none on the C2).
5. Mali-450 only: allocate one 4 KiB write-combined page for the DLBU and reserve the top of the GPU address space (details in the GPU document).
6. Map the whole register window (0x40000).
7. Initialise each IP in a fixed order: pmu, l2_cache0–2, gp, pp0–pp7, gpmmu, ppmmu0–7, dlbu, bcast, pp_bcast, ppmmu_bcast. Each IP has its window offset and its interrupt by name, and an IP that may be absent is skipped quietly (the Mali-450 offsets are in `lima_device.c`'s IP table; the PMU is at 0x2000).
8. Set up the GP and PP job pipes.
9. devfreq (§2.5), then runtime PM with a 200 ms autosuspend, then register the DRM device.

**Resume** = "bus" on, "core" on, (reset deassert), regulator on, resume each IP, resume devfreq. **Suspend** is the reverse, and refuses while jobs are queued.

**Vendor kernel, for comparison:**
- The DT gives the window plus two 4-byte "regions": 0xC883C000 (labelled for the GPU clock control) and 0xC8100000 (labelled "aobus … for gpu pmu domain").
- The platform code maps both, but in the files read (`mali_clock.c`, `platform_gx.c`, `meson_main2.c`, `scaling.c`) only the HIU mapping is used, to print HHI_MALI_CLK_CNTL in debug output. The AO mapping is unused, and none of these files touches an AO power bit or a SoC reset.
- Whether another vendor file uses the AO mapping is **UNVERIFIED**.
- The vendor's clocks list fclk_div3/4/5/7, gp0_pll and the three Mali clocks; power inside the GPU goes through the Mali's own PMU code.

**Haiku driver attachment:**
- `supports_device()` can compare the first "fdt/compatible" string with "amlogic,meson-gxbb-mali". The FDT bus adds every string as its own attribute, and `get_attr_string()` returns the first.
- The node has no `status` property, so it counts as enabled.

---

## 6. Minimal bring-up checklist

| # | Step | Registers / values | Source |
|---|---|---|---|
| 1 | Attach to the FDT node `/soc/apb@d0000000/gpu@c0000` | compatible "amlogic,meson-gxbb-mali", "arm,mali-450"; no `status` (enabled) | board DTB (U-Boot control DTB), Linux `meson-gxbb.dtsi` |
| 2 | Translate and map the window | `get_reg(0)` = 0xC0000, plus the apb range → **0xD00C0000**, size 0x40000 | DT `ranges`; datasheet memory map; Haiku `fdt_device_get_reg()` doesn't translate (`meson_gx_mmc.cpp` does) |
| 3 | Map HHI | 0xC883C000, one page (as meson_vdec does) | Linux `gxbb.c` |
| 4 | Find the interrupts by name | `interrupt-names` → index → `get_interrupt()`: gp 192, gpmmu 193, pp 194, pmu 195, pp0 196, ppmmu0 197, pp1 198, ppmmu1 199, pp2 200, ppmmu2 201; level-high (the GIC's default in Haiku) | DT; datasheet IRQ table; Haiku `arch_int_gicv2.cpp` |
| 5 | Bus clock | nothing: clk81 (HHI_MPEG_CLK_CNTL 0xC883C174 bit 7) is always on; no Mali GCLK bit exists | Linux `gxbb.c` (`CLK_IS_CRITICAL`); datasheet clock-gating table |
| 6 | Source available | 500 MHz: set HHI_MPLL_CNTL6 (0xC883C294) bit 29. 666 MHz: nothing (bit 28 always on). 744 MHz: GP0 per §2.4 (CNTL2–4 under RESET, M = 62, N = 1, OD = 1, ENABLE, wait for LOCK) | Linux `gxbb.c`, `clk-pll.c`; vendor `gp0_pll.c` |
| 7 | Core clock on | HHI_MALI_CLK_CNTL (0xC883C1B0): cold, from 0 → 0x00000B00 (500 MHz on mali_0). If a branch is already running, use the glitch-free path §2.7(b) | Linux `gxbb.c` + clock-framework order; vendor `mali_clock.c` |
| 8 | SoC reset | none (Linux on GXBB never uses RESET_MALI / RESET_MALI_CAPB3) | Linux DT + binding + `lima_device.c` |
| 9 | Power outside the GPU | none (no domain, no memory power-down, no AO bits in Linux). Optionally log AO 0xC81000EC (bits 3:2), 0xC81000F0 (bits 7:4) and DMC 0xC8838000 (bits 2:1) | Linux `meson-ee-pwrc.c`; datasheet §18 and §32 (diagnostics only) |
| 10 | GPU-internal bring-up | PMU power-up (GP, then PP0, then PP1/2), L2, MMUs, soft resets, interrupt masks: see the GPU document | lima; datasheet §18.4 |
| 11 | Install the interrupt handlers | after the GPU-side masks are in a known state; each handler clears its cause | lima |
| 12 | Rate changes later | §2.7(b) only; never rewrite the active branch | §2.6 |
| 13 | Shutdown | GPU idle and powered down (GPU document) → clear the active branch's gate → optionally GP0 RESET = 1, ENABLE = 0 | `lima_device.c` suspend; `clk-pll.c` |

---

## 7. Register reference (everything outside the GPU)

| Register | Address | Bits used | Who uses it |
|---|---|---|---|
| HHI_GP0_PLL_CNTL | 0xC883C040 | 31 LOCK, 30 ENABLE, 29 RESET, 17:16 OD, 13:9 N, 8:0 M | §2.4 |
| HHI_GP0_PLL_CNTL2 / 3 / 4 | 0xC883C044 / 048 / 04C | init 0x69C80000 / 0x0A5590C4 / 0x0000500D | §2.4 |
| HHI_MEM_PD_REG0 | 0xC883C100 | no Mali bits | §4.1 |
| HHI_GCLK_MPEG0/1/2, HHI_GCLK_OTHER | 0xC883C140 / 144 / 148 / 150 | no Mali bits | §2.1 |
| HHI_MPEG_CLK_CNTL | 0xC883C174 | bit 7 = clk81 gate (critical) | §2.1 |
| HHI_MALI_CLK_CNTL | 0xC883C1B0 | 31 select; 27:25 / 24 / 22:16 mali_1; 11:9 / 8 / 6:0 mali_0 | §2.2 |
| HHI_MPLL_CNTL | 0xC883C280 | fixed PLL, read-only (confirms 2 GHz) | §2.3 |
| HHI_MPLL_CNTL6 | 0xC883C294 | bits 27–31 = fclk_div2/3/4/5/7 gates | §2.3 |
| RESET0 (pulse) | 0xC1104404 | bit 20 Mali_capb3 (unused by Linux on GXBB) | §3 |
| RESET2 (pulse) | 0xC110440C | bit 14 MALI (unused by Linux on GXBB) | §3 |
| RESET0_LEVEL / RESET2_LEVEL | 0xC1104480 / 0xC1104488 | bit 20 / bit 14, active-low (0 = held in reset) | §3 |
| RESETn_MASK (watchdog) | 0xC1104440 + 4n | 1 = not reset by the watchdog | §3.2 |
| AO_RTI_GEN_PWR_SLEEP0 | 0xC81000E8 | no GPU bit known | §4.2 |
| AO_RTI_GEN_PWR_ISO0 | 0xC81000EC | bits 3:2 "GPU ISO" per the datasheet (**UNVERIFIED**) | §4.2 |
| AO_RTI_GEN_PWR_SLEEP_ACK0 | 0xC81000F0 | bits 7:4 Mali domain acks (**UNVERIFIED**) | §4.2 |
| DMC_REQ_CTRL | 0xC8838000 | bits 1, 2 = Mali ports enabled (firmware) | §4.3 |
| DMC_SOFT_RST | 0xC8838004 | bits 1, 2 = Mali ports out of reset (firmware) | §4.3 |
| DMC_N_CLK_CTRL | 0xC88380CC | bit 3 Mali n_clk manual control (firmware) | §4.3 |
| Mali PMU (inside the window) | 0xD00C2000 | GPU document | §4.1 |

---

## 8. Open questions (UNVERIFIED items, collected)

Most of these can be settled with read-only KDL commands on the board (`dw -p <address> 1`), once someone chooses to run them. M43 stage 2 did; its results are summarized at the top.

1. **Boot state:** what the firmware leaves in HHI_MALI_CLK_CNTL (0xC883C1B0), HHI_GP0_PLL_CNTL (0xC883C040) and HHI_MPLL_CNTL6 (0xC883C294). Mainline U-Boot writes none of them.
2. **Fixed PLL rate** on this board (HHI_MPLL_CNTL, 0xC883C280). Nominal 2 GHz gives the fclk rates in §2.3.
3. **Reset level bits:** whether 0xC1104480 bit 20 and 0xC1104488 bit 14 read 1 (out of reset) after boot, as the fact that Linux needs no deassert implies.
4. **AO ISO0 bits 3:2** (0xC81000EC): whether they exist as GPU isolation, their boot value, and whether they ever need clearing. **SLEEP_ACK0 bits 7:4** (0xC81000F0): whether they track the GPU PMU.
5. Whether the glitch-free mux needs **both branches running** at the moment bit 31 flips (both drivers make sure they are), and whether any delay is needed (Linux none, vendor 1 µs).
6. **MPLL indices 2 and 3:** Linux says mpll2 then mpll1, the vendor mpll1 then mpll2. Irrelevant if they're never selected.
7. **GP0 reserved bits 27 and 25**, which the vendor sets and Linux doesn't.
8. **VDD_EE voltage** on the C2, and whether 744 MHz is stable at it. No Linux DT scales it; the vendor listed 1.15 V.
9. Whether a SoC-level **RESET_MALI pulse** is safe or useful on GXBB for recovery. Linux never uses it on GXBB.
10. Whether **DMC ports 1 and 2** are enabled by the firmware (DMC_REQ_CTRL bits 2:1, DMC_SOFT_RST bits 2:1).
11. Which way **devfreq's starting-point rounding** goes (the devfreq core was not read). It only matters for copying Linux's first-poll behaviour.

---

## Sources

**Linux mainline**, master at cbbddb6b08cc (HEAD when fetched, 2026-10-31 22:05 UTC), from https://raw.githubusercontent.com/torvalds/linux/master/ + path:
- arch/arm64/boot/dts/amlogic/meson-gxbb.dtsi, meson-gx.dtsi, meson-gx-mali450.dtsi, meson-gxbb-odroidc2.dts
- drivers/clk/meson/gxbb.c (the old gxbb.h no longer exists; its offsets are now at the top of gxbb.c), drivers/clk/meson/clk-pll.c, clk-regmap.c, clk-regmap.h
- drivers/clk/clk.c (rate protection for CLK_SET_RATE_GATE, mux parent choice, reparent ordering), drivers/clk/clk-mux.c
- include/dt-bindings/clock/gxbb-clkc.h, include/dt-bindings/reset/amlogic,meson-gxbb-reset.h
- drivers/reset/amlogic/reset-meson.c, reset-meson-common.c (the driver moved here from drivers/reset/reset-meson.c)
- drivers/pmdomain/amlogic/meson-ee-pwrc.c (drivers/soc/amlogic/meson-ee-pwrc.c and meson-gx-pwrc-vpu.c no longer exist)
- drivers/gpu/drm/lima/lima_device.c, lima_drv.c, lima_devfreq.c
- Documentation/devicetree/bindings/gpu/arm,mali-utgard.yaml
- drivers/base/platform.c (assigned-clocks applied before probe)

**Hardkernel vendor kernel** `odroidc2-3.14.y` at c3e4c730feb1750940971cae9ca1da3ca50f1d56, from https://raw.githubusercontent.com/hardkernel/linux/c3e4c730feb1750940971cae9ca1da3ca50f1d56/ + path:
- arch/arm64/boot/dts/meson64_odroidc2.dts, arch/arm64/boot/dts/amlogic/mesongxbb.dtsi
- drivers/gpu/arm/mali/platform/meson_bu/mali_clock.c, mali_clock.h, platform_gx.c, meson_main2.c, scaling.c
- drivers/amlogic/clk/clk-gxbb.c, drivers/amlogic/clk/gp0_pll.c
- the file list from https://api.github.com/repos/hardkernel/linux/git/trees/c3e4c730feb1750940971cae9ca1da3ca50f1d56?recursive=1

**In the source trees `tools/haiku-setup.sh` sets up, and this repository:**
- `u-boot/` (mainline U-Boot v2026.10): `u-boot.dtb` (the DTB Haiku boots with), `drivers/reset/reset-meson.c`, `drivers/clk/meson/gxbb.c`, `arch/arm/include/asm/arch-meson/clock-gx.h`
- `boot-scripts/boot-emmc.txt`
- `haiku/src/add-ons/kernel/bus_managers/fdt/fdt_module.cpp`, `haiku/src/system/kernel/arch/arm/arch_int_gicv2.cpp`, `haiku/src/add-ons/kernel/busses/mmc/meson_gx_mmc.cpp`, `haiku/src/add-ons/kernel/drivers/video/meson_vdec/`
- `docs/m26-vdec-facts.md`, `docs/m26-vdec-hevc-facts.md`, `docs/m27-irq-uart-i2c-facts.md`

**S905 public datasheet V1.1.4** (text extracted earlier for M27, from https://web.archive.org/web/20240712025337id_/https://dn.odroid.com/S905/DataSheet/S905_Public_Datasheet_V1.1.4.pdf). Used:
- §18 (power domains, Mali power modes, AO power registers)
- §22.3 (clock gating) and §22.4 (HHI_GP0_PLL_CNTL, HHI_MEM_PD_REG0, HHI_MALI_CLK_CNTL)
- the reset registers
- the memory map
- the interrupt table
- §32 (DMC)

**ODROID-C2 schematic rev 0.2** (text extracted earlier for M27): the VDD_EE and VDDEE_PWM nets.
