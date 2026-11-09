/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */


#include "WeatherView.h"

#include <stdio.h>

#include <Alert.h>
#include <Dragger.h>
#include <Font.h>
#include <Window.h>


enum {
	kMsgReading = 'wbRd',
	kMsgNoBoard = 'wbNo'
};

static const bigtime_t kInterval = 1000000;
static const bigtime_t kRetryInterval = 3000000;
static const uint16 kDarkLevel = 256;
	// what the Si1132's light channels read in the dark

static const char* kDegree = "\xc2\xb0";


static rgb_color
mix(rgb_color a, rgb_color b, float amount)
{
	rgb_color color;
	color.red = (uint8)(a.red + (b.red - a.red) * amount);
	color.green = (uint8)(a.green + (b.green - a.green) * amount);
	color.blue = (uint8)(a.blue + (b.blue - a.blue) * amount);
	color.alpha = 255;
	return color;
}


static BSize
panel_size(const BFont& font)
{
	font_height height;
	font.GetHeight(&height);
	float line = ceilf((height.ascent + height.descent + height.leading)
		* 1.35f);
	float em = font.Size();
	// title, the big temperature (two lines), four rows, two graphs
	return BSize(ceilf(em * 17), ceilf(line * 15.5f + em * 1.5f));
}


WeatherView::WeatherView()
	:
	BView(BRect(BPoint(0, 0), panel_size(*be_plain_font)), "WeatherView",
		B_FOLLOW_NONE, B_WILL_DRAW | B_FRAME_EVENTS | B_SUBPIXEL_PRECISE),
	fReader(-1),
	fWake(-1),
	fQuit(false),
	fReplicant(false),
	fFahrenheitFirst(false),
	fHaveReading(false),
	fBoardMissing(false),
	fHistoryCount(0),
	fHistoryNext(0)
{
	_AddDragger();
}


WeatherView::WeatherView(BMessage* archive)
	:
	BView(archive),
	fReader(-1),
	fWake(-1),
	fQuit(false),
	fReplicant(true),
	fFahrenheitFirst(false),
	fHaveReading(false),
	fBoardMissing(false),
	fHistoryCount(0),
	fHistoryNext(0)
{
	archive->FindBool("fahrenheit first", &fFahrenheitFirst);
	// a replicant archived by an older version takes this one's size; the
	// dragger comes back with the archive's child views, but where it was:
	// a view not in a window doesn't move its children when it is resized
	BSize size = panel_size(*be_plain_font);
	ResizeTo(size.width, size.height);
	_PlaceDragger();
}


WeatherView::~WeatherView()
{
	_StopReader();
}


BArchivable*
WeatherView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "WeatherView"))
		return NULL;
	return new WeatherView(archive);
}


status_t
WeatherView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kAppSignature);
	if (status == B_OK)
		status = archive->AddBool("fahrenheit first", fFahrenheitFirst);
	return status;
}


BSize
WeatherView::PreferredPanelSize()
{
	return panel_size(*be_plain_font);
}


void
WeatherView::_AddDragger()
{
	BRect frame = Bounds();
	frame.left = frame.right - 7;
	frame.top = frame.bottom - 7;
	AddChild(new BDragger(frame, this,
		B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM));
}


/*!	Puts the dragger in the bottom right corner. */
void
WeatherView::_PlaceDragger()
{
	for (int32 i = 0; BView* child = ChildAt(i); i++) {
		BDragger* dragger = dynamic_cast<BDragger*>(child);
		if (dragger == NULL)
			continue;
		BRect bounds = Bounds();
		dragger->MoveTo(bounds.right - dragger->Bounds().Width(),
			bounds.bottom - dragger->Bounds().Height());
	}
}


void
WeatherView::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	SetHighUIColor(B_PANEL_TEXT_COLOR);
	fTarget = BMessenger(this);
	_StartReader();
}


void
WeatherView::DetachedFromWindow()
{
	_StopReader();
	BView::DetachedFromWindow();
}


void
WeatherView::_StartReader()
{
	if (fReader >= 0)
		return;
	fQuit = false;
	fWake = create_sem(0, "weather board wake");
	fReader = spawn_thread(_ReaderThread, "weather board reader",
		B_LOW_PRIORITY, this);
	if (fReader >= 0)
		resume_thread(fReader);
}


void
WeatherView::_StopReader()
{
	if (fReader < 0)
		return;
	fQuit = true;
	release_sem(fWake);
	status_t result;
	wait_for_thread(fReader, &result);
	fReader = -1;
	delete_sem(fWake);
	fWake = -1;
}


status_t
WeatherView::_ReaderThread(void* self)
{
	((WeatherView*)self)->_Reader();
	return B_OK;
}


/*!	Runs in its own thread: the chips take a few ms to measure, which the
	window (the Desktop's, for a replicant) shouldn't wait for.
*/
void
WeatherView::_Reader()
{
	WeatherSensors sensors;
	while (!fQuit) {
		bigtime_t wait = kInterval;
		WeatherReading reading;
		if ((sensors.IsOpen() || sensors.Open() == B_OK)
			&& sensors.Read(reading) == B_OK) {
			BMessage message(kMsgReading);
			message.AddData("reading", B_RAW_TYPE, &reading,
				sizeof(reading));
			fTarget.SendMessage(&message, (BHandler*)NULL, 100000);
		} else {
			sensors.Close();
			BMessage message(kMsgNoBoard);
			fTarget.SendMessage(&message, (BHandler*)NULL, 100000);
			wait = kRetryInterval;
		}
		acquire_sem_etc(fWake, 1, B_RELATIVE_TIMEOUT, wait);
	}
}


void
WeatherView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgReading:
		{
			const void* data;
			ssize_t size;
			if (message->FindData("reading", B_RAW_TYPE, &data, &size)
					!= B_OK || size != sizeof(WeatherReading)) {
				break;
			}
			memcpy(&fReading, data, sizeof(fReading));
			fHaveReading = true;
			fBoardMissing = false;
			Sample& sample = fHistory[fHistoryNext];
			sample.climate = fReading.climateValid;
			sample.light = fReading.lightValid;
			sample.temperature = fReading.temperature;
			sample.visible = max_c((int)fReading.visible - kDarkLevel, 0);
			sample.infrared = max_c((int)fReading.infrared - kDarkLevel,
				0);
			fHistoryNext = (fHistoryNext + 1) % kHistorySize;
			if (fHistoryCount < kHistorySize)
				fHistoryCount++;
			Invalidate();
			break;
		}

		case kMsgNoBoard:
			fBoardMissing = true;
			Invalidate();
			break;

		case B_ABOUT_REQUESTED:
		{
			BAlert* alert = new BAlert("About Weather Board",
				"Weather Board\n\n"
				"Hardkernel's Weather Board 2 on the ODROID-C2's header "
				"(I2C B, pins 27 and 28): temperature, humidity and "
				"pressure from its BME280, light and UV index from its "
				"Si1132.\n\nClick the panel to switch between Celsius "
				"and Fahrenheit; the graph follows.\n\n"
				"Copyright 2026, haiku-odroid-c2 port authors.", "OK");
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
WeatherView::MouseDown(BPoint where)
{
	fFahrenheitFirst = !fFahrenheitFirst;
	Invalidate();
}


void
WeatherView::_DrawRow(float& y, const char* label, const char* value)
{
	font_height height;
	GetFontHeight(&height);
	float line = ceilf((height.ascent + height.descent + height.leading)
		* 1.35f);
	float margin = be_plain_font->Size();
	BRect bounds = Bounds();

	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color dim = mix(text, ui_color(B_PANEL_BACKGROUND_COLOR), 0.4f);
	SetHighColor(dim);
	DrawString(label, BPoint(margin, y + height.ascent));
	SetHighColor(text);
	DrawString(value, BPoint(bounds.right - margin - StringWidth(value),
		y + height.ascent));
	y += line;
}


bool
WeatherView::_SampleValue(int32 index, Series series, float& value) const
{
	const Sample& sample = fHistory[(fHistoryNext - fHistoryCount + index
		+ kHistorySize) % kHistorySize];
	switch (series) {
		case kTemperature:
			// in the unit shown first
			value = fFahrenheitFirst ? sample.temperature * 9 / 5 + 32
				: sample.temperature;
			return sample.climate;
		case kVisible:
			value = sample.visible;
			return sample.light;
		case kInfrared:
			value = sample.infrared;
			return sample.light;
	}
	return false;
}


/*!	The last two minutes of one or more series, newest on the right, scaled
	to their range (from zero if \a fromZero, and at least \a minimumSpan),
	with the range in the left corners, the series' names in their colours
	at the top right and, if \a showTime, the time covered at the bottom
	right.
*/
void
WeatherView::_DrawGraph(BRect frame, const Series* series,
	const rgb_color* colors, const char* const* names, int32 count,
	bool fromZero, float minimumSpan, const char* format, bool showTime)
{
	rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	SetHighColor(mix(background, text, 0.08f));
	FillRoundRect(frame, 3, 3);

	// labels, small, in strips of their own at the top and bottom
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() * 0.75f);
	font_height height;
	font.GetHeight(&height);
	float labelHeight = ceilf(height.ascent + height.descent) + 2;

	float low = 1e9f, high = -1e9f;
	int32 points = 0;
	for (int32 s = 0; s < count; s++) {
		for (int32 i = 0; i < fHistoryCount; i++) {
			float value;
			if (!_SampleValue(i, series[s], value))
				continue;
			low = min_c(low, value);
			high = max_c(high, value);
			points++;
		}
	}
	if (points > 0) {
		if (fromZero)
			low = 0;
		if (high - low < minimumSpan) {
			if (fromZero)
				high = low + minimumSpan;
			else {
				float middle = (high + low) / 2;
				low = middle - minimumSpan / 2;
				high = middle + minimumSpan / 2;
			}
		}

		BRect plot = frame.InsetByCopy(3, 3);
		plot.top += labelHeight;
		plot.bottom -= labelHeight;
		float step = plot.Width() / (kHistorySize - 1);
		float x = plot.right - step * (fHistoryCount - 1);
		SetPenSize(1.5f);
		for (int32 s = 0; s < count; s++) {
			SetHighColor(colors[s]);
			BPoint previous;
			bool havePrevious = false;
			for (int32 i = 0; i < fHistoryCount; i++) {
				float value;
				if (!_SampleValue(i, series[s], value)) {
					havePrevious = false;
					continue;
				}
				BPoint point(x + step * i, plot.bottom
					- (value - low) / (high - low) * plot.Height());
				if (havePrevious)
					StrokeLine(previous, point);
				previous = point;
				havePrevious = true;
			}
		}
		SetPenSize(1);
	}

	SetFont(&font);
	rgb_color dim = mix(text, background, 0.4f);
	float top = frame.top + 2 + height.ascent;
	float bottom = frame.bottom - 2 - height.descent;
	char label[32];
	if (points > 0) {
		SetHighColor(dim);
		snprintf(label, sizeof(label), format, high);
		DrawString(label, BPoint(frame.left + 4, top));
		snprintf(label, sizeof(label), format, low);
		DrawString(label, BPoint(frame.left + 4, bottom));
	}
	float right = frame.right - 4;
	for (int32 s = count - 1; s >= 0; s--) {
		SetHighColor(colors[s]);
		float width = StringWidth(names[s]);
		DrawString(names[s], BPoint(right - width, top));
		right -= width + font.Size() * 0.6f;
	}
	if (showTime && fHistoryCount > 0) {
		SetHighColor(dim);
		snprintf(label, sizeof(label), "last %d s", (int)fHistoryCount);
		DrawString(label, BPoint(frame.right - 4 - StringWidth(label),
			bottom));
	}
}


void
WeatherView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color dim = mix(text, background, 0.4f);
	float margin = be_plain_font->Size();

	SetHighColor(background);
	FillRect(bounds);
	SetHighColor(tint_color(background, B_DARKEN_2_TINT));
	StrokeRect(bounds);

	// the title, and whether readings come in
	SetFont(be_bold_font);
	font_height height;
	GetFontHeight(&height);
	float y = margin * 0.6f;
	SetHighColor(text);
	DrawString("Weather Board", BPoint(margin, y + height.ascent));
	SetFont(be_plain_font);
	const char* state = fBoardMissing ? "not found"
		: fHaveReading ? "live" : "starting";
	float stateWidth = StringWidth(state);
	SetHighColor(dim);
	DrawString(state, BPoint(bounds.right - margin - stateWidth,
		y + height.ascent));
	float dot = height.ascent * 0.5f;
	SetHighColor(fBoardMissing ? rgb_color{200, 60, 50, 255}
		: fHaveReading ? rgb_color{60, 170, 80, 255}
		: rgb_color{200, 160, 40, 255});
	FillEllipse(BRect(bounds.right - margin - stateWidth - dot * 2.2f,
		y + height.ascent - dot * 1.4f, bounds.right - margin - stateWidth
			- dot * 0.6f, y + height.ascent + dot * 0.2f));
	y += ceilf((height.ascent + height.descent + height.leading) * 1.35f);

	if (fBoardMissing && !fHaveReading) {
		SetHighColor(text);
		DrawString("No Weather Board found on", BPoint(margin,
			y + height.ascent * 2));
		DrawString("I2C B (header pins 27, 28).", BPoint(margin,
			y + height.ascent * 3.4f));
		SetHighColor(dim);
		DrawString("Trying again every 3 s.", BPoint(margin,
			y + height.ascent * 5.2f));
		return;
	}

	// the temperature, big, in both units
	char value[64];
	char other[64];
	bool climate = fHaveReading && fReading.climateValid;
	if (climate) {
		double celsius = fReading.temperature;
		double fahrenheit = celsius * 9 / 5 + 32;
		snprintf(value, sizeof(value), "%.1f %sC", celsius, kDegree);
		snprintf(other, sizeof(other), "%.1f %sF", fahrenheit, kDegree);
		if (fFahrenheitFirst) {
			char swap[64];
			strcpy(swap, value);
			strcpy(value, other);
			strcpy(other, swap);
		}
	} else {
		strcpy(value, "--.- ");
		other[0] = '\0';
	}
	BFont big(be_plain_font);
	big.SetSize(be_plain_font->Size() * 2.4f);
	big.SetFace(B_BOLD_FACE);
	SetFont(&big);
	font_height bigHeight;
	big.GetHeight(&bigHeight);
	SetHighColor(text);
	DrawString(value, BPoint(margin, y + bigHeight.ascent));
	float valueWidth = StringWidth(value);
	SetFont(be_plain_font);
	SetHighColor(dim);
	DrawString(other, BPoint(margin + valueWidth + margin * 0.6f,
		y + bigHeight.ascent));
	y += ceilf(bigHeight.ascent + bigHeight.descent + margin * 0.4f);

	// the other readings
	if (climate) {
		snprintf(value, sizeof(value), "%.1f %%", fReading.humidity);
		_DrawRow(y, "Humidity", value);
		snprintf(value, sizeof(value), "%.1f hPa", fReading.pressure);
		_DrawRow(y, "Pressure", value);
	} else {
		_DrawRow(y, "Humidity", "--");
		_DrawRow(y, "Pressure", "--");
	}
	if (fHaveReading && fReading.lightValid) {
		int visible = max_c((int)fReading.visible - kDarkLevel, 0);
		int infrared = max_c((int)fReading.infrared - kDarkLevel, 0);
		snprintf(value, sizeof(value), "%d visible, %d IR", visible,
			infrared);
		_DrawRow(y, "Light", value);
		snprintf(value, sizeof(value), "%.1f", fReading.uvIndex);
		_DrawRow(y, "UV index", value);
	} else {
		_DrawRow(y, "Light", "--");
		_DrawRow(y, "UV index", "--");
	}

	// the last two minutes of temperature and of light
	float graphTop = y + margin * 0.3f;
	float graphBottom = bounds.bottom - margin * 0.9f;
	float gap = margin * 0.4f;
	float graphHeight = floorf((graphBottom - graphTop - gap) / 2);
	if (graphHeight > 8) {
		static const Series kTemperatureSeries[] = { kTemperature };
		static const char* const kTemperatureNames[] = { "temperature" };
		const rgb_color temperatureColors[] = { { 220, 90, 50, 255 } };
		char format[16];
		snprintf(format, sizeof(format), "%%.1f %s%s", kDegree,
			fFahrenheitFirst ? "F" : "C");
		_DrawGraph(BRect(margin, graphTop, bounds.right - margin,
				graphTop + graphHeight), kTemperatureSeries,
			temperatureColors, kTemperatureNames, 1, false,
			fFahrenheitFirst ? 0.9f : 0.5f, format, false);

		static const Series kLightSeries[] = { kVisible, kInfrared };
		static const char* const kLightNames[] = { "visible", "IR" };
		const rgb_color lightColors[] = { { 200, 150, 0, 255 },
			{ 150, 60, 170, 255 } };
		_DrawGraph(BRect(margin, graphBottom - graphHeight,
				bounds.right - margin, graphBottom), kLightSeries,
			lightColors, kLightNames, 2, true, 10, "%.0f", true);
	}
	SetFont(be_plain_font);
}
