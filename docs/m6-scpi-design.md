# M6 design notes: CPU clock and temperature through SCPI (BL30)

> **Status:** Design written 2026-10-28, before `meson_scpi`; built as designed in M6. Since M30 the governor is on by default (`dvfs off` in the driver settings turns it off), samples every 50 ms, and goes down to 250 MHz in power-saving mode. `sysinfo` shows the clock through the PMU cycle counter (M7). GET_DVFS_INFO returns a 6-entry table, 100 to 1536 MHz, and the boot index reads 0.

Research for M6 item 3, written 2026-10-28, without the board. The facts come from the device tree, our serial logs, the firmware blobs in `amlogic-boot-fip/odroid-c2/`, and the sources below. Compare with `m6-ethernet-design.md`.

## Sources and licences
**Usable:**
- **Device trees** (dual GPL/MIT): `meson-gx.dtsi` and `meson-gxbb-odroidc2.dts` in `u-boot/dts/upstream/src/arm64/amlogic/`. **DT bindings** (GPL-2.0 OR BSD-2-Clause): `Bindings/firmware/arm,scpi.yaml` and `Bindings/mailbox/amlogic,meson-gxbb-mhu.yaml`.
- **TF-A** (BSD-3-Clause): `plat/amlogic/common/aml_mhu.c`, `aml_scpi.c` and `plat/amlogic/gxbb/gxbb_def.h`. This is the AP side of Amlogic's legacy SCPI on the *secure* link, and the best permissive reference for the handshake and the command word.
- **ARM DUI 0922**, "ARM Compute Subsystem SCP: Message Interface Protocols" (public): standard command numbers and status codes. Arm's site needs scripts, so I couldn't fetch it; the numbers below were cross-checked against Linux (fact only).
- **Our firmware:** `bl301.bin` (amlogic-boot-fip b6600f2) holds the OPP tables. I found them with a byte scan.

**Avoid:**
- Linux `drivers/firmware/arm_scpi.c`, `drivers/mailbox/platform_mhu.c` and `drivers/hwmon/scpi-hwmon.c` are GPL-2.0. Facts taken from them are marked "(from Linux, fact only)".
- **No BSD has an SCPI or Amlogic MHU driver.** NetBSD's `meson_thermal.c` reads G12A's on-chip sensor, not GXBB's.

## Device tree (what U-Boot passes)
- **`/scpi`** is a root-level node, not under `/firmware`: `compatible = "amlogic,meson-gxbb-scpi", "arm,scpi-pre-1.0"`, `mboxes = <&mailbox 1 &mailbox 2>`, `shmem = <&cpu_scp_lpri &cpu_scp_hpri>`. The first entry of each list is the low-priority (LP) link, the second the high-priority (HP) link.
  - **`clocks`** (`arm,scpi-clocks`) contains `clocks-0` = `scpi_dvfs` (`arm,scpi-dvfs-clocks`, `#clock-cells = 1`, `clock-indices = <0>`, output `"vcpu"`). DVFS domain 0 is the CPU cluster. **The C2 .dts sets `&scpi_clocks { status = "disabled"; }`** (Linux f7bcd4b6f698, 2017; see Risks).
  - **`sensors`** (`amlogic,meson-gxbb-scpi-sensors`, `arm,scpi-sensors`, `#thermal-sensor-cells = 1`) stays enabled.
- **`mailbox@404`** (`amlogic,meson-gxbb-mhu`), in `hiubus` (bus@c883c000): registers **0xc883c404**, length 0x4c. Interrupts GIC SPI 208, 209 and 210, rising edge, so Haiku INTIDs **240–242**. `#mbox-cells = 1`; no `status`, so enabled.
- **`sram@c8000000`** (`mmio-sram`, 0x14000, `ranges = <0 0x0 0xc8000000 0x14000>`): `cpu_scp_lpri` (`amlogic,meson-gxbb-scp-shmem`) at reg `<0x13000 0x400>`, so **0xc8013000**; `cpu_scp_hpri` at `<0x13400 0x400>`, so **0xc8013400**. The node names say `@0` and `@200`; only `reg` counts.
- **CPUs:** each `cpu@N` has `clocks = <&scpi_dvfs 0>` and `#cooling-cells = <2>`. **There is no `operating-points` table: the operating points come only from the firmware.** The first serial log (M1) shows `clocks = <0x5 0x0>` on all four CPUs, so the SCPI nodes are in the passed DT (`/scpi` itself wasn't printed).
- **`thermal-zones/cpu-thermal`:** sensor `<&scpi_sensors 0>`, polled every 1000 ms (250 ms while passive). Trips: passive **80 °C**, hot **90 °C**, critical **110 °C**, each with 2 °C hysteresis. Both cooling maps throttle all four CPUs.
- **Haiku FDT details:** `get_reg()` returns `reg` untranslated, so use `translate_fdt_address()` from `meson_gx_mmc.cpp`. `/scpi` has no `reg`; reach the mailbox and SRAM through their phandles with `fdt_bus_module_info::node_by_phandle`, which nothing in the tree calls yet. *Unverified.*

## Mailbox (MHU) on GXBB
- **Layout:** six one-word mailboxes, each with SET/STAT/CLR at +0x0/+0x4/+0x8, spaced 0xC apart from 0xc883c404. Mailboxes 0–2 carry SCP→AP replies and raise the interrupts; 3–5 carry AP→SCP commands. Link *n* uses mailbox *n* to receive and *n*+3 to send (from Linux, fact only: link offsets 0x0/0xc/0x18, send side +0x24). TF-A's `AML_HIU_MAILBOX_SET_0`/`SET_3` at …404/…428 confirm this.

  | Link | Use | Send: SET / STAT / CLR | Reply: SET / STAT / CLR | Reply IRQ | Shared memory: reply / send |
  |---|---|---|---|---|---|
  | 0 | secure, BL31 only: **never touch** | 0xc883c428 / 42c / 430 | 0xc883c404 / 408 / 40c | INTID 240 | SRAM +0x13800 / +0x13a00 (TF-A) |
  | 1 | low priority (DT mbox 1) | 0xc883c434 / 438 / 43c | 0xc883c410 / 414 / 418 | INTID 241 | 0xc8013000 / 0xc8013200 |
  | 2 | high priority (DT mbox 2) | 0xc883c440 / 444 / 448 | 0xc883c41c / 420 / 424 | INTID 242 | 0xc8013400 / 0xc8013600 |

- **Shared memory:** each 0x400 region is split in half: the first half is SCP→AP, the second AP→SCP. Linux does the same (fact only), and TF-A's secure pair at 0x13800/0x13a00 has the same split.
- **Handshake** (TF-A pattern):
  1. Wait for send STAT == 0, then write the payload into the send half.
  2. Write the command word to send SET. The SCP clears send STAT once it has taken the message; the binding says there is "no specified way" to know otherwise, so the sender polls STAT.
  3. Reply STAT becomes non-zero (and the reply IRQ fires). Read the reply half, then write the STAT value to reply CLR (TF-A writes 0xffffffff).
- **Polling is enough**, as in TF-A: we send a few commands per second at most. Interrupts can come later.
- **ARM's own MHU** (`arm,mhu`) is different: STAT/SET/CLEAR at 0x0/0x8/0x10, with links in separate blocks (from Linux, fact only). Don't reuse its offsets. Amlogic packs three links into 0x4c bytes of the HIU block, and the command itself goes in the doorbell word.

## Legacy SCPI messages
- **Command word** (written to send SET): bits 6:0 are the command ID, and **bits 28:20 the payload size** in bytes (TF-A `aml_scpi_cmd`). There is no sender or token field, so only one command can be in flight per link. The payload starts at byte 0 of the send half.
- **Standard SCPI, for comparison (DUI 0922):** the header is in shared-memory word 0 (bits 6:0 ID, 15:8 sender ID, which Linux uses as a token, 24:16 size). Status is in word 1, the payload from byte 8, and the doorbell carries only a slot bit.
- **Legacy reply:** word 0 of the reply half is the status, and the payload starts at byte 4 (from Linux, fact only; TF-A also reads its result from word 0). The firmware doesn't report a reply length, so read the expected size. TF-A decodes a size from reply STAT bits 28:20; *unverified* on links 1–2.
- **Status codes:** 0 success, 1 PARAM, 2 ALIGN, 3 SIZE, 4 HANDLER, 5 ACCESS, 6 RANGE, 7 TIMEOUT, 8 NOMEM, 9 PWRSTATE, 10 SUPPORT (not supported), 11 DEVICE, 12 BUSY.
- **Which link:** legacy firmware wants SET_DVFS and GET_DVFS on the **high-priority** link, along with CSS power state, RTC, clock set/get, PSU and sensor configuration. Everything else goes on the low-priority link (from Linux, fact only).

  | Command | ARM ID | **Legacy ID** | Link | Send payload | Reply payload (after the status word) |
  |---|---|---|---|---|---|
  | SCPI_CAPABILITIES | 0x02 | **0x02** | LP | none | u32 protocol version, event version, platform version; u32 commands[4] |
  | GET_DVFS_INFO | 0x09 | **0x0d** | LP | u8 domain (0) | u8 domain, u8 count (≤16), u16 latency µs; then per OPP: u32 Hz, u32 mV |
  | SET_DVFS | 0x0a | **0x0e** | HP | u8 domain, u8 index | none |
  | GET_DVFS | 0x0b | **0x0f** | HP | u8 domain | u8 index |
  | SENSOR_CAPABILITIES | 0x15 | **0x1a** | LP | none | u16 sensor count |
  | SENSOR_INFO | 0x16 | **0x1b** | LP | u16 id | u16 id, u8 class (0 temperature, 1 voltage, 2 current, 3 power, 4 energy), u8 trigger, char name[20] |
  | SENSOR_VALUE | 0x17 | **0x1c** | LP | u16 id | u32 value. Standard SCPI sends u64; legacy firmware fills only the low 32 bits. |

- **Units:** frequency in Hz, voltage in mV, latency in µs. **Amlogic temperatures are whole °C**, where standard SCPI uses m°C (Linux's GXBB sensor scale table, fact only). A C2 running Armbian reported `aml_thermal temp1_input: 39.000`.
- **Capabilities:** Linux accepts a legacy SCPI_CAPABILITIES reply of status 10 or all zeros as "pre-1.0 firmware" (fact only).
- **Access width:** the shared memory is Device memory, because `map_physical_memory()` defaults to uncached, which is nGnRnE on arm64. Use aligned 32-bit accesses only, never `memcpy`.

## Firmware on this board
- **Versions:** BL30 is `amlogic_v1.1.3046-00db630-dirty 2016-08-31 … tao.zeng@droid04`; it prints "secure / high / low task start!", probably one task per link (*inference*). BL2 (Nov 2015) sets **VCCK 1100 mV and the CPU to 1536 MHz**. BL31 is v1.0 from Oct 2015.
- **OPP tables in `bl301.bin`:**
  - **At 0x1730, 13 entries:** 100, 250 and 500 MHz at 870 mV; 1000 at 990; 1296, 1536, 1656 and 1680 at 1100; 1752, 1896, 1920, 1944 and 2016 at 1130 mV.
  - **At 0x1798, 6 entries:** the same, ending at 1536 MHz.
  - **Which table GET_DVFS_INFO returns is unverified.** Linux maintainers say Hardkernel's firmware reports up to 2 GHz. **Index 5 is 1536 MHz in both tables.**

## Haiku today
- **cpufreq framework:** `headers/os/drivers/cpufreq.h`, with modules under `power/cpufreq/`; only `intel_pstates` and `amd_pstates` exist, both x86.
  - `load_cpufreq_module()` (`kernel/cpu.cpp`) loads the highest-ranked module after the boot volume mounts (`main.cpp:355`).
  - The scheduler calls `increase/decrease_cpu_performance(delta)` (scale 0–1000) from `CPUEntry::TrackActivity` → `_RequestPerformanceLevel`, inside the reschedule, per CPU, with interrupts off. Load tracking runs only when a module exists (`scheduler.cpp:780`). cpuidle is x86 only.
- **Frequency reporting:** arm64 `arch_get_frequency()` returns 0, so `sysinfo` shows no frequency.
- **Thermal:** `drivers/power/{acpi,pch,amd}_thermal` publish `/dev/power/<name>/N`. `read()` returns text with a `Current Temperature: 45.0 C` line; the ioctl `drvOpGetThermalType` (`B_DEVICE_OP_CODES_END + 10001`) returns tenths of a degree. ActivityMonitor opens only `/dev/power/acpi_thermal/0` and parses that line; it isn't in `@minimum` anyway. Haiku has no generic sensor or thermal framework.
- **Probing:** `drivers/power` is probed for FDT nodes when `/dev/power` is opened, which `power_daemon` (in `@minimum`) does at boot. `SYSTEM_ADD_ONS_DRIVERS_POWER` (`minimum:347`) is x86-only today.

## Recommended design
- **Driver:** a device-manager driver `drivers/power/meson_scpi` that attaches like `meson_dwmac`.
  - **Attach:** match `amlogic,meson-gxbb-scpi`, follow the `mboxes`/`shmem` phandles, and map page 0xc883c000 (touch only 0x404–0x44f; the rest is HHI clock registers) and page 0xc8013000.
  - **Transport:** one mutex per link, polling, and a 100 ms timeout (Linux uses 30 ms). After any timeout, log it and stop sending rather than leave a link wedged.
  - **At attach:** send SCPI_CAPABILITIES (log only), read the sensors, run GET_DVFS_INFO(0) and GET_DVFS(0), and log it all.
- **Device `power/meson_scpi/0`:**
  - **read:** text, starting with a `Current Temperature:` line, then the critical trip from the DT, the current MHz and index, the OPP list with blocked entries marked, and the mode.
  - **write:** `<MHz>` (the highest OPP not above it), `max` or `auto`. **ioctl:** a `drvOpGetThermalType`-compatible struct, plus get/set index.
  - **Userland:** `cat` and `echo` are enough, so no new tool is needed.
- **Safety rules:**
  - **Hard cap 1536 MHz**, the boot frequency. Higher indexes are never sent, so DVFS can only lower the clock from what runs today.
  - Because the DT disables `scpi_clocks`, DVFS was planned to stay off unless `~/config/settings/kernel/drivers/meson_scpi` says `dvfs on` (since M30 it's on unless the file says `dvfs off`). Temperature always works.
- **`auto` governor** (a kernel thread):
  - **Load:** every 100 ms, compute the busiest CPU's load from `cpu_get_active_time()` deltas. Above 80 %, go to 1536 MHz; below 30 %, step down one OPP. Floor at 1000 MHz at first.
  - **Thermal:** at 80 °C or above, step down every 250 ms; at 90 °C or above, go to the floor; recover below the trip minus 2 °C. Ignore readings outside 0–125 °C, and act only on two readings in a row.
- **Optional later:** an arm64 `arch_get_frequency()` hook fed by the driver, for `sysinfo`; an ActivityMonitor path list; a `power/cpufreq/meson_scpi` module that only records per-CPU requests and wakes the driver's thread through an exported module interface.
- **Why not a cpufreq module first:** its hooks run with interrupts off, where a mailbox round trip can't run; all four CPUs share one domain; and it is looked up by path, so it can't share the driver's transport without that extra interface.

## Plan
About **600–900 lines**. Every step runs on the board.

| # | Step | Pass when |
|---|---|---|
| 0 | Attach, read only: DT walk, mappings, all six STAT values | Addresses 0xc883c404, 0xc8013000 and 0xc8013400 logged; STATs 0; `scpi_clocks` reported disabled |
| 1 | Transport + SCPI_CAPABILITIES | A reply (any status) within 100 ms; `reboot` and `shutdown` still work afterwards (BL30 is shared with PSCI) |
| 2 | Sensors | Count ≥ 1; sensor 0 is class 0; a plausible °C value that rises under `tools/stress.sh` |
| 3 | `/dev/power/meson_scpi/0` read | `cat` shows `Current Temperature:` |
| 4 | GET_DVFS_INFO and GET_DVFS | Table logged (13 or 6 OPPs); record what the boot index reads |
| 5 | SET_DVFS through `write` (opt-in, capped) | 1000 MHz gives GET_DVFS = 3, and a timed busy loop runs about 1.5× longer (the 24 MHz timer is independent of the CPU clock); back to 1536 |
| 6 | `auto` governor + thermal steps | Drops to the floor at idle, 1536 under load; 30-minute stress soak with no hang; USB keyboard and mouse still fine |

## Risks
- **OPPs above 1536 MHz in the firmware's table:** per Linux, the C2 can't run all cores above 1536 MHz, and these OPPs crashed systems under ondemand; Amlogic dropped them for GXBB. The cap is enforced in the driver, not taken from the firmware.
- **The boot index may be wrong:** Linux notes that a Hardkernel U-Boot change made the firmware report about 100 MHz at boot. Show "unknown" until the first SET_DVFS.
- **BL30 also serves BL31's PSCI** (CPU_ON, reset), so a stuck link could break reboot. Keep one command in flight, use timeouts, and never spin forever.
- **Spurious temperatures:** a vendor-kernel C2 reported −1 °C readings, which triggered CPU shutdowns (LibreELEC forum). Hence the filter.
- **Low clocks lengthen interrupt latency**, and `dwc2` split timing is sensitive to that: floor at 1000 MHz until tested. 1296 and 1536 MHz share 1100 mV, so real power savings start at 1000 MHz and below.
- **No thermal framework:** the 110 °C critical trip is only logged, plus a drop to the floor. The C2 has a passive heatsink and no fan.
- ***Unverified:*** `node_by_phandle` has no existing user; the reply STAT contents on links 1–2; the sensor name (`aml_thermal` per Armbian).
