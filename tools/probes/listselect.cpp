/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * listselect: BListView's selection after RemoveItems(). Before the fix,
 * RemoveItems() kept the first and last selected index of the items it
 * removed, so CurrentSelection() named the wrong item (the Remote Control
 * preferences edited the original profile of a duplicate).
 */
#include <stdio.h>

#include <Application.h>
#include <ListView.h>
#include <StringItem.h>


static int sFailures = 0;


class CountingListView : public BListView {
public:
	CountingListView() : BListView("list", B_MULTIPLE_SELECTION_LIST),
		changes(0) {}
	virtual void SelectionChanged() { changes++; }
	int32 changes;
};


static void
fill(BListView& list, int32 count)
{
	while (BListItem* item = list.RemoveItem((int32)0))
		delete item;
	for (int32 i = 0; i < count; i++) {
		char name[16];
		snprintf(name, sizeof(name), "item %d", (int)i);
		list.AddItem(new BStringItem(name));
	}
}


static void
check(const char* what, BListView& list, int32 first, int32 second)
{
	int32 a = list.CurrentSelection(0);
	int32 b = list.CurrentSelection(1);
	bool ok = a == first && b == second;
	// and the items themselves agree
	for (int32 i = 0; i < list.CountItems(); i++) {
		bool selected = list.ItemAt(i)->IsSelected();
		if (selected != (i == first || i == second))
			ok = false;
	}
	printf("%s %s: CurrentSelection() %d, %d (want %d, %d)\n",
		ok ? "ok  " : "FAIL", what, (int)a, (int)b, (int)first, (int)second);
	if (!ok)
		sFailures++;
}


int
main()
{
	setvbuf(stdout, NULL, _IONBF, 0);
	BApplication app("application/x-vnd.haiku-odroid-c2-listselect");
	CountingListView list;

	// the Remote Control case: empty the list, add again, select
	fill(list, 4);
	list.Select(1);
	list.RemoveItems(0, list.CountItems());
	fill(list, 4);
	list.Select(3);
	check("emptied, refilled, Select(3)", list, 3, -1);

	// a selection after the removed range moves up
	fill(list, 6);
	list.Select(4);
	list.Select(5, true);
	list.RemoveItems(1, 2);
	check("4 and 5 selected, 1-2 removed", list, 2, 3);

	// a selected item in the range goes, the one after it moves up
	fill(list, 6);
	list.Select(2);
	list.Select(4, true);
	list.RemoveItems(2, 1);
	check("2 and 4 selected, 2 removed", list, 3, -1);

	// a selection before the range stays
	fill(list, 6);
	list.Select(0);
	list.RemoveItems(3, 2);
	check("0 selected, 3-4 removed", list, 0, -1);

	// everything selected goes
	fill(list, 6);
	list.Select(1, 3);
	list.RemoveItems(0, 6);
	check("1-3 selected, all removed", list, -1, -1);

	// the selection changed only when selected items went
	fill(list, 6);
	list.Select(5);
	list.changes = 0;
	list.RemoveItems(0, 2);
	bool quiet = list.changes == 0;
	list.RemoveItems(3, 1);
	bool told = list.changes == 1;
	printf("%s SelectionChanged(): %s when unselected items went, %s when "
		"the selected one did\n", quiet && told ? "ok  " : "FAIL",
		quiet ? "not called" : "called", told ? "called once" : "not once");
	if (!quiet || !told)
		sFailures++;

	while (BListItem* item = list.RemoveItem((int32)0))
		delete item;
	printf("%d failures\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
