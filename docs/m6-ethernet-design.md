# M6 design notes: DesignWare Ethernet (GMAC 3.70a + RTL8211F)

Research for M6, written 2026-10-28 during the unattended M4.5 run. **Nothing here is implemented.** Compare with `m5-usb-dwc2-design.md`.

## Sources and licences
**Usable:**
- **NetBSD `sys/dev/ic/dwc_gmac.c`** (BSD-2): the main reference, with the same descriptor model as the C2.
- **NetBSD `sys/arch/arm/amlogic/meson_dwmac.c`** (BSD-2): the Amlogic PRG_ETH0 setup.
- **FreeBSD `sys/dev/dwc/*`** (BSD-2): a second reference. Note that its default MDC divider is wrong for this SoC.
- **OpenBSD `if_dwge.c`** (ISC): a third.
- **Linux device trees** for addresses (dual GPL/MIT).

**Avoid:**
- Linux `stmmac` and `dwmac-meson8b`, and U-Boot `designware.c`: GPL.
- The BSD `rgephy.c` files carry the 4-clause advertising licence. Only use their documented register facts.

## Hardware on the ODROID-C2
- **MAC:** 0xc9410000 (0x10000). Its parent `soc` has an empty `ranges`, so no translation is needed.
- **Interrupt:** GIC SPI 8, so Haiku IRQ **40**.
- **Amlogic PRG_ETH0:** 0xc8834540. For RGMII: bit 0 = 1, TX delay (bits 6:5) = 1 for 2 ns, PHY clock enable (bit 10), clock generator enable (bit 12), MP2 divider (bits 9:7). Expected value about **0x1621**; read it and compare. *Unverified.*
- **PHY:** RTL8211F at MDIO address 0, ID **0x001c/0xc916**. Reset is GPIOZ_14 (active-low). Its interrupt pin has no Haiku GPIO-interrupt driver, so **poll the link over MDIO** instead.
- **What U-Boot leaves set up:** it brings up Ethernet on every boot for TFTP (1000 full duplex in our logs). Clocks, pinmux, the power domain, PRG_ETH0, PHY reset and the MAC address (from the eFuse) all stay. U-Boot also adds `local-mac-address` to the device tree Haiku gets. *Unverified.*
  - The driver redoes only the DMA side: soft reset, rings, MAC config, filter, interrupts.
- **DMA is not cache-coherent** (no `dma-coherent` in the DT). Use the `meson_gx_mmc` cache maintenance pattern:
  - **Descriptors:** 16 bytes each, but put **one per 64-byte cache line** (chained mode) so that cleaning or invalidating one never touches its neighbour.
  - **Buffers:** 2 KiB slots aligned to a cache line.

## Controller model (normal descriptors)
- **Identification:** VERSION (0x20) should read 0x1137, and HW_FEATURE (0x1058) bit 24 = 0, which means **normal descriptors**. A Linux log from this board says "Normal descriptors".
- **MAC registers:**

  | Offset | Register | Notes |
  |---|---|---|
  | 0x000 | MAC_CONFIGURATION | 1000 Mbps: PS=0, FES=0; DM full duplex; TE/RE; IPC for RX checksum; ACS |
  | 0x004 | FRAME_FILTER | |
  | 0x008 / 0x00C | Hash table high / low | |
  | 0x010 / 0x014 | MDIO address / data | GB busy bit; **CR = 4** (clk81 is about 167 MHz, so /102 gives about 1.6 MHz) |
  | 0x038 / 0x03C | Interrupt status / mask | |
  | 0x040 / 0x044 | MAC address high / low | |
  | 0x10C / 0x110 | MMC counter interrupt masks | write all ones |

- **DMA registers:**

  | Offset | Register | Notes |
  |---|---|---|
  | 0x1000 | BUS_MODE | SWR soft reset; PBL 8 |
  | 0x1004 / 0x1008 | TX / RX poll demand | |
  | 0x100C / 0x1010 | RX / TX descriptor list address | |
  | 0x1014 | STATUS | write 1 to clear |
  | 0x1018 | OPERATION_MODE | store-and-forward RSF\|TSF, ST/SR start |
  | 0x101C | INTERRUPT_ENABLE | NIE, RIE, TIE, AIE, FBE |
  | 0x1058 | HW_FEATURE | |

- **Descriptors:** desc0 status with OWN in bit 31, desc1 control (TX: IC/LS/FS/TCH, buffer size 10:0), desc2 buffer address, desc3 next descriptor (chained).
  - **Handover order:** write all fields, barrier, set OWN, clean the cache line, then poll demand.
  - **RX:** walk while OWN=0; require FS and LS; frame length includes the 4-byte FCS, so strip it.
- **Init:**
  1. Read the MAC address.
  2. DMA soft reset; time out after at least 100 ms and log it.
  3. Bus mode.
  4. Rings.
  5. Link from the PHY: speed and duplex go into MAC_CONFIGURATION.
  6. Filter and MAC address.
  7. MAC TE|RE.
  8. OPERATION_MODE start and interrupt enable.
- **PHY:** standard BMCR/BMSR; read BMSR twice, because link status is latched. RTL8211F register 0x1A (page 0xa43) gives link, duplex and speed.
  - Leave the RGMII delay registers (page 0xd08) alone; just log them. With `phy-mode = "rgmii"` the MAC adds the 2 ns TX delay through PRG_ETH0, and U-Boot's setup already works at 1000 Mbps.

## Haiku side
- **Native device-manager driver** `drivers/network/meson_dwmac`. Attach to the FDT like `meson_gx_mmc`:
  - match `amlogic,meson-gxbb-dwmac`;
  - skip disabled nodes;
  - publish `net/meson_dwmac/0`, the way `virtio_net` does.
- **Hooks:** open, close, free, read and write (copying through the 2 KiB slots), and control with:
  - `ETHER_INIT`, `ETHER_GETADDR` (both required);
  - `ETHER_GETFRAMESIZE`;
  - `ETHER_GET_LINK_STATE`, plus the link-state semaphore;
  - `ETHER_SETPROMISC`, `ETHER_ADDMULTI`/`REMMULTI`, `ETHER_NONBLOCK`.

  Reference: `drivers/network/virtio/virtio_net.cpp`.
- **RX checksum:** the hardware result can feed Haiku's `NET_BUFFER_L3/L4_CHECKSUM_VALID` later.
- **Why not the FreeBSD compat layer and FreeBSD's `if_dwc`:** the compat layer has no device-tree root bus (only PCI and USB), no `ofw_bus`/clk/gpio/reset shims, and no cache maintenance in `bus_dma`, which assumes coherent memory. Making it work would mean changing code shared by every compat driver. That isn't worth it for one NIC.

## Plan
About **1.2–1.8k lines**, roughly 1–2 weeks with board iterations. Each step has a check you can see from the laptop, over the crossover cable to the host's USB NIC:

| # | Step | Pass when |
|---|---|---|
| 0 | Read-only register dump at attach | VERSION 0x1137; normal descriptors; the board's MAC; PRG_ETH0 and the MDC divider logged |
| 1 | MDIO | PHY ID reads 0x001c/0xc916 |
| 2 | Link | 1000/full; unplug and replug noticed within 2 s |
| 3 | TX | A raw frame shows up in `sudo tcpdump -i <host NIC> -e -xx ether src <board MAC>` |
| 4 | RX | `arping` from the laptop reaches the driver with the right payload (proves cache invalidation) |
| 5 | Network stack | `ifconfig` 10.42.0.2; ping both ways; a 100 MB transfer over `ftpd`/`telnetd` with matching checksums; `ping -f` soak |

**Biggest risks:**
- cache maintenance on the rings;
- DMA soft reset hanging if the PHY clocks are missing;
- the MDC divider;
- PRG_ETH0 and the power domain, if U-Boot networking is ever skipped.

## USB or Ethernet first?
My recommendation: **Ethernet first.**
- **It's less code with less risk:** about half the size of the USB driver, with no split-transaction timing, and U-Boot already sets the hardware up.
- **It's fully testable from this laptop:** tcpdump, ping and file transfers. The USB work, by contrast, needs a keyboard and mouse plugged into the C2.
- **It makes every later milestone faster:** once Haiku has a network, `telnetd` and `ftpd` (both in the minimum image) give a remote shell and file transfer. That replaces the boot-script diagnostics, and makes the USB bring-up much easier to debug.

USB is still what makes the desktop usable on its own, so it comes straight after.
