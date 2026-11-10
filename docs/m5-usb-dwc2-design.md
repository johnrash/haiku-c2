# M5 design notes: DWC2 USB host (keyboard and mouse)

> **Status:** Research written 2026-10-28, before any USB code. Ethernet went first, and dwc2 was then built in M5 with split transactions as planned here. The channel handling changed later: two lanes (M6), separate bulk lanes (M80), and isochronous transfers (M12, M20, M45).

Research for M5, written 2026-10-28 during the unattended M4.5 run. See `m6-ethernet-design.md`.

## Sources and licences
**Usable:**
- **FreeBSD `sys/dev/usb/controller/dwc_otg.c`, `dwc_otg.h`, `dwc_otgreg.h`** (BSD-2-Clause): slave/PIO mode, with a complete split-transaction state machine.
- **Plan 9 `sys/src/9/bcm/usbdwc.c`** (MIT, Plan 9 Foundation): buffer-DMA mode, about 1000 lines, written for keyboard, mouse, Ethernet and flash drives. This is the best model for a small driver.
- **NetBSD glue** (BSD-2): `meson_usbphy.c` (the GXBB USB2 PHY sequence), `mesongxbb_clkc.c` (clock gates), `meson_resets.c`, `mesongxbb_pinctrl.c` and `dwc2_fdt.c` (Amlogic parameters).
- **Linux device trees** for addresses (dual GPL/MIT).

**Avoid:**
- NetBSD `sys/external/bsd/dwc2/dist/*`, and OpenBSD's `dwc2_core*`/`dwc2_hcd*`/`dwc2_hw.h`/`dwc2_params.c`: these are dual-licensed copies of the Linux dwc2 code.
- EDK2's `DwUsbHostDxe`: unclear provenance.
- Linux and U-Boot drivers: GPL.

## Hardware on the ODROID-C2
- **Host controller:** usb1, DWC2 at **0xc9100000** (0x40000), interrupt GIC SPI 31 (**INTID 63**), level-high. The micro-USB OTG port (usb0, 0xc9000000) is disabled in the DT.
- **Clocks:** the core gate is HHI_GCLK_MPEG2 (0xc883c148) bit 8. The PHY needs MPEG1 (0xc883c144) bit 26 (usb_general) and bit 22 (USB1).
  - Ungate them **before** touching 0xc9100000: a gated Amlogic block stalls the bus, as `sd_emmc_a` showed.
- **PHY:** `amlogic,meson-gxbb-usb2-phy` at 0xc0000020, reset 34 (shared with usb0). Following NetBSD (BSD-2):
  1. Ungate the clocks, deassert the reset, enable VBUS, wait 1 ms.
  2. In the CFG register (+0x00), set bit 15.
  3. In the CTRL register (+0x04), set FSEL[24:22] = 5 (24 MHz) and POR (bit 15).
  4. Wait 1 ms, clear POR, wait 50 ms, then expect CLK_DET (bit 8) to be set.
  5. Host mode: set ADP_BC (+0x0c) bit 16, then check that bit 26 is clear.
- **Reset controller:** level register for reset 34 is probably 0xc1104484 bit 2, with 1 = released. *Verify.*
- **Board GPIOs:**
  - GPIOAO_5 is VBUS enable (active-high).
  - GPIOAO_4 is the GL852G hub reset (active-low).
  - AO GPIO output-enable and output registers are at 0xc8100024; the mux is at 0xc8100014.
  - Keep the UART_AO_B and I2C_AO alternate functions off on these pins.
- **What U-Boot leaves:** it never touches USB in our boot flow; `usb start` is not run. The driver must do clocks, reset, PHY, VBUS and the hub reset itself.
- **Hub:** the 4 USB-A ports sit behind a **Genesys GL852G**, a high-speed hub with a transaction translator. Low- and full-speed keyboards and mice therefore need **split transactions**. These are the hard part of this driver.

## Haiku side
- **Attach:** a device-manager module matching `amlogic,meson-gxbb-usb` on the FDT bus. Copy `meson_gx_mmc`:
  - skip disabled nodes;
  - translate `reg` through `ranges`;
  - publish a child with `B_DEVICE_FIXED_CHILD` set to the USB stack's controller module.

  Its `init_bus` gets the `Stack` and creates a `BusManager` subclass (pattern: `busses/usb/ehci.cpp`). No FDT-attached USB host exists in Haiku yet.
- **`BusManager` methods to implement:** `InitCheck`, `Start`, `Stop`, `SubmitTransfer`, `CancelQueuedTransfers` (must call `Finished(B_CANCELED)`), `NotifyPipeChange`, `TypeName`.
  - `Start()` creates the emulated root hub, calls `SetRootHub` and `RegisterNode`, and only then calls `BusManager::Start()`.
- **Root hub emulation:** like `ehci_rh.cpp`, a static `ProcessTransfer` for the standard hub requests, mapped onto HPRT.
- **Stack behaviour that shapes the design:**
  - **Hub status changes are polled** by the explore thread every second. Each hub's interrupt transfer is queued once and never completes, so it **must not hold a hardware channel**.
  - **Split decision:** use splits when the pipe is low/full speed and `Pipe::HubAddress()` is not the root hub's address. `HubPort()` gives the TT port.
  - **Data toggle** lives in the driver, persisted through `Pipe::DataToggle()`/`SetDataToggle()`.
  - **Completion:** the interrupt handler does the minimum; a finisher thread calls `Transfer::Finished()` after `PrepareKernelAccess()`.
- **Memory:** `Stack::AllocateChunk` gives contiguous, cacheable memory below 4 GB. DWC2 DMA does not snoop the caches, so use cache maintenance as `meson_gx_mmc` does:
  - `dc cvac` before the DMA engine reads memory;
  - `dc civac` before and after it writes memory.

  Use 64-byte-aligned bounce buffers, and size IN buffers to whole packets.

## Controller model (DWC2 host mode)
- **Init:**
  1. GRSTCTL soft reset.
  2. GUSBCFG: force host mode, UTMI+ PHY. Take the PHY width from GHWCFG4, TRDTIM 5 or 9 to match.
  3. PCGCCTL = 0.
  4. FIFOs: NetBSD's Amlogic values are RX 512, NPTX 500, PTX 500 words.
  5. Channel count from GHWCFG2 (16 on Amlogic).
  6. HCFG.FSLSPCLKSEL = 1.
  7. Power the port (HPRT.PWR), then reset it (PRTRST for 60 ms).
- **HPRT trap:** ENA is write-1-to-clear. Always write HPRT from a masked copy, never read-modify-write, or the port gets disabled.
- **Channels:** one transaction (packet) per channel at a time, released after every NAK, with a retry next microframe.
  - Halting: set CHDIS and wait for CHHLTD before reusing the channel.
  - Never touch channels at or above the hardware count; doing so resets the core.
- **Splits:**
  - HCSPLT = SPLTENA | hub address << 7 | port. Low-speed devices also need HCCHAR.LSPDDEV.
  - State machine: start-split → (ACK) → complete-split next microframe → (NYET) repeat → data or ACK means done. On NAK or error, start a new start-split. Give up after 8 microframes.
  - **FreeBSD's trick:** issue interrupt endpoints as CONTROL type. The TT then treats them as non-periodic, so there is no periodic complete-split window to hit. Poll them from a timer at the endpoint interval.
  - **Plan 9's approach:** turn "halted + ACK" into the complete-split inside the interrupt handler.
- **Register map:**

  | Register | Offset |
  |---|---|
  | GAHBCFG | 0x008 |
  | GUSBCFG | 0x00C |
  | GRSTCTL | 0x010 |
  | GINTSTS / GINTMSK | 0x014 / 0x018 |
  | GRXSTSR / GRXSTSP | 0x01C / 0x020 |
  | GRXFSIZ / GNPTXFSIZ | 0x024 / 0x028 |
  | GHWCFG1–4 | 0x044–0x050 |
  | HPTXFSIZ | 0x100 |
  | HCFG / HFIR / HFNUM | 0x400 / 0x404 / 0x408 |
  | HAINT / HAINTMSK | 0x414 / 0x418 |
  | HPRT | 0x440 |
  | Channel n: HCCHAR, HCSPLT, HCINT, HCINTMSK, HCTSIZ, HCDMA | 0x500 + 0x20·n, then +4, +8, +0xC, +0x10, +0x14 |
  | PCGCCTL | 0xE00 |
  | FIFO windows | 0x1000·(n+1) |

## Plan
About **2.5–3.5k lines**, in the order below. Each step has a pass check on the board.

| # | Step | Pass when |
|---|---|---|
| 0 | Clocks, reset, PHY, VBUS | PHY CLK_DET = 1, GSNPSID reads 0x4F54xxxx, no bus hang |
| 1 | Core init, port power, hub reset pulse | HPRT.CONNSTS = 1, and a port interrupt arrives on INTID 63 |
| 2 | Root port reset, one control transfer on channel 0 | High speed; GET_DESCRIPTOR returns 05e3:0610 (GL852G) |
| 3 | Root-hub emulation, high-speed control | The stack enumerates the hub; its ports show plugged devices |
| 4 | Split control transfers | Keyboard and mouse get an address and a configuration (`listusb`) |
| 5 | Interrupt IN with splits | Typing and mouse movement work on the desktop |
| 6 | Hardening | Unplug/replug, STALL, a long soak with no leaked channels |

**Mode:** buffer DMA with bounce buffers (Plan 9 style), plus FreeBSD's per-transfer state machine. Slave/PIO mode is the fallback.

**Biggest risks:**
- split timing, given Haiku's interrupt latency;
- channel-halt quirks;
- HPRT write-1-to-clear bits;
- a GUSBCFG PHY-width mismatch, which fails silently;
- having no emulator: every step is a board iteration.

**Test hardware needed:** a USB keyboard and mouse plugged into the C2.
