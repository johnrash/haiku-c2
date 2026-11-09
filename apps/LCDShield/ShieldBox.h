/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef SHIELD_BOX_H
#define SHIELD_BOX_H


#include <String.h>
#include <View.h>


class BCheckBox;
class BMenuField;
class BStringView;
class BTextControl;
class ShieldPanel;


/*!	All of LCD Shield in one view: the panel (ShieldPanel) and its
	controls, the pages, the LED mode, the unit of the temperatures, the
	clock's AM/PM, a message. The window holds one; its dragger puts the
	whole box on the Desktop, as a replicant that runs the shield inside
	Tracker. The children are not archived: a box builds them anew from the
	settings in its archive.
*/
class ShieldBox : public BView {
public:
								ShieldBox(const BMessage* settings);
								ShieldBox(BMessage* archive);

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Draw(BRect updateRect);

			// what the app keeps in its settings
			void				GetSettings(BMessage& settings) const;

	static	void				ShowAbout();

private:
			void				_Build(const BMessage* settings);
			void				_SendToPanel(BMessage* message);
			void				_ShowState(BMessage* state);

			bool				fReplicant;
			ShieldPanel*		fPanel;
			BStringView*		fPageLabel;
			BMenuField*			fLedField;
			BCheckBox*			fFahrenheitBox;
			BCheckBox*			fTwelveHourBox;
			BTextControl*		fMessageControl;
			BString				fShownMessage;
};


#endif	// SHIELD_BOX_H
