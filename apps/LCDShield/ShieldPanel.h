/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef SHIELD_PANEL_H
#define SHIELD_PANEL_H


#include <string>

#include <Messenger.h>
#include <String.h>
#include <View.h>

#include "ShieldDevice.h"
#include "Snapshot.h"


class BMessageRunner;


extern const char* kAppSignature;


enum {
	// what a panel is told, by its window, its own clicks or another panel
	kMsgSetPage = 'spag',			// int32 "page", or int32 "step"
	kMsgSetLedMode = 'sled',		// int32 "mode", or int32 "step"
	kMsgToggleLed = 'togl',			// int32 "led"
	kMsgSetFahrenheit = 'sfah',		// bool "fahrenheit"
	kMsgSetTwelveHour = 'stwe',		// bool "12 hour clock"
	kMsgSetMessage = 'smsg',		// string "message"

	// what a panel tells the view it is in (ShieldBox) when that changes:
	// int32 "page", "page count", "led mode"; bool "fahrenheit",
	// "12 hour clock", "driving", "shield"; string "message"
	kMsgPanelState = 'pstt'
};


/*!	The 16x2 LCD + IO Shield on screen, and the demo that runs it: the
	display's pages, the LEDs, the buttons. It lives in a ShieldBox, with the
	controls: in the window, or as a replicant on the Desktop, running the
	shield inside Tracker.

	Only one panel drives the shield at a time, the one holding the lock
	file; it keeps its state (and its messenger) in a state file. Any other
	panel, the window's or a replicant, shows the shield as it is, follows
	that state, and sends what it is told to the driving panel. When that
	one goes, the next takes over where it was.
*/
class ShieldPanel : public BView {
public:
	enum Page {
		kClock,
		kCpu,
		kBlinken,
		kMemory,
		kNetwork,
		kWeather,
		kMessage,
		kPageCount
	};

	enum LedMode {
		kLedLoad,
		kLedScanner,
		kLedManual,
		kLedModeCount
	};

								ShieldPanel(const BMessage* settings);
	virtual						~ShieldPanel();

			// a replicant of 1.2, the panel alone, comes back as a ShieldBox
	static	BArchivable*		Instantiate(BMessage* archive);

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);

			// the smallest it gets; a wider one has bigger characters, and
			// is taller
	static	BSize				PanelSize();
			void				SetWidth(float width);

	static	const char*			PageName(int32 page);
	static	const char*			LedModeName(int32 mode);

			// the page, the LED mode and the LEDs set by hand, the unit,
			// the clock, the message: what the app keeps in its settings
			void				GetSettings(BMessage& settings) const;

private:
	enum CharacterSet {
		kNoCharacters,
		kBarCharacters,
		kBlinkenCharacters
	};

			void				_Init();
			void				_ApplySettings(const BMessage* settings);

	static	status_t			_SamplerThread(void* self);
	static	status_t			_ButtonThread(void* self);
			bool				_OpenDevice();
			void				_StartThreads();
			void				_StopThreads();

			// who drives the shield
			void				_OpenFiles();
			void				_CloseFiles();
			void				_Tick();
			bool				_TryToDrive();
			void				_StopDriving();
			void				_WriteState();
			bool				_ReadState(BMessenger* driver);
			void				_Command(BMessage* message);
			void				_Apply(BMessage* message);
			void				_NotifyParent();

			// the pages and the LEDs
			int32				_NextPage(int32 page, int32 step) const;
			void				_SetPage(int32 page);
			void				_ShowPage();
			void				_Lines(BString& first, BString& second);
			void				_BlinkenLines(std::string lines[2]);
			int32				_CharacterSetFor(int32 page) const;
			void				_UseCharacters(int32 set);
			BString				_Bar(float value, int32 cells);
			BString				_Temperature(float celsius) const;
			void				_UpdateLeds();
			void				_UpdateMirror();
			void				_ShowMenu(BPoint where);

			// drawing
			struct Geometry {
				float			margin;		// the plain font's size
				float			pitchX;		// a dot and its gap
				float			pitchY;
				float			rowGap;		// between the rows of characters
				float			cellWidth;
				float			cellHeight;
				float			bezelX;
				float			bezelY;
				float			textLeft;
				float			textTop;
				float			lcdHeight;
				float			width;
				float			height;
			};

	static	void				_GetGeometry(float width, Geometry& geometry);
			BRect				_LcdFrame() const;
			BRect				_LedFrame(uint32 index) const;
			BRect				_ButtonFrame(uint32 index) const;
			void				_DrawCell(BPoint origin, uint8 code);

			BMessenger			fSelf;
			BMessageRunner*		fTicker;

			ShieldDevice		fDevice;
			bool				fDeviceOpen;
			bigtime_t			fLastOpenTry;
			thread_id			fSampler;
			thread_id			fButtonReader;
			volatile bool		fQuitting;

			int					fLockFile;
			int					fPresenceFile;
			bool				fDriving;
			std::string			fWrittenState;

			// the demo's state (the driving panel's, followed by the others)
			int32				fPage;
			int32				fLedMode;
			uint32				fManualLeds;
			bool				fFahrenheit;
			bool				fTwelveHour;
			BString				fMessage;

			Snapshot			fSnapshot;
			bool				fHaveSnapshot;
			float				fSmoothedLoad;
			float				fCoreLoad[kMaxCores];
			int32				fScanner;
			int32				fScannerStep;
			std::string			fShown[2];
			int32				fCharacterSet;
			uint8				fCharacters[8][8];

			// what is drawn: the shield as its devices say it is
			lcd_shield_text		fText;
			uint32				fLeds;
			uint32				fButtons;

			std::string			fNotified;
			Geometry			fGeometry;
};


#endif	// SHIELD_PANEL_H
