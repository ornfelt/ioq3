/*
gfx_glimp.c - renderer_gfx's window and device on the gfx library. It replaces
code/sdl/sdl_glimp.c (GLimp_*) and sdl_gamma.c for renderer_gfx; renderer_opengl1 / 2 keep
those. GLimp_Init creates the gfx window and its device for the backends r_gfxDevice /
r_gfxWindow select, GLimp_EndFrame swaps, GLimp_Shutdown deletes the window, and
GfxGLimp_GetWindow hands the window to the client's input (GFX_STATUS.md, "Window handoff").
The GL calls of the back end go to the device through gfx_gl.c.
*/

#include <stdarg.h>

#include "gfx_gl.h"
#include "gfx_glimp.h"
#include "gfx_public.h"

// every value of gfx's backend enums, built into this gfx or not
#define GFX_NUM_DEVICE_BACKENDS	( GFX_DEVICE_JKG + 1 )
#define GFX_NUM_WINDOW_BACKENDS	( GFX_WINDOW_ANDROID + 1 )

#define GFX_BACKEND_LIST_SIZE	64

#define R_MODE_FALLBACK 3 // 640 * 480

// the color / depth / stencil bits of the window gfx creates (R8G8B8A8 + D24S8)
#define GFX_COLOR_BITS		24
#define GFX_DEPTH_BITS		24
#define GFX_STENCIL_BITS	8

// the overbright shifts a gamma ramp may make (R_SetColorMappings allows up to 2 bits)
#define GFX_MAX_OVERBRIGHT_SHIFT	2

// sdl_glimp.c's cvars that the menus and configs know; gfx reads none of them (it has no window
// position, resize flag or SDL driver choice), but they stay registered as in the GL renderers
cvar_t *r_allowSoftwareGL;
cvar_t *r_allowResize;
cvar_t *r_centerWindow;
cvar_t *r_sdlDriver;
cvar_t *r_preferOpenGLES;

/*
===============
GfxGLimp_Error

gfx's own error reports, routed to the console as warnings.
===============
*/
static void GfxGLimp_Error( const char *fmt, ... )
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	ri.Printf( PRINT_WARNING, "gfx: %s\n", msg );
}

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
message. gfx makes its D3D devices only on its win32 window (window.c), so such a device keeps
the device asked for and gets that window, instead of falling back to the defaults.
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

	if ( ( *device == GFX_DEVICE_D3D9 || *device == GFX_DEVICE_D3D11 || *device == GFX_DEVICE_D3D12 )
		&& *window != GFX_WINDOW_WIN32 && gfx_has_window_backend( GFX_WINDOW_WIN32 ) ) {
		ri.Printf( PRINT_ALL, "r_gfxDevice %s needs the win32 window, using it instead of %s\n",
			gfx_device_backend_name( *device ), gfx_window_backend_name( *window ) );
		*window = GFX_WINDOW_WIN32;
	}
}

/*
===============
GfxGLimp_PrintBackends

gfxinfo: the devices and window systems this gfx build has, and the ones in use.
===============
*/
void GfxGLimp_PrintBackends( void )
{
	char devices[GFX_BACKEND_LIST_SIZE];
	char windows[GFX_BACKEND_LIST_SIZE];
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

	if ( gfxGl.window ) {
		ri.Printf( PRINT_ALL, "gfx device: %s, window: %s\n",
			gfx_device_backend_name( gfx_window_get_device_backend( gfxGl.window ) ),
			gfx_window_backend_name( gfx_window_get_window_backend( gfxGl.window ) ) );
	} else {
		ri.Printf( PRINT_ALL, "gfx device: -, window: -\n" );
	}
}

#ifdef _WIN32

/*
===============
GfxGLimp_DesktopSize

The desktop resolution, for r_mode -2, r_fullscreen and the display aspect (sdl_glimp.c asks
SDL_GetDesktopDisplayMode; gfx has no display query)
===============
*/
static qboolean GfxGLimp_DesktopSize( int *width, int *height )
{
	*width = GetSystemMetrics( SM_CXSCREEN );
	*height = GetSystemMetrics( SM_CYSCREEN );
	return *width > 0 && *height > 0;
}

static BOOL CALLBACK GfxGLimp_FindClientWindow( HWND hwnd, LPARAM found )
{
	char title[64];

	if ( GetWindowTextA( hwnd, title, sizeof( title ) ) > 0 && !strcmp( title, CLIENT_WINDOW_TITLE ) ) {
		*(HWND *)found = hwnd;
		return FALSE;
	}
	return TRUE;
}

/*
===============
GfxGLimp_ClientWindow

This thread's window titled CLIENT_WINDOW_TITLE: the gfx window, whichever backend made it
===============
*/
static HWND GfxGLimp_ClientWindow( void )
{
	HWND hwnd = NULL;

	EnumThreadWindows( GetCurrentThreadId(), GfxGLimp_FindClientWindow, (LPARAM)&hwnd );
	return hwnd;
}

/*
===============
GfxGLimp_ReadWindowPixels

glReadPixels of the color buffer, for screenshots and video capture: gfx has no read back, so
the rectangle is read from the gfx window's client area on the screen (GDI). x, y count from the
bottom left as in GL; bgra gets width * height 8-bit BGRA pixels, bottom row first (GL's row
order). What the screen shows is the frame presented last - one frame before the one
glReadPixels would read - and a window covered by another one shows that one.
===============
*/
qboolean GfxGLimp_ReadWindowPixels( int x, int y, int width, int height, byte *bgra )
{
	HWND hwnd;
	RECT rect;
	POINT origin;
	BITMAPINFO info;
	HDC screen, memory;
	HBITMAP bitmap;
	HGDIOBJ old;
	int rows = 0;

	if ( width <= 0 || height <= 0 ) {
		return qfalse;
	}
	hwnd = GfxGLimp_ClientWindow();
	if ( !hwnd ) {
		return qfalse;
	}
	origin.x = 0;
	origin.y = 0;
	if ( !GetClientRect( hwnd, &rect ) || !ClientToScreen( hwnd, &origin ) ) {
		return qfalse;
	}

	Com_Memset( &info, 0, sizeof( info ) );
	info.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = height;	// positive: bottom-up rows
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;

	screen = GetDC( NULL );
	if ( !screen ) {
		return qfalse;
	}
	memory = CreateCompatibleDC( screen );
	bitmap = CreateCompatibleBitmap( screen, width, height );
	old = SelectObject( memory, bitmap );
	// GL's y is from the bottom of the client area, the screen's from the top
	if ( BitBlt( memory, 0, 0, width, height, screen, origin.x + x, origin.y + rect.bottom - y - height, SRCCOPY ) ) {
		SelectObject( memory, old );
		rows = GetDIBits( memory, bitmap, 0, height, bgra, &info, DIB_RGB_COLORS );
	} else {
		SelectObject( memory, old );
	}
	DeleteObject( bitmap );
	DeleteDC( memory );
	ReleaseDC( NULL, screen );

	return rows == height;
}

#else

static qboolean GfxGLimp_DesktopSize( int *width, int *height )
{
	return qfalse;
}

qboolean GfxGLimp_ReadWindowPixels( int x, int y, int width, int height, byte *bgra )
{
	return qfalse;
}

#endif

/*
===============
GfxGLimp_SetMode

GLimp_SetMode on gfx: the window and device for mode and the given backends. r_fullscreen makes
a desktop-sized window and turns it borderless fullscreen on its monitor, at the desktop's mode
(gfx changes no display mode); a window backend without that keeps the desktop-sized window.
===============
*/
static qboolean GfxGLimp_SetMode( int mode, qboolean fullscreen, enum gfx_device_backend deviceBackend, enum gfx_window_backend windowBackend )
{
	struct gfx_window_properties properties;
	struct gfx_window *window;
	int desktopWidth, desktopHeight;
	int width, height;
	qboolean haveDesktop;
	char error[MAX_STRING_CHARS];

	ri.Printf( PRINT_ALL, "Initializing gfx display\n" );

	haveDesktop = GfxGLimp_DesktopSize( &desktopWidth, &desktopHeight );
	if ( haveDesktop ) {
		displayAspect = (float)desktopWidth / (float)desktopHeight;
		ri.Printf( PRINT_ALL, "Display aspect: %.3f\n", displayAspect );
	} else {
		ri.Printf( PRINT_ALL, "Cannot determine display aspect, assuming 1.333\n" );
	}

	ri.Printf( PRINT_ALL, "...setting mode %d:", mode );

	if ( mode == -2 || fullscreen ) {
		// use desktop video resolution
		if ( haveDesktop ) {
			glConfig.vidWidth = desktopWidth;
			glConfig.vidHeight = desktopHeight;
		} else {
			glConfig.vidWidth = 640;
			glConfig.vidHeight = 480;
			ri.Printf( PRINT_ALL, "Cannot determine display resolution, assuming 640x480\n" );
		}
		glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
	} else if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode ) ) {
		ri.Printf( PRINT_ALL, " invalid mode\n" );
		return qfalse;
	}
	ri.Printf( PRINT_ALL, " %d %d\n", glConfig.vidWidth, glConfig.vidHeight );

	gfx_window_properties_init( &properties );
	properties.window_backend = windowBackend;
	properties.device_backend = deviceBackend;
	properties.color_format = GFX_FORMAT_R8G8B8A8_UNORM;
	properties.depth_stencil_format = GFX_FORMAT_D24_UNORM_S8_UINT;
	properties.samples = (uint32_t)MAX( r_ext_multisample->integer, 0 );

	window = gfx_create_window( CLIENT_WINDOW_TITLE, (uint32_t)glConfig.vidWidth, (uint32_t)glConfig.vidHeight, &properties );
	if ( !window ) {
		ri.Printf( PRINT_ALL, "Couldn't create the gfx window (device %s, window %s)\n",
			gfx_device_backend_name( deviceBackend ), gfx_window_backend_name( windowBackend ) );
		return qfalse;
	}

	if ( fullscreen ) {
		gfx_window_set_mode( window, GFX_WINDOW_MODE_BORDERLESS_FULLSCREEN );
	}

	// gfx makes its windows dpi aware and grows a small one (keeping its shape) to most of the
	// screen, so the mode is a minimum: the frame is drawn at the client area the window got
	width = (int)gfx_window_get_width( window );
	height = (int)gfx_window_get_height( window );
	if ( width > 0 && height > 0 && ( width != glConfig.vidWidth || height != glConfig.vidHeight ) ) {
		ri.Printf( PRINT_ALL, "gfx sized the window to %d %d\n", width, height );
		glConfig.vidWidth = width;
		glConfig.vidHeight = height;
		glConfig.windowAspect = (float)width / (float)height;
	}

	glConfig.isFullscreen = fullscreen;
	glConfig.colorBits = GFX_COLOR_BITS;
	glConfig.depthBits = GFX_DEPTH_BITS;
	glConfig.stencilBits = GFX_STENCIL_BITS;
	glConfig.stereoEnabled = qfalse;
	glConfig.displayFrequency = 0;
	ri.Printf( PRINT_ALL, "Using %d color bits, %d depth, %d stencil display.\n",
		glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );

	gfx_make_current( window );
	if ( !GfxGL_Init( window, deviceBackend, error, sizeof( error ) ) ) {
		// the shaders are part of the install (GLimp_Init's could-not-load-OpenGL case)
		gfx_delete_window( window );
		ri.Error( ERR_FATAL, "GLimp_Init() - gfx: %s", error );
	}
	gfx_set_swap_interval( window, r_swapInterval->integer );

	qglClearColor( 0, 0, 0, 1 );
	qglClear( GL_COLOR_BUFFER_BIT );
	gfx_window_show( window );
	gfx_swap_buffers( window );
	GfxGL_BeginFrame();

	ri.Printf( PRINT_ALL, "gfx device: %s, window: %s\n",
		gfx_device_backend_name( deviceBackend ), gfx_window_backend_name( windowBackend ) );
	return qtrue;
}

/*
===============
GfxGLimp_InitExtensions

GLimp_InitExtensions on gfx: the GL extensions renderergl1 uses are emulated by gfx_gl.c, so each
is on unless its cvar turns it off, and its entry points are gfx_gl.c's. Compressed textures are
not offered (the GL1 path lets the driver compress; gfx uploads what it is given).
===============
*/
static void GfxGLimp_InitExtensions( qboolean fixedFunction )
{
	if ( !r_allowExtensions->integer )
	{
		ri.Printf( PRINT_ALL, "* IGNORING OPENGL EXTENSIONS *\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

	glConfig.textureCompression = TC_NONE;
	ri.Printf( PRINT_ALL, "...GL_EXT_texture_compression_s3tc not found\n" );

	// OpenGL 1 fixed function pipeline
	if ( fixedFunction )
	{
		// GL_EXT_texture_env_add
		if ( r_ext_texture_env_add->integer )
		{
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
		}
		else
		{
			glConfig.textureEnvAddAvailable = qfalse;
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
		}

		// GL_ARB_multitexture
		qglMultiTexCoord2fARB = NULL;
		qglActiveTextureARB = NULL;
		qglClientActiveTextureARB = NULL;
		if ( r_ext_multitexture->value )
		{
			GLint glint = 0;

			qglMultiTexCoord2fARB = GfxGL_MultiTexCoord2fARB;
			qglActiveTextureARB = GfxGL_ActiveTextureARB;
			qglClientActiveTextureARB = GfxGL_ClientActiveTextureARB;
			qglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glint );
			glConfig.numTextureUnits = (int) glint;
			ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
		}

		// GL_EXT_compiled_vertex_array
		qglLockArraysEXT = NULL;
		qglUnlockArraysEXT = NULL;
		if ( r_ext_compiled_vertex_array->value )
		{
			qglLockArraysEXT = GfxGL_LockArraysEXT;
			qglUnlockArraysEXT = GfxGL_UnlockArraysEXT;
			ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	}

	textureFilterAnisotropic = qfalse;
	if ( r_ext_texture_filter_anisotropic->integer )
	{
		maxAnisotropy = GFX_GL_MAX_ANISOTROPY;
		ri.Printf( PRINT_ALL, "...using GL_EXT_texture_filter_anisotropic (max: %i)\n", maxAnisotropy );
		textureFilterAnisotropic = qtrue;
	}
	else
	{
		ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_filter_anisotropic\n" );
	}

	ri.Printf( PRINT_ALL, "...using GL_SGIS_texture_edge_clamp\n" );
	haveClampToEdge = qtrue;
}

/*
===============
GfxGLimp_ConfigStrings

The renderer strings of glConfig: gfx, the device and window in use, and the extensions
===============
*/
static void GfxGLimp_ConfigStrings( enum gfx_device_backend deviceBackend, enum gfx_window_backend windowBackend )
{
	Q_strncpyz( glConfig.vendor_string, "gfx", sizeof( glConfig.vendor_string ) );
	Com_sprintf( glConfig.renderer_string, sizeof( glConfig.renderer_string ), "renderer_gfx on gfx %s / %s",
		gfx_device_backend_name( deviceBackend ), gfx_window_backend_name( windowBackend ) );
	Com_sprintf( glConfig.version_string, sizeof( glConfig.version_string ), "gfx %s",
		gfx_device_backend_name( deviceBackend ) );
	Q_strncpyz( glConfig.extensions_string, GFX_GL_EXTENSIONS, sizeof( glConfig.extensions_string ) );
}

/*
===============
GLimp_Init

Creates the gfx window and device (sdl_glimp.c: the SDL window and GL context)
===============
*/
void GLimp_Init( qboolean fixedFunction )
{
	enum gfx_device_backend deviceBackend;
	enum gfx_window_backend windowBackend;
	qboolean fullscreen;

	ri.Printf( PRINT_DEVELOPER, "Glimp_Init( )\n" );

	r_allowSoftwareGL = ri.Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );
	r_sdlDriver = ri.Cvar_Get( "r_sdlDriver", "", CVAR_ROM );
	r_allowResize = ri.Cvar_Get( "r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_centerWindow = ri.Cvar_Get( "r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_preferOpenGLES = ri.Cvar_Get( "r_preferOpenGLES", "-1", CVAR_ARCHIVE | CVAR_LATCH );

	if( ri.Cvar_VariableIntegerValue( "com_abnormalExit" ) )
	{
		ri.Cvar_Set( "r_mode", va( "%d", R_MODE_FALLBACK ) );
		ri.Cvar_Set( "r_fullscreen", "0" );
		ri.Cvar_Set( "r_centerWindow", "0" );
		ri.Cvar_Set( "com_abnormalExit", "0" );
	}

	gfx_error_callback = GfxGLimp_Error;

	ri.Sys_GLimpInit( );

	GfxGLimp_Backends( &deviceBackend, &windowBackend );
	fullscreen = r_fullscreen->integer != 0;

	// Create the window and its device
	if( GfxGLimp_SetMode( r_mode->integer, fullscreen, deviceBackend, windowBackend ) )
		goto success;

	// Try again on the default backends, gfx's "safe mode"
	ri.Sys_GLimpSafeInit( );

	if( deviceBackend != R_GFX_DEFAULT_DEVICE || windowBackend != R_GFX_DEFAULT_WINDOW )
	{
		ri.Printf( PRINT_ALL, "Falling back on r_gfxDevice %s, r_gfxWindow %s\n",
			gfx_device_backend_name( R_GFX_DEFAULT_DEVICE ), gfx_window_backend_name( R_GFX_DEFAULT_WINDOW ) );

		if( GfxGLimp_SetMode( r_mode->integer, fullscreen, R_GFX_DEFAULT_DEVICE, R_GFX_DEFAULT_WINDOW ) )
			goto success;
	}

	// Finally, try the default screen resolution
	if( r_mode->integer != R_MODE_FALLBACK )
	{
		ri.Printf( PRINT_ALL, "Setting r_mode %d failed, falling back on r_mode %d\n",
				r_mode->integer, R_MODE_FALLBACK );

		if( GfxGLimp_SetMode( R_MODE_FALLBACK, qfalse, R_GFX_DEFAULT_DEVICE, R_GFX_DEFAULT_WINDOW ) )
			goto success;
	}

	// Nothing worked, give up
	ri.Error( ERR_FATAL, "GLimp_Init() - could not create the gfx window" );

success:
	// These values force the UI to disable driver selection
	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;

	// gfx has no gamma ramp. A ramp that only shifts by the overbright bits (r_gamma 1) is drawn
	// at the end of the frame (GLimp_SetGamma); with another r_gamma the renderer brightens the
	// textures instead, as on a GL driver without the ramp
	glConfig.deviceSupportsGamma = !r_ignorehwgamma->integer && r_gamma->value == 1.0f;

	GfxGLimp_ConfigStrings( gfx_window_get_device_backend( gfxGl.window ), gfx_window_get_window_backend( gfxGl.window ) );

	// initialize extensions
	GfxGLimp_InitExtensions( fixedFunction );

	ri.Cvar_Get( "r_availableModes", "", CVAR_ROM );

	// The client finds the window through GfxGLimp_GetWindow; there is no SDL window to hand IN_Init
	ri.IN_Init( NULL );
}

/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame( void )
{
	if ( !gfxGl.window ) {
		return;
	}

	// don't flip if drawing to front buffer
	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		GfxGL_Overbright();
		gfx_swap_buffers( gfxGl.window );
	}

	if( r_fullscreen->modified )
	{
		if( r_fullscreen->integer && ri.Cvar_VariableIntegerValue( "in_nograb" ) )
		{
			ri.Printf( PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n");
			ri.Cvar_Set( "r_fullscreen", "0" );
			r_fullscreen->modified = qfalse;
		}

		// gfx cannot switch a window to fullscreen in place: always sdl_glimp.c's slow way
		if( !!r_fullscreen->integer != !!glConfig.isFullscreen )
			ri.Cmd_ExecuteText( EXEC_APPEND, "vid_restart\n" );

		r_fullscreen->modified = qfalse;
	}

	GfxGL_BeginFrame();
}

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( void )
{
	struct gfx_window *window = gfxGl.window;

	ri.IN_Shutdown();

	if ( window ) {
		GfxGL_Shutdown();
		gfx_delete_window( window );
	}
}

/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void GLimp_Minimize( void )
{
	if ( gfxGl.window ) {
		gfx_window_minimize( gfxGl.window );
	}
}

/*
===============
GLimp_LogComment
===============
*/
void GLimp_LogComment( char *comment )
{
}

/*
===============
GfxGLimp_RampShift

The shift n of a ramp that is min( 255, i << n ) on every channel, or -1 for any other ramp
===============
*/
static int GfxGLimp_RampShift( unsigned char *ramps[3] )
{
	int shift, channel, i;

	for ( shift = 0; shift <= GFX_MAX_OVERBRIGHT_SHIFT; shift++ ) {
		for ( channel = 0; channel < 3; channel++ ) {
			for ( i = 0; i < 256; i++ ) {
				if ( ramps[channel][i] != MIN( 255, i << shift ) ) {
					break;
				}
			}
			if ( i < 256 ) {
				break;
			}
		}
		if ( channel == 3 ) {
			return shift;
		}
	}
	return -1;
}

/*
=================
GLimp_SetGamma

gfx has no gamma ramp: GLimp_EndFrame doubles the frame once per overbright bit
(GfxGL_Overbright), which is the ramp R_SetColorMappings makes with r_gamma 1. A ramp with a gamma curve
(r_gamma changed since GLimp_Init) keeps only its overbright shift until the next vid_restart,
which moves the gamma into the textures.
=================
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
	unsigned char *ramps[3];
	int shift;

	ramps[0] = red;
	ramps[1] = green;
	ramps[2] = blue;
	shift = GfxGLimp_RampShift( ramps );
	if ( shift < 0 ) {
		ri.Printf( PRINT_ALL, "gfx: r_gamma takes effect after vid_restart (no gamma ramp)\n" );
		shift = MIN( MAX( tr.overbrightBits, 0 ), GFX_MAX_OVERBRIGHT_SHIFT );
	}
	gfxGl.overbrightPasses = shift;
}

/*
===============
GfxGLimp_GetWindow

The window handoff (gfx_public.h): renderer_gfx and the client are one binary, so the client's
input asks for the window with a plain call
===============
*/
struct gfx_window *GfxGLimp_GetWindow( void )
{
	return gfxGl.window;
}
