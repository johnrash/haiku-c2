/* Writes ~/config/settings/Locale settings for English (United States), as
   the Locale preferences would. Images get a copy (configs/locale-settings)
   so the regular image's FirstBootPrompt does not hold back the desktop. */
#include <FormattingConventions.h>
#include <Message.h>
#include <MutableLocaleRoster.h>

using BPrivate::MutableLocaleRoster;

int
main()
{
	BMessage languages;
	languages.AddString("language", "en");
	MutableLocaleRoster::Default()->SetPreferredLanguages(&languages);
	BFormattingConventions conventions("en_US");
	MutableLocaleRoster::Default()->SetDefaultFormattingConventions(conventions);
	return 0;
}
