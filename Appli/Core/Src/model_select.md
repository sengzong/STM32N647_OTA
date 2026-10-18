# Latest firmware installation page

Only one staged latest firmware is displayed. Installing it asks FSBL to save
the current application as the previous-version backup and boot the replacement.
The independent PC-side receiver also supports requesting rollback without this UI.

The sole ready image is selected automatically. Yes opens a confirmation page;
a second press installs it. Empty entries cannot be selected, and selecting an
entry redraws the button. Incoming transfers suppress installation touches and
return to the refreshed selection page. Installation failures remain visible.

UART logs include LCD dimensions, touch initialization, hit areas, tap coordinates,
and confirmation events so touch failure can be distinguished from reset failure.
