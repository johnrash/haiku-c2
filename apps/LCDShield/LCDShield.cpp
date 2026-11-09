/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * LCD Shield: a demo of Hardkernel's 16x2 LCD + IO Shield on the
 * ODROID-C2 (milestone M68 (docs/milestones.md)), through meson_gpio's /dev/misc/shield devices.
 * The display shows pages (the clock, the CPU, Blinken: a bar for each
 * core, memory, the network, a Weather Board on I2C B, a message), SW1
 * turns them, SW2 changes what the LEDs show (CPU load, a scanner, set by
 * hand); temperatures in Celsius or Fahrenheit, the clock in 24 hours or
 * with AM/PM. The window mirrors the shield and has the same controls; its
 * dragger puts all of it on the Desktop as a replicant, which goes on
 * running the shield in Tracker.
 */


#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <String.h>

#include "ShieldBox.h"
#include "ShieldDevice.h"
#include "ShieldWindow.h"


const char* kAppSignature = "application/x-vnd.C2-LCDShield";


class ShieldApp : public BApplication {
public:
	ShieldApp()
		:
		BApplication(kAppSignature)
	{
	}

	virtual void AboutRequested()
	{
		ShieldBox::ShowAbout();
	}

	virtual void ReadyToRun()
	{
		ShieldDevice device;
		status_t status = device.Open();
		if (status != B_OK) {
			BString text;
			text.SetToFormat("The LCD + IO Shield's devices can't be opened "
				"(%s).\n\nIs the shield on the header, and is \"lcd_io_shield "
				"true\" in ~/config/settings/kernel/drivers/meson_gpio? It "
				"takes a reboot.", strerror(status));
			BAlert* alert = new BAlert("LCD Shield", text, "Quit", NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go();
			Quit();
			return;
		}
		device.Close();

		BMessage settings;
		ShieldWindow::LoadSettings(settings);
		(new ShieldWindow(settings))->Show();
	}
};


int
main()
{
	ShieldApp app;
	app.Run();
	return 0;
}
