/*
===========================================================================
Copyright (C) 2009 Cyril Gantin

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
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================

Integration of libcurl for requesting maps from an online repository.

Usage:
  \download <mapname>     - blocking download ( hold ESC to abort )
  \download <mapname> &   - background download
  \download -             - abort current background download
  \download               - show help or background download progress

Cvar dl_source defines the url from which to query maps, eg: http://someserver/somescript.php?q=%m
The %m token is replaced with the actual map name in the query.

The server MUST return an appropriate content-type. Accepted content-type values are either application/zip
or application/octet-stream. Other content-type values will be treated as errors or queries that didn't
yield results.

The server MAY return a content-disposition header with the actual name of the pk3 file. In the absence
of a content-disposition header, the client will write the pack to a default <mapname>.pk3 location. The
filename MUST have a pk3 extension. The filename MUST NOT have directory path information - slashes (/),
backslashes (\) and colons (:) are not accepted. Finally the filename MUST consist of us-ascii characters
only (chars 32 to 126, included). A filename that doesn't comply to these specifications will raise an
error and abort the transfer.

The server MAY redirect the query to another url. Multiple redirections are permitted - limit depends on
libcurl's default settings. The end query MUST return a "200 OK" http status code.

It is desirable that the server returns a content-length header with the size of the pk3 file.

The server MAY return a custom x-dfengine-motd header. Its value is a string that MUST NOT exceed 127
chars. The x-dfengine-motd string will be displayed after the download is complete. This is the place
where you take credits for setting up a server. :)

Downloaded files are written to the current gamedir of the home directory - eg. C:\quake3\mymod\ in
windows; ~/.q3a/mymod/ in linux. Name collision with an existing pk3 file will result in a failure and
be left to the user to sort out.

*/

#include "client.h"

static cvar_t *dl_verbose;	// 1: show http headers; 2: http headers +curl debug info
static cvar_t *dl_showprogress;	// 0: do not show; 1: show console progress; 2: show progress in one line
static cvar_t *dl_showmotd;	// show server message
static cvar_t *dl_source;	// url to query maps from; %m token will be replaced by mapname
static cvar_t *dl_usebaseq3;	// whether to download pk3 files in baseq3 (default is off)

static qboolean curl_initialized;
static char useragent[256];
static CURL *curl = NULL;
static CURLM *curlm = NULL;
static fileHandle_t f = 0;
static char path[MAX_OSPATH];
static char dl_error[1024];	// if set, will be used in place of libcurl's error message.
static char motd[128];


static size_t Curl_WriteCallback_f(void *ptr, size_t size, size_t nmemb, void *stream) {
	if (!f) {
		char dir[MAX_OSPATH];
		char dirt[MAX_OSPATH];
		char *c;
		// make sure Content-Type is either "application/octet-stream" or "application/zip".
		if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &c) != CURLE_OK
				|| !c
				|| (Q_stricmp(c, "application/octet-stream")
					&& Q_stricmp(c, "application/zip"))) {
			Q_strncpyz(dl_error, "No pk3 returned - requested map is probably unknown.", sizeof(dl_error));
			return 0;
		}
		// make sure the path doesn't have directory information.
		for (c=path; *c; c++) {
			if (*c == '\\' || *c == '/' || *c == ':') {
				Com_sprintf(dl_error, sizeof(dl_error), "Destination filename \"%s\" is not valid.", path);
				return 0;
			}
		}

		// make sure the file has an appropriate extension.
		c = path +strlen(path) -4;
		if (c <= path || strcmp(c, ".pk3")) {
			Com_sprintf(dl_error, sizeof(dl_error), "Returned file \"%s\" has wrong extension.", path);
			return 0;
		}

		// make out the directory in which to place the file
		Q_strncpyz(dir, (dl_usebaseq3->integer)?"baseq3":FS_GetCurrentGameDir(), sizeof(dir));
		if (strlen(path) +strlen(dir) +1 >= sizeof(path)) {
			Com_sprintf(dl_error, sizeof(dl_error), "Returned filename is too large.");
			return 0;
		}

		Com_sprintf(dirt, sizeof(dirt), "%s/%s", dir, path);
		strcpy(path,dirt);

		// in case of a name collision, just fail - leave it to the user to sort out.
		if (FS_FileExists(path)) {
			Com_sprintf(dl_error, sizeof(dl_error), "Failed to download \"%s\", a pk3 by that name exists locally.", path);
			return 0;
		}

		// change the extension to .tmp - it will be changed back once the download is complete.
		c = path +strlen(path) -4;
		strcpy(c, ".tmp"); 

		// FS should write the file in the appropriate gamedir and catch unsanitary paths.
		f = FS_SV_FOpenFileWrite(path);
		if (!f) {
			Com_sprintf(dl_error, sizeof(dl_error), "Failed to open \"%s\" for writing.\n", path);
			return 0;
		}
		Com_Printf("Writing to: %s\n", path);
	}
	return FS_Write(ptr, size*nmemb, f);
}

static size_t Curl_HeaderCallback_f(void *ptr, size_t size, size_t nmemb, void *stream) {
	char buf[1024];
	char *c;

	// make a copy and remove the trailing crlf chars.
	if (size*nmemb >= sizeof(buf)) {
		Q_strncpyz(dl_error, "Curl_HeaderCallback_f() overflow.", sizeof(dl_error));
		return (size_t)-1;
	}
	Q_strncpyz(buf, ptr, size*nmemb+1);
	c = buf +strlen(buf)-1;
	while (c>buf && (*c == '\r' || *c == '\n')) {
		*(c--) = 0;
	}

	// make sure it's not empty.
	if (c <= buf) {
		return size*nmemb;
	}

	// verbose output
	if (dl_verbose->integer > 0) {
		Com_Printf("< %s\n", buf);
	}
	/**
	 * Check whether this is a content-disposition header.
	 * Apparently RFC2183 has precise rules for the presentation of the filename attribute.
	 * No one seems to respect those, though.
	 * Accepted presentations:
	 *	filename="somefile.pk3"
	 *	filename='somefile.pk3'
	 *	filename=somefile.pk3
	 * Quoted strings won't support escaping (eg. "some\"file.pk3").
	 * Malformed quoted strings that miss the trailing quotation mark will pass.
	 * Only us-ascii chars are accepted.
	 * The actual filename will be validated later, when the transfer is started.
	 */
	if (!strncasecmp(buf, "content-disposition:", 20)) {
		const char *c = strstr(buf, "filename=") +9;
		if (c != (char*)9) {
			const char *e;
			char token=0;
			if (*c == '"' || *c == '\'') {
				token = *c++;
			}
			for (e=c; *e && *e != token; e++) {
				if (*e<32 || *e > 126) {
					Q_strncpyz(dl_error, "Server returned an invalid filename.", sizeof(dl_error));
					return (size_t)-1;
				}
			}
			if (e == c || e-c >= sizeof(path)) {
				Q_strncpyz(dl_error, "Server returned an invalid filename.", sizeof(dl_error));
				return (size_t)-1;
			}
			Q_strncpyz(path, c, e-c+1);	// +1 makes room for the trailing \0
		}
	}

	// catch x-dfengine-motd headers
	if (!strncasecmp(buf, "x-dfengine-motd: ", 17)) {
		if (strlen(buf) >= 17+sizeof(motd)) {
			if (dl_showmotd->integer) {
				Com_Printf("Warning: server motd string too large.\n");
			}
		} else {
			Q_strncpyz(motd, buf+17, sizeof(motd));
			Cvar_Set( "cl_downloadMotd", motd );
		}
	}
	return size*nmemb;
}

static int Curl_VerboseCallback_f(CURL *curl, curl_infotype type, char *data, size_t size, void *userptr) {
	char buf[1024];
	char *c, *l;

	if ((type != CURLINFO_HEADER_OUT || dl_verbose->integer < 1)
			&& (type != CURLINFO_TEXT || dl_verbose->integer < 2)) {
		return 0;
	}
	if (size >= sizeof(buf)) {
		Com_Printf("Curl_VerboseCallback_f() warning: overflow.\n");
		return 0;
	}
	Q_strncpyz(buf, data, size+1);	// +1 makes room for the trailing \0
	if (type == CURLINFO_HEADER_OUT) {
		for (l=c=buf; c-buf<size; c++) {
			// header lines should have linefeeds.
			if (*c == '\n' || *c == '\r') {
				*c = 0;
				if (c>l) {
					Com_Printf("> %s\n", l);
				}
				l = c+1;
			}
		}
		return 0;
	}
	// CURLINFO_TEXT (has its own linefeeds)
	Com_Printf("%s", buf);	// Com_Printf(buf) would result in random output/segfault if buf has % chars.
	return 0;
}

/**
 * This callback is called on regular intervals, whether data is being transferred or not.
 */
static int Curl_ProgressCallback_f(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
	
	if( curlm ) // dont print progress in console if nonblocking download
	{
		DL_Info( qfalse );
	}
	else // print progress if blocking download
	{
		DL_Info( qtrue );

		// pump events and refresh screen
		Com_EventLoop();
		SCR_UpdateScreen();
		if (Key_IsDown(K_ESCAPE)) {
			Q_strncpyz(dl_error, "Download aborted.", sizeof(dl_error));
			return -1;
		}
	}

	return 0;
}



static void Curl_ShowVersion_f(void) {
	Com_Printf("%s\n", curl_version());
}



static void Curl_Download_f( void )
{
	qboolean nonblocking;
	int state;

	// interrupt download: \download -
	if( Cmd_Argc() >= 2 && !Q_strncmp( "-", Cmd_Argv(1), 1 ) )
	{
		if( DL_Active() )		
			DL_Interrupt();
		else
			Com_Printf( "No download in progress.\n" );
		return;
	}

	if( DL_Active() )
	{
		Com_Printf( "Already downloading map '%s'.\n", Cvar_VariableString("cl_downloadName") );
		DL_Info( qtrue );
		return;
	}

	// help: \download
	if( Cmd_Argc() < 2 )
	{
		Com_Printf( "How to use:\n"
					" \\download <mapname>     - blocking download ( hold ESC to abort )\n"
					" \\download <mapname> &   - background download\n"
					" \\download -             - abort current background download\n"
					" \\download               - show help or background download progress\n"
		);
		return;
	}

	// non blocking download: \download <mapname> &
	nonblocking = ( Cmd_Argc() >= 3 ) && ( !Q_strncmp( "&", Cmd_Argv(2), 1 ) );
	
	// initialize download
	state = DL_Begin( Cmd_Argv(1), nonblocking );
	if( state != 1 ) return;

	// if blocking, download all file now
	if( !nonblocking ) DL_Continue();
}





//
// Interface
//
void DL_Init( void ) {
	if (!curl_global_init(CURL_GLOBAL_ALL)) {
		char *c;
		Cmd_AddCommand("curl_version", Curl_ShowVersion_f);
		Cmd_AddCommand("download", Curl_Download_f);
		curl_initialized = qtrue;

		// set user-agent, something along the lines of "dfengine/1.## (libcurl/#.##.# linked libs...)"
		Q_strncpyz(useragent, Q3_VERSION, sizeof(useragent));
		for (c=useragent; *c; c++) {
			if (*c == ' ') *c = '/';
		}
		Com_sprintf(useragent, sizeof(useragent), "%s (%s) ", useragent, curl_version());
	} else {
		Com_Printf("Failed to initialize libcurl.\n");
	}
	dl_verbose = Cvar_Get("dl_verbose", "0", 0);
	dl_source = Cvar_Get("dl_source", "http://q3a.ath.cx/getpk3bymapname.php/%m", CVAR_ARCHIVE);
	dl_showprogress = Cvar_Get("dl_showprogress", "1", CVAR_ARCHIVE);
	dl_showmotd = Cvar_Get("dl_showmotd", "1", CVAR_ARCHIVE);
	dl_usebaseq3 = Cvar_Get("dl_usebaseq3", "0", CVAR_ARCHIVE);
}

// 0 : no active,  1 : active blocking,  2 : active nonblocking
int DL_Active( void ) {
	if( curlm && curl ) return 2;
	if( curl ) return 1;
	return 0;
}

// the engine might be going dedicated, remove client commands
void DL_Shutdown( void ) {
	if (curl_initialized) {
		if (curlm) {
			curl_multi_cleanup(curlm);
			curlm = NULL;
		}
		if (curl) {
			curl_easy_cleanup(curl);
			curl = NULL;
		}
		if (f) {
			FS_FCloseFile(f);
			f = 0;
		}
		curl_global_cleanup();
		curl_initialized = qfalse;
		Cmd_RemoveCommand("curl_version");
		Cmd_RemoveCommand("download");
	}
}



// -1 : error,  0 : map already exists,  1 : ok
int DL_Begin( const char *map, qboolean nonblocking )
{
	char url[1024];
	char urlt[1024];
	CURLMcode resm;
	char *c;

	if( DL_Active() ) {
		Com_Printf("Already downloading map '%s'.\n", Cvar_VariableString("cl_downloadName") );
		return -1;
	}

	if (FS_FileIsInPAK(va("maps/%s.bsp", map), NULL) != -1) {
		Com_Printf("Map already exists locally.\n");
		return 0;
	}
	if (strncasecmp(dl_source->string, "http://", 7)) {
		if (strstr(dl_source->string, "://")) {
			Com_Printf("Invalid dl_source.\n");
			return -1;
		}
		Cvar_Set("dl_source", va("http://%s", dl_source->string));
	}
	if ((c = strstr(dl_source->string, "%m")) == 0) {
		Com_Printf("Cvar dl_source is missing a %%m token.\n");
		return -1;
	}
	if (strlen(dl_source->string) -2 +strlen(curl_easy_escape(curl, map, 0)) >= sizeof(url)) {
		Com_Printf("Cvar dl_source too large.\n");
		return -1;
	}

	Q_strncpyz(url, dl_source->string, c-dl_source->string +1);	// +1 makes room for the trailing 0
	Com_sprintf(urlt, sizeof(urlt), "%s%s%s", url, curl_easy_escape(curl, map, 0), c+2);
	strcpy(url,urlt);

	// set a default destination filename; Content-Disposition headers will override.
	Com_sprintf(path, sizeof(path), "%s.pk3", map);

	curl = curl_easy_init();
	if (!curl) {
		Com_Printf("Download failed to initialize.\n");
		return -1;
	}

	*dl_error = 0;
	*motd = 0;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, useragent);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);	// fail if http returns an error code
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Curl_WriteCallback_f);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Curl_HeaderCallback_f);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, Curl_VerboseCallback_f);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, Curl_ProgressCallback_f);
	//curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)(4*1024) ); // 4 KB/s for testing timeouts

	if( nonblocking )
	{
		curlm = curl_multi_init();
		if( !curlm )
		{
			curl_easy_cleanup( curl );
			curl = NULL;
			Com_Printf("Download failed to initialize ( nonblocking ).\n");
			return -1;
		}
		resm = curl_multi_add_handle( curlm, curl );
		if( resm != CURLM_OK )
		{
			curl_multi_cleanup( curlm );
			curl_easy_cleanup( curl );
			curlm = NULL;
			curl = NULL;
			Com_Printf("Download failed to initialize ( nonblocking ).\n");
			return -1;
		}	
	}

	Com_Printf("Attempting download: %s\n", url);

	Cvar_Set( "cl_downloadName", map );  // show the ui download progress screen
	Cvar_SetValue( "cl_downloadSize", 0 );
	Cvar_SetValue( "cl_downloadCount", 0 );
	Cvar_SetValue( "cl_downloadTime", cls.realtime ); // download start time offset

	return 1;
}



void DL_End( CURLcode res, CURLMcode resm )
{
	CURLMsg *msg;
	int msgs;

	if( dl_verbose->integer == 0 && dl_showprogress->integer == 2 && !curlm )
		Com_Printf( "\n" );
	
	if( curlm )
	{	
		// res = final download result
		while( ( msg = curl_multi_info_read( curlm, &msgs ) ) )
		{
			if( msg->msg != CURLMSG_DONE )
			{
				if( dl_error[0] == '\0' )
					Q_strncpyz( dl_error, "Download Interrupted.", sizeof(dl_error) );
			}
			else if( msg->easy_handle == curl )
			{
				if( msg->data.result != CURLE_OK );
					res = msg->data.result;
			}
			else
			{
				Com_Printf( "Invalid cURL handle.\n" );
			}
		}

		curl_multi_cleanup( curlm );
		curlm = NULL;		
	}

	if( curl )
	{
		curl_easy_cleanup( curl );
		curl = NULL;
	}

	// get possible error messages
	if( !*dl_error && res != CURLE_OK )
		Q_strncpyz( dl_error, curl_easy_strerror(res), sizeof(dl_error) );
	if( !*dl_error && resm != CURLM_OK )
		Q_strncpyz( dl_error, curl_multi_strerror(resm), sizeof(dl_error) );
	if( !*dl_error && !f )
		Q_strncpyz( dl_error, "File is not opened.", sizeof(dl_error) );

	if (f) {
		FS_FCloseFile(f);
		f = 0;
		if (!*dl_error) {	// download succeeded
			char dest[MAX_OSPATH];
			Com_Printf("Download complete, restarting filesystem.\n");
			Q_strncpyz(dest, path, strlen(path)-3);	// -4 +1 for the trailing \0
			Q_strcat(dest, sizeof(dest), ".pk3");
			if (!FS_FileExists(dest)) {
				FS_SV_Rename(path, dest);
				FS_Restart(clc.checksumFeed);
				if (dl_showmotd->integer && *motd) {
					Com_Printf("Server motd: %s\n", motd);
				}
			} else {
				// normally such errors should be caught upon starting the transfer. Anyway better do
				// it here again - the filesystem might have changed, plus this may help contain some
				// bugs / exploitable flaws in the code.
				Com_Printf("Failed to copy downloaded file to its location - file already exists.\n");
				FS_HomeRemove(path);
			}
		} else {
			FS_HomeRemove(path);
		}
	}

	Cvar_Set( "cl_downloadName", "" );  // hide the ui downloading screen
	Cvar_SetValue( "cl_downloadSize", 0 );
	Cvar_SetValue( "cl_downloadCount", 0 );
	Cvar_SetValue( "cl_downloadTime", 0 );
	Cvar_Set( "cl_downloadMotd", "" );

	if( *dl_error )
	{
		if( clc.state == CA_CONNECTED )
			Com_Error( ERR_DROP, "%s\n", dl_error ); // download error while connecting, can not continue loading
		else
			Com_Printf( "%s\n", dl_error ); // download error while in game, do not disconnect

		*dl_error = '\0';
	}
	else
	{
		// download completed, request new gamestate to check possible new map if we are not already in game
		if( clc.state == CA_CONNECTED )
			CL_AddReliableCommand( "donedl", qfalse); // get new gamestate info from server
	}
}



// -1 : error,  0 : done,  1 : continue
int DL_Continue( void )
{
	CURLcode  res  = CURLE_OK;
	CURLMcode resm = CURLM_OK;
	int running = 1;
	int state = -1;

	if( !curl ) return 0;

	if( curlm )	// non blocking
	{
		resm = CURLM_CALL_MULTI_PERFORM;
		while( resm == CURLM_CALL_MULTI_PERFORM )
			resm = curl_multi_perform( curlm, &running );
		if( resm == CURLM_OK )
			state = ( running ? 1 : 0 );
	}
	else // blocking
	{
		// NOTE:
		// blocking download has its own curl loop, so we need check events and update screen in Curl_ProgressCallback_f,
		// this loop is not updating time cvars ( com_frameMsec, cls.realFrametime, cls.frametime, cls.realtime, ... )
		// and this will cause client-server desynchronization, if download takes long time timeout can occur.
		res = curl_easy_perform( curl ); // returns when error or download is completed/aborted
		state = ( res == CURLE_OK ? 0 : -1 );
	}

	if( state != 1 ) // no continue ( done or error )
		DL_End( res, resm );

	return state;
}



void DL_Interrupt( void )
{
	if( !curl ) return;
	Q_strncpyz( dl_error, "Download Interrupted.", sizeof(dl_error) );
	DL_End( CURLE_OK, CURLM_OK );
}



void DL_Info( qboolean console )
{
	static double lastTime = 0.0;
	double dltotal, dlnow, speed, time;
	int timeleft;
	CURLcode res;

	if( !DL_Active() ) return;
	if( clc.state != CA_CONNECTED && !console ) return;

	res = curl_easy_getinfo( curl, CURLINFO_TOTAL_TIME, &time );					// total downloading time
	if( res != CURLE_OK ) time = -1.0;

	res = curl_easy_getinfo( curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &dltotal );	// file size bytes
	if( res != CURLE_OK || dltotal < 0.0 ) dltotal = 0.0;

	res = curl_easy_getinfo( curl, CURLINFO_SIZE_DOWNLOAD, &dlnow );				// current bytes
	if( res != CURLE_OK || dlnow < 0.0 ) dlnow = 0.0;
	if( dltotal > 0.0 && dlnow > dltotal ) dlnow = 0.0;

	res = curl_easy_getinfo( curl, CURLINFO_SPEED_DOWNLOAD, &speed );				// download rate bytes/sec
	if( res != CURLE_OK ) speed = -1.0;

	// update ui download progress screen cvars
	if( clc.state == CA_CONNECTED )
	{
		Cvar_SetValue( "cl_downloadSize",  (float)dltotal );
		Cvar_SetValue( "cl_downloadCount", (float)dlnow );
	}

	// print download progress in console
	if( console && dl_showprogress->integer && dlnow > 0.0 )
	{
		// 8 times per second is enough
		if( time-lastTime > 1.0/8.0 || time-lastTime < 0.0 || dlnow == dltotal )
		{
			lastTime = time;

			if( dl_verbose->integer == 0 && dl_showprogress->integer == 2 && !curlm )
				Com_Printf( "\r" ); // overwrite old progress line

			if (dltotal != 0.0	// content-size is known
					&& dlnow <= dltotal) {	// and appropriate
				if (dltotal > 1024.0 * 1024.0) {	// MB range
					Com_Printf("%.1f/%.1fMB", dlnow/1024.0/1024.0, dltotal/1024.0/1024.0);
				} else if (dltotal > 10240.0) {		// KB range (>10KB)
					Com_Printf("%.1f/%.1fKB", dlnow/1024.0, dltotal/1024.0);
				} else {							// byte range
					Com_Printf("%.0f/%.0fB", dlnow, dltotal);
				}
			} else {	// unknown content-size
				if (dlnow > 1024.0 * 1024.0) {		// MB range
					Com_Printf("%.1fMB", dlnow/1024.0/1024.0);
				} else if (dlnow > 10240.0) {		// KB range (>10KB)
					Com_Printf("%.1fKB", dlnow/1024.0);
				} else {							// byte range
					Com_Printf("%.0fB", dlnow);
				}
			}
			if (speed >= 0.0) {
				Com_Printf(" @%.1fKB/s", speed/1024.0);
			}
			if (dltotal != 0.0 && dlnow <= dltotal) {		
				Com_Printf(" (%2.1f%%)", 100.0*dlnow/dltotal);
				if( time > 0.0 && dlnow > 0.0 ) {
					timeleft = (int) ( (dltotal-dlnow) * time/dlnow );
					Com_Printf(" time left: %d:%.2d", timeleft/60, timeleft%60);
				}
			}

			if( dl_verbose->integer == 0 && dl_showprogress->integer == 2 && !curlm )
				Com_Printf( "      " );	// make sure line is totally overwriten
			else
				Com_Printf( "\n" );		// or start a new line
		}
	}
}

