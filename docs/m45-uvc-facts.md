<!--
Copyright 2026, haiku-odroid-c2 port authors.
Distributed under the terms of the MIT License.
-->

# M45: Logitech C920 (UVC) on the C2's DWC2 host, facts

> Facts read on 2026-11-01 from these sources:
> - the fork (`haiku/`, branch odroid-c2): the DWC2 driver, the USB stack, usb_raw, the USB Kit and the usb_webcam add-on;
> - the C920's descriptors as `listusb -v` printed them on the board (saved as `c920-desc.txt`);
> - Linux master at cbbddb6b08cc: `drivers/usb/dwc2` (SPDX `(GPL-2.0+ OR BSD-3-Clause)`) and `drivers/media/usb/uvc` (`GPL-2.0-or-later`; the headers `GPL-2.0`, `video.h` `GPL-2.0 WITH Linux-syscall-note`);
> - FreeBSD main at 63302e904a52: `sys/dev/usb/controller/dwc_otg.c` (BSD-2-Clause);
> - the USB Video Class 1.1 specification set from usb.org.
>
> No code, macros or comments were copied. Register and field names follow the Synopsys/Linux naming so they can be matched across sources. Nothing was built and no board or device was touched. **UNVERIFIED** marks what the sources do not settle. Every download is listed in §E. Haiku `file:line` references are relative to `haiku/`; Linux ones are relative to `drivers/usb/dwc2/` or `drivers/media/usb/uvc/`.

## 0. In brief

- **The C920's isochronous transfers are refused today.** The port's isochronous code only serves full-speed devices behind the hub's transaction translator. A high-speed device gets `B_NOT_SUPPORTED` (`dwc2_isochronous.cpp:141-147`).
- **Buffer DMA, with the channel re-armed every microframe, is the only workable mode on this board.**
  - Descriptor DMA is one global HCFG bit, and Linux states that split transactions do not work in it (§A.4).
  - The keyboard, the mouse and the USB audio adapter all need splits behind the GL852G.
  - Linux never enables host descriptor DMA for any platform. On GXBB it runs buffer DMA and starts periodic channels from the SOF interrupt (§A.5).
- **The fork's UVC code has not been built since 2010** (`usb_webcam/Jamfile:31`). Its data path has several bugs (§C.1).
- **usb_raw lets only one isochronous transfer per device be in flight.** It holds the device lock while it waits (§C.2).
- **First milestone without high bandwidth:** YUY2 320×240 at 30 fps needs 588 bytes per microframe, so alternate setting 4 (1 × 640 bytes) is enough.
- **YUY2 640×480 at 30 fps** needs 2316 bytes per microframe: alt 10 (3 × 896 bytes).
- **MJPEG at alt 11** (3 × 1024 bytes) carries frames of up to about 816 KB at 30 fps.

---

## A. The DWC2 controller

### A.1 This board's core (values read on the board, decoded with Linux's field layout)

| Register | Value | Field | Meaning |
|---|---|---|---|
| GSNPSID | 0x4F54310A | | core release 3.10a |
| GHWCFG2 | 0x228FC855 | [2:0] = 5 | operating mode: SRP-capable host |
| | | [4:3] = 2 | internal DMA |
| | | [7:6] = 1, [9:8] = 0 | UTMI+ high-speed PHY, no dedicated full-speed PHY |
| | | [17:14] = 15 | 16 host channels |
| | | [18] = 1, [19] = 1 | periodic endpoints supported; dynamic FIFO sizing |
| | | [23:22] = 2, [25:24] = 2 | depth fields of the non-periodic and periodic request queues. Linux decodes them as field × 2 = 4 entries (`params.c:980-987`). The databook's encoding is **UNVERIFIED**. |
| GHWCFG3 | 0x07C05068 | [3:0] = 8 | 19-bit transfer-size counter: at most 524,287 bytes per channel enable |
| | | [6:4] = 6 | 10-bit packet counter: at most 1,023 packets |
| | | [31:16] = 1984 | total data FIFO, in 32-bit words |
| GHWCFG4 | 0xC9E04030 | [15:14] = 1 | 16-bit UTMI+ |
| | | [25] = 0 | no dedicated Tx FIFOs (these matter only in device mode) |
| | | [30] = 1 | descriptor DMA implemented (Linux `hw->dma_desc_enable`, `params.c:1008`) |
| | | [31] = 1 | Linux calls it `GHWCFG4_DESC_DMA_DYN` (`hw.h:290`) but never reads it. Meaning **UNVERIFIED**. |

FIFO split today (`dwc2.cpp:420-432`): Rx 512 words, non-periodic Tx 500, periodic Tx 500. That is 1512 of the 1984 words. Linux's Amlogic table uses the same three values (`params.c:168-182`).

### A.2 How the port drives it today

- **DMA mode.** Buffer DMA: GAHBCFG.DMAEN with INCR8 bursts (`dwc2.cpp:457-458`).
  - All data goes through one 32-bit physically contiguous bounce area, with explicit cache maintenance: `dc cvac` before the controller reads, `dc civac` around its writes (`dwc2.cpp:102-121`).
  - Area size 196 KiB: 64 KiB for control and bulk, 4 KiB for interrupt, and 2 streams × 4 slots × 16 KiB for isochronous (`dwc2.cpp:90-91, 210-232`; `dwc2.h:31-36`).
- **Channels.**
  - Channel 0 is the control/bulk "lane", channel 1 the interrupt lane. Each lane has a worker thread at `B_URGENT_DISPLAY_PRIORITY` that runs one transfer at a time: it writes HCSPLT, HCINT, HCINTMSK, HCTSIZ, HCDMA, then HCCHAR with CHENA, and sleeps until the halt (`dwc2.cpp:1047-1108`).
  - Channels 2 and 3 are the isochronous streams (`kIsoStreamCount`). Channels 4-15 are unused.
- **Interrupts.** HAINTMSK enables only channels 0-3, and GINTMSK only HCHINT (`dwc2.cpp:445-458`). There are no SOF or port interrupts. The handler passes each halt to its lane, or to `_IsoHalted` under the spinlock `fIsoLock` (`dwc2.cpp:1384-1414`).
- **Field values the port always writes.**
  - HCCHAR.MC is always 1 (`dwc2.cpp:1034`, `dwc2_isochronous.cpp:421`).
  - Only the DATA0, DATA1 and MDATA/SETUP PID codes are defined. DATA2 (code 1) is missing (`dwc2_hardware.h:118-121`).
- **Splits.** A low- or full-speed device behind the hub gets one packet per start-split/complete-split pair. Interrupt endpoints are issued as control type through the translator (`dwc2.cpp:1021-1028, 1180-1293`).
- **Isochronous today: full speed behind the translator only** (design notes in `dwc2_isochronous.cpp:5-32`). Each stream has a channel, a kernel timer and a queue of transfers waiting in bounce slots.
  - **Arming the first part.** The timer fires about 30 µs into the microframe before the packet's start microframe: microframe 0 for OUT, 2 for IN. The delay is computed from HFNUM.FRREM and HFIR (`:549-564`). `_IsoArm` then starts the start-split, or marks the packet `B_DEV_TOO_LATE` if the window has passed (`:349-375`).
  - **Further parts** are armed from the channel-halt interrupt, for the next microframe, with ODDFRM = parity(HFNUM + 1) (`_IsoHalted` `:442-511`, `_IsoStartPart` `:382-435`). For OUT these are 188-byte start-split parts; for IN they are complete-splits.
  - **Finishing.** A finished transfer goes to a worker at `B_REAL_TIME_PRIORITY` (`dwc2.cpp:481-482`). It copies IN data out of the slot, fills in the packet descriptors and calls `Finished()` (`:612-675`). Packet *i* lands at the sum of the earlier packets' `request_length` (`:649`).
  - **Limits.**
    - At most 4 transfers queued per stream; beyond that `B_BUSY` (`:197-198`).
    - At most 128 packets per transfer, and packets × stride ≤ 16 KiB (`:155-168`).
    - `request_length` ≤ `MaxPacketSize()` (`:180`).
    - `USB_ISO_ASAP` is ignored. `*starting_frame_number` returns the 1 ms frame of the first packet, masked with 0x7ff (`:226-244`).
  - **Evidence that halt-to-re-arm works on this board** (`logs/m1-2026-10-27.log:217562`): 120,036 OUT packets of two start-split parts each, with 0 "split gaps" (every second part went out in the next microframe). Kernel timers fired up to 103 µs late.
- **The C920's endpoints today.**
  - **Video:** a high-speed device on the hub's high-speed port needs no split (`_NeedsSplit`, `dwc2.cpp:1006-1011`), so its isochronous transfers are rejected with "isochronous endpoints of high speed devices are not supported" (`dwc2_isochronous.cpp:141-147`).
  - **Video Control interrupt endpoint** (64 bytes, high speed): already served by the interrupt lane.

### A.3 Hardware facts for high-speed isochronous IN in buffer DMA

Channel registers sit at 0x500 + 0x20·n (Linux `hw.h:745-812`; the port's `dwc2_hardware.h:32-37` agrees).

| Register | Field | Value for a high-speed isochronous IN microframe |
|---|---|---|
| HCCHAR | [10:0] MPS | `wMaxPacketSize` bits 10:0 (1024 for alt 11). Haiku's `Pipe::MaxPacketSize()` is already masked (`bus_managers/usb/Pipe.cpp:47-52`). |
| | [14:11] EPNUM, [15] EPDIR | endpoint 1, direction IN |
| | [17] LSPDDEV | 0 |
| | [19:18] EPTYPE | 1 (isochronous) |
| | [21:20] MC/EC | Transactions per microframe for high-speed periodic endpoints: 1-3, which is `Pipe::MaxBurst()` + 1 (`Pipe.cpp:51`). For splits the same field is an error count: Linux writes 3 for periodic splits (`hcd.c:1239-1244`), the port writes 1. |
| | [28:22] DEVADDR | device address |
| | [29] ODDFRM | The channel runs in the next microframe whose HFNUM bit 0 equals this bit. If that is the current microframe's parity, it runs at once. Sources: Linux `hcd.c:958-1055`; the port's notes, `dwc2_isochronous.cpp:17-18, 356-360`. |
| | [30] CHDIS, [31] CHENA | Write CHENA last. To halt, write CHDIS and CHENA together, then wait for CHHLTD. |
| HCSPLT | | 0 (no split) |
| HCTSIZ | [18:0] XFERSIZE | MC × MPS. Linux always programs whole packets for IN (`hcd.c:1276-1290`). |
| | [28:19] PKTCNT | equal to MC (Linux sets MC from the packet count, `hcd.c:1296-1301`) |
| | [30:29] PID | Codes: 0 DATA0, 1 DATA2, 2 DATA1, 3 MDATA (`hw.h:797-802`). Starting PID for IN by MC: 1 → DATA0, 2 → DATA1, 3 → DATA2. For OUT: DATA0 for one transaction, otherwise MDATA (`hcd.c:1057-1077`). |
| HCDMA | | 32-bit aligned bus address of this microframe's buffer. Linux warns on unaligned non-split DMA (`hcd.c:2648`). |

- **USB-level sequence** (USB 2.0 §5.9.2; UVC 1.1 Figure 2-13, p. 27). In a high-bandwidth IN microframe the device answers:
  - DATA2, DATA1, DATA0 when it sends three packets;
  - DATA1, DATA0 when it sends two;
  - DATA0 alone when it sends one.

  DATA0 marks the microframe's last transaction. FreeBSD's dwc_otg (slave mode) reads the PID of each received packet and treats anything other than DATA0 as "more follows" (`dwc_otg.c:1286-1300`).
- **When the microframe's transactions end:**
  - After PKTCNT packets, or a short packet, the channel sets XFERCOMPL and halts by itself (CHHLTD). XFERSIZE then holds the bytes not received, so actual = programmed − remaining (Linux `hcd_intr.c:556-620`).
  - One enable serves one (micro)frame only. Linux caps a periodic transfer at MC × MPS (`hcd.c:1252-1266`) and programs the channel again for every (micro)frame. In DMA mode it queues one request and lets the core issue the MC transactions (`hcd.c:2919-2927`).
  - FRMOVRUN means the transactions could not finish within the scheduled (micro)frame (Linux packet status `-ENOSR` for IN, `hcd_intr.c:575-580`).
  - A periodic halt with no reason bit is treated by Linux as "incomplete periodic transfer" (`hcd_intr.c:1920-1932`).
  - XACTERR fails the packet with no retry. BBLERR means babble. A NAK must never happen on isochronous (`hcd_intr.c:1270-1272`).
  - **UNVERIFIED:** what PKTCNT, PID and XFERCOMPL show when a device ends a 3-transaction microframe early with a full-size DATA0 or DATA1 (no short packet). Linux has no special case for it.
- **Frame counter.**
  - HFNUM (0x408): FRNUM [15:0], which Linux masks to 14 bits (0x3FFF). At high speed, bits [2:0] are the microframe and [13:3] the 1 ms frame (`hcd.h:649-657`). FRREM [31:16] is the number of PHY clocks left in the current microframe.
  - HFIR (0x404): FRINT [15:0] is the number of PHY clocks per microframe. Bit 16 RLDCTRL is set by Linux on cores ≥ 2.92a (`hcd.c:2160-2167`); the port does not set it.
- **Scheduling limits.**
  - **Bus time** (USB 2.0 §5.11.3): one 1024-byte high-speed isochronous transaction takes about 17.7 µs without bit stuffing and 20.6 µs at worst, plus host delay. So 3 × 1024 bytes uses about 53-62 µs of the 125 µs microframe.
  - **Periodic budget:** at most 80 % of a microframe (100 µs) may be periodic. Linux books exactly that much (`core.h:800`, `hcd_queue.c:93`).
  - **Rx FIFO:** IN data passes through the shared Rx FIFO.
    - Linux's minimum for high-bandwidth endpoints is 2 × (1024/4 + 2) + channels, which is 532 words for 16 channels (`hcd.c:207-221`).
    - Both the port and Linux on Amlogic use 512 (`dwc2.cpp:420`, `params.c:175`). 472 words are unallocated.
    - Whether 512 is enough is **UNVERIFIED**.
  - **Periodic Tx FIFO** (HPTXFSIZ): holds OUT data only. HPTXSTS (0x410) gives free queue entries in [23:16] and free FIFO words in [15:0].
- **Interrupts.**
  - Per channel, HCINTn: 0 XFERCOMPL, 1 CHHLTD, 2 AHBERR, 3 STALL, 4 NAK, 5 ACK, 6 NYET, 7 XACTERR, 8 BBLERR, 9 FRMOVRUN, 10 DATATGLERR (`dwc2_hardware.h:92-103`, Linux `hw.h:776-792`). Descriptor DMA adds 11 BNA, 12 XCS_XACT and 13 FRM_LIST_ROLL.
  - Global, GINTSTS: 3 SOF (every microframe at high speed), 21 INCOMPL_IP (incomplete periodic), 24 PRTINT, 25 HCHINT (`hw.h:131-155`).
  - In buffer DMA mode the halt interrupt alone reports every outcome.

### A.4 Descriptor (scatter-gather) DMA

- **Enable.** HCFG (0x400) bit 23 DESCDMA, one bit for the whole host (`hw.h:685`).
  - Linux sets it only on a core ≥ 2.90a, with GHWCFG4 bit 30 set, in a host-capable operating mode (`hcd.c:2170-2191`). This board passes all three on paper.
- **Periodic schedule.**
  - HCFG [25:24] FRLISTEN selects 8, 16, 32 or 64 entries; bit 26 PERSCHEDENA turns the schedule on.
  - HFLBADDR (0x41C) holds the frame list's base. Each entry is a 32-bit mask of the channels active in that 1 ms frame. Linux uses 64 entries (`hcd_ddma.c:151-171, 200-250`).
- **Channel registers.**
  - HCDMA holds the base of the channel's descriptor list. Linux aligns lists to 512 bytes: 64 descriptors in general, 256 for high-speed isochronous, i.e. 32 frames × 8 microframes (`hw.h:886-887`, `hcd.c:5257-5291`).
  - HCTSIZ: PID [30:29]; NTD [15:8] = descriptors − 1; SCHINFO [7:0] = microframe mask for high-speed periodic endpoints (0xFF for every microframe). XFERSIZE and PKTCNT are unused (`hcd.c:1407-1470`).
  - Where HCDMA reports the current descriptor index is not shown by Linux: **UNVERIFIED**.
- **Descriptor format.** Two words: status, then buffer address (`hw.h:828-849`).
  - Isochronous status: [31] A (active); [29:28] STS (0 = OK, 1 = packet error, which covers transaction errors and transfers that could not complete in the scheduled (micro)frame); [25] IOC; [11:0] byte count. On completion of an IN descriptor, [11:0] holds the bytes not received.
  - Other transfer types: [16:0] bytes, [24] SUP (setup), [26] EOL, [23] ALT_QTD, [22:17] QTD offset.
- **High-speed isochronous in Linux.**
  - One descriptor per microframe, at index (frame mod 32) × 8 + microframe.
  - The first descriptor is placed 1-2 frames ahead of HFNUM, aligned to 8 (`hcd_ddma.c:398-450`).
  - At most 3072 bytes per descriptor (`:508`). IOC is set on each URB's last descriptor.
  - The channel stays enabled for the whole session and is not halted on XFERCOMPL (`:797-855`).
- **Splits are refused:** "SPLIT Transfers are not supported in Descriptor DMA mode" (`hcd_ddma.c:317-320`).
- **Mixing modes.**
  - Linux defines no per-channel mode bit.
  - It switches HCFG.DESCDMA only at connection time, in its `dma_desc_fs_enable` mode, for a full-speed device directly on the root port (`hcd.c:3585-3601`). It switches it off on disconnect (`hcd_intr.c:374-381`).
  - On the C2 the root port always sees the high-speed hub, and every full- or low-speed device needs splits. Descriptor DMA would therefore cut off the keyboard, the mouse and USB audio.
  - Per-channel mixing in hardware is **UNVERIFIED**: no databook was available, and nothing in the sources suggests it is possible.

### A.5 Linux on GXBB

- **Parameters.** `amlogic,meson-gxbb-usb` selects `dwc2_set_amlogic_params` (`params.c:168-182, 329-330`): high speed, UTMI, 16 channels, FIFOs 512/500/500, INCR8 bursts, no power-down.
  - Host DMA is on.
  - `dma_desc_enable` and `dma_desc_fs_enable` default to false (`params.c:540-543`), and no platform function sets them.
  - So GXBB runs buffer DMA, as the port does.
- **Periodic scheduling.**
  - The microframe scheduler is on (`params.c:529`).
  - While any periodic queue head exists, the SOF interrupt is unmasked (`hcd_queue.c:1691`).
  - Each SOF moves the queue heads due next into the ready list and starts their channels (`dwc2_sof_intr`, `hcd_intr.c:110-149`), with ODDFRM set for the "wire frame" (`hcd.c:958-1055`).
  - An endpoint with interval 1 therefore costs one SOF interrupt plus one channel-halt interrupt per microframe: 16,000 interrupts/s.
- **Quirks.** There is no Amlogic- or Meson-specific code in `hcd*.c` or `core.c`, and no high-bandwidth quirk.

### A.6 What the port must add, options and costs

**Needed whatever the option:**
- High-speed streams without HCSPLT. MC and PID come from `MaxBurst()`.
- A service interval of 2^(bInterval−1) microframes: 1 for the C920's video endpoint, 8 for its microphone.
- One packet descriptor per service interval, with `request_length` ≤ MPS × (`MaxBurst()` + 1), so at most 3072 bytes.
- `PID_DATA2` (code 1) in `dwc2_hardware.h`.
- Larger slots. For example, 32 packets × 3072 bytes = 96 KiB per transfer; with 4-6 transfers per stream that is 0.4-0.6 MiB per high-speed stream. Today all isochronous slots together are 128 KiB.
- More stream channels in HAINTMSK, if the USB audio adapter (2 streams) and the C920 (video, plus microphone) are to work at the same time.

| Option | How | Interrupts (interval 1) | Re-arm deadline | Cost |
|---|---|---|---|---|
| **A1** halt-chained, one channel | As the full-speed parts already work: at the halt for microframe N, program the next packet with ODDFRM = parity(HFNUM + 1). A timer restarts a broken chain. A missed microframe marks its packet `B_DEV_TOO_LATE`. | 8,000/s per stream | From the halt (about 55-65 µs into N at 3 × 1024) to the last start in N+1 that still fits (about 60 µs into N+1): roughly 120 µs, **UNVERIFIED** | About 300-400 lines on `dwc2_isochronous.cpp`; reuses its timer, queue and worker |
| **A2** SOF-driven (Linux's way) | Unmask GINTSTS.SOF; at the SOF of N, arm the channel for N+1 | 16,000/s | About one microframe plus the slack inside N+1 | About 100 more lines; twice the interrupts |
| **A3** two channels, alternating parity | At SOF(N), or from a timer, arm the idle channel for N+1 while the other runs N. A halted channel cannot be re-armed during its own microframe: with the same parity it would start at once. | as A2 | most slack | Most complex; worth it only if A1 drops packets |
| **B** descriptor DMA | Frame list plus a 256-entry descriptor ring, one entry per microframe | IOC every 32 descriptors, for example: 250/s | descriptors queued milliseconds ahead | **Not usable here** (§A.4); every transfer type would also need rewriting in descriptor form |

The interrupt cost on this board has not been measured. At an assumed 3-6 µs per halt (GIC entry, about six register accesses, one spinlock), 8,000 interrupts/s would take 2.5-5 % of one A53 core (**UNVERIFIED**). Copying from bounce slots into the client's buffer moves at most 24.6 MB/s.

---

## B. UVC streaming as the C920 needs it

### B.1 VS_PROBE_CONTROL and VS_COMMIT_CONTROL

**Requests.** Class-specific interface requests:
- bmRequestType 0x21 for SET, 0xA1 for GET;
- bRequest: SET_CUR 0x01, GET_CUR 0x81, GET_MIN 0x82, GET_MAX 0x83, GET_RES 0x84, GET_LEN 0x85, GET_INFO 0x86, GET_DEF 0x87;
- wValue = selector << 8 (PROBE 0x01, COMMIT 0x02);
- wIndex = streaming interface number (1 on the C920).

Linux `video.h:67-74, 132-133` agrees with UVC 1.1 Table 4-47 (p. 103).

| Offset | Field | Size | Notes |
|---|---|---|---|
| 0 | bmHint | 2 | Fields the device should keep fixed: D0 dwFrameInterval, D1 wKeyFrameRate, D2 wPFrameRate, D3 wCompQuality, D4 wCompWindowSize |
| 2 | bFormatIndex | 1 | C920: 1 = YUY2, 2 = MJPEG |
| 3 | bFrameIndex | 1 | 1-based; the frame descriptor's own bFrameIndex |
| 4 | dwFrameInterval | 4 | 100 ns units (333333 = 30 fps) |
| 8, 10, 12, 14 | wKeyFrameRate, wPFrameRate, wCompQuality, wCompWindowSize | 2 each | |
| 16 | wDelay | 2 | ms |
| 18 | dwMaxVideoFrameSize | 4 | set by the device |
| 22 | dwMaxPayloadTransferSize | 4 | set by the device: bytes per payload transfer, i.e. per serviced microframe on isochronous |
| 26 | dwClockFrequency | 4 | UVC 1.1 and later |
| 30 | bmFramingInfo | 1 | D0: FID required; D1: EOF may be present (stream-based formats only) |
| 31-33 | bPreferedVersion, bMinVersion, bMaxVersion | 3 | |

- **Length.** 26 bytes for UVC 1.0, 34 for 1.1, 48 for 1.5. Linux chooses by bcdUVC: below 0x0110 → 26, below 0x0150 → 34, otherwise 48 (`uvc_video.c:276-289`).
  - The C920 (08e5) reports UVC 1.0, so it gets **26 bytes**. dwClockFrequency then comes from the VC header: 30 MHz.
- **Sequence** (UVC 1.1 §4.3.1.1.1-2, pp. 112-113):
  1. SET_CUR(PROBE) with the format, frame and hinted fields. Fields left to negotiate are zero.
  2. GET_CUR(PROBE) returns the negotiated values. The device stalls if negotiation fails.
  3. SET_CUR(COMMIT) **with exactly the values from GET_CUR(PROBE)**.
  4. SET_INTERFACE to the chosen alternate setting.
  5. If bandwidth runs out, negotiate again. The device must answer with decreasing data rates.
  6. To stop, SET_INTERFACE to alt 0.
- **Linux's fixups** (`uvc_video.c:163-274`):
  - for uncompressed formats it takes dwMaxVideoFrameSize from the frame descriptor's dwMaxVideoFrameBufferSize;
  - it masks a dwMaxPayloadTransferSize of 0xFFFFxxxx;
  - it clips the payload size to the largest endpoint payload;
  - the optional FIX_BANDWIDTH quirk computes about w × h × bpp/8 × fps / 8000 + 12, at least 1024.

### B.2 Choosing the alternate setting

- Interface 1, alt 0, has no endpoint. Alts 1-11 each have isochronous IN endpoint 0x81.
- Linux takes the alt with the smallest payload per microframe that is ≥ dwMaxPayloadTransferSize. The payload is (bits 10:0) × (1 + bits 12:11) (`uvc_video.c:2042-2102`).
- If dwMaxPayloadTransferSize is 0, Linux takes the lowest alt.

### B.3 Payload header (UVC 1.1 Tables 2-5 and 2-6, pp. 31-33)

| Byte | Field |
|---|---|
| 0 | bHeaderLength. Linux drops a packet if this is < 2 or larger than the packet. |
| 1 | bmHeaderInfo: D0 FID (toggles at every new frame), D1 EOF (optional), D2 PTS present, D3 SCR present, D4 reserved, D5 STI (still image), D6 ERR (error in this payload; details in the Stream Error Code control), D7 EOH |
| +4 | dwPresentationTime, if D2. Constant over one frame, in dwClockFrequency units. |
| +6 | scrSourceClock, if D3: [31:0] source clock, [42:32] 1 kHz SOF counter |

- **One header per payload transfer.** On the C920's endpoint that means one per serviced microframe: all MC transactions of a microframe form one payload transfer (UVC 1.1 Figure 2-13).
- A packet of exactly bHeaderLength bytes carries no data.
- **Linux's frame rules** (`uvc_video.c:1183-1270`):
  - an FID change while data is buffered completes the frame;
  - EOF completes it too;
  - ERR marks the frame bad;
  - data is ignored until the first FID change.
- **Quirks.** Linux's C920 entry is product 082d, with RESTORE_CTRLS_ON_INIT and INVALID_DEVICE_SOF (`uvc_driver.c:2693-2702`). Product 08e5 is not in Linux's table, so it gets generic handling.
- **UNVERIFIED for this camera:** the C920's header length (12 bytes, with PTS and SCR, is typical) and whether it sets EOF.

### B.4 YUY2 and MJPEG

- **YUY2.**
  - GUID 32595559-0000-0010-8000-00AA00389B71; the dump's bytes `59 55 59 32 00 00 10 00 80 00 00 aa 00 38 9b 71` match.
  - Packed 4:2:2, 16 bits per pixel, frame = w × h × 2 bytes. No padding; transfers are aligned to macropixels (Uncompressed payload 1.1 §2.2-2.3, §3.3.2).
  - Byte order Y0 U Y1 V. This is the FourCC convention; the UVC text only says "packed 4:2:2". It is the same order as Haiku's `B_YCbCr422` (`headers/os/interface/GraphicsDefs.h:216`).
- **MJPEG** (MJPEG payload 1.1 §3.3, pp. 8-9). Every frame is a baseline JPEG:
  - SOI; APPn (optional); DRI (optional); DQT (required); DHT (optional); SOF0 only; SOS; data; EOI.
  - Without DHT, the standard tables of ISO 10918-1 K.3.3 apply.
  - YCbCr 4:2:2, 8 bits per component, every frame a key frame.
  - The format descriptor is 11 bytes long; bmFlags D0 means FixedSizeSamples.
  - Whether the C920 leaves DHT out is **UNVERIFIED**. A decoder needs the K.3.3 fallback in case it does.
- **listusb misreads the MJPEG format descriptor.** It decodes it with the uncompressed layout (the dump's "GUID ... Bits per pixel 62"). Read from its bytes: bmFlags = 1, bDefaultFrameIndex = 1 (**UNVERIFIED** reading).

### B.5 The C920's descriptors (from the board dump)

- **Device.** 046d:08e5, bcdDevice 0x0021, class EF/02/01 (interface association), EP0 64 bytes, one configuration.
- **Interface 0, Video Control.**
  - UVC 1.0, wTotalLength 214, clock 30 MHz.
  - Camera input terminal 1; processing unit 3; extension units 6, 8, 9, 10, 11, 13; streaming output terminal 4, fed by unit 3.
  - Interrupt IN endpoint, 64 bytes, bInterval 8: 128 microframes = 16 ms.
- **Interface 1, Video Streaming.**
  - Input header: 2 formats, endpoint 0x81, terminal link 4, still-capture method 0, no trigger; class descriptors total 12,039 bytes.
  - Color matching for both formats: BT.709 primaries and transfer characteristics, SMPTE 170M (BT.601) matrix.
- **Interfaces 2-3, UAC 1.0 microphone.**
  - Feature unit 5 has mute and volume.
  - Alts 1-3: 16, 24 and 32 kHz, 2 channels × 16 bits, isochronous IN of 68, 100 and 132 bytes, bInterval 4 = 8 microframes = 1 ms.
  - Its interface association descriptor prints under interface 1, alt 11 (type 0x0B: first interface 2, count 2, class 1).

| bFrameIndex | Size | YUY2 fps (max) | MJPEG fps (max) |
|---|---|---|---|
| 1 | 640×480 | 30 | 30 |
| 2-9 | 160×90, 160×120, 176×144, 320×180, 320×240, 352×288, 432×240, 640×360 | 30 | 30 |
| 10 | 800×448 | 30 | 30 |
| 11, 12 | 800×600, 864×480 | 24 | 30 |
| 13, 14 | 960×720, 1024×576 | 15 | 30 |
| 15 | 1280×720 | 10 | 30 |
| 16 | 1600×896 | 7.5 | 30 |
| 17 | 1920×1080 | 5 | 30 |
| 18 | 2560×1472 | 2 | (no MJPEG) |

- Every MJPEG frame lists 7 intervals starting at 30 fps. The YUY2 frames up to 800×448 list 30, 24, 20, 15, 10, 7.5 and 5 fps.
- **Default frames.** YUY2 frame 1 (640×480). MJPEG, by the misprinted bytes, also frame 1.

| Alt | `wMaxPacketSize` field | Transactions × bytes | Bytes per microframe | MB/s |
|---|---|---|---|---|
| 1-6 | 192, 384, 512, 640, 800, 944 | 1 × size | 192 … 944 | 1.5 … 7.6 |
| 7 | 0x0A80 (2688) | 2 × 640 | 1280 | 10.2 |
| 8 | 0x0B20 (2848) | 2 × 800 | 1600 | 12.8 |
| 9 | 0x0BE0 (3040) | 2 × 992 | 1984 | 15.9 |
| 10 | 0x1380 (4992) | 3 × 896 | 2688 | 21.5 |
| 11 | 0x1400 (5120) | 3 × 1024 | 3072 | 24.6 |

### B.6 Bandwidth of useful modes (computed; 12-byte headers assumed)

Bytes per microframe = frame bytes × fps / 8000, plus the header. The alt is the smallest one that holds it.

| Mode | Data rate | Bytes per microframe incl. header | Alt needed |
|---|---|---|---|
| YUY2 320×240 at 30 | 4.61 MB/s (36.9 Mbit/s) | 588 | **4** (1 × 640) |
| YUY2 640×360 at 30 | 13.8 MB/s | 1740 | 9 |
| YUY2 640×480 at 30 | 18.4 MB/s (147.5 Mbit/s) | 2316 | **10** |
| YUY2 800×448 at 30, 800×600 at 24, 1600×896 at 7.5 | 21.5-23.0 MB/s | 2700-2892 | 11 |
| YUY2 1280×720 at 10, 960×720 at 15, 1920×1080 at 5 | 18.4-20.7 MB/s | 2316-2604 | 10 |
| YUY2 2560×1472 at 2 | 15.1 MB/s | 1896 | 9 |
| MJPEG at 30 fps | depends on the scene | | The device chooses dwMaxPayloadTransferSize (**UNVERIFIED** per mode) |

Largest MJPEG frame each alt carries at 30 fps: alt 4 167 KB, alt 6 249 KB, alt 7 338 KB, alt 9 526 KB, alt 10 714 KB, alt 11 816 KB. Typical C920 MJPEG frame sizes for 720p and 1080p were not measured (**UNVERIFIED**).

---

## C. Haiku's side, in the fork

### C.1 usb_webcam (`src/add-ons/media/media-add-ons/usb_webcam`)

- **Build state.**
  - UVC is commented out: `#	UVCCamDevice.cpp UVCDeframer.cpp` (`Jamfile:31`). It has been off since upstream commit cea369ce9d (2010, "UVC webcam addon isn't ready yet"); the fork has no change of its own here.
  - The built add-on holds only the QuickCam, Sonix and NW80x code. The object directory has no `UVCCamDevice.o`.
  - The isochronous pump is compiled in: `SUPPORT_ISO` is defined under `__HAIKU__` (`CamDevice.h:11-15`).
  - `README.txt` dates from 2004-2008 and still lists "write isochronous code when USB Kit supports it".
- **What exists.**
  - Matching: a generic UVC class entry, plus the IAD device class EF/02 (`UVCCamDevice.cpp:18-44`). The C920 matches through the class entries.
  - Descriptor parsing for VC (header, terminals, processing and extension units) and VS (input header, uncompressed and MJPEG format and frame descriptors, still image, color matching) (`:153-539`).
  - Processing-unit controls published as a BParameterWeb (`:775` on).
  - Probe/commit (`:621-703`), alternate-setting choice (`:706-752`), a deframer (`UVCDeframer.cpp`) and a color conversion (`:1278` on).
  - Nothing of it has been verified to work.
- **Formats.**
  - Only the uncompressed format is ever requested (`:640`).
  - MJPEG frame descriptors are collected (`fMJPEGFrames`) but never used.
  - The NV12 GUID is used only for printing.
- **Sizes and rate.**
  - The Producer forces 320×240 (`Producer.cpp:31-32, 500-505`), outputs only `B_RGB32` (`:219`) and defaults to `FIELD_RATE` 5 fps (`:50`).
  - CodyCam asks for 320×240 `B_RGB32` (`apps/codycam/CodyCam.cpp:38-39, 288`).
- **Bugs found by reading** (each is a reading of the code; none tested on hardware):
  1. **Frame index off by one.** `AcceptVideoFrame` stores the 0-based list index as bFrameIndex (`UVCCamDevice.cpp:609`). On the C920, 320×240 is list item 5 but bFrameIndex 6, so the request asks for frame 5, which is 320×180.
  2. **Endpoint search uses the wrong loop variable:** `EndpointAt(i)` instead of `k` (`:111, 135`). On the streaming interface it finds nothing anyway, because alt 0 has no endpoint. `_SelectBestAlternate` sets `fIsoIn` later.
  3. **Alternate choice compares raw `wMaxPacketSize`.** `BUSBEndpoint::MaxPacketSize()` returns the raw field (`kits/device/USBEndpoint.cpp:114-117`), and `_SelectBestAlternate` compares that raw value with dwMaxPayloadTransferSize (`:725-730`). The raw values for alts 7-11 (2688-5120) exceed their real payloads (1280-3072), so any payload above 1280 bytes gets too small an alt. For example, 2316 bytes gets alt 7, which carries 1280.
  4. **Commit sends the original request,** not the GET_CUR(PROBE) result (`:685-687`). The spec requires the probe result (§B.1). The GET_CUR reply's length is not checked either.
  5. **Wrong color conversion.** `_DecodeColor` is a Bayer demosaic copied from the Sonix code. It reads w × h bytes as raw sensor samples, so a YUY2 frame decodes as noise (`:1266-1271, 1278` on).
  6. **Deframer** (`UVCDeframer.cpp:31-67`):
     - `fInputBuffer` (a BMallocIO) is never reset, so every frame carries all bytes since the stream started and memory grows without bound;
     - on an FID toggle, the new frame's first payload is appended to the old frame before it closes;
     - with EOF and FID both used, each frame is split in two;
     - `buf[0]` is read without a length check;
     - the ERR bit is ignored.
  7. **Pump** (`CamDevice.cpp:566-618`):
     - 16 packets of `request_length` 256 bytes into a single 4096-byte page (`:68-69, 567-572`). At high speed that is 2 ms per transfer, and too small for any C920 alt above 1 (192 bytes).
     - The data index advances by `actual_length` (`:610`), but host controllers place packet *i* at the sum of the earlier `request_length`s (§C.2).
     - A failing transfer loops at once, without sleeping (`:595-597`).
  8. **Frame queue.** The deframer queues up to 50 frames (`CamDeframer.h:16`) and the Producer takes the oldest (`CamDeframer.cpp:124-134`). At 5 fps against a 30 fps camera, frames get older and older until the queue is full; after that, new frames are dropped.

### C.2 The isochronous path from the USB Kit to the host controller

- **USB Kit.** `BUSBEndpoint::IsochronousTransfer(data, length, descriptors, count)` (`kits/device/USBEndpoint.cpp:197-217`).
  - It returns the total length, or `B_ERROR` for any status other than success.
  - There is no start frame, no flags and no asynchronous form.
  - `BUSBInterface::SetAlternate` sends `SET_ALT_INTERFACE` and re-reads the endpoints (`USBInterface.cpp:222-235`).
- **usb_raw** (`drivers/bus/usb/usb_raw.cpp:767-899`):
  - copies the packet descriptors in;
  - passes the user data pointer through unchanged;
  - calls `queue_isochronous` with `startingFrameNumber = NULL` and flags 0 (`:861-865`);
  - **holds `device->lock` from queueing until completion** (`:852-883`), waiting on the device's single `notify` semaphore (`:875`);
  - copies the descriptors back afterwards.

  So there is one isochronous transfer in flight per device. Every gap between transfers loses microframes, and control requests to the same device (the camera's controls, for instance) wait for the transfer to finish.
- **Stack.** `queue_isochronous` (`bus_managers/usb/usb.cpp:445-459`) calls `IsochronousPipe::QueueIsochronous` (`Pipe.cpp:299-337`). That resets each descriptor's `actual_length` and `status`, builds a `Transfer` (`SetData`, callback, `usb_isochronous_data`: descriptors, count, start-frame pointer, flags; `usbspec_private.h:53-58`) and calls `BusManager::SubmitTransfer`.
- **Packet descriptor** (`headers/os/drivers/USB_isochronous.h:12-16`): `int16 request_length`, `int16 actual_length`, `status_t status`. The flag `USB_ISO_ASAP` is 0x01 (`USB3.h:77-78`).
- **What a host controller must do:**
  - set each descriptor's `actual_length` and `status`;
  - put IN data for packet *i* at the sum of the earlier `request_length`s;
  - call `Finished(status, total)`, then delete the transfer;
  - for a user buffer, call `InitKernelAccess()` when the transfer is submitted and `PrepareKernelAccess()` in its own thread.

  `PrepareKernelAccess()` clones the whole user area into the kernel, once per transfer (`Transfer.cpp:166-229`). That costs one area clone per transfer: about 250 per second at 4 ms per transfer.
- **How the other host controllers treat high speed:**
  - **XHCI** requires every `request_length` ≤ `MaxPacketSize()` and their sum = the data length (`xhci.cpp:983-996`). Mult is not handled ("TODO: We do not currently take Mult into account at all!", `:1090`). With `USB_ISO_ASAP` or a NULL pointer it starts at MFINDEX/8 + 1 and writes that frame back (`:1094-1109`).
  - **EHCI** uses one packet size, data length / packet count, capped at `MaxPacketSize()` (`ehci.cpp:1164-1173`). It sets the iTD's MUL to `MaxBurst()` + 1 (`:1283`) and places packets at that fixed stride (`:3053-3116`).
  - So **no Haiku host controller carries more than one transaction's worth per packet today.** For the port: one descriptor per service interval, up to MPS × (MaxBurst + 1) bytes, as in Linux (3072, `hcd_ddma.c:508`).

### C.3 Applications and the image

| Piece | In this port's image? | Notes |
|---|---|---|
| usb_webcam.media_addon | yes (`build/jam/images/definitions/regular:229`; present in `haiku.hpkg` built 2026-11-01) | without UVC (§C.1) |
| CodyCam | yes (`regular:37, 60`; `haiku.hpkg`) | default video input (`GetVideoInput`, `CodyCam.cpp:234`), 320×240 `B_RGB32`; periodic captures and uploads |
| Cortex (demo) | yes (`regular:96`) | connects any producer to a consumer, e.g. to `video_window_demo.media_addon` (in the image) |
| Media preferences | yes | picks the default video input (`preferences/media/MediaWindow.cpp`) |
| usb_raw, dwc2 | yes (`build/jam/packages/Haiku:195`; `haiku.hpkg`) | |

`configs/haiku-UserBuildConfig` adds build features and packages but removes nothing from the regular image.

---

## D. What to build, in stages

1. **A probe tool, no media code** (about 150 lines; style of `tools/probes/`). From userland through usb_raw:
   - probe/commit (26 bytes) for YUY2 320×240 at 30 fps, then select alt 4;
   - read isochronous transfers; print the C920's dwMaxPayloadTransferSize per mode, its header length and flags, and the bytes per frame;
   - try MJPEG frames too.

   This answers the open questions about the camera before any big change.
2. **dwc2: high-speed isochronous IN, one transaction per microframe** (option A1, about 300-400 lines):
   - streams without HCSPLT; the bInterval period; per-microframe re-arm from the halt;
   - a timer to restart a broken chain; late packets marked `B_DEV_TOO_LATE`;
   - bigger slots; statistics in the stop summary, as for full speed.

   Test with the step 1 tool at alt 4.
3. **dwc2: high bandwidth** (about 100-150 lines):
   - MC 2-3 with DATA1/DATA2 start PIDs, `request_length` up to 3072;
   - Rx FIFO enlarged towards 1024 words (472 are free);
   - measure behaviour on early-ended microframes, FRMOVRUN counts, interrupt latency and CPU at 8,000 interrupts/s.

   Test alts 7-11 (YUY2 640×480 at 30 is alt 10).
4. **usb_raw: concurrent isochronous transfers** (about 100-150 lines). A completion semaphore and status per request, and no device lock while waiting; or a separate queue/wait command pair. The add-on can then keep 3-4 transfers queued (the dwc2 stream allows 4 today). Linux keeps 5 URBs of 32 packets in flight (`uvcvideo.h:54-56`).
5. **usb_webcam UVC** (about 400-600 lines):
   - enable it in the Jamfile;
   - fix bugs 1-4 and 6-8 of §C.1;
   - a pump with 32-64 packets of MPS × mult per transfer, on 2-4 threads or with asynchronous transfers;
   - YUY2 → `B_RGB32` (or offer `B_YCbCr422`);
   - sizes and frame rates beyond 320×240 and 5 fps in the Producer.

   Test with CodyCam and with Cortex → video window.
6. **MJPEG** (about 150-300 lines): decode with the Translation Kit's JPEG translator or with FFmpeg's mjpeg decoder (`ffmpeg6` is in the image). Insert the K.3.3 tables if DHT is missing. Measure the CPU cost of 720p and 1080p at 30 fps on the A53 cores. The VDEC_1 MJPEG path (`docs/m25-vdec-feasibility.md:44-47`) is optional.
7. **Hardening.**
   - Unplug while streaming; stop/start; alternate-setting changes.
   - Coexistence with HID devices and the USB audio adapter on the same hub.
   - Optional: the C920's microphone through usb_audio. It is high-speed isochronous with an 8-microframe interval, the same code path.

### Open questions

- **The C920 itself:** its dwMaxPayloadTransferSize per mode, its header length, whether it sets EOF, whether its MJPEG frames include DHT, and its typical MJPEG frame sizes. Step 1 answers these.
- **DWC2 behaviour:** HCTSIZ and HCINT after a high-bandwidth IN microframe ends with a full-size DATA0 or DATA1; the latest point in a microframe at which an enabled periodic channel still runs in it; FRMOVRUN against a halt with no reason bit when armed late. The databook was not available.
- **Rx FIFO:** whether 512 words carry 3 × 1024 bytes per microframe without errors.
- **GHWCFG2 queue depths:** 4 or 8 entries? And what GHWCFG4 bit 31 means.
- **Load:** interrupt latency and CPU cost at 8,000 halts/s with the desktop, VNC and USB audio running. Whether option A1 is enough, or A2/A3 is needed.
- **usb_raw:** whether usb_raw and usb_audio may use the same device at once (the camera's video and microphone).

---

## E. Downloads and sources

**Downloads** (each in its own directory under a scratch directory, `uvc-research/`; none was run):

| Directory | File | Source URL | Bytes |
|---|---|---|---|
| linux-dwc2 | hw.h, hcd.h, hcd.c, hcd_ddma.c, hcd_intr.c, hcd_queue.c, params.c, core.h, core.c | `https://raw.githubusercontent.com/torvalds/linux/cbbddb6b08cc9900a87cccaa9b22bbe06aa4f50a/drivers/usb/dwc2/<file>` | 30,495; 27,055; 167,954; 37,108; 67,027; 63,870; 31,276; 64,955; 34,904 |
| linux-uvc | uvc_video.c, uvc_driver.c, uvcvideo.h | `…/cbbddb6b08cc9900a87cccaa9b22bbe06aa4f50a/drivers/media/usb/uvc/<file>` | 70,895; 94,996; 21,852 |
| linux-uvc | video.h | `…/cbbddb6b08cc9900a87cccaa9b22bbe06aa4f50a/include/uapi/linux/usb/video.h` | 19,116 |
| freebsd-dwc_otg | dwc_otg.c | `https://raw.githubusercontent.com/freebsd/freebsd-src/63302e904a52a30692659a9144825b657269aa4c/sys/dev/usb/controller/dwc_otg.c` | 117,903 |
| uvc11-spec | USB_Video_Class_1_1_090711.zip (17 PDFs; read: Class 1.1, Payload Uncompressed 1.1, Payload MJPEG 1.1, FAQ 1.1, converted with `pdftotext`) | `https://www.usb.org/sites/default/files/USB_Video_Class_1_1_090711.zip` | 7,111,972 |

Commit IDs were taken from the GitHub API, `repos/<repo>/commits/<branch>`; the last change to `drivers/usb/dwc2` is dated 2026-10-05.

**In this repo:**
- `haiku/src/add-ons/kernel/busses/usb/dwc2.cpp`, `dwc2.h`, `dwc2_hardware.h`, `dwc2_isochronous.cpp`, `ehci.cpp`, `xhci.cpp`, `uhci.cpp`, `ohci.cpp`
- `haiku/src/add-ons/kernel/bus_managers/usb/` (`Pipe.cpp`, `Device.cpp`, `Transfer.cpp`, `usb.cpp`, `usb_private.h`, `usbspec_private.h`)
- `haiku/src/add-ons/kernel/drivers/bus/usb/usb_raw.cpp`, `usb_raw.h`
- `haiku/src/kits/device/USBEndpoint.cpp`, `USBInterface.cpp`
- `haiku/headers/os/drivers/USB3.h`, `USB_isochronous.h`, `usb/USB_video.h`
- `haiku/src/add-ons/media/media-add-ons/usb_webcam/` (all files)
- `haiku/src/apps/codycam/`, `haiku/build/jam/images/definitions/regular`, `haiku/build/jam/packages/Haiku`
- `configs/haiku-UserBuildConfig`
- the built `generated.arm64/.../packaging/packages/haiku.hpkg`, listed with the host `package` tool
- `docs/m5-usb-dwc2-design.md`, `docs/milestones.md` (M12 and the high-speed isochronous assessment)
- `logs/m1-2026-10-27.log`
