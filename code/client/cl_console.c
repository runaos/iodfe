/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// console.c

#include "client.h"

#define PCRE_STATIC 1
#include <pcre.h>	// Cgg

int g_console_field_width = 78;


#define	NUM_CON_TIMES 4

#define		CON_TEXTSIZE	0x30000
typedef struct {
	qboolean	initialized;

	short	text[CON_TEXTSIZE];
	int		current;		// line where next message will be printed
	int		x;				// offset in current line for next print
	int		display;		// bottom of console displays this line

	int 	linewidth;		// characters across screen
	int		totallines;		// total lines in console scrollback

	float	xadjust;		// for wide aspect screens

	float	displayFrac;	// aproaches finalFrac at scr_conspeed
	float	finalFrac;		// 0.0 to 1.0 lines of console to display
	float	userFrac;		// 0.0 to 1.0 - for user Configurations. Don't want to mess with finalFrac - marky
	int		vislines;		// in scanlines

	int		times[NUM_CON_TIMES];	// cls.realtime time the line was generated
								// for transparent notify lines
	vec4_t	color;
} console_t;

extern	console_t	con;

console_t	con;

cvar_t		*con_timestamp;
cvar_t		*con_timedisplay;
cvar_t		*con_drawversion;

cvar_t		*con_conspeed;
cvar_t		*con_notifytime;
// Cgg
cvar_t		*con_useshader;
cvar_t		*con_opacity;
cvar_t		*con_rgb;

#define MAX_CON_FILTERS 10
cvar_t *con_filters[MAX_CON_FILTERS];
cvar_t *con_filter;
pcre *con_filters_compiled[MAX_CON_FILTERS];
pcre *con_timestampre;
// !Cgg

#define	DEFAULT_CONSOLE_WIDTH	78

vec4_t	console_color = {1.0, 1.0, 1.0, 1.0};


/*
================
Con_ShowPCREVersion_f (Cgg - pcre support)
================
*/
static void Con_ShowPCREVersion_f(void) {
	Com_Printf("%s\n", pcre_version());
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void) {
	// Can't toggle the console when it's the only thing available
	if ( clc.state == CA_DISCONNECTED && Key_GetCatcher( ) == KEYCATCH_CONSOLE ) {
		return;
	}

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width - (con_timedisplay->integer ? 9 : 0);

	Con_ClearNotify ();
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_CONSOLE );
}

/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f (void) {
	chat_playerNum = -1;
	chat_team = qfalse;
	Field_Clear( &chatField );
	// Cgg
	chatField.widthInChars = cls.glconfig.vidWidth / TINYCHAR_WIDTH -7;	// -7: "say: " + padding
	//chatField.widthInChars = 30;
	// !Cgg

	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f (void) {
	chat_playerNum = -1;
	chat_team = qtrue;
	Field_Clear( &chatField );
	// Cgg
	chatField.widthInChars = cls.glconfig.vidWidth / TINYCHAR_WIDTH -12; // -12: "say_team: " + padding
//	chatField.widthInChars = 25;
	// !Cgg
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_MessageMode3_f
================
*/
void Con_MessageMode3_f (void) {
	chat_playerNum = VM_Call( cgvm, CG_CROSSHAIR_PLAYER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	// Cgg
	chatField.widthInChars = cls.glconfig.vidWidth / TINYCHAR_WIDTH -7;	// -7: "say: " + padding
//	chatField.widthInChars = 30;
	// !Cgg
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_MessageMode4_f
================
*/
void Con_MessageMode4_f (void) {
	chat_playerNum = VM_Call( cgvm, CG_LAST_ATTACKER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	// Cgg
	chatField.widthInChars = cls.glconfig.vidWidth / TINYCHAR_WIDTH -7; // -7: "say: " + padding
//	chatField.widthInChars = 30;
	// !Cgg
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void) {
	int		i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		con.text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';
	}

	Con_Bottom();		// go to end
}

						
/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f (void)
{
	int		l, x, i;
	short	*line;
	fileHandle_t	f;
	char	buffer[1024];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("usage: condump <filename>\n");
		return;
	}

	Com_Printf ("Dumped console text to %s.\n", Cmd_Argv(1) );

	f = FS_FOpenFileWrite( Cmd_Argv( 1 ) );
	if (!f)
	{
		Com_Printf ("ERROR: couldn't open.\n");
		return;
	}

	// skip empty lines
	for (l = con.current - con.totallines + 1 ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for (x=0 ; x<con.linewidth ; x++)
			if ((line[x] & 0xff) != ' ')
				break;
		if (x != con.linewidth)
			break;
	}

	// write the remaining lines
	buffer[con.linewidth] = 0;
	for ( ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for(i=0; i<con.linewidth; i++)
			buffer[i] = line[i] & 0xff;
		for (x=con.linewidth-1 ; x>=0 ; x--)
		{
			if (buffer[x] == ' ')
				buffer[x] = 0;
			else
				break;
		}
		strcat( buffer, "\n" );
		FS_Write(buffer, strlen(buffer), f);
	}

	FS_FCloseFile( f );
}

						
/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;
	
	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		con.times[i] = 0;
	}
}

						

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		i, j, width, oldwidth, oldtotallines, numlines, numchars;
	short	tbuf[CON_TEXTSIZE];

	width = (cls.glconfig.vidWidth / SMALLCHAR_WIDTH) - 2;	// Cgg - was SCREEN_WIDTH

	if (width == con.linewidth)
		return;

	if (width < 1)			// video hasn't been initialized yet
	{
		width = DEFAULT_CONSOLE_WIDTH;
		con.linewidth = width;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		for(i=0; i<CON_TEXTSIZE; i++)

			con.text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';
	}
	else
	{
		oldwidth = con.linewidth;
		con.linewidth = width;
		oldtotallines = con.totallines;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		numlines = oldtotallines;

		if (con.totallines < numlines)
			numlines = con.totallines;

		numchars = oldwidth;
	
		if (con.linewidth < numchars)
			numchars = con.linewidth;

		Com_Memcpy (tbuf, con.text, CON_TEXTSIZE * sizeof(short));
		for(i=0; i<CON_TEXTSIZE; i++)

			con.text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';


		for (i=0 ; i<numlines ; i++)
		{
			for (j=0 ; j<numchars ; j++)
			{
				con.text[(con.totallines - 1 - i) * con.linewidth + j] =
						tbuf[((con.current - i + oldtotallines) %
							  oldtotallines) * oldwidth + j];
			}
		}

		Con_ClearNotify ();
	}

	con.current = con.totallines - 1;
	con.display = con.current;
}

/*
==================
Cmd_CompleteTxtName
==================
*/
void Cmd_CompleteTxtName( char *args, int argNum ) {
	if( argNum == 2 ) {
		Field_CompleteFilename( "", "txt", qfalse, qtrue );
	}
}


/*
================
Con_Init
================
*/
void Con_Init (void) {
	int		i;
	const char *errptr;
	int erroffset;

	con_notifytime = Cvar_Get ("con_notifytime", "3", CVAR_ARCHIVE);
	con_conspeed = Cvar_Get ("scr_conspeed", "3", CVAR_ARCHIVE);
	con_timestamp = Cvar_Get ("con_timestamp", "1", CVAR_ARCHIVE);
	con_timedisplay = Cvar_Get ("con_timedisplay", "3", CVAR_ARCHIVE);
	con_drawversion = Cvar_Get ("con_drawversion", "1", CVAR_ARCHIVE);
		
	// Cgg
	con_useshader = Cvar_Get("con_useshader", "0", CVAR_ARCHIVE);
	con_opacity = Cvar_Get("con_opacity", "0.95", CVAR_ARCHIVE);
	con_rgb = Cvar_Get("con_rgb", ".05 .05 .1", CVAR_ARCHIVE);
	con_filter = Cvar_Get("con_filter", "1", CVAR_ARCHIVE);
	for (i=0; i<MAX_CON_FILTERS; i++) {
		con_filters[i] = Cvar_Get(va("con_filter%i", i), "", CVAR_ARCHIVE);
	}
	// !Cgg

	con_timestampre = pcre_compile("^\\d\\d:\\d\\d:\\d\\d\\s", 0, &errptr, &erroffset, NULL);

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;
	for ( i = 0 ; i < COMMAND_HISTORY ; i++ ) {
		Field_Clear( &historyEditLines[i] );
		historyEditLines[i].widthInChars = g_console_field_width;
	}
	CL_LoadConsoleHistory( );

	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("messagemode", Con_MessageMode_f);
	Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand ("messagemode3", Con_MessageMode3_f);
	Cmd_AddCommand ("messagemode4", Con_MessageMode4_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f);
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );
	Cmd_AddCommand ("pcre_version", Con_ShowPCREVersion_f );	// Cgg
}

/*
================
Con_Shutdown
================
*/
void Con_Shutdown(void)
{
	Cmd_RemoveCommand("toggleconsole");
	Cmd_RemoveCommand("messagemode");
	Cmd_RemoveCommand("messagemode2");
	Cmd_RemoveCommand("messagemode3");
	Cmd_RemoveCommand("messagemode4");
	Cmd_RemoveCommand("clear");
	Cmd_RemoveCommand("condump");
}

/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed (qboolean skipnotify)
{
	int		i;
	char txt[MAXPRINTMSG];
	char *txtt=txt;
	pcre **re;
	int ovector[30];
	char ch;
	qboolean copy;

	// Cgg
	if (con.linewidth < sizeof(txt) && Cvar_VariableIntegerValue("con_filter")) {
		for (i=con.linewidth-1,copy=qfalse; i>=0; i--) {
			ch = con.text[(con.current%con.totallines)*con.linewidth+i] &0xff;
			if (ch != ' ') {
				copy = qtrue;
			}
			txt[i] = (copy) ? ch : 0;
		}

		if (con_timestamp && con_timestamp->integer && pcre_exec(con_timestampre, NULL, txt, strlen(txt), 0, 0, ovector, 30) > 0)
		txtt += 9;
		
		for (re=con_filters_compiled; re-con_filters_compiled<MAX_CON_FILTERS; re++) {
			if (*re && pcre_exec(*re, NULL, txtt, strlen(txtt), 0, 0, ovector, 30) > 0) {
				con.x = 0;
				for(i=0; i<con.linewidth; i++) {
					con.text[(con.current%con.totallines)*con.linewidth+i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';
				}
				return;
			}
		}
	}
	

	// mark time for transparent overlay
	if (con.current >= 0)
	{
    if (skipnotify)
		  con.times[con.current % NUM_CON_TIMES] = 0;
    else
		  con.times[con.current % NUM_CON_TIMES] = cls.realtime;
	}

	con.x = 0;
	if (con.display == con.current)
		con.display++;
	con.current++;
	for(i=0; i<con.linewidth; i++)
		con.text[(con.current%con.totallines)*con.linewidth+i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';
}

/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
#define Q_RAW_ESCAPE 1	// Cgg: see below
void CL_ConsolePrint( char *txt ) {
	int		y, l;
	unsigned char	c;
	unsigned short	color;
	qboolean skipnotify = qfalse;		// NERVE - SMF
	int prev;							// NERVE - SMF
	char raw;	// Cgg: Q_RAW_ESCAPE char stops color sequences from being interpreted.
	
	if (con.x==0 && con_timestamp && con_timestamp->integer) {
		char txtt[MAXPRINTMSG];
		qtime_t	now;
		Com_RealTime( &now );
		Com_sprintf(txtt,sizeof(txtt),"^9%02d:%02d:%02d ^7%s",now.tm_hour,now.tm_min,now.tm_sec,txt);
		strcpy(txt,txtt);
	}

	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( txt, "[skipnotify]", 12 ) ) {
		skipnotify = qtrue;
		txt += 12;
	}
	
	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}

	if (!con.initialized) {
		con.color[0] = 
		con.color[1] = 
		con.color[2] =
		con.color[3] = 1.0f;
		con.linewidth = -1;
		Con_CheckResize ();
		con.initialized = qtrue;
	}

	color = ColorIndex(COLOR_WHITE);

	raw = 0;
	while ( (c = *txt) != 0 ) {
		if (!raw) {
			if ( Q_IsColorString( txt ) ) {
				color = ColorIndex( *(txt+1) );
				txt += 2;
				continue;
			}
			// Cgg - repeated escape codes (^^) were broken
			if (*txt == Q_COLOR_ESCAPE && *(txt+1) == Q_COLOR_ESCAPE) {
				txt++;
			}
			// !Cgg
		}

		// count word length
		for (l=0 ; l< con.linewidth ; l++) {
			if ( txt[l] <= ' ') {
				break;
			}

		}

		// word wrap
		if (l != con.linewidth && (con.x + l >= con.linewidth) ) {
			Con_Linefeed(skipnotify);

		}

		txt++;

		switch (c)
		{
		case Q_RAW_ESCAPE:
			raw = raw^1;
			break;
		case '\n':
			raw = 0;
			Con_Linefeed (skipnotify);
			break;
		case '\r':
			con.x = 0;
			break;
		default:	// display character and advance
			y = con.current % con.totallines;
			con.text[y*con.linewidth+con.x] = (color << 8) | c;
			con.x++;
			if (con.x >= con.linewidth) {
				Con_Linefeed(skipnotify);
				con.x = 0;
			}
			break;
		}
	}


	// mark time for transparent overlay
	if (con.current >= 0) {
		// NERVE - SMF
		if ( skipnotify ) {
			prev = con.current % NUM_CON_TIMES - 1;
			if ( prev < 0 )
				prev = NUM_CON_TIMES - 1;
			con.times[prev] = 0;
		}
		else
		// -NERVE - SMF
			con.times[con.current % NUM_CON_TIMES] = cls.realtime;
	}
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
void Con_DrawInput (void) {
	int		y;
	int		x = 0;

	if ( clc.state != CA_DISCONNECTED && !(Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = con.vislines - ( SMALLCHAR_HEIGHT * 2 );
	
	if (con_timedisplay->integer & 1) {
		char ts[9];
		int i;
		
		qtime_t	now;
		Com_RealTime( &now );
		Com_sprintf(ts,sizeof(ts),"%02d:%02d:%02d",now.tm_hour,now.tm_min,now.tm_sec);
		
		re.SetColor( g_color_table[ColorIndex(COLOR_ORANGE)] );
		for (i = 0 ; i<8 ; i++) {
			SCR_DrawSmallChar( con.xadjust + (i+1) * SMALLCHAR_WIDTH, y, ts[i] );
		}
		x = 9;
	}

	re.SetColor( con.color );
	SCR_DrawSmallChar( con.xadjust + (x+1) * SMALLCHAR_WIDTH, y, ']' );

	Field_Draw( &g_consoleField, con.xadjust + (x+2) * SMALLCHAR_WIDTH, y,
		SCREEN_WIDTH - 3 * SMALLCHAR_WIDTH, qtrue, qtrue );
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	int		x, v;
	short	*text;
	int		i;
	int		time;
	int		skip;
	int		currentColor;

	currentColor = 7;
	re.SetColor( g_color_table[currentColor] );

	v = 0;
	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if (time > con_notifytime->value*1000)
			continue;
		text = con.text + (i % con.totallines)*con.linewidth;

		if (cl.snap.ps.pm_type != PM_INTERMISSION && Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
			continue;
		}

		for (x = con_timestamp->integer ? 9 : 0; x < con.linewidth ; x++) {
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}
			if ( ( ((text[x]>>8)&15)%10 ) != currentColor ) {
				currentColor = ((text[x]>>8)&15)%10;
				re.SetColor( g_color_table[currentColor] );
			}
			SCR_DrawSmallChar( cl_conXOffset->integer + con.xadjust + (x+1-(con_timestamp->integer ? 9 : 0))*SMALLCHAR_WIDTH, v, text[x] & 0xff );
		}

		v += SMALLCHAR_HEIGHT;
	}

	re.SetColor( NULL );

	if (Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
		return;
	}

	// draw the chat line
	if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE )
	{
		if (chat_team)
		{
			SCR_DrawSmallStringExt(8, v, "say_team:", colorWhite, qfalse, qfalse);
			skip = 11;
		}
		else
		{
			SCR_DrawSmallStringExt(8, v, "say:", colorWhite, qfalse, qfalse);
			skip = 6;
		}

		Field_Draw(&chatField,  skip * TINYCHAR_WIDTH, v,
			SCREEN_WIDTH - ( skip + 1 ) * TINYCHAR_WIDTH, qtrue, qtrue);

		v += BIGCHAR_HEIGHT;
	}

}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( float frac ) {
	int				i, x, y;
	int				rows;
	short			*text;
	int				row;
	int				lines;
//	qhandle_t		conShader;
	int				currentColor;
//	vec4_t			color;

	lines = cls.glconfig.vidHeight * frac;
	if (lines <= 0)
		return;

	if (lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	// on wide screens, we will center the text
	con.xadjust = 0;
	SCR_AdjustFrom640( &con.xadjust, NULL, NULL, NULL );

	// draw the background
	y = frac * SCREEN_HEIGHT - 1;	// Cgg - was 2
	if ( y < 1 ) {
		y = 0;
	}
	else {
		// Cgg
		if (con_useshader->integer) {
			SCR_DrawPic( 0, 0, SCREEN_WIDTH, y, cls.consoleShader );
		} else {
			vec4_t color;
			char *c = con_rgb->string;
			color[0] = atof(COM_Parse(&c));
			color[1] = atof(COM_Parse(&c));
			color[2] = atof(COM_Parse(&c));
			color[3] = con_opacity->value;
			SCR_FillRect( 0, 0, SCREEN_WIDTH, y, color );
		}
		// !Cgg
	}

	SCR_FillRect( 0, y, SCREEN_WIDTH, 1, colorOrange );	// Cgg - orange & 1px instead of 2

	// draw the version number

	re.SetColor( g_color_table[ColorIndex(COLOR_ORANGE)] );	// Cgg - orange

	if (con_drawversion->integer) {
		i = strlen( Q3_VERSION );

		for (x=0 ; x<i ; x++) {

			SCR_DrawSmallChar( cls.glconfig.vidWidth - ( i - x ) * SMALLCHAR_WIDTH, 

				(lines-(SMALLCHAR_HEIGHT+SMALLCHAR_HEIGHT/2)), Q3_VERSION[x] );

		}
	}

	if (con_timedisplay->integer & 2) {
		char ts[30];
		qtime_t	now;

		Com_RealTime( &now );
		Com_sprintf(ts,sizeof(ts),"%02d:%02d:%02d %04d-%02d-%02d",now.tm_hour,now.tm_min,now.tm_sec,1900 + now.tm_year,1 + now.tm_mon,now.tm_mday);
		i = strlen( ts );

		for (x = 0 ; x<i ; x++) {
			SCR_DrawSmallChar( cls.glconfig.vidWidth - ( i - x ) * SMALLCHAR_WIDTH, lines - (SMALLCHAR_HEIGHT * (con_drawversion->integer ? 2 : 1) + SMALLCHAR_HEIGHT/2), ts[x]);
		}
	}


	// draw the text
	con.vislines = lines;
	rows = (lines-SMALLCHAR_WIDTH)/SMALLCHAR_WIDTH;		// rows of text to draw

	y = lines - (SMALLCHAR_HEIGHT*3);

	// draw from the bottom up
	if (con.display != con.current)
	{
	// draw arrows to show the buffer is backscrolled
		re.SetColor( g_color_table[ColorIndex(COLOR_ORANGE)] );	// Cgg - orange
		for (x=0 ; x<con.linewidth ; x+=4)
			SCR_DrawSmallChar( con.xadjust + (x+1)*SMALLCHAR_WIDTH, y, '^' );
		y -= SMALLCHAR_HEIGHT;
		rows--;
	}
	
	row = con.display;

	if ( con.x == 0 ) {
		row--;
	}

	currentColor = 7;
	re.SetColor( g_color_table[currentColor] );

	for (i=0 ; i<rows ; i++, y -= SMALLCHAR_HEIGHT, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines) {
			// past scrollback wrap point
			continue;	
		}

		text = con.text + (row % con.totallines)*con.linewidth;

		for (x=0 ; x<con.linewidth ; x++) {
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}

			if ( ( ((text[x]>>8)&15)%10 ) != currentColor ) {
				currentColor = ((text[x]>>8)&15)%10;
				re.SetColor( g_color_table[currentColor] );
			}
			SCR_DrawSmallChar(  con.xadjust + (x+1)*SMALLCHAR_WIDTH, y, text[x] & 0xff );
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();

	re.SetColor( NULL );
}



/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	// check for console width changes from a vid mode change
	Con_CheckResize ();

	// if disconnected, render console full screen
	if ( clc.state == CA_DISCONNECTED ) {
		if ( !( Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( clc.state == CA_ACTIVE ) {
			Con_DrawNotify ();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole (void) {
	// Cgg - check for updated con_filters
	cvar_t **cvar;
	pcre **re;
	const char *errptr;
	int erroffset;
	for (cvar=con_filters; cvar-con_filters<MAX_CON_FILTERS; cvar++) {
		if (!*cvar || !(*cvar)->modified) {
			continue;
		}
		re = con_filters_compiled+(cvar-con_filters);
		(*cvar)->modified = qfalse;
		if (!strlen((*cvar)->string)) {
			*re = NULL;
			continue;
		}
		*re = pcre_compile((*cvar)->string, 0, &errptr, &erroffset, NULL);
		if (!*re) {
			Com_Printf("Failed to compile %c%s\n", Q_RAW_ESCAPE, (*cvar)->string);
			Com_Printf(va("%c%%%ic %%s\n", Q_RAW_ESCAPE, erroffset+19), '^', errptr);
			Cvar_Set((*cvar)->name, "");
			(*cvar)->modified = qfalse;
		}
	}
	// !Cgg

	// decide on the destination height of the console
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		con.finalFrac = con.userFrac;	// marky
	else
		con.finalFrac = 0;				// none visible
	
	// scroll towards the destination height
	if (con.finalFrac < con.displayFrac)
	{
		con.displayFrac -= con_conspeed->value*cls.realFrametime*0.001;
		if (con.finalFrac > con.displayFrac)
			con.displayFrac = con.finalFrac;

	}
	else if (con.finalFrac > con.displayFrac)
	{
		con.displayFrac += con_conspeed->value*cls.realFrametime*0.001;
		if (con.finalFrac < con.displayFrac)
			con.displayFrac = con.finalFrac;
	}

}

// marky
/*
==================
Con_SetFrac
==================
*/
void Con_SetFrac(const float conFrac)
{
	// clamp the cvar value
	if (conFrac < .1f) {	// don't let the console be hidden
		con.userFrac = .1f;
	} else if (conFrac > 1.0f) {
		con.userFrac = 1.0f;
	} else {
		con.userFrac = conFrac;
	}
}
// !marky


void Con_PageUp( void ) {
	con.display -= 2;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_PageDown( void ) {
	con.display += 2;
	if (con.display > con.current) {
		con.display = con.current;
	}
}

void Con_Top( void ) {
	con.display = con.totallines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_Bottom( void ) {
	con.display = con.current;
}


void Con_Close( void ) {
	if ( !com_cl_running->integer ) {
		return;
	}
	Field_Clear( &g_consoleField );
	Con_ClearNotify ();
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CONSOLE );
	con.finalFrac = 0;				// none visible
	con.displayFrac = 0;
}
