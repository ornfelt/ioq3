/*
gfx_glimp.c - renderer_gfx's window and device on the gfx library. It replaces
code/sdl/sdl_glimp.c (GLimp_*) and sdl_gamma.c for renderer_gfx; renderer_opengl1 / 2 keep
those. So far (GFX_STATUS.md phase 0) it holds the backend selection: which gfx device and
window system r_gfxDevice / r_gfxWindow name, and the gfxinfo lines about them. The window is
still sdl_glimp.c's until phase 1.
*/

#include "tr_local.h"
#include "gfx_glimp.h"

// every value of gfx's backend enums, built into this gfx or not
#define GFX_NUM_DEVICE_BACKENDS	( GFX_DEVICE_JKG + 1 )
#define GFX_NUM_WINDOW_BACKENDS	( GFX_WINDOW_ANDROID + 1 )

#define GFX_BACKEND_LIST_SIZE	64

/*
===============
GfxGLimp_DeviceFromName

The gfx device an r_gfxDevice value names (case-insensitive), or -1.
===============
*/
static int GfxGLimp_DeviceFromName( const char *name )
{
	int i;

	for ( i = 0; i < GFX_NUM_DEVICE_BACKENDS; i++ ) {
		if ( !Q_stricmp( name, gfx_device_backend_name( (enum gfx_device_backend)i ) ) ) {
			return i;
		}
	}
	return -1;
}

/*
===============
GfxGLimp_WindowFromName

The gfx window system an r_gfxWindow value names (case-insensitive), or -1.
===============
*/
static int GfxGLimp_WindowFromName( const char *name )
{
	int i;

	for ( i = 0; i < GFX_NUM_WINDOW_BACKENDS; i++ ) {
		if ( !Q_stricmp( name, gfx_window_backend_name( (enum gfx_window_backend)i ) ) ) {
			return i;
		}
	}
	return -1;
}

/*
===============
GfxGLimp_Backends

The gfx device and window system r_gfxDevice / r_gfxWindow (latched, so a vid_restart applies
them) name. A name gfx does not know, or a backend this gfx build lacks
(gfx_has_device_backend / gfx_has_window_backend), falls back to the default with a console
message.
===============
*/
void GfxGLimp_Backends( enum gfx_device_backend *device, enum gfx_window_backend *window )
{
	int found;

	found = GfxGLimp_DeviceFromName( r_gfxDevice->string );
	if ( found < 0 || !gfx_has_device_backend( (enum gfx_device_backend)found ) ) {
		ri.Printf( PRINT_WARNING, "r_gfxDevice \"%s\" is %s, using %s\n", r_gfxDevice->string,
			found < 0 ? "unknown" : "not built into this gfx", gfx_device_backend_name( R_GFX_DEFAULT_DEVICE ) );
		found = R_GFX_DEFAULT_DEVICE;
	}
	*device = (enum gfx_device_backend)found;

	found = GfxGLimp_WindowFromName( r_gfxWindow->string );
	if ( found < 0 || !gfx_has_window_backend( (enum gfx_window_backend)found ) ) {
		ri.Printf( PRINT_WARNING, "r_gfxWindow \"%s\" is %s, using %s\n", r_gfxWindow->string,
			found < 0 ? "unknown" : "not built into this gfx", gfx_window_backend_name( R_GFX_DEFAULT_WINDOW ) );
		found = R_GFX_DEFAULT_WINDOW;
	}
	*window = (enum gfx_window_backend)found;
}

/*
===============
GfxGLimp_PrintBackends

gfxinfo: the devices and window systems this gfx build has, and the ones the cvars select.
===============
*/
void GfxGLimp_PrintBackends( void )
{
	char devices[GFX_BACKEND_LIST_SIZE];
	char windows[GFX_BACKEND_LIST_SIZE];
	enum gfx_device_backend device;
	enum gfx_window_backend window;
	int i;

	devices[0] = '\0';
	for ( i = 0; i < GFX_NUM_DEVICE_BACKENDS; i++ ) {
		if ( gfx_has_device_backend( (enum gfx_device_backend)i ) ) {
			Q_strcat( devices, sizeof( devices ), gfx_device_backend_name( (enum gfx_device_backend)i ) );
			Q_strcat( devices, sizeof( devices ), " " );
		}
	}

	windows[0] = '\0';
	for ( i = 0; i < GFX_NUM_WINDOW_BACKENDS; i++ ) {
		if ( gfx_has_window_backend( (enum gfx_window_backend)i ) ) {
			Q_strcat( windows, sizeof( windows ), gfx_window_backend_name( (enum gfx_window_backend)i ) );
			Q_strcat( windows, sizeof( windows ), " " );
		}
	}

	ri.Printf( PRINT_ALL, "gfx devices: %s\n", devices );
	ri.Printf( PRINT_ALL, "gfx windows: %s\n", windows );

	GfxGLimp_Backends( &device, &window );
	ri.Printf( PRINT_ALL, "gfx device: %s, window: %s\n",
		gfx_device_backend_name( device ), gfx_window_backend_name( window ) );
}
