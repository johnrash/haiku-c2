# Milestones

The port was built in numbered milestones, M0 to M90, from October to
November 2026. The tools, the docs and the Haiku patches refer to them as
"Mxx". Some code comments in the patches still say "PLAN.md Mxx": PLAN.md
was the working log the milestones come from, and it is not published.

Numbers like +257 are the fork's versions: the count of commits after
upstream's hrev60299 (`git describe --tags` gives `hrev60299-257-g...`). The
patches in `haiku-patches/` are numbered differently. Patch 0001 is the
first commit after the upstream base a12d4cd1a5 (hrev60299+6), and the
private fork's internal artwork (+67, +80, +81) isn't in the series. So up
to +66, +N is patch N-6; from +68 to +79 it's patch N-7; and from +82 on
it's patch N-9. A tree built from the patches therefore calls itself
hrev60299+349 where the private fork says +352.

"The test board" is the C2 on the bench's crossover cable (10.42.0.2), with
a serial console and a power relay. "The LAN boards" are other C2s on an
ordinary home network, updated from the port's own package repository.

Some milestones were work on apps that aren't tied to the C2 and aren't
published here; they're listed as such.

- **M0:** Build environment
- **M1:** Haiku loader runs on the C2 (no Haiku code changes)
- **M2:** Amlogic UART, the first sign of life
- **M3:** Kernel bring-up on real hardware
- **M4:** SD/eMMC driver, booting to the desktop
- **M4.5:** Make storage solid
- **M5:** Ethernet and USB
- **M6:** standalone use, faster USB, CPU clock and temperature, native HDMI
- **M7:** SSH, a VNC server (an app not published here), and a self-contained microSD image
- **M8:** more apps, OpenGL, and the A53 erratum fixes for the prebuilt packages
- **M9:** easy wins, NEON, video overlays, a package repository
- **M10:** storage performance
- **M11:** growing the system partition
- **M12:** USB audio
- **M13:** HDMI audio
- **M14:** internal artwork for the port's own builds (not part of this repository)
- **M14b:** internal artwork for the port's own builds (not part of this repository)
- **M15:** crashes and tearing found on two C2s
- **M16:** Tracker breadcrumbs
- **M17:** kernel panic at shutdown in TCP
- **M18:** Tracker names wrap in icon view
- **M19:** work on an app that isn't part of this repository
- **M20:** a list of fixes: first-boot resize, Mini icon view, the USB audio adapter's buttons, 44.1 kHz, heavy video stutter; power-off checked
- **M21:** items 2-6 of the list after M20, simplest first
- **M22:** work on an app that isn't part of this repository
- **M23:** the RTC shield keeps the time
- **M24:** IR remote control
- **M25:** the known gaps, then the follow-ups
- **M26:** hardware video decoding
- **M27:** the BeBox Device Kit classes on the 40-pin header
- **M28:** work on an app that isn't part of this repository
- **M29:** a display connected after boot gets its own mode
- **M30:** the CPU clock follows the load by default
- **M31:** video shown from the decoder's own frames
- **M32:** work on an app that isn't part of this repository
- **M33:** work on an app that isn't part of this repository
- **M34:** work on an app that isn't part of this repository
- **M35:** Korean (and Japanese and Chinese) text shows
- **M36:** a frozen MediaPlayer, and the rw_lock it showed
- **M37:** BListView::RemoveItems() keeps the selection right
- **M38:** FreeType 2.14.3 for the bootstrap 2.6.3
- **M39:** zstd packages install
- **M40:** an empty SD slot or eMMC socket is probed forever
- **M41:** the video items
- **M42:** remote profiles for one application
- **M43:** hardware OpenGL on the Mali-450
- **M44:** work on an app that isn't part of this repository
- **M45:** the Logitech C920 webcam (UVC) on the test board
- **M46:** data written before a power loss is lost
- **M47:** NeoCam
- **M48:** hardware H.264 encoding
- **M49:** the small and cosmetic items
- **M50:** the bigger items, 1: the C920's microphone, and NeoCam clips with sound
- **M51:** the bigger items, 2: the webcam's MJPEG on the S905's decoder
- **M52:** the bigger items, 3: a bit rate for H.264 clips
- **M53:** the bigger items, 4: display modes, 4K at up to 30 Hz
- **M54:** the bigger items, 5: HDMI audio extras
- **M55:** the bigger items, 7: Mali GPU speed
- **M56:** the bigger items, 8: colour emoji
- **M57:** +257 rolled out to the LAN boards
- **M58:** open items, 1: no more lost sound in clips
- **M59:** open items, 2: the microphone after an unplug and replug
- **M60:** open items, 3: the video decoding leftovers
- **M61:** open items, 4: GP and PP jobs side by side (measured, not adopted; `patches/haiku/meson_mali-gp-pp-overlap.patch`)
- **M62:** open items, 5: emoji sequences drawn as one emoji
- **M63:** +264 rolled out to the LAN boards
- **M64:** Wi-Fi for the spare C2s, an RTL8812BU USB adapter
- **M65:** I2C B with a real device, Hardkernel's Weather Board 2
- **M66:** Weather Board, a demo app and Desktop replicant for the Weather Board 2
- **M67:** the package repository at +264
- **M68:** Hardkernel's 16x2 LCD + IO Shield, drivers and a demo
- **M69:** +267 and the LCD Shield demo on a second board
- **M70:** LCD Shield 1.1, the Blinken page
- **M71:** MediaPlayer's right arrow went back in some MP4 films
- **M72:** the new keyboard behind a hub, through ten reboots
- **M73:** a +269 eMMC module
- **M74:** growing a mounted BFS volume freed its block cache under readers
- **M75:** Weather Board and LCD Shield in the package repository
- **M76:** +270 rolled out to the LAN boards
- **M77:** an x86_64 build of the fork, for a ThinkPad X1 Carbon (5th generation)
- **M78:** Firefox 157.0.1 on the C2
- **M79:** the screen saver and the blue screen at shutdown
- **M80:** the RTL8812BU adapter scans, and audio through a cable works, on the test board
- **M81:** Ethernet stops sending under load
- **M82:** socket lifetime bugs, and what the rest of the memory growth was
- **M83:** Python 3.15 and its modules
- **M84:** building on the board: gcc and make
- **M85:** PyQt6
- **M86:** rsync
- **M87:** LCD Shield 1.5: D5 and D7, Fahrenheit, all of it as a Desktop replicant, an icon, AM/PM, a dot-matrix display
- **M88:** Terminal without antialiasing, app_server's monochrome text
- **M89:** work on an app that isn't part of this repository
- **M90:** a microSD grows at its first boot like an eMMC module
