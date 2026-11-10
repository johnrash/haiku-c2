# The 40-pin header as a Be-style API (M27)

> **Status:** Current as of M27. Since then I2C B was tested with Hardkernel's Weather Board 2 (M65), and with `lcd_io_shield true` the `meson_gpio` driver claims the LCD + IO shield's pins (M68). UART A has not been tried with a real device.

A set of classes in libdevice, in the style of the BeBox's Device Kit and its GeekPort: `Open`, `Close`, `IsOpen`, `Read`, `Write`, plus `CountDevices` and `GetDeviceName` to list what there is (as `BSerialPort` and `BJoystick` have). The BeBox classes `BDigitalPort`, `BA2D` and `BD2A` keep their Be Book behaviour. The rest of the header is covered by `BDigitalPin`, `BPWM`, `BI2CDevice` and `BGeekPort`, plus Haiku's own `BSerialPort`. Everything below is built and tested on the board (`tools/probes/geekporttest.cpp`, nothing wired to the header).

## Rules for every class

- **One owner per pin.** A pin belongs to the object (or driver) that has it until `Close()` or the program's end. Any other `Open()` of it fails with `B_BUSY`, whatever class asks, and the `gpio` command leaves it alone. The kernel's `meson_header` module keeps the owners. `BGeekPort` shows them.
- **Nothing that could harm the board** (a rule of the port since 2026-10-30). These are left out on purpose:
  - pin 33's PWM: the other channel of its controller (PWM AB) sets a core voltage regulator, and so does PWM D, so the AB and CD controllers are never touched;
  - pins 3 and 5 and the I2C A bus behind them: the RTC shield, whose clock driver uses the bus;
  - the ADC's internal switches, some of which drive an input;
  - drive strength, and any SoC pad that isn't on the header.
- **Voltages**, stated in each header file:
  - the GPIO, PWM, UART and I2C pins are 3.3 V and **not 5 V tolerant**;
  - the analog inputs take **0 to 1.8 V**.

  Software can't protect against wiring.
- **Names** are of the Be kind: "DigitalA", "A2D1", "D2A1", "PWM1". Single pins are "Pin7" (the header's numbering) or the SoC's name ("GPIOX_21"). The serial port and the I2C bus are both called "header".
- **Units** are modern where the BeBox had none: nanoseconds for PWM, and system_time() for edges. `BA2D` keeps millivolts, as the BeBox did.

## The pins

| Pins | Function | Class |
|---|---|---|
| 7, 12, 13, 15, 16, 18, 21, 22 | DigitalA (bits 0–7) | `BDigitalPort` |
| 23, 24, 26, 29, 31, 32, 35, 36 | DigitalB (bits 0–7) | `BDigitalPort` |
| the pins above, and 8, 10, 11, 19, 27, 28, 33 | one GPIO pin, with edges | `BDigitalPin` |
| 40, 37 | A2D1, A2D2: millivolts, 0–1.8 V | `BA2D` |
| 11, 19 | D2A1, D2A2: PWM as a D/A converter, 94 kHz, value/255 | `BD2A` |
| 11, 19 | PWM1, PWM2: PWM with a chosen period and pulse width | `BPWM` |
| 8 (TX), 10 (RX) | UART A as "header", 3.3 V serial | `BSerialPort` |
| 27 (SDA), 28 (SCL) | I2C B as "header", 100 kHz | `BI2CDevice` |
| 3, 5 | I2C A: the RTC shield | none (the kernel's) |
| 1, 17 (3.3 V), 2, 4 (5 V), 38 (1.8 V), grounds | power | listed by `BGeekPort` |

A pin has one role at a time. For example, pin 11 is D2A1, PWM1 or a `BDigitalPin`, whichever opens it first.

## BDigitalPin: one GPIO pin (DigitalPin.h)

```cpp
enum pin_pull { B_PIN_PULL_NONE, B_PIN_PULL_DOWN, B_PIN_PULL_UP };
enum pin_edge { B_PIN_RISING_EDGE = 1, B_PIN_FALLING_EDGE = 2, B_PIN_ANY_EDGE = 3 };

status_t    Open(const char* name);     // "Pin7", "GPIOX_21": an input, its pull as it was
void        Close();                    // the pin becomes an input
status_t    SetAsInput();
status_t    SetAsOutput(bool level);    // the level first: no glitch
status_t    SetPull(pin_pull pull);
status_t    Read(bool* level);          // inputs and outputs
status_t    Write(bool level);          // B_NOT_ALLOWED on an input
status_t    WaitForEdge(pin_edge edge, bigtime_t timeout = B_INFINITE_TIMEOUT,
                bool* _level = NULL, bigtime_t* _when = NULL);
status_t    StartWatching(BMessenger target, pin_edge edge = B_PIN_ANY_EDGE);
status_t    StopWatching();
int32       CountDevices();             // 23
status_t    GetDeviceName(int32 index, char* name, size_t size);
```

- **Edges are interrupts** from the SoC's GPIO interrupt block:
  - It has 8 channels for the whole header. Each kind of edge of a pin takes one, so `B_PIN_ANY_EDGE` takes two (the S905 has no "both edges" mode). When they are all taken, the call returns `B_BUSY`.
  - Recording starts at the first `WaitForEdge()` (or `StartWatching()`) and goes on until `Close()` or a different kind of edge is asked for. A loop of waits misses nothing: up to 32 edges wait to be read. Older ones are dropped, and "missed" says how many.
  - An output sees its own edges, because the block reads the pad.
- `StartWatching()` sends `B_DIGITAL_PIN_CHANGED` from a thread of its own, with the fields "pin" (int32), "edge" (int32), "level" (bool), "when" (int64) and "missed" (int32, only when edges were lost). While watching, `WaitForEdge()` returns `B_BUSY`.
- **Measured** (21 checks, run 5 times in a row):
  - an edge is recorded about 3 µs after the write that made it;
  - 20 writes 2.6 µs apart gave all 20 edges, in order;
  - 100 rising edges 100 µs apart gave 100 messages;
  - starting to record on a pin that's already at the level the edge leads to records no edge.
- **Pin 7** on the test board was held high from outside (probably the RTC shield's interrupt line, when it carried that shield), so its pull-down had no effect there.

## BPWM: a PWM output with your period (PWM.h)

```cpp
status_t    Open(const char* name);     // "PWM1" (pin 11), "PWM2" (pin 19): stopped, low
void        Close();                    // stops it: the pin low
status_t    SetPeriod(uint32 nanoseconds);
status_t    SetFrequency(float hertz);
status_t    SetPulseWidth(uint32 nanoseconds);
status_t    SetDutyCycle(float fraction);   // 0 to 1, kept when the period changes
uint32      Period() const;             // what the hardware makes of them
uint32      PulseWidth() const;
float       DutyCycle() const;
status_t    Start();
status_t    Stop();                     // the pin low
```

- The period can be from about 84 ns to **0.35 s** (3 Hz): 24 MHz divided by 1 to 128, and at most 65536 counts.
- Servo example: `SetPeriod(20000000)`, `SetPulseWidth(1500000)`, `Start()`. At 50 Hz the pulse width moves in steps of **0.33 µs** (the crystal divided by 8).
- Closing stops the output, unlike `BD2A`: a motor or servo shouldn't keep running when its program ends. The two channels have independent clock dividers.
- The controller takes new counts only from a write that changes them, and loses writes that come too close together. The driver spaces them 25 µs apart (found on the board in M27).

## BSerialPort "header": UART A on pins 8 and 10

- **Driver.** `meson_uart` publishes `/dev/ports/header`, so `BSerialPort::Open("header")` works, and so does any termios program.
  - Opening takes pins 8 and 10. Closing lets the written bytes go out, then gives the pins back.
  - Programs get the slave side of a tty pair, as with a pty, so termios applies as on a terminal: VMIN, VTIME, and `BSerialPort`'s timeouts.
- **Speeds.** The UART divides 8 MHz, so these are exact: 500000, 1000000, 2000000 and 4000000 bit/s. 9600 to 230400 bit/s are within 0.8 %. 460800 is 2 % off, 921600 is 3.5 % off and 1500000 is 6.7 % off. There is no RTS/CTS (those lines aren't on the header). A break holds TX low.
- **Measured** (20 checks):
  - frames sent in 8N1, 8E1 and 7O2 decode correctly from pin 8;
  - the speed measured over long writes matches the UART's own rate within 0.02 %, from 9600 bit/s to 4 Mbit/s, with no gaps;
  - 11 bytes received at 1200 bit/s, made by switching pin 10's pull-up and pull-down. While UART A has the pin, its pad can't be driven as GPIO, and the UART's "invert RX" bit does nothing;
  - the 0.2 s read timeout holds.
- **Two fixes in Haiku code it needed:**
  - The tty module: a read with VMIN 0 and VTIME set returned at once. Now it waits up to VTIME for a byte, as POSIX says.
  - `BSerialPort::NumCharsAvailable()` was a stub that always said 0.
- 3.3 V logic levels: an RS-232 port (±12 V) would damage the board. A USB-serial adapter set to 3.3 V is right.

## BI2CDevice: a device on the header's I2C bus (I2CDevice.h)

```cpp
status_t    Open(const char* bus, uint8 address);   // "header", 0x08 to 0x77
void        Close();
ssize_t     Read(void* buffer, size_t length);
ssize_t     Write(const void* buffer, size_t length);
status_t    WriteRead(const void* command, size_t commandLength,
                void* buffer, size_t length);        // a repeated start between them
status_t    ReadRegister(uint8 reg, uint8* value);
status_t    WriteRegister(uint8 reg, uint8 value);
bool        IsPresent();                // does it answer its address?
int32       CountDevices();             // the buses: "header"
status_t    GetDeviceName(int32 index, char* name, size_t size);
status_t    Scan(const char* bus, uint8* addresses, int32 maxCount, int32* count);
```

- **The bus.** I2C B runs at 100 kHz, and the board has 1 kΩ pull-ups on it.
  - `meson_i2c` publishes it as `/dev/bus/i2c/header`. The first device opened takes pins 27 and 28; the last one closed gives them back.
  - Several devices can share the bus, each at its own address. A device that doesn't answer gives `B_DEV_NOT_READY`.
- **Kept away from programs.** The device tree has this bus disabled, and nothing scans it at boot. The bus manager's generic `bus_raw` device can't use it either, unless a program has it open. I2C A, the RTC's bus, has no program interface.

## BGeekPort: what every pin is doing (GeekPort.h)

```cpp
static int32    CountPins();                         // 40
static status_t GetPinInfo(int32 pin, geekport_pin_info* info);
    // name ("GPIOX_21", "3.3V", "GND", "AIN0"), capabilities (B_GEEKPORT_GPIO,
    // _PWM, _UART, _I2C, _ANALOG, _POWER, _GROUND), function now ("input",
    // "output", "PWM", "UART A", "I2C B"), user ("program", "UART A", "I2C A")
    // and its team
```

Reading `/dev/misc/gpio` (the `gpio` command) prints the same table.

## The kernel side

| Piece | Does |
|---|---|
| `meson_header` (module) | the pin table, owners, and the pin mux under one lock |
| `meson_gpio` | `/dev/misc/gpio`: GPIO, pulls, PWM E/F, edges (GPIO interrupt block, INTIDs 96–103) |
| `meson_saradc` | `/dev/misc/adc/0` and `1` |
| `meson_uart` | `/dev/ports/header` (UART A, INTID 58) |
| `meson_i2c` | I2C A for the RTC; I2C B as `/dev/bus/i2c/header` |

The private interfaces are headers/private/drivers/meson_gpio.h, meson_saradc.h, meson_i2c.h and meson_header.h. The hardware facts are in docs/m27-geekport-facts.md and docs/m27-irq-uart-i2c-facts.md.
