/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef SHIELD_WINDOW_H
#define SHIELD_WINDOW_H


#include <Window.h>


class ShieldBox;


/*!	The window: LCD Shield's box (ShieldBox), the panel and its controls,
	whose dragger puts all of it on the Desktop.
*/
class ShieldWindow : public BWindow {
public:
								ShieldWindow(const BMessage& settings);

	virtual	bool				QuitRequested();

	static	status_t			LoadSettings(BMessage& settings);

private:
			status_t			_SaveSettings();

			ShieldBox*			fBox;
};


#endif	// SHIELD_WINDOW_H
