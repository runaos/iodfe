🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻🔻

This is old and bad, use this instead:

https://github.com/JBustos22/oDFe + https://github.com/Jelvan1/cgame_proxymod

🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺🔺

iodfengine is a defrag-oriented engine, a fork of ioquake3 with some dfengine features and some new things.

source: https://github.com/runaos/iodfe

ioq3 - http://ioquake3.org/
dfengine - http://cggdev.org/, http://q3defrag.org/files/dfengine/ (source mirror: https://github.com/cliffrowley/dfengine)

Features from dfengine:

in_mouse 3 - raw input for Windows, an alternative to sometimes buggy SDL input (in_mouse 1)
con_filter[0-9]
/download
/cl_mapAutoDownload
Drakkar's fast loading code
See README-DFEngine for detailed info

Other features:

iodfe_hud_snap_draw 1	- snapping hud, shows zones of possible acceleration (for 8 ms frametime)
	_snap_auto	- auto-shifting angle of the hud for different strafe styles
	_snap_def	- offset with no keys pressed or with scr_hud_snap_auto 0
	_snap_speed	- calculate zones for the stated speed instead
colors and position settings:
	_snap_rgba1
	_snap_rgba2
	_snap_y
	_snap_h

iodfe_hud_pitch	- angle marks, setting it to "-15 70" for example will put two marks at -15 and 70 degrees of pitch 
colors and position settings:
	_pitch_rgba
	_pitch_thickness
	_pitch_width
	_pitch_x

con_timestamp [0-1]	- adds a timestamp at each message in console
con_timedisplay [0-3]	- displays time at input line (1), at right bottom console corner (2) or at both places (3)

con_drawversion		- toggles version at right bottom console corner
con_filter			- toggles filtering with con_filter[0-9] vars
con_completemapnames	- toggles /map and /devmap autocompletion

con_notifylines		- number of lines shown in chat notify display
con_notifykeep		- keeps it not erased after opening console
con_notifyx		- x pos
con_notifyy		- y pos

ctrl+enter in console sends message with /team_say
ctrl+shift+enter sends it with /tell to df_mp_trackplayernum

in_keyboardRepeatDelay [ms]
in_keyboardRepeatInterval [ms]
in_numpadbug [0-1] - fixes non-working numpad on Windows

r_xpos, r_ypos		- game window position
