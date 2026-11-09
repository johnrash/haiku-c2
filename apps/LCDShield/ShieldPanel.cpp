/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */


#include "ShieldPanel.h"

#include <algorithm>

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#include <DataIO.h>
#include <File.h>
#include <FindDirectory.h>
#include <FormattingConventions.h>
#include <Locale.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Window.h>

#include "LcdFont.h"
#include "ShieldBox.h"


enum {
	kMsgTick = 'tick',
	kMsgSnapshot = 'snap',
	kMsgButton = 'butn',
};

static const bigtime_t kTick = 100000;
static const bigtime_t kSampleInterval = 100000;
static const bigtime_t kOpenRetry = 3000000;
static const bigtime_t kSendTimeout = 200000;

// in the system's temporary directory: who drives the shield (flock()),
// who else is there (shared flock()), and the driving panel's state
static const char* kLockName = "LCDShield.lock";
static const char* kPresenceName = "LCDShield.present";
static const char* kStateName = "LCDShield.state";

static const char* kPageNames[] = {
	"Clock", "CPU", "Blinken", "Memory", "Network", "Weather", "Message"
};
static const char* kLedModeNames[] = {
	"CPU load", "Scanner", "Manual"
};

// the bar graph's characters: code n has n of the five columns lit (codes
// 1 to 5; 0 can't be written in a string)
static const uint8 kBarRows[5] = { 0x10, 0x18, 0x1c, 0x1e, 0x1f };

// Blinken's: bars from the left edge (codes 1 to 4: one to four columns lit
// from the left) and from the right edge (codes 5, 6, 7, 0: one to four from
// the right), the whole height; a full cell is the display's own block
static const uint8 kBlinkenLeft[4] = { 1, 2, 3, 4 };
static const uint8 kBlinkenRight[4] = { 5, 6, 7, 0 };
static const uint8 kFullBlock = 0xff;
static const char* kFullBlockText = "\xe2\x96\x88";		// U+2588

static const char* kDegree = "\xc2\xb0";

static BString
temporary_path(const char* name)
{
	BPath path;
	if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &path) != B_OK)
		path.SetTo("/tmp");
	path.Append(name);
	return path.Path();
}


//	#pragma mark - construction


ShieldPanel::ShieldPanel(const BMessage* settings)
	:
	BView(BRect(BPoint(0, 0), PanelSize()), "ShieldPanel", B_FOLLOW_NONE,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init();
	_GetGeometry(Bounds().Width() + 1, fGeometry);
	_ApplySettings(settings);
	SetToolTip("SW1: the next page, SW2: the next LED mode, an LED: on or "
		"off.\nRight-click for the pages, the LEDs, Fahrenheit and AM/PM.");
}


ShieldPanel::~ShieldPanel()
{
	_StopThreads();
}


void
ShieldPanel::_Init()
{
	fTicker = NULL;
	fDeviceOpen = false;
	fLastOpenTry = 0;
	fSampler = -1;
	fButtonReader = -1;
	fQuitting = false;
	fLockFile = -1;
	fPresenceFile = -1;
	fDriving = false;
	fPage = kClock;
	fLedMode = kLedLoad;
	fManualLeds = 0;
	fFahrenheit = false;
	// the clock as the Time preferences have it, until it's set here
	BFormattingConventions conventions;
	fTwelveHour = BLocale::Default()->GetFormattingConventions(&conventions)
			== B_OK
		&& !conventions.Use24HourClock();
	memset(&fSnapshot, 0, sizeof(fSnapshot));
	fHaveSnapshot = false;
	fSmoothedLoad = 0;
	memset(fCoreLoad, 0, sizeof(fCoreLoad));
	fScanner = 0;
	fScannerStep = 1;
	fCharacterSet = kNoCharacters;
	memset(fCharacters, 0, sizeof(fCharacters));
	memset(fText.codes, ' ', sizeof(fText.codes));
	fLeds = 0;
	fButtons = 0;
}


void
ShieldPanel::_ApplySettings(const BMessage* settings)
{
	if (settings == NULL)
		return;
	const char* message;
	if (settings->FindString("message", &message) == B_OK)
		fMessage = message;
	int32 value;
	if (settings->FindInt32("page", &value) == B_OK && value >= 0
		&& value < kPageCount && (value != kMessage || !fMessage.IsEmpty())) {
		fPage = value;
	}
	if (settings->FindInt32("led mode", &value) == B_OK && value >= 0
		&& value < kLedModeCount) {
		fLedMode = value;
	}
	if (settings->FindInt32("manual leds", &value) == B_OK)
		fManualLeds = (uint32)value & ((1u << LCD_SHIELD_LEDS) - 1);
	bool fahrenheit;
	if (settings->FindBool("fahrenheit", &fahrenheit) == B_OK)
		fFahrenheit = fahrenheit;
	bool twelveHour;
	if (settings->FindBool("12 hour clock", &twelveHour) == B_OK)
		fTwelveHour = twelveHour;
}


void
ShieldPanel::GetSettings(BMessage& settings) const
{
	settings.AddInt32("page", fPage);
	settings.AddInt32("led mode", fLedMode);
	settings.AddInt32("manual leds", (int32)fManualLeds);
	settings.AddBool("fahrenheit", fFahrenheit);
	settings.AddBool("12 hour clock", fTwelveHour);
	settings.AddString("message", fMessage);
}


/*!	A replicant of 1.2 was the panel alone, with its dragger: it comes back
	as the whole box, which builds its children anew (the old dragger left
	out).
*/
BArchivable*
ShieldPanel::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "ShieldPanel"))
		return NULL;
	BMessage box(*archive);
	box.RemoveName("_views");
	return new ShieldBox(&box);
}


/*static*/ const char*
ShieldPanel::PageName(int32 page)
{
	return page >= 0 && page < kPageCount ? kPageNames[page] : "?";
}


/*static*/ const char*
ShieldPanel::LedModeName(int32 mode)
{
	return mode >= 0 && mode < kLedModeCount ? kLedModeNames[mode] : "?";
}


//	#pragma mark - in a window


void
ShieldPanel::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	SetHighUIColor(B_PANEL_TEXT_COLOR);
	fSelf = BMessenger(this);

	_OpenFiles();
	_OpenDevice();

	BMessage tick(kMsgTick);
	fTicker = new BMessageRunner(fSelf, &tick, kTick);
	_Tick();
}


void
ShieldPanel::DetachedFromWindow()
{
	delete fTicker;
	fTicker = NULL;
	_StopThreads();
	if (fDriving)
		_StopDriving();
	_CloseFiles();
	fDevice.Close();
	fDeviceOpen = false;
	BView::DetachedFromWindow();
}


bool
ShieldPanel::_OpenDevice()
{
	fLastOpenTry = system_time();
	if (fDevice.Open() != B_OK)
		return false;
	fDeviceOpen = true;
	_StartThreads();
	return true;
}


void
ShieldPanel::_StartThreads()
{
	fQuitting = false;
	if (fSampler < 0) {
		fSampler = spawn_thread(_SamplerThread, "shield sampler",
			B_LOW_PRIORITY, this);
		if (fSampler >= 0)
			resume_thread(fSampler);
	}
	if (fButtonReader < 0) {
		fButtonReader = spawn_thread(_ButtonThread, "shield buttons",
			B_NORMAL_PRIORITY, this);
		if (fButtonReader >= 0)
			resume_thread(fButtonReader);
	}
}


void
ShieldPanel::_StopThreads()
{
	fQuitting = true;
	status_t result;
	if (fSampler >= 0)
		wait_for_thread(fSampler, &result);
	if (fButtonReader >= 0)
		wait_for_thread(fButtonReader, &result);
	fSampler = fButtonReader = -1;
}


/*static*/ status_t
ShieldPanel::_SamplerThread(void* self)
{
	ShieldPanel* panel = (ShieldPanel*)self;
	Sampler sampler;
	while (!panel->fQuitting) {
		Snapshot snapshot;
		sampler.Sample(snapshot);
		BMessage message(kMsgSnapshot);
		message.AddData("snapshot", B_RAW_TYPE, &snapshot, sizeof(snapshot));
		panel->fSelf.SendMessage(&message, (BHandler*)NULL, kSendTimeout);
		snooze(kSampleInterval);
	}
	return B_OK;
}


/*static*/ status_t
ShieldPanel::_ButtonThread(void* self)
{
	ShieldPanel* panel = (ShieldPanel*)self;
	while (!panel->fQuitting) {
		lcd_shield_button_event event;
		if (panel->fDevice.WaitButton(200000, event) != B_OK)
			continue;
		BMessage message(kMsgButton);
		message.AddInt32("button", event.button);
		message.AddBool("pressed", event.pressed != 0);
		panel->fSelf.SendMessage(&message, (BHandler*)NULL, kSendTimeout);
	}
	return B_OK;
}


//	#pragma mark - who drives the shield


void
ShieldPanel::_OpenFiles()
{
	fLockFile = open(temporary_path(kLockName).String(), O_RDWR | O_CREAT,
		0644);
	fPresenceFile = open(temporary_path(kPresenceName).String(),
		O_RDWR | O_CREAT, 0644);
	if (fPresenceFile >= 0)
		flock(fPresenceFile, LOCK_SH);
}


void
ShieldPanel::_CloseFiles()
{
	// closing them gives the locks back
	if (fLockFile >= 0)
		close(fLockFile);
	if (fPresenceFile >= 0)
		close(fPresenceFile);
	fLockFile = fPresenceFile = -1;
}


void
ShieldPanel::_Tick()
{
	if (!fDeviceOpen) {
		// no shield (yet): another look every few seconds
		if (system_time() - fLastOpenTry < kOpenRetry || !_OpenDevice()) {
			_NotifyParent();
			return;
		}
		Invalidate();
	}

	if (!fDriving && !_TryToDrive()) {
		// another panel drives the shield: follow its state, show its
		// characters
		_ReadState(NULL);
		_UseCharacters(_CharacterSetFor(fPage));
	}
	if (fDriving) {
		_UpdateLeds();
		_ShowPage();
		_WriteState();
	}
	_UpdateMirror();
	_NotifyParent();
}


/*!	Takes the shield over when no other panel holds it. A panel that has
	followed another one goes on with its state.
*/
bool
ShieldPanel::_TryToDrive()
{
	if (fLockFile < 0 || flock(fLockFile, LOCK_EX | LOCK_NB) != 0)
		return false;
	fDriving = true;
	fShown[0].clear();
	fShown[1].clear();
	fCharacterSet = kNoCharacters;
	fWrittenState.clear();
	return true;
}


/*!	Leaves the shield to the panel that will take it over, or, as the last
	panel, dark with a word that the demo ended.
*/
void
ShieldPanel::_StopDriving()
{
	bool others = true;
	if (fPresenceFile >= 0) {
		flock(fPresenceFile, LOCK_UN);
		if (flock(fPresenceFile, LOCK_EX | LOCK_NB) == 0) {
			others = false;
			flock(fPresenceFile, LOCK_UN);
		}
	}
	if (!others) {
		if (fDeviceOpen) {
			fDevice.SetLeds(0);
			fDevice.Clear();
			fDevice.SetLine(0, "LCD Shield demo");
			fDevice.SetLine(1, "has ended.");
		}
		unlink(temporary_path(kStateName).String());
	}
	fDriving = false;
	if (fLockFile >= 0)
		flock(fLockFile, LOCK_UN);
}


/*!	The driving panel's state, for the others: its messenger, the page, the
	LED mode, the unit, the message. Written when it changes, whole (a new
	file renamed over the old one).
*/
void
ShieldPanel::_WriteState()
{
	BMessage state;
	state.AddMessenger("messenger", fSelf);
	GetSettings(state);
	BMallocIO buffer;
	if (state.Flatten(&buffer) != B_OK)
		return;
	std::string data((const char*)buffer.Buffer(), buffer.BufferLength());
	if (data == fWrittenState)
		return;

	BString path = temporary_path(kStateName);
	BString temporary = path;
	temporary << ".new";
	int fd = open(temporary.String(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	bool written = write(fd, data.data(), data.size()) == (ssize_t)data.size();
	close(fd);
	if (written && rename(temporary.String(), path.String()) == 0)
		fWrittenState = data;
}


/*!	Follows the driving panel: its state becomes this one's. \a driver, if
	given, gets its messenger.
*/
bool
ShieldPanel::_ReadState(BMessenger* driver)
{
	BFile file(temporary_path(kStateName).String(), B_READ_ONLY);
	BMessage state;
	if (file.InitCheck() != B_OK || state.Unflatten(&file) != B_OK)
		return false;
	if (driver != NULL && state.FindMessenger("messenger", driver) != B_OK)
		return false;
	fMessage = "";
	_ApplySettings(&state);
	return true;
}


/*!	What this panel is told goes to the driving panel, if that is another
	one; it changes the shield, and this one follows.
*/
void
ShieldPanel::_Command(BMessage* message)
{
	if (!fDriving && fDeviceOpen) {
		BMessenger driver;
		if (_ReadState(&driver) && driver.IsValid() && driver != fSelf
			&& driver.SendMessage(message, (BHandler*)NULL, kSendTimeout)
				== B_OK) {
			return;
		}
	}
	_Apply(message);
}


void
ShieldPanel::_Apply(BMessage* message)
{
	switch (message->what) {
		case kMsgSetPage:
		{
			int32 page;
			int32 step;
			if (message->FindInt32("page", &page) == B_OK) {
				if (page >= 0 && page < kPageCount
					&& (page != kMessage || !fMessage.IsEmpty())) {
					_SetPage(page);
				}
			} else if (message->FindInt32("step", &step) == B_OK)
				_SetPage(_NextPage(fPage, step));
			break;
		}

		case kMsgSetLedMode:
		{
			int32 mode;
			int32 step;
			if (message->FindInt32("mode", &mode) != B_OK) {
				if (message->FindInt32("step", &step) != B_OK)
					break;
				mode = ((fLedMode + step) % kLedModeCount + kLedModeCount)
					% kLedModeCount;
			}
			if (mode < 0 || mode >= kLedModeCount)
				break;
			// by hand, the LEDs start as they are
			if (mode == kLedManual && fLedMode != kLedManual)
				fManualLeds = fLeds;
			fLedMode = mode;
			break;
		}

		case kMsgToggleLed:
		{
			int32 led;
			if (message->FindInt32("led", &led) != B_OK || led < 0
				|| led >= LCD_SHIELD_LEDS) {
				break;
			}
			if (fLedMode != kLedManual) {
				fManualLeds = fLeds;
				fLedMode = kLedManual;
			}
			fManualLeds ^= 1u << led;
			break;
		}

		case kMsgSetFahrenheit:
		{
			bool fahrenheit;
			if (message->FindBool("fahrenheit", &fahrenheit) == B_OK)
				fFahrenheit = fahrenheit;
			break;
		}

		case kMsgSetTwelveHour:
		{
			bool twelveHour;
			if (message->FindBool("12 hour clock", &twelveHour) == B_OK)
				fTwelveHour = twelveHour;
			break;
		}

		case kMsgSetMessage:
		{
			const char* text;
			if (message->FindString("message", &text) != B_OK)
				break;
			fMessage = text;
			if (!fMessage.IsEmpty())
				_SetPage(kMessage);
			else if (fPage == kMessage)
				_SetPage(kClock);
			break;
		}
	}

	if (fDriving) {
		_UpdateLeds();
		_ShowPage();
		_WriteState();
	}
	_UpdateMirror();
	_NotifyParent();
}


/*!	What the box shows besides the panel: the page, the LED mode, the unit,
	whether this panel drives the shield, the message.
*/
void
ShieldPanel::_NotifyParent()
{
	if (Parent() == NULL || Looper() == NULL)
		return;
	int32 count = fMessage.IsEmpty() ? kPageCount - 1 : kPageCount;
	BMessage state(kMsgPanelState);
	state.AddInt32("page", fPage);
	state.AddInt32("page count", count);
	state.AddInt32("led mode", fLedMode);
	state.AddBool("fahrenheit", fFahrenheit);
	state.AddBool("12 hour clock", fTwelveHour);
	state.AddBool("driving", fDriving);
	state.AddBool("shield", fDeviceOpen);
	state.AddString("message", fMessage);

	BMallocIO buffer;
	if (state.Flatten(&buffer) != B_OK)
		return;
	std::string data((const char*)buffer.Buffer(), buffer.BufferLength());
	if (data == fNotified)
		return;
	fNotified = data;
	Looper()->PostMessage(&state, Parent());
}


void
ShieldPanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTick:
			_Tick();
			break;

		case kMsgSnapshot:
		{
			const void* data;
			ssize_t size;
			if (message->FindData("snapshot", B_RAW_TYPE, &data, &size)
					!= B_OK || size != sizeof(Snapshot)) {
				break;
			}
			memcpy(&fSnapshot, data, sizeof(fSnapshot));
			fSmoothedLoad = fHaveSnapshot
				? fSmoothedLoad * 0.7f + fSnapshot.cpuLoad * 0.3f
				: fSnapshot.cpuLoad;
			// Blinken's bars: up at once, down more slowly
			for (int32 i = 0; i < fSnapshot.coreCount; i++) {
				float load = fSnapshot.coreLoad[i];
				fCoreLoad[i] = load > fCoreLoad[i] ? load
					: fCoreLoad[i] * 0.6f + load * 0.4f;
			}
			fHaveSnapshot = true;
			break;
		}

		case kMsgButton:
		{
			// the shield's own buttons: only the driving panel acts on them
			int32 button;
			bool pressed;
			if (!fDriving || message->FindInt32("button", &button) != B_OK
				|| message->FindBool("pressed", &pressed) != B_OK
				|| !pressed) {
				break;
			}
			BMessage command(button == 0 ? kMsgSetPage : kMsgSetLedMode);
			command.AddInt32("step", 1);
			_Apply(&command);
			break;
		}

		case kMsgSetPage:
		case kMsgSetLedMode:
		case kMsgToggleLed:
		case kMsgSetFahrenheit:
		case kMsgSetTwelveHour:
		case kMsgSetMessage:
			_Command(message);
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


//	#pragma mark - the pages and the LEDs


int32
ShieldPanel::_NextPage(int32 page, int32 step) const
{
	// the message page only when there is one
	int32 count = fMessage.IsEmpty() ? kPageCount - 1 : kPageCount;
	return ((page + step) % count + count) % count;
}


void
ShieldPanel::_SetPage(int32 page)
{
	fPage = page;
}


/*!	A bar of \a cells characters for a value from 0 to 1, in fifths of a
	character.
*/
BString
ShieldPanel::_Bar(float value, int32 cells)
{
	int32 units = (int32)(value * cells * 5 + 0.5f);
	BString bar;
	for (int32 i = 0; i < cells; i++) {
		int32 lit = min_c(max_c(units - i * 5, 0), 5);
		bar << (lit == 0 ? ' ' : (char)lit);
	}
	return bar;
}


/*!	"21.5°C", or "70.7°F" in Fahrenheit. */
BString
ShieldPanel::_Temperature(float celsius) const
{
	BString text;
	if (fFahrenheit)
		text.SetToFormat("%.1f%sF", celsius * 9 / 5 + 32, kDegree);
	else
		text.SetToFormat("%.1f%sC", celsius, kDegree);
	return text;
}


void
ShieldPanel::_Lines(BString& first, BString& second)
{
	const Snapshot& s = fSnapshot;
	switch (fPage) {
		case kClock:
		{
			time_t now = time(NULL);
			struct tm local;
			localtime_r(&now, &local);
			char text[32];
			strftime(text, sizeof(text), "%a %e %b %Y", &local);
			first = text;
			if (fTwelveHour) {
				// "3:42:07 PM", "12:05:33 AM"
				int hour = local.tm_hour % 12;
				snprintf(text, sizeof(text), "%d:%02d:%02d %s",
					hour == 0 ? 12 : hour, local.tm_min, local.tm_sec,
					local.tm_hour < 12 ? "AM" : "PM");
			} else
				strftime(text, sizeof(text), "%H:%M:%S", &local);
			second = text;
			while (second.Length() < LCD_SHIELD_COLUMNS - 2)
				second << ' ';
			second << "C2";
			break;
		}

		case kCpu:
			first.SetToFormat("CPU %s%3d%%", _Bar(fSmoothedLoad, 8).String(),
				(int)(fSmoothedLoad * 100 + 0.5f));
			if (s.temperature > -100) {
				second.SetToFormat("%s %4d MHz",
					_Temperature(s.temperature).String(), (int)s.clockMHz);
			} else
				second = "no temperature";
			break;

		case kMemory:
		{
			float used = s.memoryTotal > 0
				? (float)s.memoryUsed / s.memoryTotal : 0;
			first.SetToFormat("Mem %s%3d%%", _Bar(used, 8).String(),
				(int)(used * 100 + 0.5f));
			second.SetToFormat("%d/%d MB  %dT", (int)(s.memoryUsed >> 20),
				(int)(s.memoryTotal >> 20), (int)s.teams);
			break;
		}

		case kNetwork:
		{
			first = s.address;
			bigtime_t minutes = s.uptime / 60000000;
			second.SetToFormat("up %dd %02d:%02d", (int)(minutes / 1440),
				(int)(minutes / 60 % 24), (int)(minutes % 60));
			break;
		}

		case kWeather:
			if (!s.weatherValid) {
				first = "No Weather Board";
				second = "on I2C B (27/28)";
				break;
			}
			first.SetToFormat("%s  %.0f%% RH",
				_Temperature(s.weather.temperature).String(),
				s.weather.humidity);
			second.SetToFormat("%.1f hPa", s.weather.pressure);
			if (s.weather.lightValid)
				second << " UV" << (int32)(s.weather.uvIndex + 0.5);
			break;

		case kMessage:
		{
			// two lines of 16 characters (the device counts UTF-8), broken
			// after the last space that lets the first one fit
			first = fMessage;
			second = "";
			int32 characters = 0;
			int32 lastSpace = -1;
			for (int32 i = 0; i < fMessage.Length(); i++) {
				if ((fMessage.ByteAt(i) & 0xc0) == 0x80)
					continue;
				if (characters == LCD_SHIELD_COLUMNS) {
					int32 end = fMessage.ByteAt(i) == ' ' ? i
						: lastSpace > 0 ? lastSpace : i;
					first.SetTo(fMessage, end);
					const char* rest = fMessage.String() + end;
					while (*rest == ' ')
						rest++;
					second = rest;
					break;
				}
				if (fMessage.ByteAt(i) == ' ')
					lastSpace = i;
				characters++;
			}
			break;
		}
	}
}


int32
ShieldPanel::_CharacterSetFor(int32 page) const
{
	switch (page) {
		case kCpu:
		case kMemory:
			return kBarCharacters;
		case kBlinken:
			return kBlinkenCharacters;
		default:
			return kNoCharacters;
	}
}


/*!	The characters a page draws with: the bar's on the CPU and memory
	pages, Blinken's on its own. The driving panel defines them on the
	display (which changes what is shown at once); the others only need
	them to draw.
*/
void
ShieldPanel::_UseCharacters(int32 set)
{
	if (set == fCharacterSet || set == kNoCharacters)
		return;
	memset(fCharacters, 0, sizeof(fCharacters));
	if (set == kBarCharacters) {
		for (uint32 code = 1; code <= 5; code++) {
			for (uint32 row = 1; row < 7; row++)
				fCharacters[code][row] = kBarRows[code - 1];
		}
	} else {
		for (uint32 columns = 1; columns <= 4; columns++) {
			uint8 left = (0x1f << (5 - columns)) & 0x1f;
			uint8 right = (1 << columns) - 1;
			for (uint32 row = 0; row < 8; row++) {
				fCharacters[kBlinkenLeft[columns - 1]][row] = left;
				fCharacters[kBlinkenRight[columns - 1]][row] = right;
			}
		}
	}
	if (fDriving) {
		for (uint32 code = 0; code < 8; code++)
			fDevice.DefineCharacter(code, fCharacters[code]);
	}
	fCharacterSet = set;
	Invalidate();
}


/*!	Two bars a line, one a core: the first core's from the left edge, the
	second's from the right, meeting in the middle at full load (8 cells,
	40 columns each).
*/
void
ShieldPanel::_BlinkenLines(std::string lines[2])
{
	static const int32 kHalf = LCD_SHIELD_COLUMNS / 2;
	for (int32 row = 0; row < 2; row++) {
		uint8 cells[LCD_SHIELD_COLUMNS];
		memset(cells, ' ', sizeof(cells));
		for (int32 side = 0; side < 2; side++) {
			int32 core = row * 2 + side;
			if (core >= fSnapshot.coreCount)
				continue;
			int32 units = (int32)(fCoreLoad[core] * kHalf * 5 + 0.5f);
			for (int32 i = 0; i < kHalf; i++) {
				int32 lit = min_c(max_c(units - i * 5, 0), 5);
				uint8 code = lit == 0 ? ' ' : lit == 5 ? kFullBlock
					: side == 0 ? kBlinkenLeft[lit - 1]
					: kBlinkenRight[lit - 1];
				// from the outside in
				cells[side == 0 ? i : LCD_SHIELD_COLUMNS - 1 - i] = code;
			}
		}
		lines[row].clear();
		for (int32 i = 0; i < LCD_SHIELD_COLUMNS; i++) {
			if (cells[i] == kFullBlock)
				lines[row] += kFullBlockText;
			else
				lines[row] += (char)cells[i];
		}
	}
}


void
ShieldPanel::_ShowPage()
{
	std::string lines[2];
	if (fPage == kBlinken && fHaveSnapshot) {
		_UseCharacters(kBlinkenCharacters);
		_BlinkenLines(lines);
	} else {
		_UseCharacters(_CharacterSetFor(fPage));
		BString text[2];
		if (!fHaveSnapshot && fPage != kClock && fPage != kMessage) {
			text[0] = kPageNames[fPage];
			text[1] = "...";
		} else
			_Lines(text[0], text[1]);
		// padded or cut to 16 characters, as SetLine() would
		for (int32 row = 0; row < 2; row++) {
			int32 characters = 0;
			const char* c = text[row].String();
			while (*c != '\0' && characters < LCD_SHIELD_COLUMNS) {
				lines[row] += *c++;
				while ((*c & 0xc0) == 0x80)
					lines[row] += *c++;
				characters++;
			}
			lines[row].append(LCD_SHIELD_COLUMNS - characters, ' ');
		}
	}
	for (int32 row = 0; row < 2; row++) {
		if (lines[row] == fShown[row])
			continue;
		if (fDevice.SetLineRaw(row, lines[row].data(), lines[row].size())
				== B_OK) {
			fShown[row] = lines[row];
		}
	}
}


void
ShieldPanel::_UpdateLeds()
{
	uint32 leds = 0;
	switch (fLedMode) {
		case kLedLoad:
		{
			// a bar: D1 lit from any load, all seven at full load
			int32 lit = (int32)ceilf(fSmoothedLoad * LCD_SHIELD_LEDS - 0.05f);
			if (lit > 0)
				leds = (1u << min_c(lit, (int32)LCD_SHIELD_LEDS)) - 1;
			break;
		}
		case kLedScanner:
			leds = 1u << fScanner;
			fScanner += fScannerStep;
			if (fScanner <= 0 || fScanner >= LCD_SHIELD_LEDS - 1)
				fScannerStep = -fScannerStep;
			break;
		case kLedManual:
			leds = fManualLeds;
			break;
	}
	if (leds != fDevice.Leds())
		fDevice.SetLeds(leds);
}


/*!	The shield as its devices say it is: what is drawn. */
void
ShieldPanel::_UpdateMirror()
{
	lcd_shield_text text;
	if (fDevice.GetText(text) != B_OK)
		return;
	uint32 leds = fDevice.Leds();
	uint32 buttons = fDevice.Buttons();
	if (memcmp(&text, &fText, sizeof(text)) == 0 && leds == fLeds
		&& buttons == fButtons) {
		return;
	}
	// the display, or the row under it, whichever changed
	if (memcmp(&text, &fText, sizeof(text)) != 0)
		Invalidate(_LcdFrame());
	if (leds != fLeds || buttons != fButtons) {
		BRect row = Bounds();
		row.top = _LcdFrame().bottom + 1;
		Invalidate(row);
	}
	fText = text;
	fLeds = leds;
	fButtons = buttons;
}


void
ShieldPanel::_ShowMenu(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("shield", false, false);
	for (int32 page = 0; page < kPageCount; page++) {
		if (page == kMessage && fMessage.IsEmpty())
			continue;
		BMessage* message = new BMessage(kMsgSetPage);
		message->AddInt32("page", page);
		BMenuItem* item = new BMenuItem(kPageNames[page], message);
		item->SetMarked(page == fPage);
		menu->AddItem(item);
	}
	menu->AddSeparatorItem();
	for (int32 mode = 0; mode < kLedModeCount; mode++) {
		BMessage* message = new BMessage(kMsgSetLedMode);
		message->AddInt32("mode", mode);
		BString label;
		label.SetToFormat("LEDs: %s", kLedModeNames[mode]);
		BMenuItem* item = new BMenuItem(label, message);
		item->SetMarked(mode == fLedMode);
		menu->AddItem(item);
	}
	menu->AddSeparatorItem();
	BMessage* message = new BMessage(kMsgSetFahrenheit);
	message->AddBool("fahrenheit", !fFahrenheit);
	BMenuItem* item = new BMenuItem("Temperatures in Fahrenheit", message);
	item->SetMarked(fFahrenheit);
	menu->AddItem(item);
	message = new BMessage(kMsgSetTwelveHour);
	message->AddBool("12 hour clock", !fTwelveHour);
	item = new BMenuItem("Clock with AM/PM", message);
	item->SetMarked(fTwelveHour);
	menu->AddItem(item);

	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(ConvertToScreen(where), true, false, true);
}


//	#pragma mark - drawing


/*!	The display's dots as big as \a width (in pixels) allows, in whole
	pixels: a character five dots and a gap wide, eight dots and a bit of a
	gap high, a dot 0.6 by 0.7 mm on the module; three to seven dots of room
	on either side of the 16 characters (the bezel takes the rest), two and
	a half above and below them. Under the display, the row with SW1, SW2
	and the LEDs. Wider than \a width when a dot more is worth a few pixels.
*/
/*static*/ void
ShieldPanel::_GetGeometry(float width, Geometry& geometry)
{
	float margin = be_plain_font->Size();
	float bezel = std::max(3.0f, roundf(margin * 0.35f));
	float smallest = std::max(2.0f, roundf(margin / 4));

	// the 16 characters are 95 dots across
	float available = width - 2 * bezel;
	float pitch = std::max(smallest, floorf(available / 101));
	if ((pitch + 1) * 101 - available <= available * 0.12f)
		pitch++;
	float screenWidth = std::min(std::max(available, pitch * 101),
		pitch * 110);

	geometry.margin = margin;
	geometry.pitchX = pitch;
	geometry.pitchY = std::max(pitch, roundf(pitch * 7 / 6));
	geometry.rowGap = std::max(1.0f, roundf(geometry.pitchY / 2));
	geometry.cellWidth = 6 * pitch;
	geometry.cellHeight = 8 * geometry.pitchY + geometry.rowGap;
	geometry.width = std::max(width, screenWidth + 2 * bezel);
	geometry.bezelX = floorf((geometry.width - screenWidth) / 2);
	geometry.bezelY = bezel;
	float vertical = roundf(geometry.pitchY * 2.5f);
	float textWidth = LCD_SHIELD_COLUMNS * geometry.cellWidth - pitch;
	float textHeight = LCD_SHIELD_ROWS * geometry.cellHeight
		- geometry.rowGap;
	geometry.textLeft = floorf((geometry.width - textWidth) / 2);
	geometry.textTop = bezel + vertical;
	geometry.lcdHeight = 2 * (bezel + vertical) + textHeight;
	geometry.height = geometry.lcdHeight + roundf(margin * 2.6f);
}


/*static*/ BSize
ShieldPanel::PanelSize()
{
	float margin = be_plain_font->Size();
	float pitch = std::max(2.0f, roundf(margin / 4));
	Geometry geometry;
	_GetGeometry(pitch * 104 + 2 * std::max(3.0f, roundf(margin * 0.35f)),
		geometry);
	return BSize(geometry.width - 1, geometry.height - 1);
}


/*!	As wide as \a width pixels (or a little wider, see _GetGeometry()), and
	as tall as that makes it.
*/
void
ShieldPanel::SetWidth(float width)
{
	_GetGeometry(width, fGeometry);
	ResizeTo(fGeometry.width - 1, fGeometry.height - 1);
	Invalidate();
}


BRect
ShieldPanel::_LcdFrame() const
{
	return BRect(0, 0, Bounds().right, fGeometry.lcdHeight - 1);
}


BRect
ShieldPanel::_LedFrame(uint32 index) const
{
	float margin = fGeometry.margin;
	BRect lcd = _LcdFrame();
	float size = roundf(margin * 0.9f);
	float gap = roundf(margin * 0.6f);
	// D1 to D7 from left to right, at the right end, as on the board; room
	// for a lit one's glow
	float left = lcd.right - 2
		- (size * LCD_SHIELD_LEDS + gap * (LCD_SHIELD_LEDS - 1));
	float x = left + index * (size + gap);
	float y = lcd.bottom + roundf(margin * 0.5f) + 2;
	return BRect(x, y, x + size, y + size);
}


BRect
ShieldPanel::_ButtonFrame(uint32 index) const
{
	float margin = fGeometry.margin;
	BRect lcd = _LcdFrame();
	float width = roundf(margin * 2.6f);
	float height = roundf(margin * 1.5f);
	float x = lcd.left + index * (width + roundf(margin * 0.5f));
	float y = lcd.bottom + roundf(margin * 0.5f);
	return BRect(x, y, x + width, y + height);
}


/*!	A character's 5x8 dots at \a origin: a user character's from its rows,
	the others from our font; the dots that are off a little lighter than
	the screen, as on the module.
*/
void
ShieldPanel::_DrawCell(BPoint origin, uint8 code)
{
	static const rgb_color kLit = { 235, 245, 255, 255 };
	static const rgb_color kUnlit = { 50, 122, 243, 255 };
	const uint8* rows = code < 8 ? fCharacters[code] : lcd_glyph(code);
	float pitchX = fGeometry.pitchX;
	float pitchY = fGeometry.pitchY;
	for (uint32 row = 0; row < 8; row++) {
		for (uint32 column = 0; column < 5; column++) {
			float x = origin.x + column * pitchX;
			float y = origin.y + row * pitchY;
			SetHighColor((rows[row] & (0x10 >> column)) != 0 ? kLit : kUnlit);
			FillRect(BRect(x, y, x + pitchX - 2, y + pitchY - 2));
		}
	}
}


void
ShieldPanel::Draw(BRect updateRect)
{
	float cellWidth = fGeometry.cellWidth;
	float cellHeight = fGeometry.cellHeight;

	// the display: a dark bezel, the blue screen, white dots
	BRect lcd = _LcdFrame();
	SetHighColor(make_color(30, 40, 60));
	FillRoundRect(lcd, 6, 6);
	BRect screen = lcd.InsetByCopy(fGeometry.bezelX, fGeometry.bezelY);
	SetHighColor(make_color(40, 110, 235));
	FillRect(screen);

	lcd_shield_text text = fText;
	if (!fDeviceOpen) {
		// no shield: say so on the screen
		static const char* kNone[2] = { " No LCD + IO", " Shield found" };
		memset(text.codes, ' ', sizeof(text.codes));
		for (uint32 row = 0; row < LCD_SHIELD_ROWS; row++)
			memcpy(text.codes[row], kNone[row], strlen(kNone[row]));
	}

	for (uint32 row = 0; row < LCD_SHIELD_ROWS; row++) {
		for (uint32 column = 0; column < LCD_SHIELD_COLUMNS; column++) {
			BPoint origin(fGeometry.textLeft + column * cellWidth,
				fGeometry.textTop + row * cellHeight);
			if (!BRect(origin, origin + BPoint(cellWidth, cellHeight))
					.Intersects(updateRect)) {
				continue;
			}
			_DrawCell(origin, text.codes[row][column]);
		}
	}

	// the buttons
	BFont small(be_plain_font);
	small.SetSize(be_plain_font->Size() * 0.85f);
	SetFont(&small);
	font_height height;
	GetFontHeight(&height);
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	for (uint32 i = 0; i < LCD_SHIELD_BUTTONS; i++) {
		BRect frame = _ButtonFrame(i);
		bool down = (fButtons >> i) & 1;
		SetHighColor(down ? make_color(255, 190, 60)
			: tint_color(panel, B_DARKEN_2_TINT));
		FillRoundRect(frame, 4, 4);
		SetHighColor(tint_color(panel, B_DARKEN_4_TINT));
		StrokeRoundRect(frame, 4, 4);
		char label[8];
		snprintf(label, sizeof(label), "SW%u", (unsigned)i + 1);
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		SetLowColor(down ? make_color(255, 190, 60)
			: tint_color(panel, B_DARKEN_2_TINT));
		DrawString(label, BPoint(frame.left + (frame.Width()
			- StringWidth(label)) / 2, frame.top + (frame.Height()
			+ height.ascent - height.descent) / 2));
	}

	// the LEDs, D1 to D7
	SetLowColor(panel);
	small.SetSize(be_plain_font->Size() * 0.75f);
	SetFont(&small);
	for (uint32 i = 0; i < LCD_SHIELD_LEDS; i++) {
		BRect frame = _LedFrame(i);
		bool lit = (fLeds >> i) & 1;
		if (lit) {
			SetHighColor(make_color(120, 170, 255));
			FillEllipse(frame.InsetByCopy(-2, -2));
		}
		SetHighColor(lit ? make_color(225, 240, 255)
			: tint_color(panel, B_DARKEN_3_TINT));
		FillEllipse(frame);
		char label[8];
		snprintf(label, sizeof(label), "D%u", (unsigned)i + 1);
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString(label, BPoint(frame.left + (frame.Width()
			- small.StringWidth(label)) / 2, frame.bottom + small.Size() + 1));
	}
}


void
ShieldPanel::MouseDown(BPoint where)
{
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (Window() != NULL && Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		_ShowMenu(where);
		return;
	}

	for (uint32 i = 0; i < LCD_SHIELD_LEDS; i++) {
		if (_LedFrame(i).InsetByCopy(-3, -3).Contains(where)) {
			BMessage message(kMsgToggleLed);
			message.AddInt32("led", i);
			_Command(&message);
			return;
		}
	}
	// SW1 and SW2 as on the shield: the next page, the next LED mode
	for (uint32 i = 0; i < LCD_SHIELD_BUTTONS; i++) {
		if (_ButtonFrame(i).Contains(where)) {
			BMessage message(i == 0 ? kMsgSetPage : kMsgSetLedMode);
			message.AddInt32("step", 1);
			_Command(&message);
			return;
		}
	}
}
