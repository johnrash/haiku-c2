/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */


#include "ShieldBox.h"

#include <algorithm>

#include <math.h>
#include <string.h>

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Dragger.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include "ShieldPanel.h"


enum {
	kMsgPrevious = 'prev',
	kMsgNext = 'next',
	kMsgLedModeItem = 'ledi',
	kMsgFahrenheitBox = 'fahb',
	kMsgTwelveHourBox = 'twlb',
	kMsgShowMessage = 'mesg',
};

static const char* kName = "LCD Shield";
static const float kDraggerSize = 7;


// a view's size in pixels (Bounds() is one less)
static float
width_of(BView* view)
{
	return view->Bounds().Width() + 1;
}


static float
height_of(BView* view)
{
	return view->Bounds().Height() + 1;
}


ShieldBox::ShieldBox(const BMessage* settings)
	:
	BView(BRect(0, 0, 0, 0), kName, B_FOLLOW_NONE, B_WILL_DRAW),
	fReplicant(false)
{
	_Build(settings);
}


ShieldBox::ShieldBox(BMessage* archive)
	:
	BView(archive),
	fReplicant(true)
{
	SetName(kName);
	_Build(archive);
}


BArchivable*
ShieldBox::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "ShieldBox"))
		return NULL;
	return new ShieldBox(archive);
}


status_t
ShieldBox::Archive(BMessage* archive, bool deep) const
{
	// the children come back from the settings, as this version builds them
	status_t status = BView::Archive(archive, false);
	if (status == B_OK)
		status = archive->AddString("add_on", kAppSignature);
	if (status == B_OK)
		GetSettings(*archive);
	return status;
}


void
ShieldBox::GetSettings(BMessage& settings) const
{
	fPanel->GetSettings(settings);
}


/*!	The panel and two rows of controls under it, as small as they go; the
	dragger in the bottom right corner, clear of them.
*/
void
ShieldBox::_Build(const BMessage* settings)
{
	float spacing = be_control_look->DefaultItemSpacing();
	float inset = std::max(ceilf(spacing * 0.75f), kDraggerSize + 1);
	float gap = ceilf(spacing / 2);

	fPanel = new ShieldPanel(settings);

	// the first row: the pages, the LEDs, the units
	BButton* previous = new BButton(BRect(0, 0, 0, 0), "previous",
		"\xe2\x97\x80", new BMessage(kMsgPrevious), B_FOLLOW_NONE);
	previous->ResizeToPreferred();
	float height = height_of(previous);
	float arrowWidth = ceilf(height * 1.1f);
	previous->ResizeTo(arrowWidth - 1, height - 1);
	BButton* next = new BButton(BRect(0, 0, 0, 0), "next", "\xe2\x96\xb6",
		new BMessage(kMsgNext), B_FOLLOW_NONE);
	next->ResizeTo(arrowWidth - 1, height - 1);
	previous->SetToolTip("The previous page");
	next->SetToolTip("The next page");

	float labelWidth = 0;
	for (int32 page = 0; page < ShieldPanel::kPageCount; page++) {
		labelWidth = std::max(labelWidth,
			be_plain_font->StringWidth(ShieldPanel::PageName(page)));
	}
	labelWidth = ceilf(labelWidth + be_plain_font->StringWidth(" 7/7") + gap);
	fPageLabel = new BStringView(BRect(0, 0, labelWidth - 1, height - 1),
		"page", "", B_FOLLOW_NONE);
	fPageLabel->SetAlignment(B_ALIGN_CENTER);

	int32 ledMode = 0;
	settings->FindInt32("led mode", &ledMode);
	BPopUpMenu* menu = new BPopUpMenu("leds");
	for (int32 i = 0; i < ShieldPanel::kLedModeCount; i++) {
		BMessage* message = new BMessage(kMsgLedModeItem);
		message->AddInt32("mode", i);
		BMenuItem* item = new BMenuItem(ShieldPanel::LedModeName(i), message);
		item->SetMarked(i == ledMode);
		menu->AddItem(item);
	}
	fLedField = new BMenuField(BRect(0, 0, 0, 0), "leds", "LEDs:", menu,
		B_FOLLOW_NONE);
	fLedField->SetDivider(ceilf(be_plain_font->StringWidth("LEDs:") + gap));
	fLedField->ResizeToPreferred();

	bool fahrenheit = false;
	settings->FindBool("fahrenheit", &fahrenheit);
	fFahrenheitBox = new BCheckBox(BRect(0, 0, 0, 0), "fahrenheit",
		"\xc2\xb0" "F", new BMessage(kMsgFahrenheitBox), B_FOLLOW_NONE);
	fFahrenheitBox->ResizeToPreferred();
	fFahrenheitBox->SetValue(fahrenheit ? B_CONTROL_ON : B_CONTROL_OFF);
	fFahrenheitBox->SetToolTip("Temperatures in Fahrenheit");

	// what the clock page shows is the panel's (the Time preferences', until
	// it's set); the panel's state sets the box when it's attached
	bool twelveHour = false;
	settings->FindBool("12 hour clock", &twelveHour);
	fTwelveHourBox = new BCheckBox(BRect(0, 0, 0, 0), "12 hour clock",
		"AM/PM", new BMessage(kMsgTwelveHourBox), B_FOLLOW_NONE);
	fTwelveHourBox->ResizeToPreferred();
	fTwelveHourBox->SetValue(twelveHour ? B_CONTROL_ON : B_CONTROL_OFF);
	fTwelveHourBox->SetToolTip("The clock in 12 hours, with AM and PM");

	// the second row: the message
	const char* message = "";
	settings->FindString("message", &message);
	fShownMessage = message;
	BButton* show = new BButton(BRect(0, 0, 0, 0), "show", "Show",
		new BMessage(kMsgShowMessage), B_FOLLOW_NONE);
	show->ResizeToPreferred();
	fMessageControl = new BTextControl(BRect(0, 0, 0, 0), "message",
		"Message:", message, new BMessage(kMsgShowMessage), B_FOLLOW_NONE);
	fMessageControl->SetDivider(ceilf(be_plain_font->StringWidth("Message:")
		+ gap));
	fMessageControl->ResizeToPreferred();

	// as wide as the panel or the first row, whichever is wider
	float pages = arrowWidth * 2 + labelWidth;
	float firstRow = pages + spacing + width_of(fLedField) + spacing
		+ width_of(fFahrenheitBox) + spacing + width_of(fTwelveHourBox);
	float width = std::max(width_of(fPanel), firstRow);

	// the panel as wide as the rows: its dots grow to fit (it may take a few
	// pixels more for a bigger dot)
	float y = inset;
	fPanel->SetWidth(width);
	width = std::max(width, width_of(fPanel));
	fPanel->MoveTo(inset, y);
	AddChild(fPanel);
	y += height_of(fPanel) + gap;

	float rowHeight = std::max(std::max(height, height_of(fLedField)),
		std::max(height_of(fFahrenheitBox), height_of(fTwelveHourBox)));
	float x = inset;
	BView* left[] = { previous, fPageLabel, next };
	for (size_t i = 0; i < B_COUNT_OF(left); i++) {
		left[i]->MoveTo(x, y + floorf((rowHeight - height_of(left[i])) / 2));
		AddChild(left[i]);
		x += width_of(left[i]);
	}
	x = inset + width;
	BView* right[] = { fTwelveHourBox, fFahrenheitBox, fLedField };
	for (size_t i = 0; i < B_COUNT_OF(right); i++) {
		x -= width_of(right[i]);
		right[i]->MoveTo(x, y + floorf((rowHeight - height_of(right[i])) / 2));
		AddChild(right[i]);
		x -= spacing;
	}
	y += rowHeight + gap;

	rowHeight = std::max(height_of(fMessageControl), height_of(show));
	fMessageControl->ResizeTo(width - width_of(show) - gap - 1,
		height_of(fMessageControl) - 1);
	// its text view follows only a new divider (no FrameResized() before
	// it is attached)
	fMessageControl->SetDivider(fMessageControl->Divider());
	fMessageControl->MoveTo(inset,
		y + floorf((rowHeight - height_of(fMessageControl)) / 2));
	AddChild(fMessageControl);
	show->MoveTo(inset + width - width_of(show),
		y + floorf((rowHeight - height_of(show)) / 2));
	AddChild(show);
	y += rowHeight + inset;

	ResizeTo(width + inset * 2 - 1, y - 1);

	BRect frame = Bounds();
	frame.left = frame.right - kDraggerSize;
	frame.top = frame.bottom - kDraggerSize;
	AddChild(new BDragger(frame, this, B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM));
}


void
ShieldBox::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);

	for (int32 i = 0; BView* child = ChildAt(i); i++) {
		BControl* control = dynamic_cast<BControl*>(child);
		if (control != NULL)
			control->SetTarget(this);
	}
	fLedField->Menu()->SetTargetForItems(this);
}


void
ShieldBox::Draw(BRect updateRect)
{
	// on the Desktop, an edge round it
	if (!fReplicant)
		return;
	SetHighColor(tint_color(ViewColor(), B_DARKEN_2_TINT));
	StrokeRect(Bounds());
}


void
ShieldBox::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPrevious:
		case kMsgNext:
		{
			BMessage command(kMsgSetPage);
			command.AddInt32("step", message->what == kMsgNext ? 1 : -1);
			_SendToPanel(&command);
			break;
		}

		case kMsgLedModeItem:
		{
			int32 mode;
			if (message->FindInt32("mode", &mode) != B_OK)
				break;
			BMessage command(kMsgSetLedMode);
			command.AddInt32("mode", mode);
			_SendToPanel(&command);
			break;
		}

		case kMsgFahrenheitBox:
		{
			BMessage command(kMsgSetFahrenheit);
			command.AddBool("fahrenheit",
				fFahrenheitBox->Value() == B_CONTROL_ON);
			_SendToPanel(&command);
			break;
		}

		case kMsgTwelveHourBox:
		{
			BMessage command(kMsgSetTwelveHour);
			command.AddBool("12 hour clock",
				fTwelveHourBox->Value() == B_CONTROL_ON);
			_SendToPanel(&command);
			break;
		}

		case kMsgShowMessage:
		{
			BMessage command(kMsgSetMessage);
			command.AddString("message", fMessageControl->Text());
			_SendToPanel(&command);
			break;
		}

		case kMsgPanelState:
			_ShowState(message);
			break;

		case B_ABOUT_REQUESTED:
			ShowAbout();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
ShieldBox::_SendToPanel(BMessage* message)
{
	if (Looper() != NULL)
		Looper()->PostMessage(message, fPanel);
}


/*!	What the controls show: the page and the LED mode the panel is on, the
	units, the message. In the window, its title says whether this panel
	drives the shield or follows another one (the Desktop's).
*/
void
ShieldBox::_ShowState(BMessage* state)
{
	int32 page = 0;
	int32 count = 0;
	int32 mode = 0;
	bool fahrenheit = false;
	bool twelveHour = false;
	bool driving = false;
	bool shield = false;
	const char* message = "";
	state->FindInt32("page", &page);
	state->FindInt32("page count", &count);
	state->FindInt32("led mode", &mode);
	state->FindBool("fahrenheit", &fahrenheit);
	state->FindBool("12 hour clock", &twelveHour);
	state->FindBool("driving", &driving);
	state->FindBool("shield", &shield);
	state->FindString("message", &message);

	BString label;
	label.SetToFormat("%s %d/%d", ShieldPanel::PageName(page),
		(int)page + 1, (int)count);
	fPageLabel->SetText(label);
	BMenuItem* item = fLedField->Menu()->ItemAt(mode);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);
	fFahrenheitBox->SetValue(fahrenheit ? B_CONTROL_ON : B_CONTROL_OFF);
	fTwelveHourBox->SetValue(twelveHour ? B_CONTROL_ON : B_CONTROL_OFF);
	// another panel's message, unless this one is being typed
	if (fShownMessage != message) {
		fShownMessage = message;
		if (!fMessageControl->TextView()->IsFocus())
			fMessageControl->SetText(message);
	}

	if (fReplicant || Window() == NULL)
		return;
	const char* title = !shield ? "LCD Shield (no shield)"
		: driving ? "LCD Shield" : "LCD Shield (mirror)";
	if (strcmp(Window()->Title(), title) != 0)
		Window()->SetTitle(title);
}


/*static*/ void
ShieldBox::ShowAbout()
{
	BAlert* alert = new BAlert("About LCD Shield",
		"LCD Shield\n\n"
		"Hardkernel's 16x2 LCD + IO Shield on the ODROID-C2's header: "
		"pages on its display (the clock, the CPU, Blinken, memory, "
		"the network, a Weather Board, a message), its seven LEDs, "
		"its two buttons.\n\n"
		"Click SW1 or SW2 here as on the shield, click an LED to "
		"switch it, right-click the display for the pages, the LEDs, "
		"Fahrenheit and AM/PM. The handle in the bottom right corner puts all of "
		"it on the Desktop.\n\n"
		"Copyright 2026, haiku-odroid-c2 port authors.", "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}
