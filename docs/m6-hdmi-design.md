# M6 design notes: native HDMI display (VPU, ENCP, HDMI PLL, DesignWare HDMI TX)

Research for M6 item 4, written 2026-10-28. **Nothing here is implemented.** Compare with `m6-ethernet-design.md`. The goal is for Haiku to set HDMI modes itself, 1920x1080@60 first and the other EDID modes after, instead of keeping the 1280x720 mode U-Boot picked.

## Sources and licences
**Usable:**
- **Amlogic S905 Datasheet rev 1.1.4** (the copy Hardkernel distributes; mirror: `cgi.cse.unsw.edu.au/~cs9242/24/project/s905.pdf`). It covers the memory map (ch. 17), power and memory power-down (ch. 18), all HHI clock, HDMI PLL and HDMI PHY registers with bit fields (ch. 22.4), reset bits, **the full ENCP/VENC register set** (ch. 29, "CVBS") and the DMC canvas. It has **no VIU/OSD, VPP, VPU_HDMI_SETTING or HDMITX TOP**, and its interrupt table disagrees with the DT, so trust the DT.
- **Amlogic S905X Datasheet rev 0.2** (same VPU family; mirror: `scs.stanford.edu/~zyedidia/docs/amlogic/s905x.pdf`). §27.2 covers the VPU: VIU OSD1, VPP, VPU_HDMI_SETTING, VPU_VIU_VENC_MUX_CTRL. §27.4 covers the **HDMITX address/data/control ports and the TOP registers** with bit fields. It says the Synopsys core registers are only in vendor documents.
- **NetBSD `sys/dev/ic/dw_hdmi.c`, `dw_hdmi.h`, `dw_hdmi_phy.c`** (BSD-2): the DW core sequence (packetizer, frame composer, sampler, main controller), DDC/EDID reads with segment pointer, and a glue-hook model (enable/disable/mode set/detect) that already supports a non-Synopsys PHY (`sunxi_dwhdmi.c`). There is no register-access hook: it only knows widths 1 and 4, so the indirect port needs our own accessor. **Bug:** `I2CM_SS_SCL_HCNT_0/LCNT_0` are wrong there (0x730c/0x730e; they should be 0x7e0c/0x7e0e). Its `FC_INVIDCONF_DVI_MODE` (bit 3) really means 1 = HDMI.
- **FreeBSD `sys/dev/hdmi/dwc_hdmireg.h`** (BSD-2): the most complete BSD DW register header (PRODUCT_ID, IH_MUTE_*, AVI infoframe, I2CM SCL counts). It has no CSC registers.
- **NetBSD `sys/arch/arm/amlogic/meson_genfb.c`, `meson_vpureg.h`, `meson_canvasreg.h`** (BSD-2, Meson8b): OSD bit fields (block mode, colour matrix), canvas layout and VPP scaler registers. It adopts the firmware mode only.
- **FreeBSD `aml8726_fb.c`** (BSD-2, `stable/12`): OSD1 and canvas offsets, as a second check.
- **NXP i.MX6DQ Reference Manual** ch. 33 "HDMI Transmitter": the same Synopsys controller, with byte-wide registers at the same offsets. *Unverified:* not opened here.
- **Linux DT `meson-gx*.dtsi`** (GPL-2.0+ OR MIT) for addresses, interrupts, resets and clocks.
- **Haiku's own (MIT)** `accelerants/common` (EDID decode, mode-list builder).

**Fact only:** Linux `drivers/gpu/drm/meson/*` and `bridge/synopsys/dw-hdmi*`, and their U-Boot port `drivers/video/meson/*` and `dw_hdmi.c` (GPL-2.0), which I read in the U-Boot tree. Anything marked **(from Linux, fact only)** exists only there. That means a register value or field, never code. Write the driver from the tables in this file.

**Avoid:** NetBSD `sys/dev/videomode/videomode.c` (no licence text). No BSD has an Amlogic HDMITX TOP or Amlogic HDMI PHY driver, so those parts are new code.

## Hardware on the ODROID-C2
All addresses are from `meson-gx.dtsi`/`meson-gxbb.dtsi` plus U-Boot's `meson-gx-u-boot.dtsi`. Haiku is booted with `bootefi ... ${fdtcontroladdr}`, so it gets **U-Boot's control DT**, which has the U-Boot additions (vpu has 3 `reg` entries, hdmi-tx has 2).

| Block | Address (size) | DT node / notes |
|---|---|---|
| VPU (VIU, VPP, VENC) | 0xd0100000 (1 MiB) | `vpu@d0100000`, `amlogic,meson-gxbb-vpu`. Registers are 32-bit; byte offset = **word number × 4** (Amlogic docs give word numbers) |
| HHI (clocks, PLLs, PHY, mem power) | 0xc883c000 (0x1000 used) | vpu `reg[1]` "hhi"; also `hiubus` |
| DMC canvas LUT | 0xc8838048 (0x14) | `video-lut@48`; U-Boot DT gives vpu `reg[2]` "dmc" = 0xc8838000 |
| HDMI TX (top glue + DW core) | 0xc883a000 (0x1c) | `hdmi-tx@c883a000`, `amlogic,meson-gxbb-dw-hdmi`, status okay on the C2 |
| Reset controller | 0xc1104404 (pulse); level registers probably at 0xc1104480 (*Unverified*) | reset *n* = register *n*/32, bit *n*%32. Not needed: U-Boot already released everything |

- **Interrupts:** VPU vsync = GIC SPI 3, edge, Haiku **INTID 35**. HDMI TX = GIC SPI 57, edge, **INTID 89**.
- **HDMI TX resets:** 19 (`hdmitx_apb`), 79 (`hdmitx` system), 66 (`hdmitx_phy`). **VPU power-domain resets:** VIU 5, VENC 10, VCBUS 13, RDMA 133, VENCI 134, VENCP 135, VENCL 141 and others.
- **Clock gates** (S905 datasheet gate table, ch. 22): HHI_GCLK_MPEG2 (+0x148) bit 4 = HDMI PCLK, bit 3 = HDMI interrupt sync, bit 25 = VPU interrupt; HHI_GCLK_OTHER (+0x150) holds the VENC gates (the DT's "venci" clock for hdmi-tx is bit 8). The HDMI system clock ("isfr", 24 MHz from XTAL) is HHI_HDMI_CLK_CNTL (+0x1cc) bit 8. The DT sets the VPU clock to 666 MHz (fclk_div3, HHI_VPU_CLK_CNTL +0x1bc) and VAPB to 250 MHz (HHI_VAPBCLK_CNTL +0x1f4).
- **Power domain** `PWRC_GXBB_VPU_ID` (datasheet ch. 18): AO 0xc81000e8 bit 8 = VPU/HDMI power, bit 9 = isolation; HHI_VPU_MEM_PD_REG0/1 (+0x104/+0x108); HDMI memories in HHI_MEM_PD_REG0 (+0x100) bits 15:8, 0 = powered.
- **Board:** HPD and DDC pins are muxed to `hdmi_hpd`/`hdmi_i2c` by U-Boot; HDMI 5 V is a fixed regulator. The sink is the MS2130 capture dongle; its **first detailed timing is 1280x720** (U-Boot takes the first valid DTD), so EDID "preferred" will be 720p and 1080p must be chosen explicitly. *Unverified until the EDID is dumped.*

### What U-Boot leaves running
- **Everything, and it survives ExitBootServices.** `efi_exit_boot_services()` only calls `dm_remove_devices_active()`, which removes devices flagged `DM_FLAG_OS_PREPARE`/`ACTIVE_DMA`. `meson_vpu` has `DM_FLAG_PRE_RELOC | DM_FLAG_LEAVE_PD_ON`, `meson_dw_hdmi` has no remove hook. Confirmed in practice: Haiku's desktop runs on this pipeline today.
- So the power domain, resets, gates, pinmux, VPU clock, HDMI system clock, HDMI TX init (CSC, video packetizer, sampler, interrupts muted) and PHY setup are all done. **A driver only has to change mode-dependent registers.** It must never reset or power-cycle these blocks.
- **U-Boot's mode** (`logs/*`: `HDMI: 1280x720, 74250000 Hz pixel clock`): 1280x720@60, x8r8g8b8 (B_RGB32; `screenmode` prints "24 bits" for B_RGB32, `ScreenMode.cpp:134`), stride 5120, canvas **0x4e**.
  - **Frame buffer:** 0x7fc6c000, 0x394000 bytes (1280·720·4 + 64 KiB), at the end of RAM and `EfiReservedMemoryType` in the memory map. Too small for 1080p.
  - **GOP size bug:** the GOP reports FrameBufferSize = 3840·2160·4 (U-Boot's maximum), which runs 30 MiB past the end of RAM. The kernel log line `mark_page_range_in_use(0x7ec6c, 0x1fa4): range would extend past free list` is that range. The driver must use width·height·4, not the GOP size. *Inferred from logs.*
  - **Clock path** (U-Boot always uses its "DMT" path): HDMI PLL VCO 5940 MHz, post-dividers OD1 = 4, OD2 = 2, OD3 = 1, VID_PLL /5, VCLK /2; ENCP and HDMI TX pixel clock from vclk_div1.

## Display pipeline
`DDR → canvas 0x4e → VIU OSD1 (32-bit ARGB) → VPP postblend + RGB→YUV matrix → ENCP timing generator → VPU_HDMI_SETTING FIFO (CbYCr) → DW HDMI TX (CSC YCbCr→RGB, frame composer) → Amlogic HDMI PHY → TMDS`

Clocks: `XTAL 24 MHz → HDMI PLL (×2 pre-mult, M + frac/4096) → OD1 → OD2 = TMDS clock (PHY) → OD3 → VID_PLL_DIV /5 → VCLK_DIV → cts_encp and hdmi_tx_pixel`. TMDS = VCO/OD1/OD2 fits every entry of the Amlogic clock tables. *Inferred, unverified.*

### 1. Frame buffer and canvas (DMC, 0xc8838000)
| Offset | Register | Fields |
|---|---|---|
| 0x48 | DMC_CAV_LUT_DATAL | [28:0] address / 8; [31:29] low 3 bits of width/8 |
| 0x4c | DMC_CAV_LUT_DATAH | [8:0] rest of width/8; [21:9] height; [23:22] wrap; [25:24] block mode (0 = linear) |
| 0x50 | DMC_CAV_LUT_ADDR | [7:0] canvas index; bit 9 = write; then read DATAH back to flush |

The address field limits the buffer to the low 4 GiB (the C2 has 2 GiB). The buffer must be **physically contiguous** (one base and stride). These are in the S905 datasheet's DMC section (word 0x12–0x14).

### 2. OSD1 plane and VPP (VPU, word numbers)
| Word | Register | 1080p value / note |
|---|---|---|
| 0x1a10 | VIU_OSD1_CTRL_STAT | bit 21 OSD enable, bit 0 block 0 enable, global alpha 0xff at bit 12. Clear bit 21 to blank during a switch |
| 0x1a2d | VIU_OSD1_CTRL_STAT2 | bit 14 replace alpha with 0xff (for x8r8g8b8) |
| 0x1a1b | VIU_OSD1_BLK0_CFG_W0 | [23:16] canvas 0x4e, bit 15 little-endian, [11:8] = 5 (32 bpp), [5:2] = 1 (ARGB), bit 7 RGB in (GXBB). Mode independent |
| 0x1a1c / 0x1a1d | BLK0_CFG_W1 / W2 | source x and y: end (inclusive) in [31:16], start in [15:0]: 1919<<16, 1079<<16 |
| 0x1a1e / 0x1a13 | BLK0_CFG_W3 / W4 | destination window, same format (note W4 is 0x1a13) |
| 0x1d21 | VPP_POSTBLEND_H_SIZE | 1920 |
| 0x1d26 | VPP_MISC | bit 7 postblend enable, bit 12 OSD1 postblend (already set) |
| 0x1dc8 / 0x1dc2 / 0x1dc5 | VPP_OSD_SC_CTRL0, VSC_CTRL0, HSC_CTRL0 | 0: no scaler for progressive modes |

### 3. ENCP progressive encoder (VPU, word numbers)
U-Boot programs ENCP from any timing with simple arithmetic **(from Linux, fact only)**. Values for 1080p60 (CEA VIC 16: 148.5 MHz; H 1920/88/44/148, total 2200; V 1080/4/5/36, total 1125; +H +V). 1080p30 (VIC 34) is identical at 74.25 MHz.

| Word | Register | 1080p | 720p (U-Boot today) | Rule |
|---|---|---|---|---|
| 0x1b80 | ENCP_VIDEO_EN | 0 during the change, then 1 | | |
| 0x1b62 / 0x1b8d / 0x1b8e | VENC_DVI_SETTING / ENCP_VIDEO_MODE / _MODE_ADV | 0x21 / 0x4040 (bit 14 = DE active high) / 0x18 | same | constant |
| 0x1b97 | ENCP_VIDEO_MAX_PXCNT | 2199 | 1649 | htotal − 1 |
| 0x1bae | ENCP_VIDEO_MAX_LNCNT | 1124 | 749 | vtotal − 1 |
| 0x1ba4 / 0x1ba3 | HAVON_BEGIN / END | 192 / 2111 | 260 / 1539 | hsync+hback; + hactive − 1 |
| 0x1ba6 / 0x1baf | VAVON_BLINE / ELINE | 41 / 1120 | 25 / 744 | vsync+vback; + vactive − 1 |
| 0x1ba7–0x1bac | ENCP_VIDEO_HSO_BEGIN/END, VSO_BEGIN/END, VSO_BLINE/ELINE | 0/44, 30/50, 0/5 | 0/40, 30/50, 0/5 | encoder syncs |
| 0x1c3a–0x1c3d | ENCP_DE_H_BEGIN/END, DE_V_BEGIN/END_EVEN | 194/2114, 41/1121 | 262/1542, 25/745 | HAVON_BEGIN + 2 (latency); + hactive |
| 0x1c30/31, 0x1c32/34, 0x1c36/38 | ENCP_DVI_HSO_BEGIN/END, DVI_VSO_BLINE/ELINE_EVN, DVI_VSO_BEGIN/END_EVN | 2/46, 0/5, 2/2 | 2/42, 0/5, 2/2 | HDMI syncs: DE end + front porch (mod htotal) |
| 0x271b | VPU_HDMI_SETTING | 0x8e expected | same | bit 1 ENCP→HDMI, bits 2/3 invert H/V sync (set for +H/+V), [7:5] = 4 (CbYCr) |
| 0x271a | VPU_VIU_VENC_MUX_CTRL | ENCP selected | same | already set |

U-Boot's path writes 0 to the remaining timing words (YFP1/2_HTIME, HSPULS_*, VSPULS_*). **Stage 0 check:** compute the 720p column from the EDID timing and compare it with what U-Boot left in the registers before writing anything. The ENCP registers and their fields are in the S905 datasheet (ch. 29); the OSD, VPP and VPU_HDMI_SETTING registers are in the S905X datasheet (§27.2).

### 4. Clock tree (HHI, byte offsets from 0xc883c000)
| Offset | Register | Use |
|---|---|---|
| 0x320 | HHI_HDMI_PLL_CNTL | [8:0] M, bit 31 lock. Write 0x58000200 \| M (enabled, in reset), then [30:28] = 4 (out of reset) and poll bit 31 |
| 0x324 | HHI_HDMI_PLL_CNTL2 | [11:0] frac, bit 14 frac enable. Post-dividers: **[17:16] OD1, [23:22] OD2, [19:18] OD3** in Linux's naming (the datasheet calls them OD[1:0], OD[3:2] and OD2). Encoding: 0 = /1, 1 = /2, 2 = /4, 3 = /8 |
| 0x328–0x334 | HHI_HDMI_PLL_CNTL3–6 | fixed GXBB values 0x0d5c5091, 0x801da72c, 0x71486980, 0x00000e55 **(from Linux, fact only)** |
| 0x1a0 | HHI_VID_PLL_CLK_DIV | /5 pattern (bits 19 enable, 18 bypass, 15 preset, [17:16] shift sel, [14:0] shift value) |
| 0x164 / 0x17c / 0x194 | HHI_VID_CLK_DIV / VID_CLK_CNTL / VID_CLK_CNTL2 | VCLK divider [7:0], ENCP source [27:24]; VCLK enable bit 19, divN gates [4:0]; ENCP gate bit 2, HDMI TX pixel gate bit 5 |
| 0x1cc | HHI_HDMI_CLK_CNTL | [19:16] HDMI TX pixel source; [8] system clock enable |

- **GXBB PLL rule:** VCO = 48 MHz × (M + frac/4096); empirically M 53–123 **(from Linux, fact only)**.
- **720p → 1080p with U-Boot's settings:** VCO stays at 5940 MHz (M = 123 = 0x7b, frac = 0xc00). Only **OD2 changes from /2 to /1** (TMDS 742.5 → 1485 MHz, pixel 74.25 → 148.5 MHz). Nothing after the PLL changes, and the VCO should stay locked. *Unverified* that an OD change is glitch-free; blank first anyway.
- **Amlogic's own CEA table** (alternative): VCO 2970 MHz (M = 0x3d, frac = 0xe00); 720p OD 2/2/2, 1080p OD 1/2/2, VCLK /1. The driver needs only one scheme; keeping U-Boot's means fewer writes.

### 5. HDMI PHY (Amlogic's own, HHI)
| Offset | Register | GXBB values **(from Linux, fact only)** |
|---|---|---|
| 0x3a0 | HHI_HDMI_PHY_CNTL0 | pixel < 297 MHz: 0x33632122; 297–371 MHz: 0x33634283 |
| 0x3a4 | HHI_HDMI_PHY_CNTL1 | datasheet: bit 17 BIT_INVERT (set on GXBB), bit 3 FIFO write enable, bit 2 FIFO enable, bit 1 PHY clock enable, bit 0 soft reset. U-Boot sets [31:16] = 0x0390 \| bit 17, then pulses [3:0] = 0xf → 0xe three times, 2 ms apart |
| 0x3ac | HHI_HDMI_PHY_CNTL3 | pixel < 297 MHz: 0x2000115b; 297–371 MHz: 0xb000115b |

720p and 1080p use the **same PHY values**; only the reset pulse needs repeating after the clock change. *Unverified whether even that is needed.*

### 6. HDMI TX top glue (indirect access)
| Offset at 0xc883a000 | Port |
|---|---|
| 0x00 / 0x04 / 0x08 | TOP address / data / control |
| 0x10 / 0x14 / 0x18 | DW controller address / data / control |

- **Access:** the S905X datasheet (§27.4) documents write address, then read or write data. Control bit 15 = PREADY watchdog (U-Boot sets it), bit 12 = stack mode for access from interrupts. **GXBB quirk (from Linux, fact only):** write the address **twice**; for reads, read the data port twice and keep the second value. DW controller registers are 8 bits wide through this port. Serialize all access with one spinlock, since it is a two-step sequence.
- **TOP registers** (S905X §27.4): SW_RESET 0x00 (0 = all running), CLK_CNTL 0x01 (pixel/TMDS clock enables), HPD_FILTER 0x02, INTR_MASKN 0x03, INTR_STAT 0x04, INTR_STAT_CLR 0x05 (bit 0 core, 1 HPD rise, 2 HPD fall), BIST_CNTL 0x06 ([14:12] = 1 normal TMDS, 0 = output zero), TMDS_CLK_PTTN 0x0a–0x0c, **STAT0 0x0e bit 0 = filtered HPD**.
- **HPD comes from TOP STAT0, not from the DW PHY_STAT0**, because the Synopsys PHY is not used.

### 7. DesignWare HDMI TX controller (offsets through the DWC port)
| Offset | Register | 1080p (720p) |
|---|---|---|
| 0x0000–0x0003 | DESIGN_ID, REVISION_ID, PRODUCT_ID0/1 | read and log at attach; the version decides the erratum below |
| 0x1000 | FC_INVIDCONF | bit 6 V+, bit 5 H+, bit 4 DE high, bit 3 HDMI (1) / DVI (0), bit 0 interlace |
| 0x1001/2, 0x1003/4 | FC_INHACTV0/1, FC_INHBLANK0/1 | 1920, 280 (1280, 370) |
| 0x1005/6, 0x1007 | FC_INVACTV0/1, FC_INVBLANK | 1080, 45 (720, 30) |
| 0x1008/9, 0x100a/b | FC_HSYNCINDELAY0/1, FC_HSYNCINWIDTH0/1 | 88, 44 (110, 40) |
| 0x100c, 0x100d | FC_VSYNCINDELAY, FC_VSYNCINWIDTH | 4, 5 (5, 5) |
| 0x1019–0x101c | FC_AVICONF0–2, FC_AVIVID | AVI infoframe. U-Boot doesn't program it; optional later (VIC 16, or 4 for 720p) |
| 0x4002 | MC_SWRSTZ | write with bit 1 clear = TMDS soft reset after the timing change |
| 0x4100.. | CSC_CFG, CSC_SCALE, coefficients | YCbCr→RGB, BT.601, set by U-Boot; mode independent, leave alone |

- **Overflow erratum:** after a TMDS reset, some DW versions need FC_INVIDCONF rewritten several times or the frame composer stalls. NetBSD does it 4 times, for version 0x130a only. The count for our version is *Unverified*; rewriting it 4 times is harmless.
- **Main-controller values (NetBSD):** FC_CTRLDUR 12, FC_EXCTRLDUR 32, FC_EXCTRLSPAC 1, preambles 0x0b/0x16/0x21, MC_FLOWCTRL 0 (CSC bypass in NetBSD; ours must stay on), MC_CLKDIS 0x74. U-Boot already wrote all of these; they don't depend on the mode.

### 8. HPD and EDID over DDC (DW I2C master)
| Offset | Register | Use |
|---|---|---|
| 0x7e00 / 0x7e01 | I2CM_SLAVE / I2CM_ADDRESS | 0x50; byte offset |
| 0x7e03 | I2CM_DATAI | read data |
| 0x7e04 | I2CM_OPERATION | bit 0 read, bit 1 extended (segment) read |
| 0x7e08 / 0x7e0a | I2CM_SEGADDR / I2CM_SEGPTR | 0x30; EDID segment for blocks 2 and up |
| 0x7e0b–0x7e0e | I2CM_SS_SCL_HCNT/LCNT | U-Boot: high 0x67, low 0x78 **(from Linux, fact only)** |
| 0x0105 / 0x0185 | IH_I2CM_STAT0 / IH_MUTE_I2CM_STAT0 | bit 1 done, bit 0 error; write 1 to clear |

- Read 128 bytes per block, one byte per operation. Read block 0, then the extensions; check each checksum.
- **The dongle's EDID is unreliable right after power-up.** Apply the U-Boot patch's rule: accept only two identical reads 500 ms apart.

## Haiku side
- **Kernel driver** `drivers/graphics/meson_hdmi`: a device-manager driver on the FDT bus, like `virtio_gpu` (`supports_device`/`register_device`; publishes `graphics/meson_hdmi/0`).
  - **Matching:** match the vpu node (`amlogic,meson-gxbb-vpu`), which carries the VPU, HHI and DMC `reg` entries in U-Boot's DT, and `translate_fdt_address` it as `meson_gx_mmc` does. Find hdmi-tx (`amlogic,meson-gxbb-dw-hdmi`, status okay) as a sibling node. Fall back to the fixed addresses above if U-Boot's extra `reg` entries are missing. The FDT bus adds one `fdt/compatible` attribute per string, and the first is the one we want for both nodes.
  - **Coexistence:** app_server skips `graphics/framebuffer` and `graphics/vesa` while scanning and uses them only as a fallback (`AccelerantHWInterface.cpp:228-229, 274-281`). A native device therefore wins, and `framebuffer` can stay in the image.
- **Mode setting lives in the kernel**, not in the accelerant. The indirect HDMI ports need a lock, the snapshot/restore and the KDL command need one owner, and the accelerant stays small.
  - **Shared info area** (`B_CLONEABLE_AREA`): current `display_mode`, frame buffer area and physical address, bytes per row, and the **raw EDID with all extension blocks**.
  - **ioctls:** `B_GET_ACCELERANT_SIGNATURE`, GET_SHARED_INFO, SET_DISPLAY_MODE, READ_EDID (re-read on demand), RESTORE_BOOT_MODE.
- **Accelerant** `meson_hdmi.accelerant`, modelled on `accelerants/virtio` (the smallest EDID-driven one).
  - **Hooks app_server requires** (`AccelerantHWInterface.cpp:329, 363-381`): INIT_ACCELERANT, ACCELERANT_MODE_COUNT, GET_MODE_LIST, GET_FRAME_BUFFER_CONFIG, SET_DISPLAY_MODE, GET_DISPLAY_MODE, GET_PIXEL_CLOCK_LIMITS.
  - **Hooks to add:** GET_EDID_INFO (`EDID_VERSION_1`), GET_PREFERRED_DISPLAY_MODE, PROPOSE_DISPLAY_MODE, GET_TIMING_CONSTRAINTS, the three clone hooks, and later ACCELERANT_RETRACE_SEMAPHORE (from the vsync interrupt) and DPMS.
  - **Preferred mode:** return 1920x1080@60 when the EDID lists it. Otherwise app_server's startup order (stored config, preferred, 1024x768) would keep the dongle's 720p first DTD.
- **Mode list:** `edid_decode` then `create_display_modes()` from `libaccelerantscommon`, with `B_RGB32` only and a check hook:
  - progressive only; 640–1920 × 480–1200;
  - pixel clock ≤ 165 MHz at first (U-Boot's lowest PHY band, which goes up to 297 MHz);
  - only clocks the PLL can make.

  It **ignores CTA-861 VICs** (base block only), so if 1080p60 appears only as a CTA short descriptor, add VICs 4/16/34 ourselves. Read the EDID in the kernel through I2CM; don't use `ddc2_read_edid1`, which reads 128 bytes only.
- **Frame buffer memory:**
  - **Stage 1 adopts U-Boot's buffer.** Its physical address is `kernel_args.frame_buffer.physical_buffer.start`; size it as width·height·4.
  - **Later: one buffer for the largest mode** (1920·1200·4, about 9 MB), allocated once when the driver initialises during boot, before memory fragments. Use `create_area(B_CONTIGUOUS | B_FULL_LOCK)`, then `get_memory_map` for the physical address.
  - **Mapping:** map those pages again with `map_physical_memory(... | B_WRITE_COMBINING_MEMORY)` (Normal non-cacheable on arm64), and give app_server **that** area to clone. A clone of the anonymous contiguous area comes back write-back (`vm.cpp:2609-2634`).
  - **Cache:** the VPU does not snoop caches, and all RAM also has a write-back alias at `KERNEL_PMAP_BASE`. Run `dc civac` over the buffer once after allocation, as `meson_gx_mmc` does.
  - **Speed:** app_server draws into a RAM back buffer and copies to the front, so the uncached front buffer is not read on the hot path.
  - **After each mode set,** call `frame_buffer_update()` so KDL draws into the new buffer, as `framebuffer.cpp:68` does. Keep U-Boot's buffer (EfiReserved) untouched, for restore.

## Recovery when a mode set fails
The capture dongle is the only display, so every failure has to be visible and reversible without it.
- **Boot-state snapshot:** at attach, save every register the driver may ever write: about 60 VPU words, 12 HHI words, 25 DW bytes and the canvas. Restoring them, with U-Boot's buffer, gives exactly U-Boot's 720p picture back.
- **Mode-set order** (each step logged to serial in debug builds):
  1. Blank: clear OSD1 enable, then `ENCP_VIDEO_EN` = 0.
  2. PLL, then wait for lock.
  3. ENCP, DE and syncs.
  4. DW frame composer, TMDS soft reset, FC_INVIDCONF rewrite.
  5. PHY reset pulse.
  6. `ENCP_VIDEO_EN` = 1.
  7. Canvas, OSD window and postblend size; enable OSD1.
- **Self-check after a mode set:** the PLL locks within 10 ms, HPD (TOP STAT0) is still 1, and vsync interrupts (INTID 35) arrive at the new rate, counted for 100 ms. If any check fails, restore the snapshot and return an error. app_server then falls back to the previous mode (`VirtualScreen.cpp:593-607`).
- **Failures the registers can't show** (the dongle says "No signal"):
  - From the remote shell: `screenmode 1280 720 60` (there is no Screen preferences dialog to click).
  - From KDL over serial: a `meson_hdmi` debugger command that dumps the state, plus `meson_hdmi restore`, which puts the boot state back even if app_server is stuck.
  - The dongle needs about 10 s to lock, so judge a mode by a frame grabbed at least 15 s later.
- **A bad mode stored by app_server** would come back on every boot. Two escapes:
  - a driver settings file (`meson_hdmi`: `mode_setting off`), under which the driver only adopts U-Boot's mode;
  - the boot menu's "fail-safe video mode" (`B_SAFEMODE_FAIL_SAFE_VIDEO_MODE`), under which the driver doesn't attach at all and app_server uses `framebuffer`.
- **Last resort:** KDL `reboot` (PSCI works) or the relay.

## Plan
About **1.5–2.5k lines**: a kernel driver of about 1–1.5k and an accelerant of about 0.5–0.8k. Every stage leaves a working desktop, and the next stage starts only when the previous one is solid.

| # | Step | Pass when |
|---|---|---|
| 0 | Read-only attach: map the blocks, log the IDs, take the snapshot, add the KDL dump | DW DESIGN/REVISION ID logged; HPD = 1; PLL locked with M 0x7b, frac 0xc00, OD 4/2/1; ENCP registers equal the computed 720p column; canvas 0x4e → 0x7fc6c000, stride 5120 |
| 1 | Driver + accelerant that **adopt U-Boot's mode**; EDID over I2CM | Desktop runs on `graphics/meson_hdmi/0` (syslog), not on `framebuffer`; EDID blocks logged with valid checksums, two reads identical; the mode list is logged |
| 2 | **1920x1080@30 (VIC 34) at the unchanged 74.25 MHz clock:** new buffer, canvas, OSD window, postblend, ENCP and DE/sync, frame composer, AVI VIC | A grabbed frame shows a 1920x1080 desktop; `screenmode 1280 720 60` goes back, and `meson_hdmi restore` works |
| 3 | **1920x1080@60:** change only OD2 (/2 → /1), then pulse the PHY reset | 1080p60 desktop; 10 switches between 720p and 1080p with no loss; KDL draws correctly at 1080p |
| 4 | **Full EDID list:** general PLL solver (VCO range, OD split, VID_PLL /5, VCLK), PHY band from the TMDS rate, HPD interrupt (INTID 89) with EDID re-read, retrace semaphore, DPMS (BIST output-zero, PHY off) | Every listed mode up to 165 MHz shows correctly (for example 1024x768, 1280x1024, 1680x1050); unplugging and replugging the dongle restores the picture |

Stage 2 is the key split. 1080p30 has exactly the 1080p60 timing (2200 x 1125) at half the clock, so every VPU, ENCP and frame-composer value is already final before the PLL is touched. If the dongle refuses 1080p30, use 1280x720@50 (VIC 19, also 74.25 MHz) to prove the same paths.

## Risks
- **The only display is a capture dongle.** It locks slowly (about 10 s), serves a half-initialised EDID after power-up, and may reject modes it doesn't list. Mitigations: the snapshot, the self-checks, the KDL restore, and grabbing frames late.
- **Undocumented magic numbers.** HDMI PLL CNTL3–6 and PHY CNTL0/CNTL3 come only from GPL code; the datasheet has the fields but not the values. Keep U-Boot's live values (stages 1–3 need no new ones) and treat new PHY bands as experimental.
- **DW frame-composer stalls** after a TMDS reset (the overflow erratum): no picture although the registers look right. Rewrite FC_INVIDCONF and check the version.
- **Frame buffer.** It needs 9 MB contiguous, so allocate it at boot. Clones of contiguous areas come back write-back, so map it write-combining. The write-back alias needs a `dc civac` pass. The GOP's reported size is wrong.
- **Register latching.** The OSD and ENCP may latch some registers only at vsync. Blanking during the switch avoids garbage, but a half-applied mode is possible. *Unverified.*
- **Colour path.** The VPU converts RGB to YCbCr and the DW CSC converts back with BT.601 coefficients. This doesn't depend on the mode, but check the colours with a test pattern at 1080p.
- **HDMI vs DVI.** FC_INVIDCONF bit 3 and the AVI infoframe must match the sink. Keep U-Boot's choice until stage 4.
- **No emulator.** QEMU has no Amlogic VPU, so every step is a board iteration. The read-only stage 0 keeps the first iterations safe.
