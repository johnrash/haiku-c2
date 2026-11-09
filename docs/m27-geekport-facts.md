# GeekPort: facts for the SAR ADC and the PWMs on the 40-pin header

Facts only, for an MIT driver on the ODROID-C2 (S905, GXBB). They come from Linux mainline's SAR ADC, PWM, clock and pinctrl drivers (GPL), mainline U-Boot (GPL), the Amlogic S905 public datasheet, Hardkernel's wiki, the C2 schematic and Hardkernel's wiringPi. No code was copied. Anything not confirmed by a source, or where sources disagree without a clear answer, is marked **UNVERIFIED**. Addresses are physical. "HHI" means the HIU block at 0xc883c000, and its offsets are byte offsets.

## 0. What a minimal driver must do

**(a) Read ADC channel 0 or 1 once, in millivolts, without fighting BL30.**
1. Once: enable the ADC's bus clock gate, **HHI_GCLK_MPEG2 (0xc883c148) bit 22**. U-Boot and the datasheet say MPEG0 bit 10 instead (see 2.4). Then read SAR_ADC_REG3 (0xc110868c). If bit 28 (set by BL30 when it has initialised the ADC) is 1, leave the delays, the switches and the ADC clock as BL30 set them. Otherwise run the init in 2.6. In both cases: set REG3[25:23] = 7 (channel 7 reads its pin), set the bandgap (REG11 bit 13), set ADC_EN (REG3 bit 21), wait 5 µs, then turn on the ADC clock gate (HHI_SAR_CLK_CNTL bit 8). Then calibrate (2.8).
2. For each read, take the BL30 lock: set SAR_ADC_DELAY bit 14, wait 1 µs, then poll until bit 15 reads 0 (every 1 µs, give up after 10 ms). Empty the FIFO. Select one channel. Start sampling and poll until done. Check that the FIFO holds exactly one entry for that channel. Stop the engine and clear bit 14 (2.7).
3. Convert: raw = FIFO entry bits [9:0] (10-bit on GXBB). Apply the calibration and clamp to 0..1023. Then mV = raw × 1800 / 1024 (Linux's scale: the 1.8 V reference over 2^10). Pin 40 is channel 0 and pin 37 is channel 1. The input range is 0 to 1.8 V (section 4).

**(b) Self-test with the internal references.** Read channel 7 with REG3[25:23] set to 0 (VSS), 1 (VDD/4), 2 (VDD/2), 3 (3·VDD/4) or 4 (VDD). Wait 10 to 20 µs after each mux change, and put 7 back afterwards. The expected raw values are about 0, 256, 512, 768 and 1023. Linux's calibration uses the 1/4 and 3/4 points.

**(c) PWM on pins 33, 11 and 19, 8-bit duty at about 94 kHz from the 24 MHz crystal.** Pin 33 is PWM_A (AB block, slot A). Pin 11 is PWM_E (EF block, slot A). Pin 19 is PWM_F (EF block, slot B). Steps:
1. In the block's MISC register, change only your slot's bits with a read-modify-write: clock select = 0 (crystal), divider field = 0 (÷1), clock enable = 1.
2. Write the slot's count register: high count in [31:16], low count in [15:0].
3. Set the slot's enable bit.

Linux says the hardware stretches both counts by one cycle (3.3, **UNVERIFIED** on GXBB). If so, a 255-cycle period (94.12 kHz) with duty value v from 1 to 254 needs high = v − 1 and low = 254 − v.

GXBB has no "constant output" bit, so a count pair can never give a true 0 % or 100 %. For v = 0, clear the slot's enable bit: with the delta-sigma bit also 0, the datasheet says the output is driven low. For v = 255, the cleanest option is to switch the pin to GPIO output high. Otherwise use high = 0xffff and low = 0, which still leaves a 1-cycle low pulse in every 65537 cycles.

**Never touch the PWM_B or PWM_D bits.** On the C2 those two channels drive the VDDEE and VCCK regulators (3.7).

## 1. Blocks, addresses and device tree

U-Boot's `meson-gxbb-odroidc2.dtb` matches Linux master's dts/dtsi apart from whitespace. CBUS base 0xc1100000 is confirmed by both the DT and the datasheet (EE register = 0xc1100000 + word offset × 4). The AO PWM base is the AO bus 0xc8100000 + 0x550, which agrees with the datasheet's 0xc8100400 + 0x54 × 4.

| Block | Absolute base | DT reg size | Datasheet word offset | DT compatible | Status on C2 |
|---|---|---|---|---|---|
| SAR ADC | 0xc1108680 | 0x34 | 0x21a0 | `amlogic,meson-gxbb-saradc`, `amlogic,meson-saradc` | okay |
| PWM AB | 0xc1108550 | 0x10 | 0x2154 | `amlogic,meson-gxbb-pwm-v2`, `amlogic,meson8-pwm-v2` | disabled |
| PWM CD | 0xc1108650 | 0x10 | 0x2194 | same | disabled |
| PWM EF | 0xc11086c0 | 0x10 | 0x21b0 | same | disabled |
| PWM AO AB | 0xc8100550 | 0x10 | AO 0x54 | same | disabled |

Correction to the brief: the PWM nodes carry the **"-v2"** compatibles and a `clocks` list. The obsolete `amlogic,meson-gxbb-pwm` string is not in this DT.

- **SAR ADC interrupt:** GIC SPI 73 (0x49, so INTID 105), rising edge.
- **SAR ADC clocks:** `clkin` = xtal (24 MHz fixed clock), `core` = clkc **CLKID_SAR_ADC (23)**, `adc_clk` = clkc **CLKID_SAR_ADC_CLK (97)**, `adc_sel` = clkc **CLKID_SAR_ADC_SEL (98)**. CLKID_SAR_ADC_DIV (99) exists but is not referenced.
- **SAR ADC vref:** `vref-supply` = **VCC1V8**, a fixed 1.8 V always-on regulator (U18, RT9179GB LDO, fed from VDDIO_AO3V3).
- **EE PWM inputs (AB, CD, EF):** index 0 = xtal, index 1 = none (`<0>`, which the DT notes as an untested "vid_pll"), index 2 = CLKID_FCLK_DIV4 (6), index 3 = CLKID_FCLK_DIV3 (5).
- **AO PWM inputs:** index 0 = xtal, index 1 = CLKID_CLK81 (12).

## 2. SAR ADC

### 2.1 Register map

Offsets are bytes from 0xc1108680. Registers 0x2c to 0x34 are missing from the S905 datasheet. Linux names them from the vendor sources and uses them only on GXBB and later.

| Off | Name | Fields used |
|---|---|---|
| 0x00 | REG0 | bits 30, 29, 28: busy flags (delta, averaging, sampling; Linux treats [30:28] as one "busy" mask). 27: FIFO full. 26: FIFO empty. [25:21]: FIFO count. [18:16]: current channel. 15: temp-sensor select (Linux clears it, U-Boot sets it). 14: sampling stop. 9/10: detect IRQ enable/polarity (unused). [8:4]: FIFO-count IRQ threshold. 3: FIFO IRQ enable. 2: sampling start (write 1). 1: continuous mode (Linux leaves 0). 0: sample engine enable. |
| 0x04 | CHAN_LIST | entry n in bits [3n+2:3n], 8 entries, each a channel number 0..7. [26:24]: index of the last entry (Linux writes 0: one entry). |
| 0x08 | AVG_CNTL | channel c: sample count in [2c+1:2c] (0=1, 1=2, 2=4, 3=8) and averaging mode in [17+2c:16+2c] (0 none, 1 mean, 2 median, and median needs 8 samples). The datasheet's "4 = 8" is a typo for 3. |
| 0x0c | REG3 | 31: use the sample-clock delay (Linux sets it; the datasheet calls it meaningless). 30: internal ADC clock enable (only for SoCs before GXBB). 28: **BL30 has initialised the ADC** (a firmware flag; reserved in the datasheet). 27: continuous ring counter (0 on GXBB). 26: sampling clock phase (Linux clears it). **[25:23]: channel-7 input mux.** 22: detect enable. **21: ADC enable.** [20:16]: panel detect (unused). [15:10]: internal clock divider (Linux width 6, U-Boot width 5; unused on GXBB, see 2.4). [9:8]/[7:0]: block delay (unused). |
| 0x10 | DELAY | [25:24]: input-delay tick (0 = 111 ns, 1 = 1 µs, 2 = 10 µs, 3 = 100 µs). [23:16]: input-delay count (N+1 ticks). **15: BL30 busy. 14: kernel busy.** (Both are a software semaphore in bits the datasheet marks unused.) [9:8]: sample-delay tick (0 = 1 µs, 1 = 10 µs, 2 = 100 µs, 3 = 1 ms). [7:0]: sample-delay count (N+1). |
| 0x14 | LAST_RD | last channel-0 sample in [9:0], last channel-1 sample in [23:16] (unused). |
| 0x18 | FIFO_RD | **[14:12]: channel id. [11:0]: sample** (the datasheet says [9:0]; Linux masks to the resolution, 10 bits on GXBB). |
| 0x1c | AUX_SW | mux selection for channels 2..7 at bits [8+3(c−2)+2 : 8+3(c−2)]. 6/5: VREF_P/N mux. 4: mode select. 3: YP drive. 2: XP drive. 1/0: YM/XM drive. These switch bits are shared by channels 2..7. |
| 0x20 | CHAN_10_SW | channel 1 mux in [25:23] and switches in [22:16] (same order as AUX_SW [6:0]). Channel 0 mux in [9:7] and switches in [6:0]. |
| 0x24 | DETECT_IDLE_SW | 26: detect switches enable. Detect-mode mux in [25:23] and switches in [22:16]. Idle-mode mux in [9:7] and switches in [6:0]. |
| 0x28 | DELTA_10 | 27: temp select (channel 6). 26/15: TS_REVE1/0. [14:11]: TS_C. 10: bandgap enable (pre-GXBB only). [25:16]/[9:0]: delta values (unused). |
| 0x2c | REG11 | **13: bandgap enable (GXBB).** 6: common-mode voltage select. 5: "vref voltage". 1: end-of-conversion select. 0: vref select (not on GXBB). The meaning of bits 6, 5 and 1 is undocumented. |
| 0x30 | REG12 | MPLL workaround bits (GXLX only). Not used on GXBB. |
| 0x34 | REG13 | [13:8]: 12-bit calibration (GXL and later). Not used on GXBB. |

### 2.2 GXBB parameters (Linux platform data)

| Parameter | GXBB value |
|---|---|
| BL30 integration (lock in DELAY bits 14/15, and REG3 bit 28 checked) | yes |
| ADC clock rate | 1 200 000 Hz |
| Resolution | 10 bits |
| REG11 bit 5 ("vref voltage") | 1 |
| REG11 bit 6 (CMV select) | 1 |
| REG11 bit 1 (EOC) | 0 |
| vref-select bit (REG11 bit 0) | not applicable |
| Ring counter disable (REG3 bit 27) | 0 |
| Temperature sensor | **none** (no trimming bits for GXBB, so no temperature channel; BL30 reads the sensor) |
| MPLL workaround | no |

The channels Linux offers are 0..7 plus five channel-7 mux settings, all reported with FIFO channel id 7. GXL and later SoCs are 12-bit and disable the ring counter.

### 2.3 Channel mapping

CHAN_10_SW maps internal channel 0 to analog input 0 and channel 1 to input 1. AUX_SW maps channels 2..7 each to the input with its own number. With Linux's init, CHAN_10_SW gets mux 0 and 1 plus the XP and YP drive bits for both channels, which is 0x008c000c in the bits Linux sets. AUX_SW is written whole as **0x03eb1a0c** (muxes 2..7 plus YP and XP drive). The datasheet's reset defaults are different: channel 0 mux = 3 and channel 1 mux = 2. So if BL30 did not set these registers, the init must.

### 2.4 Clocks on GXBB

- **Bus gate:** Linux uses **HHI_GCLK_MPEG2 (0x148) bit 22**. Linux commit 75eccf5ed832 (2017, from Amlogic) says that the published GXBB datasheet v1.1.4 is wrong here, and that MPEG0 bit 10 is "SANA". The datasheet table (and U-Boot's gxbb clock driver, which still follows it) gives the SAR ADC as **HHI_GCLK_MPEG0 (0x140) bit 10**. Linux marks the gates ignore-unused, so they stay as the boot firmware left them. Setting both bits is harmless. **UNVERIFIED** which one actually gates the ADC on the C2.
- **ADC clock:** **HHI_SAR_CLK_CNTL, HHI 0x3d8 (0xc883c3d8, datasheet word 0xf6).**

| Bits | Use |
|---|---|
| [10:9] | source select: 0 = crystal (cts_oscin, 24 MHz), 1 = clk81 |
| 8 | gate (clock enable) |
| [7:0] | divider: output = source / (N+1) |

Linux sets source 0 and asks for 1.2 MHz, which gives N = 19 (0x13). The whole register then reads 0x113 once the gate is on. Linux does this only when it runs the full init, that is when REG3 bit 28 is 0. Otherwise it keeps BL30's settings and only turns the gate on.

- The internal divider and gate in REG3 ([15:10] and bit 30) are used only when the DT gives no `adc_clk`, which means only on SoCs before GXBB. Linux never writes them on GXBB.

### 2.5 The BL30 lock

BL30 (the SCP firmware) also uses the ADC: Linux's comments say it reads the temperature sensor. The two sides share a flag protocol in SAR_ADC_DELAY.
- **Take:** set bit 14 (kernel busy). Wait 1 µs. Poll bit 15 (BL30 busy) every 1 µs until it reads 0, or give up after 10 ms and clear nothing more (Linux returns an error). This is not an atomic test-and-set. Linux also holds a mutex around it.
- **Release:** clear bit 14.
- Linux holds the lock around: every sample (clear FIFO, configure, sample, stop), hw-enable (ADC_EN, bandgap, clock on) and hw-disable. It does not hold it during init.
- **UNVERIFIED:** BL30's side (it is a closed blob). We only know that Linux expects BL30 to set bit 15 while it uses the ADC and to stay away while bit 14 is set.

### 2.6 Init (Linux order, at probe)

1. Enable the bus gate (`core`).
2. Set REG3[25:23] = 7 and wait 10 to 20 µs.
3. If REG3 bit 28 is set, **stop here** and go to step 11.
4. Stop the engine: clear REG0 bit 3, set REG0 bit 14, wait for busy [30:28] = 0, clear REG0 bit 0.
5. Clear REG0 bit 15. Write CHAN_LIST = 0.
6. REG3: clear bit 26, set bit 31.
7. DELAY: input-delay count = 10 and input-delay tick = 1 (1 µs), so 11 µs between the samples of one channel. Sample-delay tick = 0. Linux means to set the sample-delay count to 10 too, but its write uses the wrong mask and leaves [7:0] unchanged (datasheet reset value 9). U-Boot has the same slip.
8. CHAN_10_SW and AUX_SW as in 2.3.
9. REG3 bit 27 = 0. REG11: bit 1 = 0, bit 5 = 1, bit 6 = 1.
10. HHI_SAR_CLK_CNTL: source 0, divider 19.
11. **hw-enable, under the lock:** vref regulator on (always on here). REG0[8:4] = 1. REG11 bit 13 = 1 (bandgap). REG3 bit 21 = 1 (ADC enable). **Wait 5 µs.** HHI_SAR_CLK_CNTL bit 8 = 1. Release the lock.
12. Calibrate (2.8). A failure is only a warning, and the driver goes on uncalibrated.

Power-down is the reverse, under the lock: clock gate off, ADC_EN off, bandgap off, regulator off.

### 2.7 One sample (Linux, with U-Boot's polling variant)

1. Take the lock (2.5).
2. Empty the FIFO: while REG0[25:21] ≠ 0, for at most 32 reads, read FIFO_RD.
3. AVG_CNTL for the channel: one sample with no averaging for a raw read, or 8 samples with mean averaging (Linux's "average_raw", and U-Boot always).
4. CHAN_LIST: [26:24] = 0 and entry 0 = channel. Also write the channel into DETECT_IDLE_SW [25:23] and [9:7]. For channel 6, clear DELTA_10 bit 27. For channel 7 (or a mux reference), change REG3[25:23] if it differs and wait 10 to 20 µs.
5. Start, as three separate read-modify-writes to REG0: set bit 3 (FIFO IRQ, threshold 1), then bit 0 (engine enable), then bit 2 (start).
6. Wait.
   - Linux waits for the FIFO interrupt, for up to **100 ms**.
   - U-Boot does not use the interrupt: it waits 1 µs, then polls REG0[30:28] every 1 µs until all three bits are 0, for up to **10 ms** (10 000 polls).
   - Linux notes the 1 µs pause before the first status read is needed: without it, busy may not have risen yet.
7. Check REG0[25:21] = 1. Read FIFO_RD. Bits [14:12] must equal the channel (7 for every mux reference). The value is bits [11:0], masked to [9:0].
8. Stop: clear REG0 bit 3. Set REG0 bit 14. Wait 1 µs, then poll busy clear (1 µs interval, up to 10 ms). Clear REG0 bit 0. Linux never clears bit 14 again. **UNVERIFIED** whether it clears itself.
9. Release the lock. Apply the calibration.

### 2.8 Channel-7 mux and calibration

REG3[25:23] values: 0 = VSS, 1 = VDD/4, 2 = VDD/2, 3 = 3·VDD/4, 4 = VDD, 7 = the channel-7 pin. The datasheet lists only 0 to 4 and calls 5 to 7 unused. Linux and U-Boot both use 7 as "channel-7 pin" and restore it after every use. "VDD" here is the ADC's own analog supply/reference, which the datasheet calls vdda.

Calibration (Linux and U-Boot):
1. Read channel 7 with the mux at VDD/4 and then at 3·VDD/4. Each read uses 8 samples with mean averaging, and the driver waits 10 to 20 µs after each mux change (U-Boot: 20 µs).
2. The targets are 1/4 and 3/4 of full scale: 256 and 768 for 10 bits. If the second reading is not larger than the first, the calibration fails.
3. scale = (768 − 256) / (v3/4 − v1/4), and offset = 256 − v1/4 × scale. Linux keeps the scale in millionths.
4. Every later raw value becomes raw × scale + offset, clamped to 0..1023.
5. Before calibration the scale is 1 and the offset is 0.

### 2.9 Reference and range

- **Reference:** VCC1V8 (1.8 V). Linux reports a scale of 1800 mV / 2^10 per count, about 1.758 mV per count, so code 1023 reads 1798 mV.
- **Analog supply:** the ADC's analog supply pin AVDD18_USB_ADC (ball D13) is fed from VCC1V8 on the schematic (through R111; its value is **UNVERIFIED**). The datasheet rates that supply at 1.71 to 1.89 V and the ADC's draw at 1.2 mA while sampling.
- **Input range:** 0 to 1.8 V, per Hardkernel. An absolute maximum for the input pins is **UNVERIFIED**: none was found in the datasheet. Treat 1.8 V as the limit.

## 3. PWM

### 3.1 Which block drives which pin

| Header pin | Pad | pinctrl group (mux reg, bit) | Controller | Slot | Count reg | MISC reg |
|---|---|---|---|---|---|---|
| 33 | GPIOX_6 | `pwm_a_x` (3, 17) | AB, 0xc1108550 | A | 0xc1108550 | 0xc1108558, A bits |
| 11 | GPIOX_19 | `pwm_e` (2, 30) | EF, 0xc11086c0 | A (= E) | 0xc11086c0 | 0xc11086c8, A bits |
| 19 | GPIOX_7 | `pwm_f_x` (3, 18) | EF, 0xc11086c0 | B (= F) | 0xc11086c4 | 0xc11086c8, B bits |

- All three mux facts match Linux's `pinctrl-meson-gxbb.c` and the port's `meson_gpio.cpp` table.
- The mux registers are at 0xc88344b0 + 4 × reg, so reg 2 = 0xc88344b8, reg 3 = 0xc88344bc and reg 8 = 0xc88344d0.
- Other functions on the same pads, whose bits must be clear:
  - GPIOX_6: `tsin_b_d_valid` (3, 9).
  - GPIOX_7: `sdio_irq` (8, 11) and `tsin_b_sop` (3, 8).
  - GPIOX_19: Linux has no other group. The datasheet and the schematic also list UART_RTS_B on this pad, but no mux bit for it is known.
- The datasheet register names confirm the slots: PWM_PWM_E is at word 0x21b0, PWM_PWM_F at 0x21b1 and PWM_MISC_REG_EF at 0x21b2.
- Hardkernel's wiki calls pin 33 "PWM0" and pin 19 "PWM1" (the vendor kernel's numbering). It gives pin 11 no PWM label.

### 3.2 Registers of one controller (GXBB: four registers, 0x10 bytes)

| Off | Name | Fields |
|---|---|---|
| 0x0 | PWM_A (C, E) | high count [31:16], low count [15:0]. The datasheet's "31-15" is a typo; Linux uses [31:16]. |
| 0x4 | PWM_B (D, F) | same layout |
| 0x8 | MISC_AB | 23: B clock enable. [22:16]: B divider (÷ N+1, N = 0..127). 15: A clock enable. [14:8]: A divider. [7:6]: B clock select. [5:4]: A clock select. 3: B delta-sigma enable. 2: A delta-sigma enable. 1: B PWM enable. 0: A PWM enable. [31:24]: unused on GXBB. |
| 0xc | DELTA_SIGMA_AB | B value [31:16], A value [15:0]. Linux does not use it. |

GXBB has **no** constant-output bits (28/29), no invert bits (26/27), and no TIME, A2, B2 or BLINK registers. The S905 datasheet lists four registers per controller and calls MISC [31:24] unused, and the DT size is 0x10. Linux enables "constant" and "polarity" only for AXG, G12 and S4 (commit 5dca8a93b015). It treats GXBB as the plain meson8-v2 type without them.

### 3.3 Period and duty encoding

- **Counting clock:** the count runs on the selected input divided by (MISC divider field + 1).
- **High phase:** the output is high for the "high" count, then low for the "low" count, and the cycle repeats. Bit 0/1 must be 1 for PWM output. When it is 0, the output follows the delta-sigma bit (2/3), and the output is low if that bit is 0 too.
- **The +1 stretch:** Linux commit dd4d280ac558 says the hardware adds one to each count, so a 0 still gives a one-cycle pulse. That would make high = (H+1) cycles, low = (L+1) cycles and the period H+L+2. The datasheet calls the counters "programmable between 1 and 65535", which fits. **UNVERIFIED** on the C2: check it with a scope.
- **What Linux computes:**
  - It asks for an input rate of 65535 × 10^9 / period_ns, rounded to what the mux and divider can give.
  - count = rate × period, which must be at most 0xffff.
  - high = rate × duty, and low = count − high.
  - It does **not** subtract the +1s, so its real period is 2 cycles long.
  - For an inverted polarity on GXBB it swaps the duty (duty = period − duty).
  - It writes the count register while the output runs and then sets the enable bit (no disable in between). **UNVERIFIED** whether a new count waits for the end of the current period.
- **Divider:** commit 51496e444687 confirms the divider is 7 bits with an off-by-one register value: field 0..127 means ÷1..÷128.

### 3.4 Clock inputs on GXBB

Clock select for EE blocks:

| Select | Input |
|---|---|
| 0 | crystal, 24 MHz |
| 1 | "vid_pll" (U-Boot calls it the HDMI PLL; Linux leaves it unconnected because the display driver changes its rate, commit 3bddf73285d5) |
| 2 | fclk_div4 |
| 3 | fclk_div3 |

The rates of fclk_div4 and fclk_div3 (nominally 500 MHz and 666.7 MHz from a 2 GHz fixed PLL) are **UNVERIFIED** here. For AO AB, select 0 = crystal, 1 = clk81, and 2/3 are grounded. With select 0 and divider 0, one count is 41.67 ns, and a 255-cycle period is 94.12 kHz (256 cycles: 93.75 kHz).

### 3.5 0 % and 100 % on GXBB (Linux)

- **Duty = 0:** Linux writes high = 0 and low = count. With the +1 behaviour this leaves a 1-cycle high pulse each period.
- **Duty = period:** Linux writes high = count and low = 0, which leaves a 1-cycle low pulse.
- Linux writes the "constant" bit only on AXG and later, never on GXBB.
- **Disabled:** Linux clears only the enable bit, and the output goes low (datasheet: with delta-sigma off the output is low).
- **"Disabled + inverted":** Linux does this by enabling the output with the fastest clock, high = 0xffff and low = 0, which it admits is high for "slightly less than 100 %".

For a true 0 %, clear the enable bit. For a true 100 %, use the pin as a GPIO driven high.

### 3.6 Clock gates

Linux enables no HHI gate for the EE PWMs. The v2 DT gives only the four mux inputs, the driver's own gate is the MISC clock-enable bit, and Linux's GXBB gate list has no PWM entry. The S905 gating table has no PWM entry either. The blocks sit behind the CBUS "PERIPHS" top-level gate (HHI_GCLK_MPEG0 bit 7), which the boot firmware leaves on because the UART and GPIO live there too. The crystal input is always running. **UNVERIFIED** that nothing else gates the PWM blocks.

### 3.7 Shared registers: PWM_B and PWM_D drive the core regulators

The C2 schematic (rev 0.2) routes GPIODV_29 (PWM_B) and GPIODV_28 (PWM_D) to the POWER sheet's VDDEE_PWM and VCCK_PWM inputs. The text extraction does not show which signal drives which, so the pairing is **UNVERIFIED**. Consequences:
- PWM_A (pin 33) shares MISC_AB with PWM_B. A driver must read-modify-write only bits 15, [14:8], [5:4], 2 and 0, and never write offset 0x4.
- The CD block must not be touched at all.
- Linux uses a spinlock for its own read-modify-writes. Whether BL30 or a bootloader changes PWM_B at run time (DVFS) is **UNVERIFIED**, so keep the read-modify-write window minimal.
- Pins 11 and 19 share MISC_EF, so they need a lock between them in the driver.

## 4. Header pins (J2)

Hardkernel's wiki (board revision 0.2, 2016-02-26) gives these J2 pins:

| Pin | Signal |
|---|---|
| 37 | **ADC.AIN1**, "10bit ADC (0~1.8V)" |
| 38 | **1.8V REF** |
| 40 | **ADC.AIN0**, "10bit ADC (0~1.8V)" |
| 39 | GND |
| 33 | GPIOX.6, "PWM0" |
| 19 | GPIOX.7, "PWM1" |
| 11 | GPIOX.19 |

The wiki also says that every header signal is 3.3 V except the analog inputs. J7 has no ADC.

- **Channel numbers:** AIN0 = SAR ADC channel 0 and AIN1 = channel 1. Hardkernel's wiringPi reads AIN0 from the vendor kernel's `saradc/ch0` and AIN1 from `ch1`, and the schematic connects net AIN0 to the SoC's SARADC_CH0 and AIN1 to SARADC_CH1.
  - The schematic symbol puts SARADC_CH0 on ball D12 and CH1 on D11, while the datasheet's pin table says D11 = CH0 and D12 = CH1. The names agree but the ball numbers do not. **UNVERIFIED** until a known voltage is applied.
- **Pin 38's rail:** the schematic text places VDDIO_AO1V8 beside pin 38. The ADC's DT reference is VCC1V8, a different 1.8 V regulator. **UNVERIFIED** which rail pin 38 carries.
- **Input path:** no series resistor or divider appears on AIN0/AIN1 in the schematic text (**UNVERIFIED**).
- **Board revisions:** wiringPi also has a revision 1 pin map (an engineering sample) with a different layout. The table above and the port's table are revision 2.

## 5. Mainline U-Boot: what differs from Linux

`drivers/adc/meson-saradc.c` (enabled in `odroid-c2_defconfig` as CONFIG_SARADC_MESON) has the same register map and the same lock protocol, with these differences:
- It **polls** busy instead of using the IRQ.
- It always uses 8-sample mean averaging.
- It skips init only if REG3 bit 28 **and** bit 21 (ADC_EN) are both set.
- It **sets** REG0 bit 15 (Linux clears it).
- It does not set the XP/YP drive bits in CHAN_10_SW.
- It never writes REG11 bits 1, 5 and 6.
- It never sets the HHI clock source or rate (it only turns the gates on).
- Its REG3 internal-divider width is 5, not 6.
- It waits 20 µs after a channel-7 mux change.
- It has a 10-bit resolution for GXBB.
- It returns an error if calibration fails (Linux only warns).
- Its gxbb clock driver uses the **old gate mapping** (CLKID_SAR_ADC = HHI_GCLK_MPEG0 bit 10, SANA = MPEG2 bit 22), the reverse of Linux since 2017.

`drivers/pwm/pwm-meson.c` differs as follows:
- It matches only the obsolete compatibles (`amlogic,meson-gxbb-pwm`, `-ao-pwm`), so it will not bind to the C2 DT's "-v2" nodes.
- It keeps the crystal or clock input already selected and chooses the divider as ⌊rate × period / (10^9 × 65535)⌋.
- Its 0 %/100 % handling is the same as Linux's.
- It lists GXBB input 1 as the HDMI PLL.
- Two of its lines look wrong, so don't copy their logic. The disable path ANDs MISC with the enable mask instead of clearing it. The probe's mux switch masks with the select field instead of its complement.

## 6. Open items (UNVERIFIED)

1. Which gate really clocks the ADC: HHI_GCLK_MPEG2 bit 22 (Linux) or MPEG0 bit 10 (datasheet/U-Boot). Setting both is safe.
2. Whether BL30 on the C2 sets REG3 bit 28. Read REG3 at attach and log it.
3. BL30's side of the DELAY-bit protocol, and whether it changes PWM_B at run time.
4. Whether REG0 bit 14 (stop) clears itself.
5. The PWM +1 stretch on GXBB, and whether count updates take effect at a period boundary.
6. The pairing of PWM_B and PWM_D with VDDEE and VCCK.
7. Pin 38's rail (VDDIO_AO1V8 vs VCC1V8), the ball numbering for AIN0/AIN1, and the absolute maximum input voltage.
8. The rates of fclk_div3 and fclk_div4.

## Sources

Downloaded into a scratch directory (`m27-sources/`) on 2026-10-30. Sizes are in bytes.

Linux master files (fetched raw):

| URL | Size | Last change |
|---|---|---|
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/iio/adc/meson_saradc.c | 47309 | 474f286ca7fb, 2026-08-21 |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/meson/gxbb.c | 91666 | 3990334628de, 2026-08-12 |
| https://raw.githubusercontent.com/torvalds/linux/master/include/dt-bindings/clock/gxbb-clkc.h | 5944 | |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/pwm/pwm-meson.c | 19246 | eff04f2a6b53, 2026-07-05 |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/pinctrl/meson/pinctrl-meson-gxbb.c | 26034 | 8a74a53ebbe3, 2024-08-07 |
| https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxbb-odroidc2.dts | 8894 | |
| https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxbb.dtsi | 17634 | |
| https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gx.dtsi | 17113 | |
| https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/iio/adc/amlogic,meson-saradc.yaml | 4023 | |
| https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/pwm/pwm-amlogic.yaml | 4661 | |

`drivers/clk/meson/gxbb.h` returned 404: it has been merged into gxbb.c.

GitHub API, commit history and commit messages:

| URL | Size |
|---|---|
| https://api.github.com/repos/torvalds/linux/commits?path=drivers/clk/meson/gxbb.c&per_page=100 | 464220 |
| https://api.github.com/repos/torvalds/linux/commits?path=drivers/pwm/pwm-meson.c&per_page=100 | 295237 |
| https://api.github.com/repos/torvalds/linux/commits/75eccf5ed832 (SAR ADC/SANA gate fix) | 4989 |
| https://api.github.com/repos/torvalds/linux/commits/dd4d280ac558 (PWM constant bit, +1 behaviour) | 10591 |
| https://api.github.com/repos/torvalds/linux/commits/5dca8a93b015 (constant/polarity only on g12, axg, s4) | 5729 |
| https://api.github.com/repos/torvalds/linux/commits/3bddf73285d5 (no vid_pll as PWM parent) | 6902 |
| https://api.github.com/repos/torvalds/linux/commits/51496e444687 (7-bit pre-divider, off by one) | 5891 |

The last-change lookups for meson_saradc.c and pinctrl-meson-gxbb.c were API queries that were not saved.

Hardkernel:

| URL | Size |
|---|---|
| https://raw.githubusercontent.com/hardkernel/wiringPi/master/wiringPi/odroidc2.c | 24829 |
| https://raw.githubusercontent.com/hardkernel/wiringPi/master/wiringPi/odroidc2.h | 2023 |
| https://web.archive.org/web/20241026031843/https://wiki.odroid.com/odroid-c2/hardware/expansion_connectors (archive of the J2/J7 wiki page) | 259183 |
| https://web.archive.org/web/20220616145209id_/https://dn.odroid.com/S905/Schematic/odroid-c2_rev0.2_20160226.pdf (C2 schematic, rev 0.2) | 1653707 |
| https://web.archive.org/web/20240712025337id_/https://dn.odroid.com/S905/DataSheet/S905_Public_Datasheet_V1.1.4.pdf (S905 datasheet; chapters 43 PWM, 45 SAR ADC, HHI tables) | 6543928 |

The live page https://wiki.odroid.com/odroid-c2/hardware/expansion_connectors and dn.odroid.com sit behind a bot check (HTTP 403), so the Wayback copies were used.

Local, not downloaded:
- `u-boot/drivers/adc/meson-saradc.c`, `u-boot/drivers/pwm/pwm-meson.c`, `u-boot/drivers/clk/meson/gxbb.c` and `u-boot/configs/odroid-c2_defconfig` (U-Boot at 5508406582f6).
- `u-boot/dts/upstream/src/arm64/amlogic/meson-gxbb-odroidc2.dtb` (decompiled with dtc), `meson-gxbb.dtsi` and `meson-gx.dtsi`.
- `haiku/src/add-ons/kernel/drivers/misc/meson_gpio/meson_gpio.cpp`.
