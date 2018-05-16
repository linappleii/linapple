/*
  Hatari - AlertHooks.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Hooked alerts, to be used instead of SDL alert windows
*/

#ifdef ALERT_HOOKS 
	// Replacement for a regular alert (with just an OK button)
	// Returns TRUE if OK clicked, FALSE otherwise
	int HookedAlertNotice(const char* szMessage);

	// Replacement for a query alert (OK and Cancel buttons)
	// Returns TRUE if OK clicked, FALSE otherwise
	int HookedAlertQuery(const char* szMessage);

	// Runtime switch to activate/deactivate alert hooks
	extern bool useAlertHooks;
#endif
