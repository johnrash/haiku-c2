/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEATHER_VIEW_H
#define WEATHER_VIEW_H


#include <Messenger.h>
#include <View.h>

#include "WeatherSensors.h"


extern const char* kAppSignature;


/*!	Shows the Weather Board's readings, a new one every second, with the
	last two minutes of temperature and of light as curves. Archivable,
	with a dragger: a replicant on the Desktop (or any shelf) reads the
	board itself.
*/
class WeatherView : public BView {
public:
								WeatherView();
								WeatherView(BMessage* archive);
	virtual						~WeatherView();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);

	static	BSize				PreferredPanelSize();

private:
	static	status_t			_ReaderThread(void* self);
			void				_Reader();
			void				_StartReader();
			void				_StopReader();
			void				_AddDragger();
			void				_PlaceDragger();

	struct Sample {
		bool	climate;
		bool	light;
		float	temperature;
		float	visible;		// counts above the dark level
		float	infrared;
	};

	enum Series {
		kTemperature,
		kVisible,
		kInfrared
	};

			void				_DrawRow(float& y, const char* label,
									const char* value);
			bool				_SampleValue(int32 index, Series series,
									float& value) const;
			void				_DrawGraph(BRect frame,
									const Series* series,
									const rgb_color* colors,
									const char* const* names, int32 count,
									bool fromZero, float minimumSpan,
									const char* format, bool showTime);

	enum { kHistorySize = 120 };

			thread_id			fReader;
			sem_id				fWake;
			volatile bool		fQuit;
			BMessenger			fTarget;
			bool				fReplicant;
			bool				fFahrenheitFirst;

			WeatherReading		fReading;
			bool				fHaveReading;
			bool				fBoardMissing;
			Sample				fHistory[kHistorySize];
			int32				fHistoryCount;
			int32				fHistoryNext;
};


#endif	// WEATHER_VIEW_H
