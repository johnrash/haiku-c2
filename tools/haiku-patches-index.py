#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# haiku-patches-index.py: writes docs/haiku-fixes.md, an index of the
# Haiku patches in haiku-patches/ by area, from their subjects.
#   haiku-patches-index.py [<output>]
import email
import os
import re
import sys
from email import policy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCHES = os.path.join(ROOT, "haiku-patches")

AREAS = [
	("Boot, kernel and system", r"^(arm64|efi|kernel|scheduler|libroot|vfs_boot|fdt|device_manager|power|Shutdown|build|TEMPORARY|registrar|autologin|debugger|Debugger|tty)"),
	("Storage and packages", r"^(mmc|meson_gx_mmc|bfs|block_cache|disk device manager|package)"),
	("USB", r"^(dwc2|usb_hid|usb_raw|usb\b|usb:|BSerialPort)"),
	("Network and Wi-Fi", r"^(meson_dwmac|net_server|arp|tcp|network|net_socket|net notifications|DHCPClient|freebsd_network|freebsd_wlan|realtekwifi|Wi-Fi|libbnetapi|Revert \"libbnetapi|ifconfig|Time)"),
	("Audio, video and cameras", r"^(usb_audio|multi_audio|mixer|media|ffmpeg|MediaPlayer|VideoRecorder|meson_vdec|meson_decoder|meson_encoder|meson_venc|meson_canvas|usb_webcam|CodyCam|NeoCam|WebPTranslator)"),
	("Display, graphics and the desktop", r"^(meson_hdmi|app_server|meson_mali|graphics|interface kit|ScreenSaver|Deskbar|Tracker|BListView|AboutSystem|Terminal)"),
	("The 40-pin header, sensors and remotes", r"^(meson_gpio|meson_i2c|meson_uart|meson_header|meson_saradc|meson_scpi|pcf8563|libdevice|meson_ir|Remote Control)"),
]


def subject(path):
	with open(path, encoding="utf-8", errors="replace") as f:
		message = email.message_from_file(f, policy=policy.default)
	text = re.sub(r"\s+", " ", str(message["subject"]))
	return re.sub(r"^\[PATCH[^\]]*\] ", "", text)


def main():
	output = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs",
		"haiku-fixes.md")
	groups = {name: [] for name, _ in AREAS}
	groups["Other"] = []
	names = sorted(n for n in os.listdir(PATCHES) if n.endswith(".patch"))
	for name in names:
		text = subject(os.path.join(PATCHES, name))
		area = next((a for a, pattern in AREAS if re.match(pattern, text)),
			"Other")
		groups[area].append((name, text))

	lines = ["# The Haiku patches by area", "",
		"The %d patches in [`haiku-patches/`](../haiku-patches) are the port's"
		% len(names),
		"commits on top of upstream Haiku a12d4cd1a5 (hrev60299+6), in order;",
		"`tools/haiku-setup.sh` applies them. Each one is a `git format-patch`",
		"file with its full commit message: what was wrong, how it was found,",
		"and how it was tested. Most are specific to the ODROID-C2 (Amlogic",
		"S905) or to arm64; many fix bugs in Haiku's common code that any",
		"platform can hit. This list is made by `tools/haiku-patches-index.py`.",
		""]
	for area in [a for a, _ in AREAS] + ["Other"]:
		if not groups[area]:
			continue
		lines += ["## %s (%d)" % (area, len(groups[area])), ""]
		for name, text in groups[area]:
			lines.append("- [%s](../haiku-patches/%s) %s"
				% (name[:4], name, text.replace("|", "\\|")))
		lines.append("")
	with open(output, "w", encoding="utf-8") as f:
		f.write("\n".join(lines))
	print("haiku-patches-index: %d patches in %s" % (len(names), output))


if __name__ == "__main__":
	main()
