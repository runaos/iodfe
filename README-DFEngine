This file describes DFEngine's commands and settings.


                                  DESCRIPTION

DFEngine is a Quake3 engine based on the original Id Software code that is
designed with the Defrag mod in mind. It features a number of modifications that
help cope with the specific needs playing Defrag entails.



                                 HTTP DOWNLOAD

DFEngine has built-in support for HTTP downloads based on libcurl. It adds a
/download command that allows requesting map packs from a web server. Such a
feature has been made possible thanks to the efforts involved in the
http://q3a.ath.cx/ online repository which covers an extensive number of maps.
An alternative repository can be set up with the dl_source cvar.

  download [mapname] (&)
      download command - appending & to the command makes the download run in
      the background

  cl_mapAutodownload [0,1]
      whether to download missing maps automatically when the client connects to
      a server

  curl_version
      version string of the curl library built into the engine

  dl_showmotd [0,1]
      whether to show the download server motd in the console

  dl_showprogress [0,2]
      whether to show the download progress in the console (1 writes new lines
      continuously, 2 writes in place)

  dl_source [URI]
      URI from which to request maps - token %m inside the string is replaced
      with the requested mapname before issuing the HTTP request

  dl_usebaseq3 [0,1]
      whether to issue the downloaded packs in baseq3 (1) or in the current
      gamedir (0)

  dl_verbose [0,2]
      verbose modes for debugging purposes



                        ENHANCED GAME WINDOW MANAGEMENT

DFEngine provides fast window switching, game minimization and fullscreen
toggling. In addition, it allows disabling the desktop shortcuts (like alt-tab)
depending on the context.

  windowMode [mode]
      set the window state - can be either: restart, minimized, windowed,
      fullscreen, swapFullscreen, swapMinimized.

  in_keyboardShortcuts [0,3]
      enable Windows desktop shortcuts (Windows only) - 0: off; 1: on; 2: only
      when console is down; 3: only when game is windowed

The Windows special keys are bindable in DFEngine as WIN and MENU.

DFengine's alt-enter shortcut actually calls the windowMode command, thus
providing fast fullscreen toggling.



                                CONSOLE FEATURES

You may define up to 20 "console filters" (con_filter0 to con_filter19) that
each take a regular expression (Perl compatible). Text matching any of these
filters will be prevented from showing in the console.

Example:

  con_filter0 "defrag" prevents any text containing "defrag" from showing up.
  con_filter0 "^defrag" removes any text beginning with "defrag".

New settings are:

  con_filter[0,19] [regexp]
      console filters

  con_height [0.0,1.0]
      default console height as a fractional value

  con_opacity [0.0,1.0]
      background opacity

  con_rgb [0.0,1.0] [0.0,1.0] [0.0,1.0]
      background color expressed as RGB components (floating point values)

  con_useshader [0,1]
      whether to use the console shader as it was in Id Sotfware's engine
      instead of the solid color background

  pcre_version
      version string of the PCRE library built into the engine

Holding the Alt key while toggling the console makes it open fullscreen. Holding
Shift gives it reduced height.



                           WINDOWS RAW INPUT SUPPORT

Microsoft introduced a new input interface with Windows XP that allows raw
access to the input devices. It provides an alternative to DirectInput.

  in_mouse [-1,3]
     enable the mouse with mode: -1: Win32; 1: DirectInput; 3: Raw Input
 
The Win32 method relies on preprocessed window messages that depend on the
screen resolution and are largely inaccurate.



                             MISC. SERVER SETTINGS

Client downloads and sv_pure are disabled by default at the server level
because these features demand the server reference (and communicate) every pack
file it loads.  This doesn't scale too well with the large number of map packs
a defrag server typically has.

  sv_noReferencedPaks [0,1]
      whether to allow the server to reference the file packs it has loaded -
      and allow both client downloads and sv_pure.



                             MISC. CLIENT SETTINGS

  com_sleepfps [rate]
      framerate limit when the game window doesn't have focus (widely inaccurate,
      actual parameter value is only indicative)
  
  ch_recordMessage [0,1]
      whether to show the "Recording demo" screen message

