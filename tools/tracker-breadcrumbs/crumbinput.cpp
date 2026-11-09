/*
 * Test helper: clicks the breadcrumb buttons of a Tracker window and types
 * into it with events sent the way the input server does, so they go
 * through the window's normal dispatching (MouseDown()/MouseUp() of the
 * view, shortcuts, the focus view). hey cannot do that: its messages go to
 * the scripting code of a view.
 *
 *   crumbinput <window title> click <segment>   click path segment n
 *   crumbinput <window title> press <segment>   button down only
 *   crumbinput <window title> release <segment> button up only
 *   crumbinput <window title> move <segment>    pointer over segment n
 *   crumbinput <window title> idle <segment>    pointer rests there (tool tip)
 *   crumbinput <window title> where <segment>   print the point used
 *   crumbinput <window title> key <hex char> [<hex modifiers>]
 *   crumbinput <window title> button <name>     click a Navigator button
 *
 * Segment 0 is "/", segment 1 the first folder, and so on. The point is
 * computed from the path in the Location field and the plain font, in the
 * middle of the component's text.
 *
 * Build: g++ -I/boot/system/develop/headers/private/app \
 *            -o crumbinput crumbinput.cpp -lbe
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Rect.h>
#include <String.h>
#include <View.h>

#include <MessengerPrivate.h>
#include <TokenSpace.h>


static BMessenger sTracker("application/x-vnd.Be-TRAK");
static const char* sWindowTitle;
static BMessenger sWindow;


static status_t
GetProperty(const char* property, const char* view, const char* parent,
	BMessage& reply)
{
	BMessage request(B_GET_PROPERTY);
	request.AddSpecifier(property);
	if (view != NULL)
		request.AddSpecifier("View", view);
	if (parent != NULL)
		request.AddSpecifier("View", parent);
	request.AddSpecifier("Window", sWindowTitle);
	status_t status = sTracker.SendMessage(&request, &reply, 2000000,
		2000000);
	if (status == B_OK && reply.what != B_REPLY)
		status = B_ERROR;
	return status;
}


static BRect
GetFrame(const char* view, const char* parent = NULL)
{
	BMessage reply;
	BRect frame;
	if (GetProperty("Frame", view, parent, reply) != B_OK
		|| reply.FindRect("result", &frame) != B_OK) {
		fprintf(stderr, "crumbinput: no frame for %s\n",
			view != NULL ? view : "window");
		exit(1);
	}
	return frame;
}


static void
Send(BMessage& message)
{
	message.AddInt64("when", system_time());
	if (sWindow.SendMessage(&message) != B_OK) {
		fprintf(stderr, "crumbinput: could not send\n");
		exit(1);
	}
	// give the window time to handle it, like real input
	snooze(30000);
}


static void
Mouse(uint32 what, BPoint screenWhere, int32 viewToken, int32 buttons)
{
	BMessage message(what);
	message.AddPoint("screen_where", screenWhere);
	message.AddInt32("buttons", buttons);
	message.AddInt32("modifiers", 0);
	message.AddInt32("_view_token", viewToken);
	if (what == B_MOUSE_DOWN)
		message.AddInt32("clicks", 1);
	if (what == B_MOUSE_MOVED)
		message.AddInt32("be:transit", B_INSIDE_VIEW);
	Send(message);
}


static void
Key(char character, int32 modifiers)
{
	char text[2] = { character, '\0' };
	int32 states[4] = { 0 };
	for (uint32 what = B_KEY_DOWN; ; what = B_KEY_UP) {
		BMessage message(what);
		message.AddInt32("modifiers", modifiers);
		message.AddInt32("key", 0);
		message.AddInt32("raw_char", (uint8)character);
		message.AddInt8("byte", character);
		message.AddString("bytes", text);
		message.AddData("states", B_UINT8_TYPE, states, 16);
		Send(message);
		if (what == B_KEY_UP)
			break;
	}
}


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s <window title> click|press|release|move|"
			"idle|where <segment> | key <hex char> [<hex modifiers>]\n", argv[0]);
		return 1;
	}

	BApplication app("application/x-vnd.test-crumbinput");
	sWindowTitle = argv[1];
	const char* command = argv[2];

	BMessage reply;
	if (GetProperty("Messenger", NULL, NULL, reply) != B_OK
		|| reply.FindMessenger("result", &sWindow) != B_OK) {
		fprintf(stderr, "crumbinput: no window \"%s\"\n", sWindowTitle);
		return 1;
	}
	BMessenger::Private(sWindow).SetTo(BMessenger::Private(sWindow).Team(),
		BMessenger::Private(sWindow).Port(), B_PREFERRED_TOKEN);

	if (strcmp(command, "key") == 0) {
		Key((char)strtol(argv[3], NULL, 16),
			argc > 4 ? strtol(argv[4], NULL, 16) : 0);
		return 0;
	}

	if (strcmp(command, "button") == 0) {
		// a named Navigator button ("back", "forward", "up", "refresh"):
		// the navigator is at the pose container's top left
		BRect window = GetFrame(NULL);
		BMessage containerReply;
		BRect container;
		BMessage request(B_GET_PROPERTY);
		request.AddSpecifier("Frame");
		request.AddSpecifier("View", (int32)1);
		request.AddSpecifier("Window", sWindowTitle);
		if (sTracker.SendMessage(&request, &containerReply) != B_OK
			|| containerReply.FindRect("result", &container) != B_OK) {
			fprintf(stderr, "crumbinput: no pose container\n");
			return 1;
		}
		BRect button = GetFrame(argv[3]);
		BMessage buttonReply;
		BMessenger buttonMessenger;
		if (GetProperty("Messenger", argv[3], NULL, buttonReply) != B_OK
			|| buttonReply.FindMessenger("result", &buttonMessenger) != B_OK) {
			fprintf(stderr, "crumbinput: no button %s\n", argv[3]);
			return 1;
		}
		BPoint where = window.LeftTop() + container.LeftTop()
			+ BPoint((button.left + button.right) / 2,
				(button.top + button.bottom) / 2);
		int32 token = BMessenger::Private(buttonMessenger).Token();
		Mouse(B_MOUSE_MOVED, where, token, 0);
		Mouse(B_MOUSE_DOWN, where, token, B_PRIMARY_MOUSE_BUTTON);
		Mouse(B_MOUSE_UP, where, token, 0);
		return 0;
	}

	// The path and where its text starts on screen: the window's content
	// frame, the pose container (View 1) and the navigator at its top left,
	// then the text view inside the Location field.
	BString path;
	if (GetProperty("Value", "Location", NULL, reply) != B_OK
		|| reply.FindString("result", &path) != B_OK) {
		fprintf(stderr, "crumbinput: no Location field\n");
		return 1;
	}
	BRect window = GetFrame(NULL);
	BMessage containerReply;
	BRect container;
	BMessage request(B_GET_PROPERTY);
	request.AddSpecifier("Frame");
	request.AddSpecifier("View", (int32)1);
	request.AddSpecifier("Window", sWindowTitle);
	if (sTracker.SendMessage(&request, &containerReply) != B_OK
		|| containerReply.FindRect("result", &container) != B_OK) {
		fprintf(stderr, "crumbinput: no pose container\n");
		return 1;
	}
	BRect bar = GetFrame("breadcrumbs");
	BRect location = GetFrame("Location");
	BRect textView;
	BMessage textReply;
	BMessage textRequest(B_GET_PROPERTY);
	textRequest.AddSpecifier("Frame");
	textRequest.AddSpecifier("View", (int32)0);
	textRequest.AddSpecifier("View", "Location");
	textRequest.AddSpecifier("Window", sWindowTitle);
	if (sTracker.SendMessage(&textRequest, &textReply) != B_OK
		|| textReply.FindRect("result", &textView) != B_OK) {
		textView.Set(2, 2, 0, 0);
	}

	BPoint barOrigin = window.LeftTop() + container.LeftTop()
		+ bar.LeftTop();
	float textLeft = location.left - bar.left + textView.left + 3;
		// 3: the text view's inset, roughly

	int32 segment = atoi(argv[3]);
	int32 start = 0;
	for (int32 i = 0; i < segment; i++) {
		int32 slash = path.FindFirst('/', i == 0 ? 0 : start);
		if (slash < 0) {
			fprintf(stderr, "crumbinput: no segment %d in %s\n", (int)segment,
				path.String());
			return 1;
		}
		start = slash + 1;
	}
	int32 end = path.FindFirst('/', segment == 0 ? 0 : start) + 1;
	if (end <= 0)
		end = path.Length();

	BFont font(be_plain_font);
	float x = textLeft + font.StringWidth(path.String(), start)
		+ font.StringWidth(path.String() + start, end - start) / 2;
	BPoint where = barOrigin + BPoint(x, bar.Height() / 2);

	BMessage barReply;
	BMessenger barMessenger;
	if (GetProperty("Messenger", "breadcrumbs", NULL, barReply) != B_OK
		|| barReply.FindMessenger("result", &barMessenger) != B_OK) {
		fprintf(stderr, "crumbinput: no breadcrumbs view\n");
		return 1;
	}
	int32 token = BMessenger::Private(barMessenger).Token();

	if (strcmp(command, "where") == 0) {
		printf("%s segment %d at %g,%g (bar at %g,%g)\n", path.String(),
			(int)segment, where.x, where.y, barOrigin.x, barOrigin.y);
	} else if (strcmp(command, "move") == 0) {
		Mouse(B_MOUSE_MOVED, where, token, 0);
	} else if (strcmp(command, "idle") == 0) {
		// the window does not add view coordinates to idle messages
		Mouse(B_MOUSE_MOVED, where, token, 0);
		BMessage idle(B_MOUSE_IDLE);
		idle.AddPoint("screen_where", where);
		idle.AddPoint("be:view_where", where - barOrigin);
		idle.AddInt32("_view_token", token);
		Send(idle);
	} else if (strcmp(command, "press") == 0) {
		Mouse(B_MOUSE_MOVED, where, token, 0);
		Mouse(B_MOUSE_DOWN, where, token, B_PRIMARY_MOUSE_BUTTON);
	} else if (strcmp(command, "release") == 0) {
		Mouse(B_MOUSE_UP, where, token, 0);
	} else if (strcmp(command, "click") == 0) {
		Mouse(B_MOUSE_MOVED, where, token, 0);
		Mouse(B_MOUSE_DOWN, where, token, B_PRIMARY_MOUSE_BUTTON);
		Mouse(B_MOUSE_UP, where, token, 0);
	} else {
		fprintf(stderr, "crumbinput: unknown command %s\n", command);
		return 1;
	}

	return 0;
}
