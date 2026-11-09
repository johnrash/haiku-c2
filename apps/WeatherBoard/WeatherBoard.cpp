/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Weather Board: the readings of Hardkernel's Weather Board 2 on the
 * ODROID-C2's header (milestone M66 (docs/milestones.md)), live, in a window or, through the
 * panel's dragger, as a replicant on the Desktop.
 */


#include <Application.h>
#include <Window.h>

#include "WeatherView.h"


const char* kAppSignature = "application/x-vnd.C2-WeatherBoard";


class WeatherWindow : public BWindow {
public:
	WeatherWindow()
		:
		BWindow(BRect(100, 100, 200, 200), "Weather Board",
			B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
				| B_QUIT_ON_WINDOW_CLOSE)
	{
		WeatherView* view = new WeatherView();
		ResizeTo(view->Bounds().Width(), view->Bounds().Height());
		AddChild(view);
		CenterOnScreen();
	}
};


class WeatherApp : public BApplication {
public:
	WeatherApp()
		:
		BApplication(kAppSignature)
	{
	}

	virtual void ReadyToRun()
	{
		(new WeatherWindow())->Show();
	}
};


int
main()
{
	WeatherApp app;
	app.Run();
	return 0;
}
