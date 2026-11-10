# Header I/O: facts for GPIO interrupts, UART A and I2C B

> **Status:** Research written before M27's header work. As built (`docs/m27-header-api.md`, patches 0149 to 0157): I2C B is attached as `/dev/bus/i2c/header` and used through `BI2CDevice`; both edges work with two channels on one pad. The datasheet's "invert RX" bit (CONTROL bit 25) does nothing on the S905.

Facts only, for MIT drivers on the ODROID-C2 (S905, GXBB). The sources are Linux mainline (GPIO interrupt multiplexer, GXBB pinctrl, Meson UART, Meson I2C, GXBB clocks, DT), mainline U-Boot (GPL), the Amlogic S905 public datasheet V1.1.4, the C2 schematic rev 0.2, and the port's own MIT code. No code, macros or comments were copied. Anything not confirmed by a source, or where sources disagree without a clear answer, is marked **UNVERIFIED**.

Conventions:
- Addresses are physical.
- CBUS = 0xc1100000. The datasheet gives CBUS registers as word offsets (address = 0xc1100000 + offset × 4). This document gives byte addresses.
- "HHI" is the HIU block at 0xc883c000, with byte offsets.
- GIC (GIC-400): distributor at 0xc4301000, CPU interface at 0xc4302000. **INTID = SPI + 32.** The port's FDT bus already adds 32 when it reads an `interrupts` property, so Haiku interrupt number = INTID.
- Pad mux and GPIO: the periphs pin controller at 0xc8834000. Mux registers PERIPHS_PIN_MUX_n are at 0xc88344b0 + 4n. GPIO words start at 0xc8834430, pull words at 0xc88344e8, pull-enable words at 0xc8834520.

## 0. What a minimal driver needs

### (a) Wait for a rising, falling or either edge on a header pin

1. **Pin.** Make the pad a GPIO input (direction bit = 1, see the port's `meson_gpio` bank table) and set its pull if needed. The interrupt block samples the pad itself, so the mux setting does not matter to it (1.8), but a GPIO input is the clean state.
2. **Choose a free channel c (0 to 7).** There are only 8 channels for the whole SoC. Write the pad's input number (1.3) into channel c's 8-bit selector:
   - channels 0 to 3: 0xc1109884, bits [8c+7 : 8c];
   - channels 4 to 7: 0xc1109888, bits [8(c−4)+7 : 8(c−4)].

   Input numbers: GPIOX_n = 106 + n, GPIOY_n = 89 + n, GPIODV_n = 59 + n. For example, header pin 7 = GPIOX_21 = 127.
3. **Set the trigger while the GIC line is disabled.** In 0xc1109880, set bit c = 1 (edge). Set bit 16+c = 0 for a rising edge or 1 for a falling edge. Optionally set a glitch filter in 0xc110988c bits [4c+2 : 4c]: 0 = off, and n = 1 to 7 requires the level to hold for about n × 3 × 125 ns (1.7).
4. **GIC.** Program INTID 96 + c as edge-triggered (the port calls `arch_int_configure_io_interrupt(irq, B_EDGE_TRIGGERED, …)`, as meson_vdec does). Clear any stale pending state, install the handler, then enable the line.
5. **Handler.** The controller has no status or clear register (1.6), so the handler just wakes the waiter. The kernel's GIC EOI is the only acknowledge.
6. **Either edge.** GXBB has no both-edges mode, and Linux refuses that type on GXBB (1.5). Use **two channels on the same pad**, one rising and one falling, or one channel whose polarity software flips after every edge (racy). On the open question in `docs/m27-header-api.md`: "any edge" costs two channels on the S905.
7. **Release.** Disable the GIC line. Park the selector at 255 ("no interrupt" in the datasheet) and clear bits c and 16+c.

### (b) 3.3 V serial port on header pins 8 (TX, GPIOX_12) and 10 (RX, GPIOX_13)

1. **Clock gate.** Set HHI_GCLK_MPEG0 (0xc883c140) bit 13 (UART0 = "UART A").
2. **Pins.** In PERIPHS_PIN_MUX_4 (0xc88344c0), set bit 13 (TX) and bit 12 (RX). Keep bits 17 and 16 clear: they select the SLIP UART on the same pads (datasheet only). Linux also enables pull-ups on both pins: for GPIOX_13, set bit 13 in the X bank's pull-enable word (0xc8834530) and pull word (0xc88344f8).
3. **Reset.** In CONTROL (0xc11084c8), set bits 22, 23 and 24 (TX reset, RX reset, clear error), then clear them.
4. **Line format, in CONTROL.**
   - [21:20] data length: 00 = 8, 01 = 7, 10 = 6, 11 = 5 bits.
   - bit 19: parity enable. Its reset value is 1, so clear it explicitly for "N".
   - bit 18: 1 = odd parity.
   - [17:16] stop bits: 00 = 1, 01 = 2.
   - bit 15 = 1: two-wire mode. The C2 has no RTS/CTS on the header.
5. **Baud rate.** First wait until TX is idle (STATUS bit 22 = 1 and bit 25 = 0). Then write REG5 (0xc11084d4) = bit 24 | bit 23 | (round(8 MHz / baud) − 1). The input is the 24 MHz crystal divided by 3.
   - 9600 to 230400 are within 0.8 %. 500k, 1M, 2M and 4M are exact.
   - **1.5 Mbit/s is +6.7 % off on this clock and will not work.** Two candidates, both **UNVERIFIED** on GXBB: clk81 / 4 with REG5 bit 24 = 0 (1.488 Mbit/s, −0.8 %), or the crystal undivided with REG5 bit 26 (exact). See 2.4.
6. **Enable.**
   - MISC (0xc11084d0): [7:0] = 1 (RX interrupt threshold) and [15:8] = 64 (TX threshold).
   - CONTROL: set bits 12 and 13 (TX and RX enable) and bit 27 (RX interrupt). Set bit 28 (TX interrupt) only while bytes are waiting to be sent.
7. **Interrupt.** GIC SPI 26 = **INTID 58**, rising edge.
   - On each interrupt: read bytes from RFIFO (0xc11084c4) while STATUS bit 20 (RX empty) is 0. Check STATUS bits 16, 17, 18 and 24 for each byte, and clear them by pulsing CONTROL bit 24.
   - Then write WFIFO (0xc11084c0) while STATUS bit 21 (TX full) is 0. Clear CONTROL bit 28 when nothing is left to send.
   - There is no interrupt status register to clear.

### (c) A program-level I2C transfer on bus B (pins 27 = SDA = GPIODV_26, 28 = SCL = GPIODV_27)

1. **The port's `meson_i2c` did not attach to I2C B then** (M27 attached it). Its `supports_device()` rejects FDT nodes whose status is not "okay", and the C2's I2C B node is "disabled". Every boot log so far shows only the controller at 0xc1108500 (I2C A, 85 boots). Everything else is ready:
   - the pin-mux table has an I2C B entry (set PERIPHS_PIN_MUX_7 bits 25 and 24, clear PERIPHS_PIN_MUX_2 bits 27 and 26);
   - the clock gate (HHI_GCLK_MPEG0 bit 9) is shared with I2C A and already on;
   - the divider code is generic.

   So a change of policy is all that is needed: either the DT node's status, or the driver accepting I2C B on request.
2. **Once attached,** the I2C bus manager publishes `/dev/bus/i2c/<n>/bus_raw`. A program opens it and calls `ioctl(fd, I2CEXEC, &exec, sizeof(exec))` with `i2c_ioctl_exec { op = I2C_OP_READ_STOP, addr = 7-bit address, cmdBuffer = &reg, cmdLength = 1, buffer, bufferLength }`. The driver then does: START, address + write, the register byte, repeated START, address + read, n bytes (the last one NACKed), STOP. That is exactly "write register address, then read".
   - `I2C_OP_WRITE_STOP` sends the command bytes followed by the data bytes as one write.
   - A NACK returns B_DEV_NOT_READY and a timeout returns B_TIMED_OUT.
   - The structure and opcodes are in `headers/private/i2c/i2c.h`, a private header (3.6).
3. **Board.** The schematic shows 1 kΩ pull-ups to VDD3V3 on I2CB.SDA and I2CB.SCL (R6, R7), so no internal pulls are needed.

## 1. GPIO interrupt controller (EE "GPIO interrupt", 8 channels)

### 1.1 Location and DT

| Item | Value | Source |
|---|---|---|
| Compatible | `amlogic,meson-gxbb-gpio-intc`, `amlogic,meson-gpio-intc` | C2 DTB, status "okay" |
| Registers | 0xc1109880, size 0x10 (CBUS words 0x2620 to 0x2623) | DT and datasheet 24.3 |
| Upstream interrupts | `amlogic,channel-interrupts` = SPI 64 to 71, so **INTID 96 to 103** (channel c → INTID 96 + c) | DT. This property has no trigger cells. Add 32 yourself, because the FDT bus does that only for `interrupts`. |
| `#interrupt-cells` | 2 (input number, Linux trigger type) | DT |
| Inputs | 133 (input numbers 0 to 132). 133 to 255 are "Undefined (no interrupt)". | Linux GXBB parameters, datasheet table III.23.11 |
| Channels | 8 | Linux and datasheet |

The datasheet's EE interrupt table lists gpio_irq[0..7] at "A53 GIC bit" 94 to 101. In that table's lower half, other entries are also 2 to 4 lower than DT + 32: uart0_irq 54 against 58, m_i2c_0 49 against 53. Its upper half agrees with DT + 32 (m_i2c_1 = 246, ao_uart = 225). Linux has used the DT numbers on the C2 for years (Ethernet PHY interrupt), so **trust the DT: INTID 96 to 103**.

### 1.2 Registers

| Address | Datasheet name | Bits | Meaning |
|---|---|---|---|
| 0xc1109880 | Edge and polarity (0x2620) | [7:0] | Edge select for channels 7..0. 1 = edge-triggered, 0 = level. |
| | | [15:8] | Unused, read-only on GXBB. On SM1 and later these are the both-edges bits; Linux's GXBB parameters have no both-edges support. |
| | | [23:16] | Polarity for channels 7..0. 1 = the pad signal is inverted before the filter and edge logic, which gives falling edge or active-low level. |
| | | [31:24] | Unused |
| 0xc1109884 | Pin select 0 to 3 (0x2621) | [7:0], [15:8], [23:16], [31:24] | 8-bit input number for channels 0, 1, 2, 3 |
| 0xc1109888 | Pin select 4 to 7 (0x2622) | same layout | Channels 4, 5, 6, 7 |
| 0xc110988c | Filter select (0x2623) | [4c+2 : 4c] for c = 0 to 7; bit 4c+3 unused | 0 = no filtering, 1 to 7 = filter length (1.7) |

All fields reset to 0. Linux writes only fields of the channel it configures, with a locked read-modify-write. It never writes the filter register.

### 1.3 Input numbers (GXBB "hwirq")

The datasheet table and Linux's GXBB bank table (first/last input number per bank) agree:

| Bank | Pads | Input numbers | Formula |
|---|---|---|---|
| GPIOAO | AO_0 to AO_13 | 0 to 13 | AO_n = n |
| GPIOZ | Z_0 to Z_15 | 14 to 29 | 14 + n |
| GPIOH | H_0 to H_3 | 30 to 33 | 30 + n |
| BOOT | BOOT_0 to BOOT_17 | 34 to 51 | 34 + n |
| CARD | CARD_0 to CARD_6 | 52 to 58 | 52 + n |
| **GPIODV** | DV_0 to DV_29 | **59 to 88** | 59 + n |
| **GPIOY** | Y_0 to Y_16 | **89 to 105** | 89 + n |
| **GPIOX** | X_0 to X_22 | **106 to 128** | 106 + n |
| GPIOCLK | CLK_0 to CLK_3 | 129 to 132 | 129 + n |

Cross-check: the C2 DT connects the PHY interrupt on GPIOZ_15 as input 29. The datasheet's prose example (boot_3 → 27) contradicts its own table, which gives 37. The example is stale; use the table.

Header pins (J2) and their input numbers:

| Pin | Pad | Input | Pin | Pad | Input |
|---|---|---|---|---|---|
| 3 | GPIODV_24 (I2C A) | 83 | 21 | GPIOX_4 | 110 |
| 5 | GPIODV_25 (I2C A) | 84 | 22 | GPIOX_3 | 109 |
| 7 | GPIOX_21 | 127 | 23 | GPIOX_2 | 108 |
| 8 | GPIOX_12 | 118 | 24 | GPIOX_1 | 107 |
| 10 | GPIOX_13 | 119 | 26 | GPIOY_14 | 103 |
| 11 | GPIOX_19 | 125 | 27 | GPIODV_26 | 85 |
| 12 | GPIOX_10 | 116 | 28 | GPIODV_27 | 86 |
| 13 | GPIOX_11 | 117 | 29 | GPIOX_0 | 106 |
| 15 | GPIOX_9 | 115 | 31 | GPIOY_8 | 97 |
| 16 | GPIOX_8 | 114 | 32 | GPIOY_13 | 102 |
| 18 | GPIOX_5 | 111 | 33 | GPIOX_6 | 112 |
| 19 | GPIOX_7 | 113 | 35 | GPIOY_3 | 92 |
| | | | 36 | GPIOY_7 | 96 |

Mainline Linux's Meson GPIO driver no longer turns GPIO lines into interrupts (pinctrl-meson.c has no gpio-to-irq hook). The bank table's numbers are only used by DT consumers that name `gpio_intc` and an input number directly.

### 1.4 Trigger semantics and the GIC side

From the datasheet, for each channel the path is: pad → 256:1 mux → polarity (optional inversion) → filter → edge/level select → GIC.

| Wanted | Edge bit c | Polarity bit 16+c | GIC config for INTID 96+c (Linux) |
|---|---|---|---|
| Rising edge | 1 | 0 | edge-triggered (rising) |
| Falling edge | 1 | 1 | edge-triggered (rising) |
| High level | 0 | 0 | level-sensitive (active high) |
| Low level | 0 | 1 | level-sensitive (active high) |

- **The GIC only sees active-high levels or rising edges.** Linux always gives the GIC "rising edge" for the two edge types and "level high" for the two level types. Polarity is done only in this block. The port's `arch_int_configure_io_interrupt()` likewise ignores polarity.
- **The 8 SPIs have no fixed trigger type.** The DT gives none, and Linux sets the GIC per request. GIC config bits for INTIDs 96 to 103 are in GICD_ICFGR6 (0xc4301c18). Channel c is field c, and its "edge" bit is bit 2c+1.
- **In edge mode the block's output is presumably a short pulse.** If the GIC is left level-sensitive, the pulse may be gone before the CPU reads IAR (the port's GICv2 comment describes this). Always switch the GIC to edge for edge modes. Pulse width is **UNVERIFIED**.
- **Level mode.** The output follows the (inverted, filtered) pad for as long as the pad is at the active level, so a level-high GIC line stays pending. Before EOI, the handler must either remove the cause or mask the GIC line, or it re-enters immediately.
- Linux changes the type only while the interrupt is masked.

### 1.5 Both edges

- GXBB uses Linux's Meson8 family parameters: 8 channels, edge bits at offset 0, polarity at offset 16, 8-bit selectors, **no both-edges support**. Asking for both edges on GXBB makes the type setter fail with an invalid-argument error, and Linux has no fallback for it in this driver. Both-edges support (bits 8 to 15 of register 0x00) starts with SM1 (S905X3). Linux notes that on S905X3 those bits work even though its datasheet names bits 24 to 31.
- **Option A, two channels:** select the same input number on two channels, one rising and one falling. The register layout allows it (each channel has its own selector), but it is **UNVERIFIED** on hardware. To tell which edge came first when both fire, read the pad's input bit.
- **Option B, one channel and software:** after each edge, read the pad level and set the polarity bit to catch the opposite edge. Then read the level again; if it already changed, report the missed edge yourself. Whether flipping the polarity bit can itself create a spurious edge inside the block is **UNVERIFIED**, so do it with the GIC line masked and clear the GIC pending bit afterwards.

### 1.6 Acknowledge

The block has only the four registers above: **no status, clear or mask register**. Linux's chip for it passes mask, unmask, EOI and affinity straight to the GIC and writes nothing to the block on an interrupt.

The datasheet's diagram also shows the old Amlogic interrupt controller's IRQ_IN2 status/clear (CBUS words 0x2618/0x2619) and mask (0x261a) registers after this block. Linux does not use them on GXBB, because the A53s use the GIC. Whether they need any setup is **UNVERIFIED**, but evidently they do not, since Linux works without them.

### 1.7 Filter (debounce)

- Datasheet section 23.3: with filtering on, the pad must stay high or low for 3n × 125 ns, n = 1 to 7. That is 375 ns to 2.625 µs.
- Datasheet register description: "7 × 3 × 111 ns" for the maximum value.
- 111 ns is a 9 MHz tick (a 27 MHz crystal divided by 3, as on older Amlogic chips), while 125 ns is the C2's 24 MHz crystal divided by 3. On the C2 the tick is very likely 125 ns, but which one applies is **UNVERIFIED**.
- Either way the filter only removes glitches shorter than about 2.6 µs. It is no use for debouncing mechanical contacts (milliseconds); do that in software.
- Linux never sets the filter.

### 1.8 Other facts

- **The input comes straight from the pad** (datasheet note), whatever the pad's mux selects. An output pin's own edges therefore trigger its interrupt, and so would a UART TX pad.
- **Power-on hazard (inferred from reset values, UNVERIFIED on hardware).** All selectors reset to 0 = GPIOAO_0, the console UART TX, which idles high, and every channel starts as level, active high. So all 8 channel outputs are probably asserted at power-on. Keep INTIDs 96 to 103 disabled in the GIC until a channel is configured, and park unused channels at input 255.
- **Linux does nothing to this block at probe on GXBB** (its init hook for this family is empty). Freeing an interrupt leaves the selector and the edge/polarity bits as they were, so firmware or earlier settings can persist.
- GXBB also has AO GPIO interrupts (ao_gpio_irq0/1, datasheet GIC bits 232/233, AO domain). Linux does not use them on GXBB. The EE block above already covers the AO pads as inputs 0 to 13.
- The block's 0x10 bytes share a 4 KiB page (0xc1109000) with the Meson timer at 0xc1109940.

### 1.9 Linux's sequence, in prose

1. **Probe:** map the 16 bytes, read the 8 SPI numbers from the DT, and create an interrupt domain of 133 inputs stacked on the GIC. No register writes.
2. **Allocate** (a consumer asks for input h with type t): take the lowest free channel. Write h into that channel's selector (read-modify-write under a spinlock). Allocate the channel's GIC SPI with "rising edge" or "level high" as described in 1.4.
3. **Set type** (masked): edge types set bit c and level types clear it. Falling and low set bit 16+c, rising and high clear it. Both-edges fails on GXBB. Then the GIC's type is set.
4. **Mask, unmask, EOI:** GIC only.
5. **Free:** release the GIC interrupt and mark the channel free.

## 2. UART A (EE UART0, "uart_A")

### 2.1 Location, DT, interrupt, FIFO

| Item | Value | Source |
|---|---|---|
| Node | `serial@84c0`, label `uart_A`, compatible `amlogic,meson-gx-uart`, **status "disabled"** on the C2 | C2 DTB |
| Registers | 0xc11084c0 to 0xc11084d7 (DT size 0x18). Datasheet: UART0, CBUS words 0x2130 to 0x2135. | DT, datasheet 41.4 |
| Interrupt | **GIC SPI 26, rising edge, INTID 58.** GICD_ICFGR3 (0xc4301c0c): field 10, edge bit 21. | DT (`<0 0x1a 1>`) |
| FIFO | **128 bytes** each way (DT `fifo-size = <128>`; datasheet table: UART0 128 bytes, the other UARTs 64). The register descriptions still say "64 bytes", which is boilerplate. | DT, datasheet 41.1 |
| Clocks | `xtal` = 24 MHz fixed clock, `pclk` = clkc CLKID_UART0 (26), `baud` = xtal | DTB `clocks = <xtal 0x03 0x1a xtal>` |
| Flow control | No `uart-has-rtscts` in the DT, so Linux always uses two-wire mode | DT, Linux |
| Aliases | The C2's `serial0` is the AO UART. uart_A has no serial alias, so Linux would give it a free line number from 6 up. | DT, Linux |

Linux's "meson-gx-uart" match has no G12A extras: no crystal ÷2 bit.

### 2.2 Registers

Offsets are from 0xc11084c0.

| Offset | Name | Use |
|---|---|---|
| 0x00 | WFIFO | Write a byte in bits [7:0] to queue it. Only write while TX is not full. |
| 0x04 | RFIFO | Read a byte from bits [7:0]. Linux masks the value to 8 bits. Check RX empty first. |
| 0x08 | CONTROL | Mode, enables, resets, interrupt enables (table below) |
| 0x0c | STATUS | Flags and FIFO counts (table below) |
| 0x10 | MISC | Interrupt thresholds, input filters, old baud extension |
| 0x14 | REG5 | New baud divider and clock select |

**CONTROL (0x08).** Bits marked "datasheet" are not used by Linux.

| Bit(s) | Meaning | Linux |
|---|---|---|
| 31 | Invert RTS (datasheet) | – |
| 30 | Mask errors (datasheet) | – |
| 29 | Invert CTS (datasheet) | – |
| 28 | TX interrupt enable: an interrupt when the TX FIFO count drops below MISC[15:8] | set while data is pending, cleared by stop-tx and shutdown |
| 27 | RX interrupt enable: an interrupt when the RX FIFO reaches MISC[7:0] bytes | set at startup, cleared at shutdown |
| 26 | Invert TX pin (datasheet) | – |
| 25 | Invert RX pin (datasheet) | – |
| 24 | Clear error. Clears STATUS bits 16 to 18. Does **not** clear itself: write 1, then 0. | pulsed |
| 23 | RX reset (state machine) | pulsed at probe |
| 22 | TX reset (state machine) | pulsed at probe |
| 21:20 | Character length: 00 = 8, 01 = 7, 10 = 6, 11 = 5 bits | set from termios |
| 19 | Parity enable (**reset value 1** per datasheet) | set or cleared from termios |
| 18 | Parity type: 0 = even, 1 = odd | from termios |
| 17:16 | Stop bits: 00 = 1, 01 = 2 | from termios |
| 15 | Two-wire mode: 1 = RX/TX only, CTS/RTS ignored; 0 = four-wire with hardware flow control | 1 unless hardware flow control is requested **and** the DT allows it |
| 14 | Unused | – |
| 13 | RX enable | set at startup, cleared by stop-rx and shutdown |
| 12 | TX enable | set at startup and by the console setup. Shutdown leaves it on. |
| 11:0 | Old baud divider (of clk81), reset value 0x120. Ignored when REG5 bit 23 = 1. | not touched |

The datasheet's description of bit 15 is cut off after "Two Wire mode:". The meaning above comes from Linux's use. With four-wire mode and CTS not muxed, what the transmitter sees on CTS is **UNVERIFIED**, so use two-wire mode.

**STATUS (0x0c, read-only).**

| Bit(s) | Meaning | Linux |
|---|---|---|
| 26 | RX state machine busy (datasheet) | – |
| 25 | TX state machine busy | "TX done" = bit 22 set **and** bit 25 clear |
| 24 | RX FIFO overflow (datasheet) | **not checked** |
| 23 | CTS level (datasheet) | – |
| 22 | TX FIFO empty | yes |
| 21 | TX FIFO full | yes |
| 20 | RX FIFO empty | yes |
| 19 | RX FIFO full (datasheet) | – |
| 18 | TX FIFO written while full | Linux counts it as an "overrun" |
| 17 | Frame error | yes. A frame error with a received byte of 0 is reported as a break. |
| 16 | Parity error | yes |
| 14:8 | TX FIFO count | – |
| 6:0 (datasheet says "8-0") | RX FIFO count | – |

The datasheet says to clear bits 16 to 18 by writing CONTROL bit 24. The two 7-bit count fields cannot show 128, so whether a full 128-byte FIFO reads as 0 is **UNVERIFIED**. Use the empty and full flags instead. Linux does not check bit 24 (the real RX overflow flag), and a driver should.

**MISC (0x10).** Datasheet defaults are given; Linux writes the whole register at startup.

| Bit(s) | Meaning |
|---|---|
| 31 | Keep clocks always on (disables automatic clock gating). Leave 0. |
| 30 | Use the old, buggy RX baud generator. Leave 0. |
| 29 | Async FIFO purge (RX DMA). Leave 0. |
| 28 | Async FIFO enable (RX bytes to DDR by DMA). Leave 0. |
| 27 | CTS filter timebase: 1 = 1 µs, 0 = "111 ns" (very likely 125 ns on a 24 MHz crystal, as in 1.7) |
| 26:24 | CTS filter: 0 = off, 7 = most |
| 23:20 | Old baud extension, bits [15:12] of the old divider. Only used without REG5 bit 23. |
| 19 | RX filter timebase: 1 = 1 µs, 0 = "111 ns" |
| 18:16 | RX filter: 0 = off, 7 = most. Datasheet: setting 1 = 375 ns to 2.6 µs, setting 2 = 3 to 21 µs. |
| 15:8 | TX interrupt threshold: an interrupt when the TX count drops below this (default 32). **Linux writes FIFO size / 2 = 64.** |
| 7:0 | RX interrupt threshold: an interrupt after this many bytes (default 15). **Linux writes 1.** |

Linux leaves the filters at 0. The datasheet describes no RX timeout interrupt. With an RX threshold above 1, the last few bytes of a burst stay in the FIFO until software polls. That is presumably why Linux uses 1, but **UNVERIFIED** that the hardware has no timeout.

**REG5 (0x14).**

| Bit(s) | Meaning | Source |
|---|---|---|
| 27 | Crystal ÷2 instead of ÷3 | Linux and U-Boot, **G12A and later only**. Not a GXBB bit (the GXBB datasheet lists bits 31 to 25 as unused). |
| 26 | Crystal undivided ("÷1") instead of ÷3 | U-Boot defines it, but neither U-Boot nor Linux uses it. **UNVERIFIED on GXBB.** |
| 24 | Baud clock from the crystal pad rather than clk81 | datasheet, Linux, U-Boot |
| 23 | Use the new 23-bit divider in [22:0] | datasheet, Linux, U-Boot |
| 22:0 | New baud divider N (datasheet default 15) | datasheet |

### 2.3 Interrupt behaviour

- There is no interrupt status or acknowledge register in the UART. Linux's handler reads STATUS:
  - if RX is not empty, it runs the receive loop;
  - then, if TX is not full **and** the TX interrupt is enabled, it refills the TX FIFO.
- The line is edge-triggered in the DT. The datasheet describes the two interrupt enables as firing "whenever a byte is written to the RX FIFO" and "whenever a byte is read from the TX FIFO", subject to the MISC thresholds. Drain the RX FIFO completely in each interrupt.
- The exact shape of the UART's interrupt output is **UNVERIFIED**.

### 2.4 Baud rate generation on GXBB

- **Linux on GXBB:** the DT's `baud` clock is the crystal, so the input is 24 MHz. Because the match has no ÷2 option, Linux uses crystal ÷ 3 = **8 MHz**:
  - N = round(8 000 000 / baud) − 1;
  - REG5 = bit 24 | bit 23 | N;
  - baud = 8 MHz / (N + 1);
  - accepted range 50 to 4 000 000.
- U-Boot sets the AO console the same way (115200: N = 68, so REG5 = 0x01800044).
- Linux has a branch for a non-crystal baud clock: N = round(clk / 4 / baud) − 1 with bit 24 clear, that is clk81 / 4 / (N + 1). It is only reached when the DT's baud clock is not 24 MHz, so it is **UNVERIFIED on GXBB**.
- On the C2, clk81 = **166 666 666 Hz** (the port's `meson_i2c` computes this from HHI_MPEG_CLK_CNTL and logs it on every boot). clk81 / 4 = 41.667 MHz.
- **Self-test without extra hardware (suggestion):** fill the TX FIFO with 128 bytes and time until STATUS shows TX empty and not busy. At 10 bits per byte, the elapsed time gives the real baud rate to about 1 %. This can settle the clk81 ÷ 4 and REG5 bit 26 questions.

| Baud | Crystal ÷ 3 (Linux, verified): N, error | clk81 ÷ 4 (**UNVERIFIED**): N, error | Crystal ÷ 1, bit 26 (**UNVERIFIED**): N, error |
|---|---|---|---|
| 9600 | 832 (0x340), +0.04 % | 4339, +0.01 % | 2499, 0 |
| 19200 | 416 (0x1a0), −0.08 % | 2169, +0.01 % | 1249, 0 |
| 38400 | 207 (0xcf), +0.16 % | 1084, +0.01 % | 624, 0 |
| 57600 | 138 (0x8a), −0.08 % | 722, +0.05 % | 416, −0.08 % |
| 115200 | 68 (0x44), +0.64 % | 361, −0.09 % | 207, +0.16 % |
| 230400 | 34 (0x22), −0.79 % | 180, −0.09 % | 103, +0.16 % |
| 460800 | 16, +2.1 % (marginal) | 89, +0.47 % | 51, +0.16 % |
| 500000 | 15, 0 | 82, +0.40 % | 47, 0 |
| 921600 | 8, −3.5 % (bad) | 44, +0.47 % | 25, +0.16 % |
| 1000000 | 7, 0 | 41, −0.79 % | 23, 0 |
| **1500000** | 4, **+6.7 % (unusable)** | 27, −0.79 % | 15, 0 |
| 2000000 | 3, 0 | 20, −0.79 % | 11, 0 |
| 3000000 | 2, −11 % (unusable) | 13, −0.79 % | 7, 0 |
| 4000000 | 1, 0 | 9, +4.2 % | 5, 0 |

The datasheet allows 1 bit/s to 8 Mbit/s, with a minimum high or low time of 60 ns on TX and RX.

### 2.5 Clock gate and resets

- `pclk` = CLKID_UART0 = **HHI_GCLK_MPEG0 (0xc883c140) bit 13**. The datasheet's EE clock-gating table also says "PERIPHS 13 UART0". Its parent is clk81.
- Linux marks all GXBB EE gates "ignore unused", so it never turns this gate off even when the UART is unused. Whether firmware leaves it on is **UNVERIFIED**: read 0xc883c140 on the board, or just set the bit.
- I2C A/B/C share bit 9 of the same register.
- The datasheet's gating table gives its base as "0xc8834400 + offset × 4". That is wrong for HHI; Linux and the port use 0xc883c000 + 0x50 × 4.
- Optional reset line, not used by Linux's UART driver: the GXBB reset ID for UART0 is 201, which is register 6 bit 9 of the reset controller at 0xc1104404 (so 0xc110441c bit 9). The datasheet's "RESET6 … PERIPHS: UART 0, bit 9" agrees, although its address text is garbled.

### 2.6 Pins

| Header pin | Pad | Function | Mux bit to set | Others on the same pad (keep clear) |
|---|---|---|---|---|
| 8 | GPIOX_12 | UART A TX | PERIPHS_PIN_MUX_4 (0xc88344c0) **bit 13** | SLIP_UART_TX = REG4 bit 17 (datasheet only; Linux lists no other group) |
| 10 | GPIOX_13 | UART A RX | PERIPHS_PIN_MUX_4 **bit 12** | SLIP_UART_RX = REG4 bit 16 (datasheet only) |
| – | GPIOX_14 | UART A CTS | REG4 bit 11 | **not on the header** |
| – | GPIOX_15 | UART A RTS | REG4 bit 10 | **not on the header** |

- **Matches the port's `meson_gpio` table:** pin 8 = mux word 4 bit 13, pin 10 = word 4 bit 12. Linux's GXBB pinctrl and the datasheet's GPIOX mux table agree.
- The C2 DT's `gpio-line-names` names GPIOX_12/13 "J2 Header Pin8/Pin10" and leaves GPIOX_14/15 unnamed, so RTS/CTS are not on the C2's header.
- Linux's `uart_a` pin group uses a pull-up on TX and RX. CTS/RTS would use no pull.
- The datasheet's pad table gives GPIOX_12 to 14 a default pull-up and 3 mA drive.
- X bank registers, bit n = GPIOX_n:

  | Purpose | Word | Address |
  |---|---|---|
  | Direction | 12 | 0xc8834460 |
  | Output | 13 | 0xc8834464 |
  | Input | 14 | 0xc8834468 |
  | Pull | 4 | 0xc88344f8 |
  | Pull enable | 4 | 0xc8834530 |

- Logic level: the earlier GeekPort facts (Hardkernel's wiki) say every J2 signal is 3.3 V except the analog inputs. The schematic text does not show which rail feeds VDDIO_X (**UNVERIFIED** from the schematic itself).

### 2.7 Linux's sequences, in prose

- **Probe:** enable the `pclk`, `xtal` and `baud` clocks. Take the baud clock rate as the UART clock (24 MHz). Read `fifo-size` (128; the default is 64). Reset the port: set the TX reset, RX reset and clear-error bits, then clear them.
- **Startup:**
  1. Pulse clear-error.
  2. Set RX enable and TX enable (one write).
  3. Set the RX and TX interrupt enables (next write).
  4. Write MISC = RX threshold 1 and TX threshold FIFO size / 2.
  5. Request the interrupt.
- **Set termios** (under the port lock):
  1. Read-modify-write CONTROL: data length, parity enable, odd/even, stop bits (1 or 2), two-wire mode.
  2. Pick a baud rate between 50 and 4 000 000.
  3. Wait until TX is empty and not busy.
  4. Write REG5 as in 2.4.

  Parity and frame errors are reported only if input parity checking is on, and ignored if "ignore parity" is set. The TX-written-while-full flag is always reported as an overrun.
- **Start TX:**
  1. While TX is not full, write the pending XON/XOFF character if any, otherwise the next byte from the software buffer.
  2. If bytes remain, set the TX interrupt enable.

  Stop TX clears the TX interrupt enable.
- **Receive loop**, for each byte while RX is not empty:
  1. Read STATUS.
  2. If any of bits 16 to 18 is set, count the error, pulse clear-error, and mark the byte (frame or parity).
  3. Read RFIFO.
  4. A frame error together with a 0 byte is a break.
- **Stop RX:** clear RX enable.
- **Shutdown:** free the interrupt, then clear RX enable and both interrupt enables. TX stays enabled, for a console.
- **Modem lines:** none. Setting them does nothing, and reading them always reports CTS asserted.
- **Polled console write:** save CONTROL, clear both interrupt enables, write each byte after waiting for TX not full, then restore CONTROL.

## 3. I2C B

### 3.1 Location, DT, interrupt, clock, pins

| Item | Value | Source |
|---|---|---|
| Node | `i2c@87c0`, label `i2c_B`, compatible `amlogic,meson-gxbb-i2c`, **status "disabled"**, no child nodes | C2 DTB |
| Registers | 0xc11087c0 to 0xc11087df (DT size 0x20; CBUS words 0x21f0 to 0x21f7) | DT |
| Interrupt | **GIC SPI 214, rising edge, INTID 246.** The datasheet's "m_i2c_1_irq = 246" agrees, so I2C B is the datasheet's "I2C Master 1". GICD_ICFGR15: field 6, edge bit 13. | DT, datasheet |
| Clock | CLKID_I2C (22) = **HHI_GCLK_MPEG0 bit 9**, shared by I2C A, B and C. Parent clk81 = 166.67 MHz. | DT, Linux gxbb.c, datasheet ("I2C Master / I2C SLAVE", bit 9) |
| Pins | Header 27 = GPIODV_26 = SDA: PERIPHS_PIN_MUX_7 (0xc88344cc) **bit 24**. Header 28 = GPIODV_27 = SCL: REG7 **bit 25**. | Linux pinctrl, datasheet DV mux table, port |
| Shared pads | UART B CTS on GPIODV_26 = PERIPHS_PIN_MUX_2 (0xc88344b8) bit 27. UART B RTS on GPIODV_27 = REG2 bit 26. Datasheet only: DVIN_CLK (REG0 bit 10), TCON_CPH (REG5 bit 10), DVIN_DE (REG0 bit 9), TCON_OEH (REG5 bit 9), TCON_VCOM (REG5 bit 8). | Linux, datasheet |
| Pull-ups | 1 kΩ to VDD3V3 on I2CB.SDA and I2CB.SCL (schematic R6, R7; I2C A has R2, R3, also 1 kΩ). The pads' own default is Z (no pull), with 4 mA (DV_26) or 3 mA (DV_27) drive. | schematic rev 0.2 text, datasheet pad table |
| Optional reset | Reset ID 143 = reset register 4 bit 15 (0xc1104414), "I2C_Master 1". Not used by Linux's I2C driver. | U-Boot reset ID header, datasheet |

### 3.2 Registers (datasheet chapter 40, Linux)

Offsets are from the controller base.

| Offset | Name | Fields |
|---|---|---|
| 0x00 | CONTROL | bit 0 START: the token list runs when this goes 0 → 1. Writing 0 while running aborts and sends STOP. bit 1 ACK_IGNORE: 1 = do not check ACKs. **Must be 0 for NACK detection**; reset value 0. bit 2 STATUS (read-only): 1 = list running. bit 3 ERROR (read-only): the device NACKed a written byte (the address byte included). Cleared one cycle after START is set. On an error the controller **sends STOP by itself**. [7:4] current token (read-only, useful to locate an error). [11:8] number of bytes read (read-only). [21:12] clock divider, low 10 bits. bit 22 manual mode. bit 23 manual SCL level. bit 24 manual SDA level. bit 25 SCL readback. bit 26 SDA readback. [29:28] clock divider bits [11:10]. bit 31 clocks always on. |
| 0x04 | SLAVE_ADDR | [7:0] 7-bit address shifted left by 1, bit 0 = 0 (the hardware sets the direction bit). [10:8] SDA filter. [13:11] SCL filter (0 = off, 7 = most). [27:16] SCL low time. bit 28 use the SCL low time field. |
| 0x08, 0x0c | TOKEN_LIST0, TOKEN_LIST1 | 16 tokens of 4 bits each. Token 1 is in bits [3:0] of LIST0, token 9 in bits [3:0] of LIST1. |
| 0x10, 0x14 | WDATA0, WDATA1 | Up to 8 bytes to write. First byte in WDATA0 [7:0], fifth in WDATA1 [7:0]. |
| 0x18, 0x1c | RDATA0, RDATA1 | Up to 8 bytes read, same byte order. (The datasheet misprints RDATA1's word offset as equal to RDATA0's.) |

Tokens:

| Value | Token |
|---|---|
| 0 | END |
| 1 | START |
| 2 | address + write |
| 3 | address + read |
| 4 | DATA (one byte, direction from the last address token) |
| 5 | DATA-LAST (a read byte that is NACKed, so the last one) |
| 6 | STOP |

The datasheet also mentions 10-bit addressing (address byte 0xF0 | A9:A8); Linux does not implement it. It says nothing about clock stretching by the slave (**UNVERIFIED**).

### 3.3 Clock divider

- **Datasheet / "Meson6" mode (SLAVE_ADDR bit 28 = 0):** CONTROL [29:28,21:12] is a quarter of the SCL period in clk81 cycles. Linux's Meson6 formula subtracts a fixed 15-cycle filter delay: divider = ceil((ceil(clk / f) − 15) / 4). That makes the period about 4 × divider + 15 cycles, with roughly 50 % duty.
- **Linux on GXBB ("high/low" mode, SLAVE_ADDR bit 28 = 1):**
  - for f ≤ 100 kHz: H = ceil(ceil(clk / f) / 2) − 15 and L = ceil(ceil(clk / f) / 4);
  - for faster buses: H = ceil(2 × clk / (5 f)) − 15 and L = ceil(3 × clk / (10 f)), which gives 40 % high and 60 % low;
  - H goes in CONTROL [21:12] with H's bits 11:10 in [29:28], L in SLAVE_ADDR [27:16], and bit 28 is set.

  From this arithmetic, the high time is about H + 15 cycles and the low time about 2 × L cycles (inferred, **UNVERIFIED**). At 100 kHz from 166.67 MHz this gives H = 819 and L = 417.
- Linux also clears both input filters at probe.
- **Why it matters at 400 kHz:** in Meson6 mode the duty is about 50 %, so the low time is 1.25 µs. That is below Fast-mode's 1.3 µs minimum, which is why Linux uses high/low mode on GXBB.

### 3.4 The port's `meson_i2c.cpp` (MIT): what it does today

- **Attaches only to enabled nodes.** `supports_device()` requires FDT bus, compatible `amlogic,meson-gxbb-i2c` (or meson-gx / meson6), and status "okay" (or "ok", or no status). The FDT bus also registers disabled nodes, and the driver turns those away. **I2C B is therefore not attached** on the C2 (85 of 85 logged boots show only 0xc1108500). `Init()` also accepts only the I2C A, B and C bases and refuses the AO controller.
- **Pin muxing, by itself** (there is no pinctrl driver). For I2C B it clears PERIPHS_PIN_MUX_2 bits 27 and 26 (UART B CTS/RTS) and sets PERIPHS_PIN_MUX_7 bits 25 and 24. For I2C A it clears REG2 bits 29 and 28 and sets REG7 bits 27 and 26. It does not clear the datasheet-only DVIN/TCON bits; they are 0 at reset.
- **Clock:** sets HHI_GCLK_MPEG0 bit 9, and computes clk81 from HHI_MPEG_CLK_CNTL (sources 0, 2, 5, 6 and 7 known; 166 666 666 Hz on the C2).
- **Speed:** a fixed **100 kHz** in Meson6 mode. Divider = ceil(clk81 / (4 × 100 kHz)) = 417. The log says 99 920 Hz; with Linux's 15-cycle filter delay the real rate is about 99.0 kHz.
- **SLAVE_ADDR:** each message writes the whole register with only the address. That clears the filters and the high/low mode bit, so the controller stays in Meson6 mode.
- **Transfers are polled:**
  - Each run writes the token lists, writes START 0 then 1, and polls STATUS (bit 2): busy-wait for 200 µs, then snooze 50 µs per check, with a **10 ms** timeout per list. Afterwards it clears START.
  - ERROR (bit 3) maps to B_DEV_NOT_READY, and the driver then sends an extra STOP list. That extra STOP is harmless, since the controller already sent one.
  - A known quirk: the first list after muxing never finishes on the C2. Setup runs a dummy STOP list that times out, and the PCF8563 probe retries up to 3 times.
- **Messages:** a START, the address token, then data in 8-byte chunks. In a read, DATA-LAST is used only for the message's final byte. STOP comes only after the last chunk when asked for.
- **ExecCommand:**
  - *write* ops send command + data as one write message;
  - *read* ops send the command as a write message without STOP, then a read message that starts with START. That START is a **repeated start**, the same as Linux;
  - block ops return B_NOT_SUPPORTED;
  - a recursive lock serialises everything;
  - acquire/release-bus are the same lock.
- **Scan:** only probes a PCF8563 at 0x51 and registers it as `nxp,pcf8563`.

### 3.5 Linux's `i2c-meson.c` on repeated start and NACK, and what a user-level WriteRead needs

**Linux's per-message steps:**
1. Set ACK_IGNORE only if the message asks to ignore NACKs; otherwise clear it.
2. Unless the message says "no start": put the address (shifted left by 1) into SLAVE_ADDR [7:0] and queue START plus the address token for the message's direction.
3. Queue up to 8 data tokens. The last token of a chunk is DATA-LAST only for the final byte of a read message. Load WDATA for writes.
4. Queue STOP only if this is the transfer's **last** message and the chunk ends it.
5. Write both token lists and raise START.
6. In the interrupt (or by polling STATUS, in atomic mode): clear START and read CONTROL.
   - If ERROR is set: no ACK (Linux returns "no such device"). The controller has already sent STOP, so stop there.
   - Otherwise copy RDATA for reads. If bytes remain, build the next chunk's list (no START or address) and raise START again.
7. After the message, clear START, which aborts and sends STOP if a timeout left the list running. The timeout is 500 ms per message.

A write message followed by a read message therefore puts a repeated START between them, with no STOP. Linux checks ACKs by default: an address NACK or a data-write NACK sets ERROR. NACKs on read bytes are the master's own (DATA-LAST).

**Gaps in the port's driver for user-level use:**
1. I2C B must be attached (3.4). Options:
   - set the DT node's status to "okay" before boot (for example with U-Boot's `fdt set` on the control FDT before `bootefi`; whether `bootefi` passes the change through is **UNVERIFIED**);
   - or have the driver accept the I2C B node when asked (a setting).

   Pins 27/28 then leave GPIO use: `meson_gpio` already refuses them while REG7 bits 24/25 are set.
2. ACK_IGNORE (CONTROL bit 1) is never cleared explicitly. It is 0 at reset, and U-Boot never sets it (and does not touch I2C B), so this works today. Clearing it at setup would make NACK detection independent of the firmware.
3. A read with 0 data bytes produces an address + read with no data, then STOP. Avoid it from userland, or reject it.
4. The fixed 100 kHz is fine. 400 kHz would need high/low mode (3.3).
5. Each ioctl takes and releases the bus lock. An op without STOP (`I2C_OP_READ` / `I2C_OP_WRITE`) leaves the bus without a STOP between ioctls, and another caller could start in between. Userland should use the `_STOP` ops.

### 3.6 The I2C bus manager: what userland can use

- For each controller that attaches, `bus_managers/i2c` takes an ID from the device manager's "i2c/path_id" generator and **publishes `bus/i2c/<id>/bus_raw`**, which is `/dev/bus/i2c/<id>/bus_raw`.
  - Today only I2C A attaches, so it is presumably ID 0. I2C B would get the next free ID; which one depends on probe order (**UNVERIFIED**).
- **The raw device supports one ioctl, `I2CEXEC`** (B_DEVICE_OP_CODES_END + 1), taking `i2c_ioctl_exec`: `{ i2c_op op; i2c_addr addr (uint16); const void* cmdBuffer; size_t cmdLength; void* buffer; size_t bufferLength; }`.
  - The kernel copies both buffers in from userland (checking user addresses) and uses stack buffers up to 32 bytes, heap beyond.
  - It locks the bus, calls the controller's exec, unlocks, and copies `buffer` back.
  - It returns the controller's status.
  - `read()` and `write()` on the node fail.
  - `I2C_EXEC_MAX_BUFFER_LENGTH` (32) is defined but **not enforced**.
- **Ops:** READ = 0, READ_STOP = 1, WRITE = 2, WRITE_STOP = 3, READ_BLOCK = 5, WRITE_BLOCK = 7. Bit 1 = write, bit 0 = stop, bit 2 = block. `meson_i2c` refuses the block ops.
- **Header:** `headers/private/i2c/i2c.h`, which is private and pulls in ACPI.h, device_manager.h and KernelExport.h. Usable from in-tree programs; not a public API.
- **Existing tool:** `src/bin/i2c` (`i2c <path-to-bus_raw>`) is in the arm64 image. It scans addresses 0 to 127 with READ_STOP, using a 1-byte command 0x00 and a 1-byte read. **Each probe writes a register-pointer byte 0 to every device that answers**, which can upset devices where a write has side effects.
- Devices registered by a controller's scan (the PCF8563) get their own driver nodes. The raw node does not stop a program from addressing them as well, but the bus lock serialises the transfers.

## 4. Open items (UNVERIFIED)

1. GPIO interrupts:
   - the pulse shape and width of a channel's output in edge mode;
   - whether two channels can select the same pad, needed for "either edge" (1.5 A);
   - whether flipping the polarity bit makes a spurious edge (1.5 B).
2. Filter tick: 125 ns (24 MHz ÷ 3) or 111 ns, as the datasheet says in two places (1.7). The same applies to the UART filter timebase.
3. Power-on state of the GPIO interrupt channels: probably all asserted, because they reset to level-high on GPIOAO_0 (1.8).
4. UART baud: clk81 ÷ 4 with REG5 bit 24 = 0, and crystal ÷ 1 with REG5 bit 26, on GXBB. Both are needed for an exact 1.5 Mbit/s, and the timing self-test in 2.4 can settle them.
5. UART:
   - what CTS reads in four-wire mode when GPIOX_14 is not muxed;
   - whether a full 128-byte FIFO shows in the 7-bit count fields;
   - whether there is no RX timeout interrupt;
   - whether firmware leaves HHI_GCLK_MPEG0 bit 13 on.
6. I2C:
   - slave clock stretching;
   - the exact high/low-mode timing (3.3);
   - which bus ID I2C B gets;
   - whether a U-Boot `fdt set` survives `bootefi`.
7. Datasheet errors found:
   - the lower half of the EE interrupt table is off by 2 to 4 from the DT;
   - the GPIO interrupt example (boot_3 = 27) contradicts its own table;
   - the clock-gating table base address is wrong;
   - the reset register addresses are garbled;
   - RDATA1's word offset is misprinted;
   - the "64-byte FIFO" text appears in UART0's register descriptions.

## Sources

Downloaded on 2026-10-31 for this research; not kept in this repository. Sizes are in bytes. Linux files are master, fetched raw:

| URL | Size |
|---|---|
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/irqchip/irq-meson-gpio.c | 20472 |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/pinctrl/meson/pinctrl-meson-gxbb.c | 26034 |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/pinctrl/meson/pinctrl-meson.h | 4730 |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/pinctrl/meson/pinctrl-meson.c | 18968 |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/tty/serial/meson_uart.c | 21586 |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/meson/gxbb.c | 91666 |
| https://raw.githubusercontent.com/torvalds/linux/master/drivers/i2c/busses/i2c-meson.c | 14746 |
| https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxbb.dtsi | 17634 |
| https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gx.dtsi | 17113 |
| https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/amlogic/meson-gxbb-odroidc2.dts | 8894 |
| https://raw.githubusercontent.com/torvalds/linux/master/include/dt-bindings/gpio/meson-gxbb-gpio.h | 2972 |
| https://raw.githubusercontent.com/torvalds/linux/master/include/dt-bindings/clock/gxbb-clkc.h | 5944 |
| https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/interrupt-controller/amlogic,meson-gpio-intc.yaml | 2908 |
| https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/serial/amlogic,meson-uart.yaml | 3015 |
| https://web.archive.org/web/20240712025337id_/https://dn.odroid.com/S905/DataSheet/S905_Public_Datasheet_V1.1.4.pdf (S905 datasheet V1.1.4. Used: 22 clock-gating table, 23.1 pin-mux tables, 23.3 GPIO interrupt, 24 interrupt table and GPIO interrupt registers, 40 I2C master, 41 UART, reset registers. Text extracted with pdftotext, layout and raw modes.) | 6543928 |
| https://web.archive.org/web/20220616145209id_/https://dn.odroid.com/S905/Schematic/odroid-c2_rev0.2_20160226.pdf (C2 schematic rev 0.2. Used: J2 header sheet, I2C pull-ups R2/R3/R6/R7.) | 1653707 |

`drivers/clk/meson/gxbb.h` returned 404: it has been merged into gxbb.c.

Local sources:
- `u-boot/`: `drivers/serial/serial_meson.c` (REG5 bits 23/24/26/27, ÷3 baud), `drivers/i2c/meson_i2c.c` (ACK_IGNORE never set, divider), `dts/upstream/include/dt-bindings/reset/amlogic,meson-gxbb-reset.h` (reset IDs 143, 196, 201), and `dts/upstream/src/arm64/amlogic/meson-gxbb-odroidc2.dtb` decompiled with `dtc -I dtb -O dts` (node status, interrupts, clocks, labels, pinctrl regs, gpio-line-names).
- `haiku/` (MIT):
  - `src/add-ons/kernel/busses/i2c/meson/meson_i2c.cpp`
  - `src/add-ons/kernel/bus_managers/i2c/{bus_raw.cpp,I2CBus.cpp,I2CModule.cpp,I2CPrivate.h}`
  - `headers/private/i2c/i2c.h`
  - `src/bin/i2c/i2c.cpp`
  - `src/add-ons/kernel/drivers/misc/meson_gpio/meson_gpio.cpp` and `headers/private/drivers/meson_gpio.h`
  - `src/system/kernel/arch/arm/arch_int_gicv2.cpp` and `src/system/kernel/arch/arm64/arch_int.cpp` (trigger configuration)
  - `src/system/kernel/arch/arm64/arch_uart_meson.cpp`
  - `src/add-ons/kernel/bus_managers/fdt/fdt_module.cpp` (SPI + 32)
  - `build/jam/images/definitions/regular` and `build/jam/packages/Haiku` (i2c tool, bus manager, meson_i2c in the arm64 image)
- the port's serial logs: `meson_i2c` lines with clk81 = 166666666 Hz, only I2C A attached (before M27).
