# Infrared receiver: facts for an MIT driver (S905 / GXBB, ODROID-C2)

> **Status:** Facts gathered 2026-10-31, before `meson_ir` (M24). The driver follows section 0. The JVC RM-RK52 decodes as JVC, address 0x8f. The ODROID remote's codes (section 6) were never tried on hardware.

Facts only, for a clean-room, MIT-licensed Haiku driver for the ODROID-C2's infrared receiver. GPL sources (Linux mainline, Hardkernel's vendor kernel and U-Boot, IRMP) were read only for facts: register addresses, bit positions, units, write sequences and timing values. No code, macros, identifier lists or comments were copied. Everything below is in my own words. Anything not confirmed by a source, or where sources disagree without a clear answer, is marked **UNVERIFIED**, with what each source says.

## Sources

In order of how much this document relies on them. Every downloaded file is listed with its URL and size in Appendix A.

1. **Amlogic S905 public datasheet V1.1.4** (the copy Hardkernel distributed):
   - chapter 42 "Infrared Remote", 42.1 to 42.4, pages 312 to 320: the decoder, an NEC example, and every register of the block at 0xc8100580;
   - AO_RTI_GEN_CTNL_REG0, pages 79 to 80;
   - table III.22.5 (AO clock gating);
   - tables II.7.8 and III.23.8 (GPIOAO pin functions), and the ball table (default pulls);
   - section 24.2, the GIC interrupt source table.

   The text was extracted with pdftotext in layout and raw modes.
2. **Linux mainline, tag v7.3**:
   - drivers/media/rc/meson-ir.c and drivers/media/rc/keymaps/rc-odroid.c;
   - the decoders ir-nec-decoder.c, ir-rc5-decoder.c, ir-rc6-decoder.c, ir-sony-decoder.c and ir-jvc-decoder.c, with rc-core-priv.h (the tolerance helpers);
   - rc-main.c (repeat period per protocol) and include/media/rc-core.h (how NEC bytes become a scancode);
   - the DT binding amlogic,meson6-ir.yaml, and meson-gx.dtsi, meson-gxbb.dtsi and meson-gxbb-odroidc2.dts;
   - drivers/pinctrl/meson/pinctrl-meson-gxbb.c and pinctrl-meson.h;
   - drivers/clk/meson/gxbb-aoclk.c and gxbb.c, with the gxbb-aoclkc clock and reset bindings.
3. **Hardkernel's vendor kernel, branch odroidc2-3.14.y**:
   - drivers/media/rc/meson-ir.c, Hardkernel's C2 variant of the Linux driver;
   - arch/arm64/boot/dts/meson64_odroidc2.dts, for its IR node;
   - Amlogic's vendor remote driver: drivers/amlogic/input/remote/remote_main.c, remote_main.h and remote_func.c.
4. **Hardkernel's vendor U-Boot, branch odroidc2-v2015.01**: arch/arm/include/asm/arch-gxb/secure_apb.h, Amlogic's list of GXBB register addresses.
5. **Mainline U-Boot v2026.10** (the port's `u-boot/` tree):
   - drivers/pinctrl/meson/pinctrl-meson-gxbb.c, the AO bank layout;
   - u-boot.dtb, identical to dts/upstream/src/arm64/amlogic/meson-gxbb-odroidc2.dtb, decompiled with `dtc`. This is the DT the board passes to Haiku.
6. **ODROID-C2 schematic rev 0.2** (odroid-c2_rev0.2_20160226.pdf): sheet 4 "C2-MAIN" and sheet 22 "Remote Control Receiver" (PDF pages 3 and 24, rendered with pdftoppm and read).
7. **Hardkernel wiki, ODROID-C2 "LIRC (Ubuntu 16.04)"**, through the Wayback Machine. The 2021-03-02 and 2022-06-16 snapshots have the same lircd.conf for the ODROID remote.
8. **sbprojects.net IR knowledge base** (San Bergmans), pages for NEC, RC-5, RC-6, Sony SIRC and JVC, each last updated 17 July 2025.
9. **IrpTransmogrifier, IrpProtocols.xml** (master). This is the DecodeIR/JP1 protocol database in IRP notation; used here for NEC1, NECx1, RC5, RC5x, RC6, Sony12/15/20 and JVC.
10. **Arduino-IRremote** (MIT): src/ir_JVC.hpp (master) and src/ir_JVC.cpp (tag 2.8.0). **IRMP** (GPL): src/irmpprotocols.h (master). Both used for the JVC disagreements and repeat timings.
11. The port's own MIT material: docs/m27-irq-uart-i2c-facts.md (GPIO interrupt block, GIC conventions), headers/private/drivers/ir_remote.h and src/add-ons/kernel/drivers/misc/meson_gpio/meson_gpio.cpp.

Used only as a cross-check: the text of the S905X datasheet rev 0.2, from earlier research (not kept in this repository). It gives the same AO_MF_IR_DEC register addresses as the S905 datasheet.

Conventions:
- Addresses are physical. The AO (always-on) bus starts at 0xc8100000. One 4 KB page there holds the AO pin mux, the AO GPIO registers, AO_RTI_GEN_CNTL_REG0, the console UART (0xc81004c0) and the IR block.
- "Offset" means offset from 0xc8100580.
- Register names are the datasheet's, without the AO_MF_IR_DEC_ prefix: LDR_ACTIVE, LDR_IDLE, LDR_REPEAT, BIT_0, REG0, FRAME, STATUS, REG1, REG2, DURATN2, DURATN3, FRAME1, STATUS_1, STATUS_2, REG3.
- A "tick" is the decoder's time unit, set by REG0's base time field.
- GIC: **INTID = SPI + 32.** The port's FDT bus adds the 32 when it reads `interrupts`.
- A "mark" is IR carrier present, which makes the receiver output low. A "space" is no carrier.

## 0. What a minimal driver needs

### (a) Initialization: raw edge timing, one interrupt per edge

The AO page must be mapped. All accesses are 32-bit. "RMW" means read-modify-write, needed where a register is shared with other functions.

1. **IR clock gate.** AO_RTI_GEN_CNTL_REG0 (0xc8100040) bit 0 must be 1, its reset value. If it is 0, set it (RMW). Leave bit 16, the IR module reset, at 0 (section 3).
2. **Pin.** In AO_RTI_PIN_MUX_REG (0xc8100014), set bit 0 (REMOTE_INPUT on GPIOAO_7) and clear bit 21 (REMOTE_OUTPUT on the same pad). Use RMW: bits 11 and 12 of this register carry the serial console. Optionally, as Linux does, clear the pad's pull enable, AO_RTI_PULL_UP_REG (0xc810002c) bit 7 (2.1, 2.2).
3. **Stop the block.** REG1 (0xc810059c) = 0x00000001: decoder disabled, reset asserted. A previous OS may have left it enabled (trap 2).
4. REG1 = 0x00000000: reset released.
5. **Mode.** REG2 (0xc81005a0) = 0x00000002: decode mode 2, "general time measurement", which is software decoding. On GXBB the mode is in REG2, not in REG1 (1.4).
6. **Time base.** REG0 (0xc8100590) = 0x00000009. Base time 9 gives a 10 µs tick. Filter, max frame time and bit 31 are all 0. Linux writes only the base field; see 1.8 for the filter.
7. **Edges.** REG1 = 0x00000004. Bits 3:2 = 01: interrupt and width measurement on both edges. Bit 1 = 0: no input inversion. Bits 14 and 29 to 31 = 0: the time base is the 1 µs tick.
8. Read STATUS (0xc8100598) and FRAME (0xc8100594) once and discard the values. Linux's hardware-decoder path and Amlogic's vendor driver both do this to drop stale state. It is harmless.
9. **GIC.** Configure INTID 228 as edge-triggered with `arch_int_configure_io_interrupt(228, B_EDGE_TRIGGERED, …)`, install the handler, then enable the line.
10. **Enable.** REG1 = 0x00008004 (bit 15 set). Discard the first event after enabling (1.6).

Linux's sequence has the same steps but does each as a read-modify-write:
- pulse REG1 bit 0;
- set REG2[3:0] = 2;
- set REG0[11:0] = 9;
- set REG1[3:2] = 01;
- set REG1 bit 15;
- only then request the interrupt.

Writing whole registers, as above, also clears fields a previous owner may have set: the time-base selects in REG1 bits 14 and 29 to 31, the filter, and the hold and compare features in REG2.

### (b) Interrupt handler

1. **Read STATUS.** Bit 8 is the input level now, just after the edge. With no inversion and the C2's active-low receiver, **bit 8 = 1 means the interval that just ended was a mark**, and 0 means it was a space. This is exactly how Linux reads it (1.6).
2. **Read REG1.** Bits 28:16 hold the length of the interval that just ended, in ticks. Duration = field × 10 µs. Linux reads STATUS first, then REG1.
3. **Acknowledge: none.** No register write is needed; the kernel's GIC end-of-interrupt is all. Linux writes nothing in its handler (1.7).
4. Take `system_time()` too. It is needed for long gaps and frame ends ((d) below).

### (c) Shutdown

1. Clear REG1 bit 15 (enable). This is all Linux does on remove and on suspend.
2. Disable the GIC line and remove the handler.
3. Optional, as Linux does on reboot and power-off: set REG2[3:0] back to 0 (hardware NEC) and REG0[11:0] back to 19 (20 µs tick). Linux does this so firmware can power the board back on from the remote. Nothing in this port's boot chain is known to use IR wake-up (**UNVERIFIED**), but restoring the values is harmless.
4. Leave the pin mux as it is, as Linux does.

### (d) Frame ends and idle

- **The hardware reports an interval only when the next edge arrives.** So the space after the last mark of a frame is reported only when the next button press begins, perhaps seconds later. A decoder must end frames with a software timer restarted at each edge. For this driver Linux uses its raw-event timeout, 125 ms by default.
- For some protocols the end can be detected sooner:
  - NEC and JVC frames are complete at the trailing mark;
  - Sony needs at least 5.7 ms of space (Linux's figure), RC-5 at least 4.9 ms and RC-6 at least 2.4 ms (section 5).
- **The width counter has 13 bits: at most 81.91 ms at 10 µs.** Every idle gap is longer than that, so it reads wrong. Compare `system_time()` between interrupts, and treat any gap over about 80 ms as a long space whatever the counter says (1.5).

## 1. The IR block at 0xc8100580

### 1.1 Location, DT, and the two decoders

| Item | Value | Source |
|---|---|---|
| Compatible | "amlogic,meson-gx-ir", "amlogic,meson-gxbb-ir"; status "okay"; `linux,rc-map-name` = "rc-odroid" | the board's DTB; Linux v7.3 meson-gx.dtsi and the C2 .dts |
| Registers | 0xc8100580, size 0x40 (AO bus offset 0x580) | DT; datasheet 42.4 |
| Interrupt | SPI 196, type 1 (edge, rising) → **INTID 228** | DT; datasheet GIC table: 228 = ao_ir_dec_irq |
| Pins | pinctrl "remote_input_ao" (GPIOAO_7), bias disabled | DT |
| Clocks, resets | none in the node | DT |

GXBB has **two IR decoders** in the AO domain:

- **The multi-format ("MF") decoder at 0xc8100580**, with 15 registers. This is the one the DT describes, and the only one the S905 datasheet documents (as AO_MF_IR_DEC_*).
- **A legacy NEC decoder at 0xc8100480**, with 8 registers at 0x480 to 0x49c (AO_IR_DEC_*).
  - It appears only in Amlogic's register list (secure_apb.h) and in the vendor remote driver. That driver's "two in one" modes run both decoders at once, addressing the legacy one at −0x100 from the MF one, from a single interrupt handler.
  - The datasheet does not mention it. Whether it also raises SPI 196 is **UNVERIFIED**; the vendor's one handler checking both suggests it does.

What Linux does for each compatible:

| Compatible | Linux register window | Mode field | Linux mode |
|---|---|---|---|
| amlogic,meson6-ir (Meson6 and Meson8: the legacy-style decoder) | up to REG1 (0x1c) | REG1 bits 8:7 | raw (2) |
| amlogic,meson8b-ir and **amlogic,meson-gxbb-ir** (GXBB, GXL, and later SoCs whose DTs reuse this string) | up to REG2 (0x20) | **REG2 bits 3:0** | raw (2) |
| amlogic,meson-s4-ir | up to FRAME1 (0x2c) | REG2 bits 3:0 | hardware NEC decoder |

So **Linux treats GXBB and GXL identically.** Both SoCs include meson-gx.dtsi with the same compatible pair. The real split is between the legacy decoder, whose mode is in REG1, and the MF decoder, whose mode is in REG2. The block at 0x580 is the MF decoder. In it, REG1 bits 8:7 are *not* a mode field: bit 8 is part of the frame bit-length field, and bit 7 is "record at error".

Hardkernel's vendor 3.14 kernel for the C2 bound a Meson6-style driver to 0xc8100580: window 0x20, compatible "amlogic, meson6-ir", plus a property "pulse-inverted".
- It wrote the mode into REG1 bits 8:7 and never touched REG2.
- It still got one interrupt per edge, because REG1 bits 3:2 choose the interrupt whatever the decode mode.
- It ignored the hardware width counter. For the C2 it had a separate code path that measured kernel time between interrupts and read only the level bit.

### 1.2 Register map (the 0x40 window)

| Off | Address | Datasheet name | Vendor driver index | Linux on GXBB | Purpose |
|---|---|---|---|---|---|
| 0x00 | 0xc8100580 | LDR_ACTIVE | 0 | hardware decoder only | leader mark window: max [28:16], min [12:0] |
| 0x04 | 0xc8100584 | LDR_IDLE | 1 | hardware only | leader space window: max [28:16], min [12:0] |
| 0x08 | 0xc8100588 | LDR_REPEAT | 2 | hardware only | repeat-leader space window: max [25:16], min [9:0] |
| 0x0c | 0xc810058c | BIT_0 | 3 | hardware only | "duration 0" window, max [25:16], min [9:0]: NEC/Sony logic 0, RC-6 half trailer bit |
| 0x10 | 0xc8100590 | REG0 | 4 | **base time** | clock override, filter, max frame time, base time |
| 0x14 | 0xc8100594 | FRAME | 5 | hardware only | decoded frame, low 32 bits. Reading clears "frame valid" |
| 0x18 | 0xc8100598 | STATUS | 6 | **bit 8 (level)** | "duration 1" window, interrupt status, input level, busy, decoder and frame status |
| 0x1c | 0xc810059c | REG1 | 7 | **width, enable, interrupt select, reset** | time base, width counter, enable, frame length, hold, checks, interrupt select, polarity, reset |
| 0x20 | 0xc81005a0 | REG2 | 8 | **mode** | repeat detection, bit order, read-clear, decode mode |
| 0x24 | 0xc81005a4 | DURATN2 | 9 | hardware only | "duration 2" window: RC-5/RC-6 half bit |
| 0x28 | 0xc81005a8 | DURATN3 | 10 | hardware only | "duration 3" window: RC-5/RC-6 whole bit |
| 0x2c | 0xc81005ac | FRAME1 | 11 | — | decoded frame, high 32 bits |
| 0x30 | 0xc81005b0 | STATUS_1 | — | — | frame-to-frame counter as it was at the last frame [19:0] |
| 0x34 | 0xc81005b4 | STATUS_2 | — | — | frame-to-frame counter, running [19:0] |
| 0x38 | 0xc81005b8 | REG3 | — | — | maximum repeat time [19:0] |
| 0x3c | 0xc81005bc | (named FRAME_RSV0 in the vendor list) | — | — | not documented: **UNVERIFIED** |

Four sources agree on these addresses:
- the datasheet;
- Amlogic's secure_apb.h, which puts the MF block at AO word 0x160 + n, that is 0x580 + 4n, and continues with FRAME_RSV1 at 0x5c0, outside the window;
- the vendor driver's register indices;
- Linux's offsets 0x00 to 0x2c.

The S905X datasheet gives the same addresses. Every field resets to 0 (datasheet).

### 1.3 Bit fields

**REG0 (offset 0x10)**

| Bits | Meaning | Raw mode |
|---|---|---|
| 31 | Override the block's internal clock gating "just in case": 1 keeps the clock running | 0; Linux never sets it |
| 30:28 | Input filter. Larger values monitor the input longer; 0 = no filtering. Unit **UNVERIFIED** | Linux leaves it as found. The vendor driver uses 7 for hardware NEC and 3 for its software modes |
| 27:25 | unused | |
| 24:12 | Maximum time of one whole frame, in ticks | hardware only; 0 |
| 11:0 | Base time N: tick = (N + 1) × the time base (1 µs by default, see REG1) | **9 = 10 µs** (Linux raw). 19 = 20 µs (Linux and vendor hardware modes) |

Linux masks the base field as 12 bits and Hardkernel's 3.14 driver as 11. The values used are small, so the difference does not matter.

**STATUS (offset 0x18)**

| Bits | Access | Meaning |
|---|---|---|
| 31 | R/W | FRAME1 holds a new frame; cleared by reading FRAME1 |
| 30 | R/W | Check logic-1 timing during hardware decoding |
| 29:20 | R/W | "Duration 1" max: NEC/Sony logic 1, RC-6 whole trailer bit |
| 19:10 | R/W | "Duration 1" min |
| 9 | R | Interrupt status. How it clears is **UNVERIFIED** |
| 8 | R | **Input level after synchronization.** The datasheet says it equals the pad level |
| 7 | R | Busy: the decoder state machine is active |
| 6:4 | R | Decoder status, for debugging the hardware modes: 0 ok, 1 last frame timed out, 2 leader error, 3 repeat error, 4 invalid bit |
| 3:0 | R | Frame status in the hardware modes: bit 3 frame valid (cleared by reading FRAME), bit 2 command and inverse differ, bit 1 address and inverse differ, bit 0 repeat |

STATUS mixes read-only status with R/W timing fields. Never write it whole in the hardware modes. In raw mode the driver never writes it.

**REG1 (offset 0x1c)**

| Bits | Meaning | Raw mode |
|---|---|---|
| 31 | Use a faster time base | 0 |
| 30 | Use the system clock in place of the 1 µs tick | 0 |
| 29 | Use the system clock in place of the "111 ns" tick | 0 |
| 28:16 | **Pulse width counter** (read-only): ticks between the two edges that last reset it. Bits 3:2 choose which edges those are | read in the handler |
| 15 | **Enable** the decoder | 1 |
| 14 | Time base = the system clock instead of the 1 µs tick (for slow clocks such as 32 kHz) | 0 |
| 13:8 | Frame length − 1, in bits (hardware modes) | 0 |
| 7 | Store frames even when an inverse check fails (hardware) | 0 |
| 6 | Hold the first frame until it is read (hardware) | 0 |
| 5:4 | Check command and/or address against its inverse (hardware) | 0 |
| 3:2 | **Interrupt select**, which also chooses the edges that reset the width counter: 00 = decoder done (one interrupt per decoded frame), 01 = rising or falling edge, 10 = falling edge, 11 = rising edge | **01** |
| 1 | Invert the input | 0 |
| 0 | Decoder reset. Linux and the vendor driver both set it and then clear it, so treat it as a level, not as self-clearing | pulse |

**REG2 (offset 0x20).** Linux says this register exists only on Meson8b and later.

| Bits | Meaning | Raw mode |
|---|---|---|
| 31:27 | unused | |
| 26, 25 | Force recording of low/high widths (hardware) | 0 |
| 24 | Detect repeats as leader plus stop bit (hardware) | 0 |
| 23:22 | unused | |
| 21:16 | Bit index for compare-bit repeat detection | 0 |
| 15 | Frame-to-frame counter unit: 0 = 100 µs, 1 = 10 µs | 0 |
| 14 | Frame-to-frame counter counts the system clock | 0 |
| 13 | Frame-to-frame counter on | 0 |
| 12 | Check the repeat time (with bit 11 or 10) | 0 |
| 11 | Detect repeats by comparing whole frames | 0 |
| 10 | Detect repeats by comparing one bit | 0 |
| 9 | 1 = reading FRAME/FRAME1 does not clear them | 0 |
| 8 | Bit order: 0 = the first bit received is the LSB, 1 = the MSB | 0 |
| 7:4 | unused | |
| 3:0 | **Decode mode.** 0 NEC; 1 skip leader; **2 general time measurement (software decoding)**; 3 Mitsubishi; 4 Thomson; 5 Toshiba; 6 Sony SIRC; 7 RC-5; 8 reserved; 9 RC-6; 0xA RCMM; 0xB Duokan; 0xC and 0xD reserved; 0xE Comcast; 0xF Sanyo | **2** |

The vendor remote driver repeats the same mode list. Linux names only 0 (NEC), 2 (raw), 9 (RC-6) and 0xE, which it calls XMP.

**The other registers:**
- LDR_ACTIVE and LDR_IDLE: max [28:16], min [12:0], 13 bits each.
- LDR_REPEAT, BIT_0, DURATN2 and DURATN3: max [25:16], min [9:0], 10 bits each.
- FRAME and FRAME1: 32-bit, read-only, cleared by a read unless REG2 bit 9 is set.
- STATUS_1, STATUS_2 and REG3: 20 bits, in units set by REG2 bits 15 and 14.

### 1.4 Raw mode versus hardware decoding on GXBB

To get raw edge timings on GXBB, write **REG2 bits 3:0 = 2** and **REG1 bits 3:2 = 01**, then set REG1 bit 15.

- The datasheet: in the hardware modes the decoder matches whole frames and stores them in FRAME/FRAME1. In "general mode" it detects each edge and records the time between edges.
- REG1 bits 3:2 choose the interrupt independently of the mode. 00 gives one interrupt per decoded frame (the hardware path); 01 gives one per edge. Hardkernel's 3.14 driver got edge interrupts with the mode left at 0.
- With a hardware mode and edge interrupts, the hardware decoder also runs and updates STATUS and FRAME. That is harmless for raw use but pointless; set mode 2.
- On Meson6 (the legacy decoder) the mode is REG1 bits 8:7, with 2 = general. On GXBB those bits belong to the frame-length and "record at error" fields.

### 1.5 Time base and counter width

- **The decoder counts a 1 µs tick by default**, with REG1 bit 14 and bits 29 to 31 all 0. The base time field multiplies it: tick = (REG0[11:0] + 1) µs.
  - The datasheet's REG0 example puts this as a 1 MHz "sys_clk" with N = 19 giving 20 µs.
  - Linux and the vendor driver both give 20 µs for N = 19 (0x13).
- Whether the 1 µs tick comes from the crystal or the AO clock is **UNVERIFIED**. It is right while the system runs: Linux decodes correctly assuming 10 µs per count.
- Linux raw mode uses **N = 9: a 10 µs tick**. Linux's hardware NEC path and all the vendor modes use N = 19: 20 µs.
- **The counter is REG1[28:16], 13 bits: at most 8191 ticks = 81.91 ms at 10 µs** (163.8 ms at 20 µs). Whether it saturates or wraps beyond that is **UNVERIFIED**. Linux does nothing about it and relies on its software timeout.
- A 10 µs resolution is ample. The shortest legitimate interval among the protocols below is RC-6's 444 µs, about 44 ticks.

### 1.6 What an edge interrupt reports

**Duration.** REG1[28:16] × tick is the interval between this edge and the previous one. With interrupt select 01, every edge restarts the measurement.
- The value must be captured at the edge rather than a live counter. Linux reads it in its handler as the length of the interval that just ended, and decodes correctly. The datasheet says the field "stores" the counter.
- So "captured, not live" is inferred from Linux working, not stated anywhere: **UNVERIFIED** by test. Test: read REG1 twice, 200 µs apart, in the handler. Equal values mean the value is captured.

**Level.** STATUS bit 8 is the synchronized input, read after the edge.
- Linux treats bit 8 = 1 as "the interval reported was a mark".
- This fits an active-low receiver with no inversion: when a mark ends, the line has just gone high.

**The inversion bit and STATUS bit 8.**
- Hardkernel's driver sets REG1 bit 1 (invert) and treats bit 8 = 1 as "a mark starts now".
- That is right only if bit 8 shows the input *after* the inverter.
- With mainline's setting (bit 1 = 0) the question does not come up. Whether bit 8 is taken before or after the inverter is **UNVERIFIED**, so keep bit 1 = 0.

**No stale-data or overflow flag** is documented for raw mode. STATUS bits 3:0 and 6:4 belong to the hardware decoder.
- **Late handler.** If the handler runs after the *next* edge, it sees that edge's level and duration, and the interval before it is lost. Detect it: two events in a row with the same level mean an edge was missed. The tightest budget is RC-6's 444 µs between edges.
- **Edge between the two reads.** An edge that falls between the STATUS read and the REG1 read pairs an old level with a new duration. This is rare, and the same check catches it.
- **Zero widths.** Amlogic's vendor driver notes that the width sometimes reads 0, and substitutes a plausible value in its software decoder. Treat 0 as invalid.
- **No meaningful duration** for the first event after enabling, or after any gap longer than 81.91 ms. Use `system_time()`: if more than about 80 ms passed since the previous interrupt, the interval was a long space whatever the counter says.

### 1.7 Interrupt generation and acknowledge

- The interrupt source is chosen by REG1 bits 3:2 (1.3). With 01 there are two interrupts per mark, one at each end.
- The DT says edge, rising. Configure the GIC as edge-triggered. The note in m27 (section 1.4) about pulse outputs and level-sensitive GIC lines applies here too.
- **No acknowledge write.**
  - Linux reads STATUS and REG1 and returns.
  - Amlogic's vendor driver only schedules a deferred handler, which reads.
  - STATUS bit 9 ("interrupt status", read-only) exists, but whether a read of STATUS or REG1 clears it, or whether the line is a pulse needing nothing, is **UNVERIFIED**.
  - An edge-configured GIC with these two reads per interrupt works in either case.
- For the hardware modes, the vendor driver reads STATUS and FRAME on resume, with a note that this is what lets IR interrupts through again. So an unread frame may block further frame interrupts in those modes.
- **Rates:**
  - An NEC frame gives 67 edges in about 68 ms, at most one per 562 µs.
  - RC-6 gives at most one per 444 µs.

### 1.8 Glitch filter

- REG0 bits 30:28: 0 = off, larger = longer monitoring. The unit is not documented (**UNVERIFIED**). The datasheet's GPIO interrupt filter uses steps of 3 × 111 ns, but nothing ties this filter to it.
- Linux raw mode leaves it as found. Linux's hardware NEC path sets 7. Amlogic's vendor driver uses 7 for its hardware NEC mode, and 3 for its software modes and most of the other hardware modes.
- Software rejection is needed anyway (trap 9). Most of Linux's decoders ignore any interval shorter than half the protocol's unit: 222 µs for RC-6 up to 444 µs for RC-5.

### 1.9 The hardware NEC decoder (for reference)

The plan does not need it, but it can decode the ODROID remote with no CPU work and one interrupt per frame. Two known configurations, both at a 20 µs tick:

| Field | Linux v7.3 (S4 only) | Amlogic vendor driver, GXBB, hardware NEC |
|---|---|---|
| LDR_ACTIVE max/min | 500/400 ticks (10.0/8.0 ms) | same |
| LDR_IDLE max/min | 300/200 (6.0/4.0 ms) | same |
| LDR_REPEAT max/min | 150/80 (3.0/1.6 ms) | same |
| BIT_0 max/min | 72/40 (1.44/0.80 ms) | same |
| STATUS "duration 1" max/min | 134/90 (2.68/1.80 ms), logic-1 check on | 134/90 |
| REG0 | filter 7, max frame 4000 (80 ms), base 19 | same (0x7FA00013) |
| REG1 | frame length 31 (32 bits), interrupt select 00 | 0x9F00: enable, 32 bits, interrupt select 00 |
| REG2 | mode 0, LSB first, no repeat compare | 0 |

The NEC bit windows (1.125 ms and 2.25 ms nominal) span the mark and the space together. So in this mode the hardware times whole bit periods, not marks.

On each frame:
- STATUS bit 3 = frame valid, bit 0 = repeat code.
- FRAME holds the 32 bits with the first one received in bit 0 (LSB-first mode). Byte 0 is the address, byte 1 its inverse, byte 2 the command, byte 3 its inverse.
- For the ODROID remote's Power key, FRAME reads 0x23DC4DB2 (section 6).

Linux enables this path only for S4, and only for NEC. On GXBB it is **UNVERIFIED** in Linux, although it is what the vendor driver used on this same block.

### 1.10 The legacy decoder at 0xc8100480

- Eight registers at 0x480 to 0x49c (vendor list), in the Meson6 layout, with the mode in REG1 bits 8:7.
- Amlogic's vendor driver runs it alongside the MF decoder in its "NEC + RCA" and "NEC + Toshiba" modes, at a 20 µs tick, from the same interrupt handler.
- Mainline Linux never touches it on GXBB.
- If firmware or a previous OS left it enabled, it might add interrupts on SPI 196 (**UNVERIFIED**). A driver can read 0xc810049c at start-up and log bit 15 (enable). The register is outside the DT's `reg` window, but in the same 4 KB page.

## 2. Pin, GPIO and board

### 2.1 Pin mux

AO_RTI_PIN_MUX_REG is at **0xc8100014**: the datasheet's register list; word 5 of the AO bus in Amlogic's list; the "mux" region of the DT's AO pin controller (offset 0x14). Bits that matter here:

| Bit | Function | Pad |
|---|---|---|
| **0** | **REMOTE_INPUT** (Linux group "remote_input_ao") | **GPIOAO_7** |
| 21 | REMOTE_OUTPUT (IR blaster output) | GPIOAO_7. Must be 0 |
| 31 | REMOTE_OUTPUT | GPIOAO_13 |
| 12, 11 | UART_AO_A TX, RX: **the serial console** | GPIOAO_0, GPIOAO_1 |
| others | UART_AO_B, AO I2C, AO PWM, I2S, SPDIF, CEC | other AO pads |

- GPIOAO_7 has only these two functions, REMOTE_INPUT (bit 0) and REMOTE_OUTPUT (bit 21). Sources:
  - the datasheet's tables II.7.8 and III.23.8;
  - Linux's GXBB AO group list, which has bit 0 and no group for bit 21;
  - Hardkernel's 3.14 driver, which sets bit 0 in this register itself.
- When Linux selects a group, it clears the other groups it knows on the same pad. It knows no group for bit 21, so it never clears that bit; the reset value is 0.
- **Use read-modify-write only.** Bits 11 and 12 carry the console.
- Whether the decoder sees the pad with bit 0 clear is **UNVERIFIED**. For the EE GPIO interrupt block, the datasheet says the input comes straight from the pad, but nothing says so for the IR decoder. Set bit 0.

### 2.2 AO GPIO registers for GPIOAO_7

These are not in the S905 datasheet. Linux v7.3 and U-Boot v2026.10 describe the AO bank identically, using the DT's regions ("mux" at 0x14, "pull" at 0x2c, "gpio" at 0x24 to 0x2b). Amlogic's register list gives the names.

| Register | Address | Bits for GPIOAO_n | GPIOAO_7 |
|---|---|---|---|
| AO_GPIO_O_EN_N | 0xc8100024 | n: direction (1 = input); 16+n: output level | bit 7; bit 23 |
| AO_GPIO_I | 0xc8100028 | n: input level | **bit 7** |
| AO_RTI_PULL_UP_REG | 0xc810002c | n: pull enable; 16+n: 1 = up, 0 = down | bit 7; bit 23 |

- The ball table gives GPIOAO_7 a pull-up by default. The C2 DT disables the bias for "remote_input_ao", so Linux clears bit 7 of 0xc810002c.
- The port's `meson_gpio` driver covers only the X, Y and DV banks of the periphs pin controller, not the AO bank.
- Whether AO_GPIO_I bit 7 reads the pad while it is muxed to REMOTE_INPUT is **UNVERIFIED**. It is normal for Meson pads, and STATUS bit 8 gives the same level from the IR block.

### 2.3 Board: the receiver

- Schematic sheet 22, "Remote Control Receiver":
  - U9, part "ROM-WT138lM(N)", is a three-pin receiver module: pin 1 output, pin 2 ground, pin 3 supply.
  - The supply comes from VDDIO_AO3V3 through R52 (470 Ω), with C29 (10 µF) to ground.
  - The output goes straight to net IR_IN.
- Sheet 4 connects IR_IN to GPIOAO.BIT7. There is no series resistor, pull-up or inverter on the board. The DT's line name for GPIOAO_7 is "IR In".
- **The output is active-low, idle high: implied, not stated.** The schematic gives no polarity, and no datasheet for the module was found. Two drivers point the same way:
  - Linux mainline decodes with no inversion and "level high after the edge = the interval was a mark", which works only for an active-low receiver;
  - Hardkernel's C2 DT sets "pulse-inverted" for the same reason.

  The module's behaviour is still **UNVERIFIED** until read on the board: with no IR present, STATUS bit 8 and AO_GPIO_I bit 7 should read 1.
- The module's carrier frequency is **UNVERIFIED**. Remotes at 36 or 40 kHz generally still work with a 38 kHz module at shorter range; this was not measured.

### 2.4 Fallback: timing the pin without the IR block

The EE GPIO interrupt controller (m27, section 1) can watch GPIOAO_7 as input number 7 (AO_n = n).
- It has no both-edges mode on GXBB, so use two channels on input 7, one rising and one falling, and timestamp each interrupt with `system_time()`.
- That block samples the pad directly, so the REMOTE_INPUT mux can stay set.

Polling AO_GPIO_I instead would need at least 20 kHz sampling to time 444 µs intervals to about 10 %. That is not practical.

## 3. Clocks, reset, power

| Gate or reset | Where | Reset value | Linux |
|---|---|---|---|
| IR decoder clock | AO_RTI_GEN_CNTL_REG0 (0xc8100040) bit 0 | 1 (on) | the AO clock controller's "remote" gate, marked "ignore unused", so Linux never turns it off. The IR node does not reference it |
| IR module reset | same register, bit 16 ("set to 1 to reset") | 0 | the AO reset line "remote" is bit 16. The IR node does not reference it, and Linux never uses it |
| AO general clock, AO register logic | HHI_GCLK_AO (0xc883c154) bits 2 and 3 | 1 | gates marked "ignore unused" |
| Clock gating inside the block | REG0 bit 31 | 0 | not used |

- The block is in the AO power domain and runs from power-on; there is no power domain to switch on. Linux does no clock or reset handling for it.
- The C2's serial console works in the same domain, so the AO clocks are running.
- **Do not pulse AO_RTI_GEN_CNTL_REG0 bit 16.** That register also holds the clock gates and resets of the AO UARTs and I2C. Use REG1 bit 0 to reset the decoder.
- The datasheet spells this register "AO_RTI_GEN_CTNL_REG0" (sic). Amlogic's list calls it AO_RTI_GEN_CNTL_REG0, AO word 0x10 = 0xc8100040.
- **State left over.** Linux's shutdown hook reprograms the block for the firmware's use after the OS stops (0 (c) step 3). That implies the block keeps its settings beyond the OS. Whether it also survives this port's reboot path (a PSCI reset) is **UNVERIFIED**. Reprogramming everything at start-up makes the question irrelevant.

## 4. Interrupt

| Item | Value | Source |
|---|---|---|
| DT | `interrupts = <0 196 1>`: SPI 196, edge rising | the board's DTB; Linux meson-gx.dtsi |
| GIC INTID | 196 + 32 = **228** | DT + 32. The datasheet's GIC table lists 228 as ao_ir_dec_irq; that half of its table agrees with DT + 32 (as m27 found) |
| Trigger | edge. The GIC itself knows only rising edge or high level | DT, Linux |
| Vendor 3.14 DT | `<0 196 1>` too | Hardkernel meson64_odroidc2.dts |
| Neighbours | 225 ao_uart (console), 226 ao_i2c_s, 227 ao_i2c_m, 229 ao_uart2, 230 ao_ir_blaster, 231 ao_cec, 232 and 233 ao_gpio | datasheet |

GICv2 registers for INTID 228, for debugging (the distributor is at 0xc4301000):

| What | Register | Address | Bits |
|---|---|---|---|
| Enable | GICD_ISENABLER7 | 0xc430111c | bit 4 |
| Trigger type | GICD_ICFGR14 | 0xc4301c38 | bits 9:8; bit 9 = 1 for edge |
| Priority | GICD_IPRIORITYR byte | 0xc43014e4 | byte |
| Target CPU | GICD_ITARGETSR byte | 0xc43018e4 | byte |

The port sets the trigger with `arch_int_configure_io_interrupt(228, B_EDGE_TRIGGERED, …)`, as `meson_uart` and `meson_vdec` do.

## 5. IR protocols

### 5.0 Common facts

- After the receiver, the driver sees only marks and spaces; the carrier is gone. All durations below are nominal lengths of marks and spaces at the receiver output.
- **Receiver skew.** Receiver modules lengthen marks and shorten spaces somewhat. IRremote's JVC capture listing shows marks of about 550 µs and spaces of about 500 µs where both are nominally 526 µs. Measure the C2's module before fixing tolerances; the amount is **UNVERIFIED**.
- **Linux's tolerances**, given in µs in each section below. Most intervals must be within ± half a unit of nominal; the NEC header mark is allowed ± 2 units. End-of-frame spaces must be at least nominal minus half a unit. Most decoders also reject any interval shorter than half a unit. This is one working implementation, not a standard.
- Linux feeds every interval to every enabled decoder; each keeps its own state. Protocol windows overlap (5.7), so a Haiku decoder should do the same.

### 5.1 NEC

| Item | Value | Sources |
|---|---|---|
| Carrier | 38 kHz (IRP: 38.4 kHz) | sbprojects, IRP |
| Unit | 562.5 µs (datasheet), 560 (sbprojects), 563 (Linux), 564 (IRP) | |
| Header | 9 ms mark (16 units), 4.5 ms space (8 units) | all |
| Bits | pulse distance: a 1-unit mark, then a space of 1 unit for 0 or 3 units (1.6875 ms) for 1. Bit periods 1.125 and 2.25 ms | all |
| Order | LSB first, byte by byte: address, inverted address, command, inverted command | all |
| Trailer | one 1-unit mark after the 32nd bit, needed to time the last bit | all |
| Frame length | 67.5 ms to the end of the last bit, plus the trailing mark. Constant for standard NEC | datasheet, sbprojects |
| Key held | not the frame again, but a **repeat code**: 9 ms mark, 2.25 ms space, 1-unit mark (11.8 ms in all) | all |
| Period | 108 ms start to start (datasheet, IRP); 110 ms (sbprojects; also Linux's key-up figure) | |
| Toggle | none | |

Variants:
- **Extended NEC.** The second byte is not the inverse of the first, so the address is 16 bits, first byte low, and the frame length varies.
  - The 256 values whose high byte is the exact inverse of the low byte are not valid extended addresses: they are standard NEC (sbprojects).
  - Linux reports it as a 24-bit scancode: address << 16 | second byte << 8 | command.
- **NEC32** (Linux's name; Apple and TiVo remotes): the command is not followed by its inverse, so Linux keeps all 32 bits.
- **Linux's classification order:** the command check first (failure → NEC32), then the address check (failure → extended), otherwise standard NEC with scancode = address << 8 | command.
- **NECx** (IRP's name, Samsung style): the header mark is 8 units (4.5 ms) instead of 16. Linux accepts it too.

Linux's windows: header mark 7882–10134; header space 3941–5067; repeat-code space 1971–2533; bit mark 282–844; space for 1: 1408–1970; space for 0: 282–844; trailing mark 282–844. A following space over 5349 completes a frame or a repeat code.

### 5.2 RC-5 (Philips)

| Item | Value | Sources |
|---|---|---|
| Carrier | 36 kHz | sbprojects, IRP |
| Unit (half a bit) | 889 µs (32 carrier periods); a bit is 1.778 ms | sbprojects, Linux, IRP |
| Header | none: two start bits instead | |
| Bits | bi-phase (Manchester): 0 = mark then space, 1 = space then mark | sbprojects, IRP, Linux |
| Frame | S1 (1), S2 (1), toggle T, 5 address bits, 6 command bits: 14 bits, MSB first, 24.9 ms | sbprojects, IRP |
| Intervals | every mark and space is 1 or 2 half-bits (889 or 1778 µs) | |
| First edge | S1's first half is a space, so the receiver's first edge comes half a bit into the frame | sbprojects |
| Key held | the whole frame again, with the same toggle | |
| Period | 114 ms (sbprojects, IRP, Linux's key-up figure). IRMP gives an 88.9 ms pause, which agrees (24.9 + 88.9) | |
| Toggle | T flips at each new key press | |

**RC-5X (extended RC-5, 7-bit command).** S2 becomes command bit 6, inverted: S2 = 1 means bit 6 = 0. Commands 0 to 63 therefore stay compatible (sbprojects). Linux applies this to every 14-bit RC-5 frame, adding 64 to the command when S2 is 0.

Naming caution: Linux's and IRP's "RC5x" is a different, 20-bit variant, with a 4-unit space after the first 8 bits and 6 extra bits at the end. The port's ir_remote.h means the 7-bit kind.

Linux's windows: each half bit 445–1333; longer intervals are split by subtracting one half bit at a time. A space over 4890 ends the frame.

### 5.3 RC-6 mode 0 (Philips)

| Item | Value | Sources |
|---|---|---|
| Carrier | 36 kHz | sbprojects, IRP |
| Unit t | 444 µs (16 carrier periods) | sbprojects, Linux, IRP |
| Leader | 6t mark (2.666 ms), 2t space (0.889 ms) | all |
| Bits | Manchester, **the opposite of RC-5**: 1 = mark then space, 0 = space then mark. A normal bit is 2t (t + t) | sbprojects, IRP, Linux |
| Header field | start bit (always 1), mode bits mb2..mb0 (000 for mode 0), then the trailer bit TR | sbprojects |
| Trailer bit | double length (2t + 2t). Its value is the **toggle** | sbprojects, Linux, IRP |
| Data | 8-bit control field (address), then 8-bit information field (command), MSB first | sbprojects, Linux, IRP |
| Frame | 52t = 23.1 ms: leader 8t, start bit 2t, mode 6t, trailer 4t, data 32t | computed |
| End | 6t (2.666 ms) of signal-free time | sbprojects |
| Intervals | marks and spaces of 1t, 2t and 3t (3t next to the trailer bit), plus the 6t/2t leader | IRMP lists 444, 889 and 1333 µs |
| Key held | the whole frame again, with the same toggle | |
| Period | 107 ms start to start (IRP). **UNVERIFIED**: IRMP uses a 45 ms pause between frames, Linux's key-up figure is 114 ms, and sbprojects gives none | |
| Toggle | TR flips at each new key press | |

Linux's windows: leader mark 2220–3108; leader space 666–1110; normal half bit 222–666; trailer half bit 666–1110. Mode 0 ends after 16 data bits. Linux's scancode is address << 8 | command.

### 5.4 Sony SIRC

| Item | Value | Sources |
|---|---|---|
| Carrier | 40 kHz | sbprojects, IRP |
| Unit | 600 µs | sbprojects, Linux, IRP |
| Header | 2.4 ms mark (4 units), 0.6 ms space | all |
| Bits | **pulse width**: 1 = 1.2 ms mark, 0 = 0.6 ms mark; every mark is followed by a 0.6 ms space | all |
| Order | LSB first: the 7-bit command first, then the address | sbprojects, Linux, IRP |
| 12-bit | 7 command + 5 address | |
| 15-bit | 7 command + 8 address | |
| 20-bit | 7 command + 5 address + 8 extended bits | |
| Trailer | none: the frame ends with the last bit's mark | |
| Frame length | 12-bit 17.4–24.6 ms; 15-bit 21–30 ms; 20-bit 27–39 ms (header and bits; computed) | |
| Telling 12, 15 and 20 apart | count bits, then wait for silence. sbprojects says at least 10 ms; Linux ends the frame at a space over 5700 µs | |
| Key held | the whole frame again | |
| Period | 45 ms start to start (sbprojects, IRP); Linux's key-up figure is 100 ms. IRMP: Sony remotes send each frame at least 3 times, with a 25 ms pause | |
| Toggle | none | |

Linux's windows: header mark 2100–2700; header space 300–900; mark for 1: 900–1500; mark for 0: 300–900; bit space over 300.

### 5.5 JVC

The sources agree on the shape of the signal and disagree on details:

| Item | sbprojects | Linux v7.3 | IrpTransmogrifier (DecodeIR) | IRremote master (MIT) | IRremote 2.8.0 | IRMP |
|---|---|---|---|---|---|---|
| Carrier | 38 kHz | — | 37.9 kHz | 38 kHz | 38 kHz | — |
| Unit | 526 µs | 525 | 527 | 526 | — | — |
| Header | 8.4 ms mark, 4.2 ms space | 16/8 units = 8.4/4.2 ms | 16/8 units | 16/8 units | 8.4/4.2 ms | **9.0/4.5 ms** (NEC's) |
| Bit 0 | 526 mark + 526 space (1.05 ms) | 1 + 1 unit | 1 + 1 | 1 + 1 | 600 + 550 µs | 560 + 560 µs |
| Bit 1 | 526 mark + 1574 space (2.10 ms) | 1 + 3 units | 1 + 3 | 1 + 3 | 600 + 1600 µs | 560 + 1690 µs |
| Fields | 8-bit address, 8-bit command | 8 + 8 | 8 + 8 | 8 + 8 | 16 bits, not split | **4-bit address, 12-bit command** |
| Bit order | **LSB first** | LSB first in each byte | LSB first | **LSB first** | **MSB first**, kept for backward compatibility | LSB first |
| Trailer | not mentioned in the text | 1-unit mark | 1-unit mark | 1 stop mark | 1 stop mark | stop bit |
| Key held | the frame again **without the header** | same | same | same | same | — |
| Period or gap | every 50 to 60 ms | needs a space over 18.1 ms after the trailer; key-up 125 ms | first frame 59.08 ms, then 46.42 ms (start to start) | 45 ms period measured on two JVC remotes; a gap of 45 units (23.6 ms) | 40 to 60 ms pause | 22 ms pause |
| Toggle | none: a missing header marks a repeat | Linux makes one up, flipped at each header | none | none | none | none |

What to take from this:
- **The signal:** an 8.4 ms mark and 4.2 ms space, 16 pulse-distance bits on a unit of about 526 µs, a trailing mark, and repeats without the header. Five of the six sources agree; IRMP's NEC-like header is the odd one out.
- **Bit order is a matter of labelling, not of the signal.** The 16 bits arrive in a fixed order. "LSB first" (sbprojects, Linux, IRP, current IRremote) puts the first bit received into bit 0 of the address. IRremote 2.x printed the 16 bits with the first one as the MSB, so codes published with it are bit-reversed relative to the others. For the JVC RM-RK52, record the raw 16 bits in arrival order and derive both readings. Only known address values can settle which reading is meant.
- **Repeat period:** 45 to 60 ms between frame starts in every measurement. The gap after a repeat frame's trailer is therefore roughly 12 to 40 ms, depending on the bits. Recognize a repeat as: 16 bits and a trailer, no header, the same bits as the previous frame, within about 100 ms of it. Linux requires the bits to be identical.
- **Lengths:** header 12.6 ms + bits 16.8–33.6 ms + trailer 0.53 ms gives a first frame of 29.9–46.7 ms. A repeat frame is 17.3–34.1 ms.

Linux's windows: header mark 8138–8662; header space 3938–4462; bit mark 263–787; space for 1: 1313–1837; space for 0: 263–787; trailing mark 263–787; end space over 18113.

### 5.6 Summary

| | NEC | RC-5 | RC-6 mode 0 | Sony SIRC | JVC |
|---|---|---|---|---|---|
| Carrier | 38 kHz | 36 kHz | 36 kHz | 40 kHz | 38 kHz |
| Header (mark/space) | 9.0 / 4.5 ms | none | 2.666 / 0.889 ms | 2.4 / 0.6 ms | 8.4 / 4.2 ms |
| Unit | 562.5 µs | 889 µs | 444 µs | 600 µs | 526 µs |
| Coding | pulse distance | Manchester, 0 = mark first | Manchester, 1 = mark first | pulse width | pulse distance |
| Bit order | LSB first | MSB first | MSB first | LSB first | LSB first (see 5.5) |
| Bits | 32 (8+8+8+8) | 14 (2 start, toggle, 5, 6) | header 1+3+1, then 16 data | 12, 15 or 20 | 16 (8+8) |
| Trailing mark | yes | no | no | no | yes |
| Key held | repeat code | whole frame | whole frame | whole frame | frame without header |
| Toggle | — | T bit | TR bit | — | — |
| Period | 108 ms | 114 ms | 107 ms (**UNVERIFIED**) | 45 ms | 45–60 ms |

### 5.7 Telling protocols apart

- **NEC and JVC headers overlap under loose windows.** Linux's NEC windows (mark 7.9–10.1 ms, space 3.9–5.1 ms) accept JVC's 8.4/4.2 ms header. Its JVC windows (mark 8.14–8.66, space 3.94–4.46) reject NEC's 9.0/4.5. Receiver skew makes this worse, so decide by structure: 32 bits means NEC; 16 bits followed by a trailing mark and a long space means JVC.
- **Sony and RC-6 overlap at the start.** Sony's header (2.1–2.7 ms) overlaps RC-6's leader (2.22–3.11 ms), and so do the spaces after them (Sony 0.3–0.9, RC-6 0.666–1.11). Running both decoders in parallel settles it within a few intervals: Sony's spaces stay at 600 µs, while RC-6 produces Manchester runs of 444, 889 and 1333 µs.
- A repeat (an NEC repeat code, a JVC frame without header) only means something shortly after a full frame from the same remote. Drop it otherwise.

## 6. The Hardkernel ODROID remote

- **Protocol: standard NEC.** Address **0xB2**, sent with its inverse 0x4D. Every command is sent with its inverse. A held key sends NEC repeat codes.
- **Linux keymap "rc-odroid"** (Christian Hewitt, 2019), the map the C2 DT names: 12 entries, NEC scancodes 0xB2nn (address << 8 | command).
- **Hardkernel's lircd.conf** (wiki):
  - timings: header 9000/4500 µs, one 563/1688, zero 563/564, trailing mark 563, repeat code 9000/2250, gap 100 ms;
  - 16 bits of "pre-data" 0x4DB2, then a 16-bit code per key.

  LIRC prints the bits in arrival order with the first bit as the MSB, so each byte appears bit-reversed: 0x4DB2 is address 0xB2 followed by its inverse 0x4D. Converted this way, all 12 LIRC codes match the Linux keymap, and every command's inverse checks.

| Key | Command | Inverse | Linux scancode | LIRC code | 32-bit word, first bit in bit 0 |
|---|---|---|---|---|---|
| Power | 0xDC | 0x23 | 0xB2DC | 0x3BC4 | 0x23DC4DB2 |
| Mute | 0x88 | 0x77 | 0xB288 | 0x11EE | 0x77884DB2 |
| Home | 0x82 | 0x7D | 0xB282 | 0x41BE | 0x7D824DB2 |
| Up | 0xCA | 0x35 | 0xB2CA | 0x53AC | 0x35CA4DB2 |
| Left | 0x99 | 0x66 | 0xB299 | 0x9966 | 0x66994DB2 |
| OK (LIRC calls it Enter) | 0xCE | 0x31 | 0xB2CE | 0x738C | 0x31CE4DB2 |
| Right | 0xC1 | 0x3E | 0xB2C1 | 0x837C | 0x3EC14DB2 |
| Down | 0xD2 | 0x2D | 0xB2D2 | 0x4BB4 | 0x2DD24DB2 |
| Menu | 0xC5 | 0x3A | 0xB2C5 | 0xA35C | 0x3AC54DB2 |
| Back | 0x9A | 0x65 | 0xB29A | 0x59A6 | 0x659A4DB2 |
| Volume down | 0x81 | 0x7E | 0xB281 | 0x817E | 0x7E814DB2 |
| Volume up | 0x80 | 0x7F | 0xB280 | 0x01FE | 0x7F804DB2 |

The last column is what FRAME would hold in hardware NEC mode (1.9). It is also what a software decoder gets if it shifts each received bit in at the top of a 32-bit word. Both sources list the same 12 buttons. Whether the remote has any others is not stated (**UNVERIFIED**; the ODROID remote wasn't available for testing).

## 7. Traps

1. **On GXBB the mode is in REG2.** Writing REG1 bits 8:7, the Meson6 way, does not select raw mode here. REG1 bits 3:2 must also be 01, or there is only one interrupt per decoded frame.
2. **Leftover configuration.** The block may keep its settings across a reboot (section 3), and Linux's shutdown deliberately leaves it enabled in hardware NEC mode at a 20 µs tick. Disable it first, then write whole registers (0 (a)). In particular REG1 bits 14 and 29 to 31 change the time base, and a read-modify-write like Linux's would keep whatever is there.
3. **The space that ends a frame arrives with the next frame.** End frames with a software timer restarted at each edge (0 (d)).
4. **The width counter has 13 bits:** 81.91 ms at 10 µs. Idle gaps and the first event after enabling are garbage; use `system_time()`.
5. **Missed edges.** The hardware keeps only the latest edge. Check that levels alternate, and treat the zero widths the vendor driver reports as invalid.
6. **Polarity.** The receiver is active-low; keep REG1 bit 1 = 0. STATUS bit 8 = 1 after an edge means a mark has just ended.
7. **Shared registers.** AO_RTI_PIN_MUX_REG holds the console's pins. AO_RTI_GEN_CNTL_REG0 holds other AO clock gates and resets. Use read-modify-write, and never pulse the AO reset bits.
8. **Configure the GIC for edges** on INTID 228, as the DT says.
9. **Noise.**
   - IR receivers emit bursts of short pulses in strong ambient infrared (sunlight, some lamps), and every edge is an interrupt.
   - Reject intervals below half the smallest unit (about 220 µs) and count them.
   - If more than a few hundred arrive in a short window, disable the block (REG1 bit 15) for a while.
   - The hardware filter (REG0 bits 30:28) helps, but its unit is unknown.
10. **Overlapping protocols** (5.7). Run the decoders in parallel, and tell NEC from JVC by the bit count.
11. **JVC bit order.** Libraries label it differently (5.5); store the raw bits.
12. **"RC5X"** means different things in different sources (5.2).
13. **The legacy decoder** at 0xc8100480 is a second, undocumented decoder on the same input (1.10).
14. **FDT match.** The C2 DT lists "amlogic,meson-gx-ir" first, and Linux matches "amlogic,meson-gxbb-ir". Accept either.
15. **Key-up timing, for comparison.** Linux releases a key after its timeout (125 ms) plus the protocol's repeat period has passed since the last frame or repeat. The repeat periods are NEC 110 ms, RC-5 and RC-6 114 ms, Sony 100 ms and JVC 125 ms.

## 8. Open items (UNVERIFIED) and how to settle them on the board

1. Is the width counter captured at the edge or live (1.6)? Read REG1 twice in the handler.
2. Beyond 8191 ticks, does the counter saturate or wrap (1.5)? Log raw values after gaps longer than 82 ms.
3. Is STATUS bit 8 taken before or after REG1 bit 1's inverter (1.6)? Toggle bit 1 with no IR present and read bit 8.
4. STATUS bit 9, and how the interrupt line deasserts (1.7).
5. The filter's unit (1.8). Try 0 and 3 with the remotes and compare edge counts.
6. The source of the 1 µs tick (1.5). Check measured durations against a known remote (NEC header: 9 ms).
7. Does the decoder need REMOTE_INPUT to see the pad (2.1)? Is AO_GPIO_I readable while the pad is muxed (2.2)?
8. Receiver polarity and carrier frequency (2.3). With no IR present, STATUS bit 8 should read 1.
9. The legacy decoder at 0x480, and whether it raises SPI 196 (1.10). Read 0xc810049c at boot.
10. What BL2, BL30 and U-Boot leave in the block at a cold boot (trap 2). Log REG0, REG1 and REG2 before reprogramming.
11. Whether the block's state survives this port's reboot (section 3).
12. What offset 0x3c (FRAME_RSV0) is.
13. RC-6's repeat period (5.3).
14. Whether the ODROID remote has more than 12 buttons (section 6).
15. The hardware NEC decoder on GXBB from this driver (1.9): used by the vendor driver, not tested here.

## Appendix A: downloads

Downloaded on 2026-10-31 for this research; not kept in this repository. Sizes are in bytes.

The two PDFs are byte-identical (same MD5) to the copies downloaded for M27. The datasheet text used here is the pdftotext output of that identical file.

| File | URL | Size |
|---|---|---|
| S905_Public_Datasheet_V1.1.4.pdf | https://web.archive.org/web/20240712025337id_/https://dn.odroid.com/S905/DataSheet/S905_Public_Datasheet_V1.1.4.pdf | 6543928 |
| odroid-c2_rev0.2_20160226.pdf | https://web.archive.org/web/20220616145209id_/https://dn.odroid.com/S905/Schematic/odroid-c2_rev0.2_20160226.pdf | 1653707 |
| linux-tags.json | https://api.github.com/repos/torvalds/linux/tags?per_page=10 (to pick the newest tag, v7.3) | 4661 |
| linux-v7.3-drivers_media_rc_meson-ir.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/meson-ir.c | 20107 |
| linux-v7.3-drivers_media_rc_keymaps_rc-odroid.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/keymaps/rc-odroid.c | 1112 |
| linux-v7.3-Documentation_devicetree_bindings_media_amlogic,meson6-ir.yaml | https://raw.githubusercontent.com/torvalds/linux/v7.3/Documentation/devicetree/bindings/media/amlogic,meson6-ir.yaml | 1031 |
| linux-v7.3-arch_arm64_boot_dts_amlogic_meson-gx.dtsi | https://raw.githubusercontent.com/torvalds/linux/v7.3/arch/arm64/boot/dts/amlogic/meson-gx.dtsi | 17113 |
| linux-v7.3-arch_arm64_boot_dts_amlogic_meson-gxbb.dtsi | https://raw.githubusercontent.com/torvalds/linux/v7.3/arch/arm64/boot/dts/amlogic/meson-gxbb.dtsi | 17641 |
| linux-v7.3-arch_arm64_boot_dts_amlogic_meson-gxbb-odroidc2.dts | https://raw.githubusercontent.com/torvalds/linux/v7.3/arch/arm64/boot/dts/amlogic/meson-gxbb-odroidc2.dts | 8894 |
| linux-v7.3-drivers_pinctrl_meson_pinctrl-meson-gxbb.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/pinctrl/meson/pinctrl-meson-gxbb.c | 26034 |
| linux-v7.3-drivers_pinctrl_meson_pinctrl-meson.h | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/pinctrl/meson/pinctrl-meson.h | 4730 |
| linux-v7.3-drivers_clk_meson_gxbb-aoclk.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/clk/meson/gxbb-aoclk.c | 7271 |
| linux-v7.3-drivers_clk_meson_gxbb.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/clk/meson/gxbb.c | 91666 |
| linux-v7.3-include_dt-bindings_clock_gxbb-aoclkc.h | https://raw.githubusercontent.com/torvalds/linux/v7.3/include/dt-bindings/clock/gxbb-aoclkc.h | 3090 |
| linux-v7.3-include_dt-bindings_reset_gxbb-aoclkc.h | https://raw.githubusercontent.com/torvalds/linux/v7.3/include/dt-bindings/reset/gxbb-aoclkc.h | 2866 |
| linux-v7.3-drivers_media_rc_ir-nec-decoder.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/ir-nec-decoder.c | 7037 |
| linux-v7.3-drivers_media_rc_ir-rc5-decoder.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/ir-rc5-decoder.c | 7897 |
| linux-v7.3-drivers_media_rc_ir-rc6-decoder.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/ir-rc6-decoder.c | 9689 |
| linux-v7.3-drivers_media_rc_ir-sony-decoder.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/ir-sony-decoder.c | 5921 |
| linux-v7.3-drivers_media_rc_ir-jvc-decoder.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/ir-jvc-decoder.c | 5518 |
| linux-v7.3-drivers_media_rc_rc-core-priv.h | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/rc-core-priv.h | 9226 |
| linux-v7.3-drivers_media_rc_rc-main.c | https://raw.githubusercontent.com/torvalds/linux/v7.3/drivers/media/rc/rc-main.c | 54657 |
| linux-v7.3-include_media_rc-core.h | https://raw.githubusercontent.com/torvalds/linux/v7.3/include/media/rc-core.h | 12273 |
| linux-v7.3-include_media_rc-map.h | https://raw.githubusercontent.com/torvalds/linux/v7.3/include/media/rc-map.h | 14835 |
| hk-linux-branches.json | https://api.github.com/repos/hardkernel/linux/branches?per_page=100 (to find the C2 branch name) | 14622 |
| hk-c2-3.14-amlogic-input.json | https://api.github.com/repos/hardkernel/linux/contents/drivers/amlogic/input?ref=odroidc2-3.14.y | 6442 |
| hk-c2-3.14-amlogic-input-remote.json | https://api.github.com/repos/hardkernel/linux/contents/drivers/amlogic/input/remote?ref=odroidc2-3.14.y | 5297 |
| hk-odroidc2-3.14.y-drivers_media_rc_meson-ir.c | https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/media/rc/meson-ir.c | 6293 |
| hk-odroidc2-3.14.y-arch_arm64_boot_dts_meson64_odroidc2.dts | https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/arch/arm64/boot/dts/meson64_odroidc2.dts | 23563 |
| hk-odroidc2-3.14.y-drivers_amlogic_input_remote_remote_main.h | https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/amlogic/input/remote/remote_main.h | 20401 |
| hk-odroidc2-3.14.y-drivers_amlogic_input_remote_remote_main.c | https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/amlogic/input/remote/remote_main.c | 26011 |
| hk-odroidc2-3.14.y-drivers_amlogic_input_remote_remote_func.c | https://raw.githubusercontent.com/hardkernel/linux/odroidc2-3.14.y/drivers/amlogic/input/remote/remote_func.c | 43676 |
| hk-uboot-odroidc2-v2015.01-secure_apb.h | https://raw.githubusercontent.com/hardkernel/u-boot/odroidc2-v2015.01/arch/arm/include/asm/arch-gxb/secure_apb.h | 189514 |
| wayback-20210302115508-wiki-c2-lirc_ubuntu16.04.html | https://web.archive.org/web/2019id_/https://wiki.odroid.com/odroid-c2/application_note/lirc/lirc_ubuntu16.04 (served the 20210302115508 snapshot) | 168733 |
| wayback-20220616141141-wiki-c2-lirc_ubuntu16.04.html | https://web.archive.org/web/2021id_/https://wiki.odroid.com/odroid-c2/application_note/lirc/lirc_ubuntu16.04 (served the 20220616141141 snapshot) | 194422 |
| sbprojects-nec.php.html | https://www.sbprojects.net/knowledge/ir/nec.php | 8530 |
| sbprojects-rc5.php.html | https://www.sbprojects.net/knowledge/ir/rc5.php | 12579 |
| sbprojects-rc6.php.html | https://www.sbprojects.net/knowledge/ir/rc6.php | 8880 |
| sbprojects-sirc.php.html | https://www.sbprojects.net/knowledge/ir/sirc.php | 10429 |
| sbprojects-jvc.php.html | https://www.sbprojects.net/knowledge/ir/jvc.php | 6441 |
| irptransmogrifier-master-IrpProtocols.xml | https://raw.githubusercontent.com/bengtmartensson/IrpTransmogrifier/master/src/main/resources/IrpProtocols.xml | 176508 |
| irremote-master-src_ir_JVC.hpp | https://raw.githubusercontent.com/Arduino-IRremote/Arduino-IRremote/master/src/ir_JVC.hpp | 10043 |
| irremote-2.8.0-src_ir_JVC.cpp | https://raw.githubusercontent.com/Arduino-IRremote/Arduino-IRremote/2.8.0/src/ir_JVC.cpp | 3271 |
| irmp-master-src_irmpprotocols.h | https://raw.githubusercontent.com/IRMP-org/IRMP/master/src/irmpprotocols.h | 104193 |

Requests that failed. Their error bodies were deleted and nothing from them is used:
- the same Hardkernel paths under a branch named "odroidc2-v3.14.y": 404;
- drivers/media/rc/keymaps/rc-odroid.c on odroidc2-3.14.y: 404;
- wiki.odroid.com fetched directly: 403;
- https://lirc-remotes.sourceforge.net/remotes-table.html, searched for JVC car-stereo remotes: 403.

Local sources, not downloaded:
- `u-boot/` (v2026.10): `drivers/pinctrl/meson/pinctrl-meson-gxbb.c`, `u-boot.dtb` (decompiled with `dtc`), `dts/upstream/src/arm64/amlogic/meson-gx.dtsi`.
- `haiku/` (MIT): `headers/private/drivers/ir_remote.h`, `src/add-ons/kernel/drivers/misc/meson_gpio/meson_gpio.cpp`, `headers/private/kernel/arch/int.h` (`arch_int_configure_io_interrupt`), and the drivers that call it (`meson_uart`, `meson_vdec`).
- `docs/m27-irq-uart-i2c-facts.md`.
