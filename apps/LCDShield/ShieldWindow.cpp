/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */


#include "ShieldWindow.h"

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include "ShieldBox.h"


static const char* kSettingsName = "LCDShield";


ShieldWindow::ShieldWindow(const BMessage& settings)
	:
	BWindow(BRect(100, 100, 200, 200), "LCD Shield", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS
			| B_QUIT_ON_WINDOW_CLOSE)
{
	fBox = new ShieldBox(&settings);
	AddChild(fBox);
	ResizeTo(fBox->Bounds().Width(), fBox->Bounds().Height());
	CenterOnScreen();
}


bool
ShieldWindow::QuitRequested()
{
	_SaveSettings();
	return true;
}


/*static*/ status_t
ShieldWindow::LoadSettings(BMessage& settings)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(kSettingsName);
	BFile file(path.Path(), B_READ_ONLY);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return settings.Unflatten(&file);
}


status_t
ShieldWindow::_SaveSettings()
{
	BMessage settings;
	fBox->GetSettings(settings);
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(kSettingsName);
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return settings.Flatten(&file);
}
