/*
gfx_input.c - the client's keyboard, text, mouse and window events from renderer_gfx's gfx window
(new file, USE_GFX; no counterpart: it stands in for the window half of code/sdl/sdl_input.c -
IN_ProcessEvents, IN_TranslateSDLToQ3Key, IN_PrintKey, IN_GobbleMotionEvents, IN_ActivateMouse
and IN_DeactivateMouse - while GfxGLimp_GetWindow() is not NULL). The port of ioq3-rs's
code/gfx/gfx_input.rs.

gfx calls the event handler from inside gfx_window_poll_events, in the middle of the window
system's message dispatch, where a Com_Error longjmp would leave the window system's state
behind. The handler therefore only copies the event into a fixed ring; IN_GfxProcessEvents drains
the ring after the poll, in the client's frame, as IN_ProcessEvents drains SDL's queue. Nothing is
allocated per event.
*/

#include "../client/client.h"
#include "../sys/sys_local.h"

#include "gfx/window.h"
#include "gfx_input.h"

#ifdef _WIN32
#include <windows.h>
#endif

// events one poll can hold before the newest are dropped (a power of two). SDL's queue holds
// 65535; a frame of gfx events is a handful, a stalled frame (a map load) a few hundred moves.
#define GFX_EVENT_QUEUE_SIZE	1024
#define GFX_EVENT_QUEUE_MASK	( GFX_EVENT_QUEUE_SIZE - 1 )

// sdl_input.c's SDL_BUTTON_X1 / SDL_BUTTON_X2: the SDL mouse buttons with K_MOUSE4 / K_MOUSE5
#define GFX_SDL_BUTTON_X1	4
#define GFX_SDL_BUTTON_X2	5

#define CTRL(a) ((a)-'a'+1)

static struct
{
	// the ring the event handler fills: len events from head
	struct gfx_event	queue[GFX_EVENT_QUEUE_SIZE];
	int					head;
	int					len;

	// events the handler could not queue since the last drain
	int					dropped;

	// the gfx keys reported down to the client, so focus loss can release them and a held key's
	// repeat after the focus came back counts as its press
	qboolean			keysDown[GFX_KEY_LAST];

	// gfx has no focus query: FocusIn / FocusOut since IN_GfxInit
	qboolean			focused;

	// the pointer position the next MOUSE_MOVE is measured from (gfx's virtual position while
	// the cursor is grabbed, the window position otherwise)
	int					mouseX;
	int					mouseY;

	// the window size the last RESIZE (or IN_GfxInit) reported
	unsigned int		width;
	unsigned int		height;

	// gfx's sdl window numbers the mouse buttons past middle as SDL does, the others one lower
	qboolean			sdlWindow;

	// IN_ProcessEvents' lastKeyDown for the gfx window's events
	keyNum_t			lastKeyDown;

	cvar_t				*keyboardDebug;
	cvar_t				*nograb;
} gfxInput;

/*
===============
IN_GfxQueueEvent

The window's event handler: queue the event, nothing else (see the top of the file).
===============
*/
static void IN_GfxQueueEvent( struct gfx_event *event )
{
	if( gfxInput.len == GFX_EVENT_QUEUE_SIZE )
	{
		gfxInput.dropped++;
		return;
	}

	gfxInput.queue[( gfxInput.head + gfxInput.len ) & GFX_EVENT_QUEUE_MASK] = *event;
	gfxInput.len++;
}

/*
===============
IN_GfxNextEvent

The next queued event, oldest first; qfalse when the ring is empty.
===============
*/
static qboolean IN_GfxNextEvent( struct gfx_event *event )
{
	if( gfxInput.len == 0 )
		return qfalse;

	*event = gfxInput.queue[gfxInput.head];
	gfxInput.head = ( gfxInput.head + 1 ) & GFX_EVENT_QUEUE_MASK;
	gfxInput.len--;
	return qtrue;
}

/*
===============
IN_GfxWindowHasOSFocus

Whether the window gfx just created has the focus. gfx cannot say (no focus query, and the win32
window takes the focus before the handler is set, so no FOCUS_IN follows); a window started
behind another program must not grab the cursor. On Windows the foreground window is asked
whether it belongs to this process - the only window the client has is gfx's; elsewhere the
window is taken as focused, as gfx's window systems there report the first focus as an event.
===============
*/
static qboolean IN_GfxWindowHasOSFocus( void )
{
#ifdef _WIN32
	DWORD processId = 0;

	GetWindowThreadProcessId( GetForegroundWindow( ), &processId );
	return processId == GetCurrentProcessId( ) ? qtrue : qfalse;
#else
	return qtrue;
#endif
}

/*
===============
IN_GfxResetMousePosition

The pointer position gfx reports now becomes the start of the next motion delta.
===============
*/
static void IN_GfxResetMousePosition( struct gfx_window *window )
{
	gfxInput.mouseX = gfx_get_mouse_x( window );
	gfxInput.mouseY = gfx_get_mouse_y( window );
}

/*
===============
IN_GfxInit
===============
*/
void IN_GfxInit( struct gfx_window *window )
{
	gfxInput.head = 0;
	gfxInput.len = 0;
	gfxInput.dropped = 0;
	Com_Memset( gfxInput.keysDown, 0, sizeof( gfxInput.keysDown ) );
	// FOCUS_IN / FOCUS_OUT say when it changes from here on
	gfxInput.focused = IN_GfxWindowHasOSFocus( );
	IN_GfxResetMousePosition( window );
	gfxInput.width = gfx_window_get_width( window );
	gfxInput.height = gfx_window_get_height( window );
	gfxInput.sdlWindow = gfx_window_get_window_backend( window ) == GFX_WINDOW_SDL ? qtrue : qfalse;
	gfxInput.lastKeyDown = 0;

	// IN_Init has registered them
	gfxInput.keyboardDebug = Cvar_Get( "in_keyboardDebug", "0", CVAR_ARCHIVE );
	gfxInput.nograb = Cvar_Get( "in_nograb", "0", CVAR_ARCHIVE );

	gfx_window_set_event_handler( window, IN_GfxQueueEvent );
}

/*
===============
IN_GfxHasFocus
===============
*/
qboolean IN_GfxHasFocus( void )
{
	return gfxInput.focused;
}

/*
===============
IN_GfxPrintKey

IN_PrintKey for a gfx key: the gfx key code and modifiers in place of SDL's scancode and sym.
===============
*/
static void IN_GfxPrintKey( enum gfx_key_code gfxKey, uint32_t mods, keyNum_t key, qboolean down )
{
	if( down )
		Com_Printf( "+ " );
	else
		Com_Printf( "  " );

	Com_Printf( "gfx key: %d", (int)gfxKey );

	if( mods & GFX_KEY_MOD_SHIFT )     Com_Printf( " GFX_KEY_MOD_SHIFT" );
	if( mods & GFX_KEY_MOD_CONTROL )   Com_Printf( " GFX_KEY_MOD_CONTROL" );
	if( mods & GFX_KEY_MOD_ALT )       Com_Printf( " GFX_KEY_MOD_ALT" );
	if( mods & GFX_KEY_MOD_SUPER )     Com_Printf( " GFX_KEY_MOD_SUPER" );
	if( mods & GFX_KEY_MOD_NUM_LOCK )  Com_Printf( " GFX_KEY_MOD_NUM_LOCK" );
	if( mods & GFX_KEY_MOD_CAPS_LOCK ) Com_Printf( " GFX_KEY_MOD_CAPS_LOCK" );

	Com_Printf( " Q:0x%02x(%s)\n", key, Key_KeynumToString( key ) );
}

/*
===============
IN_GfxKeyToQ3Key

The K_* (or lowercase ASCII) key IN_TranslateSDLToQ3Key gives for the SDL key gfx made gfxKey
from; 0 for the keys the SDL path has no key for either.
===============
*/
static keyNum_t IN_GfxKeyToQ3Key( enum gfx_key_code gfxKey )
{
	// letters and digits: SDL's keysym.sym is their lowercase ASCII
	if( gfxKey >= GFX_KEY_A && gfxKey <= GFX_KEY_Z )
		return 'a' + ( gfxKey - GFX_KEY_A );
	if( gfxKey >= GFX_KEY_0 && gfxKey <= GFX_KEY_9 )
		return '0' + ( gfxKey - GFX_KEY_0 );

	// SDLK_F1 .. SDLK_F15; F16 and up have no case there
	if( gfxKey >= GFX_KEY_F1 && gfxKey <= GFX_KEY_F15 )
		return K_F1 + ( gfxKey - GFX_KEY_F1 );

	switch( gfxKey )
	{
		case GFX_KEY_KP_0:        return K_KP_INS;
		case GFX_KEY_KP_1:        return K_KP_END;
		case GFX_KEY_KP_2:        return K_KP_DOWNARROW;
		case GFX_KEY_KP_3:        return K_KP_PGDN;
		case GFX_KEY_KP_4:        return K_KP_LEFTARROW;
		case GFX_KEY_KP_5:        return K_KP_5;
		case GFX_KEY_KP_6:        return K_KP_RIGHTARROW;
		case GFX_KEY_KP_7:        return K_KP_HOME;
		case GFX_KEY_KP_8:        return K_KP_UPARROW;
		case GFX_KEY_KP_9:        return K_KP_PGUP;
		case GFX_KEY_KP_DIVIDE:   return K_KP_SLASH;
		case GFX_KEY_KP_MULTIPLY: return K_KP_STAR;
		case GFX_KEY_KP_SUBTRACT: return K_KP_MINUS;
		case GFX_KEY_KP_ADD:      return K_KP_PLUS;
		case GFX_KEY_KP_DECIMAL:  return K_KP_DEL;
		case GFX_KEY_KP_ENTER:    return K_KP_ENTER;
		// SDLK_KP_EQUALS has no case in the SDL path (and a scancode-masked sym): no key

		case GFX_KEY_LSHIFT:
		case GFX_KEY_RSHIFT:      return K_SHIFT;
		case GFX_KEY_LCONTROL:
		case GFX_KEY_RCONTROL:    return K_CTRL;
		case GFX_KEY_LALT:
		case GFX_KEY_RALT:        return K_ALT;
#ifdef __APPLE__
		case GFX_KEY_LSUPER:
		case GFX_KEY_RSUPER:      return K_COMMAND;
#else
		case GFX_KEY_LSUPER:
		case GFX_KEY_RSUPER:      return K_SUPER;
#endif

		case GFX_KEY_LEFT:        return K_LEFTARROW;
		case GFX_KEY_RIGHT:       return K_RIGHTARROW;
		case GFX_KEY_UP:          return K_UPARROW;
		case GFX_KEY_DOWN:        return K_DOWNARROW;

		case GFX_KEY_SPACE:       return K_SPACE;
		case GFX_KEY_BACKSPACE:   return K_BACKSPACE;
		case GFX_KEY_ENTER:       return K_ENTER;
		case GFX_KEY_TAB:         return K_TAB;

		case GFX_KEY_ESCAPE:      return K_ESCAPE;
		case GFX_KEY_PAUSE:       return K_PAUSE;
		case GFX_KEY_DELETE:      return K_DEL;
		case GFX_KEY_INSERT:      return K_INS;
		case GFX_KEY_HOME:        return K_HOME;
		case GFX_KEY_PAGE_UP:     return K_PGUP;
		case GFX_KEY_PAGE_DOWN:   return K_PGDN;
		case GFX_KEY_END:         return K_END;

		// the US-layout punctuation keys: SDL's sym is their ASCII ("These happen to match the
		// ASCII chars")
		case GFX_KEY_COMMA:       return ',';
		case GFX_KEY_PERIOD:      return '.';
		case GFX_KEY_SLASH:       return '/';
		case GFX_KEY_APOSTROPHE:  return '\'';
		case GFX_KEY_SEMICOLON:   return ';';
		case GFX_KEY_GRAVE:       return '`';
		case GFX_KEY_LBRACKET:    return '[';
		case GFX_KEY_RBRACKET:    return ']';
		case GFX_KEY_BACKSLASH:   return '\\';
		case GFX_KEY_EQUAL:       return '=';
		case GFX_KEY_SUBTRACT:    return '-';

		case GFX_KEY_SCROLL_LOCK: return K_SCROLLOCK;
		case GFX_KEY_NUM_LOCK:    return K_KP_NUMLOCK;
		case GFX_KEY_CAPS_LOCK:   return K_CAPSLOCK;
		case GFX_KEY_PRINT:       return K_PRINT;

		default:                  return 0;
	}
}

/*
===============
IN_TranslateGfxToQ3Key

IN_TranslateSDLToQ3Key for a gfx key: the key map, the keyboard debug print, the console keys.
===============
*/
static keyNum_t IN_TranslateGfxToQ3Key( enum gfx_key_code gfxKey, uint32_t mods, qboolean down )
{
	keyNum_t key = IN_GfxKeyToQ3Key( gfxKey );

	if( gfxInput.keyboardDebug->integer )
		IN_GfxPrintKey( gfxKey, mods, key, down );

	if( IN_IsConsoleKey( key, 0 ) )
	{
		// Console keys can't be bound or generate characters
		key = K_CONSOLE;
	}

	return key;
}

/*
===============
IN_GfxGobbleMotionEvents

IN_GobbleMotionEvents on gfx: pump the window, drop the pointer moves it queued (keeping every
other event in order) and measure the next move from where gfx now puts the pointer.
===============
*/
static void IN_GfxGobbleMotionEvents( struct gfx_window *window )
{
	int i, kept = 0;

	gfx_window_poll_events( window );

	for( i = 0; i < gfxInput.len; i++ )
	{
		const struct gfx_event *event = &gfxInput.queue[( gfxInput.head + i ) & GFX_EVENT_QUEUE_MASK];

		if( event->type != GFX_EVENT_MOUSE_MOVE )
		{
			gfxInput.queue[( gfxInput.head + kept ) & GFX_EVENT_QUEUE_MASK] = *event;
			kept++;
		}
	}
	gfxInput.len = kept;

	IN_GfxResetMousePosition( window );
}

/*
===============
IN_GfxActivateMouse

IN_ActivateMouse on gfx: gfx_grab_cursor is SDL's relative mode plus window grab (gfx hides the
cursor, confines it and reports the motion as a virtual position).
===============
*/
void IN_GfxActivateMouse( struct gfx_window *window, qboolean isFullscreen, qboolean mouseActive )
{
	if( !mouseActive )
	{
		gfx_grab_cursor( window );

		IN_GfxGobbleMotionEvents( window );
	}

	// in_nograb makes no sense in fullscreen mode
	if( !isFullscreen )
	{
		if( gfxInput.nograb->modified || !mouseActive )
		{
			if( gfxInput.nograb->integer )
				gfx_ungrab_cursor( window );
			else
				gfx_grab_cursor( window );
			IN_GfxResetMousePosition( window );

			gfxInput.nograb->modified = qfalse;
		}
	}
}

/*
===============
IN_GfxDeactivateMouse

IN_DeactivateMouse on gfx, for an active mouse: gfx_ungrab_cursor shows the cursor again and puts
it back where the grab started. The SDL path warps it to the window center instead; gfx's
gfx_set_mouse_position takes screen coordinates on the win32 window and window coordinates on
the others (GFX_STATUS.md, gfx gaps), so gfx's own placement is kept, as in ioq3-rs.
===============
*/
void IN_GfxDeactivateMouse( struct gfx_window *window )
{
	IN_GfxGobbleMotionEvents( window );

	gfx_ungrab_cursor( window );
	IN_GfxResetMousePosition( window );
}

/*
===============
IN_GfxButtonToQ3Key

The K_MOUSE / K_AUX key of a gfx mouse button, as the SDL path numbers SDL's buttons. gfx's
buttons past middle are SDL's button numbers less one on its win32 and glfw windows, and SDL's
own numbers on its sdl window (GFX_STATUS.md, gfx gaps).
===============
*/
static keyNum_t IN_GfxButtonToQ3Key( enum gfx_mouse_button button )
{
	int sdlButton;

	switch( button )
	{
		case GFX_MOUSE_BUTTON_LEFT:   return K_MOUSE1;
		case GFX_MOUSE_BUTTON_MIDDLE: return K_MOUSE3;
		case GFX_MOUSE_BUTTON_RIGHT:  return K_MOUSE2;
		default:                      break;
	}

	sdlButton = gfxInput.sdlWindow ? (int)button : (int)button + 1;

	switch( sdlButton )
	{
		case GFX_SDL_BUTTON_X1: return K_MOUSE4;
		case GFX_SDL_BUTTON_X2: return K_MOUSE5;
		default:                return K_AUX1 + ( sdlButton - GFX_SDL_BUTTON_X2 + 1 ) % 16;
	}
}

/*
===============
IN_GfxKeyDown

A key down from gfx, a repeat included: the SDL_KEYDOWN case of IN_ProcessEvents.
===============
*/
static void IN_GfxKeyDown( int eventTime, enum gfx_key_code gfxKey, uint32_t mods, qboolean repeat )
{
	keyNum_t key;

	if( repeat && Key_GetCatcher( ) == 0 )
		return;

	if( ( key = IN_TranslateGfxToQ3Key( gfxKey, mods, qtrue ) ) )
		Com_QueueEvent( eventTime, SE_KEY, key, qtrue, 0, NULL );

	if( key == K_BACKSPACE )
		Com_QueueEvent( eventTime, SE_CHAR, CTRL('h'), 0, 0, NULL );
	else if( keys[K_CTRL].down && key >= 'a' && key <= 'z' )
		Com_QueueEvent( eventTime, SE_CHAR, CTRL(key), 0, 0, NULL );

	gfxInput.lastKeyDown = key;
}

/*
===============
IN_GfxKeyUp

A key up from gfx: the SDL_KEYUP case of IN_ProcessEvents.
===============
*/
static void IN_GfxKeyUp( int eventTime, enum gfx_key_code gfxKey, uint32_t mods )
{
	keyNum_t key;

	if( ( key = IN_TranslateGfxToQ3Key( gfxKey, mods, qfalse ) ) )
		Com_QueueEvent( eventTime, SE_KEY, key, qfalse, 0, NULL );

	gfxInput.lastKeyDown = 0;
}

/*
===============
IN_GfxChar

A character from gfx: the SDL_TEXTINPUT case of IN_ProcessEvents for one code point. gfx has
already decoded the UTF-8 (sdl) or UTF-16 (win32); the control characters win32's WM_CHAR also
delivers (backspace, enter, escape, CTRL-letters) are dropped, since SDL's text input never has
them and the key case queues the ones the console needs.
===============
*/
static void IN_GfxChar( int eventTime, uint32_t codepoint )
{
	int utf32 = (int)codepoint;

	if( gfxInput.lastKeyDown == K_CONSOLE )
		return;

	if( utf32 < ' ' || utf32 == K_BACKSPACE )
		return;

	if( IN_IsConsoleKey( 0, utf32 ) )
	{
		Com_QueueEvent( eventTime, SE_KEY, K_CONSOLE, qtrue, 0, NULL );
		Com_QueueEvent( eventTime, SE_KEY, K_CONSOLE, qfalse, 0, NULL );
	}
	else
		Com_QueueEvent( eventTime, SE_CHAR, utf32, 0, 0, NULL );
}

/*
===============
IN_GfxReleaseKeys

The window lost the focus: release the keys gfx reported down. SDL resets its keyboard state on
focus loss and sends their key ups; gfx keeps them down and sends nothing, so without this a key
held while alt-tabbing away (alt itself) would stay down in the client.
===============
*/
static void IN_GfxReleaseKeys( int eventTime )
{
	int gfxKey;

	for( gfxKey = 0; gfxKey < GFX_KEY_LAST; gfxKey++ )
	{
		if( gfxInput.keysDown[gfxKey] )
		{
			gfxInput.keysDown[gfxKey] = qfalse;
			IN_GfxKeyUp( eventTime, (enum gfx_key_code)gfxKey, 0 );
		}
	}
}

/*
===============
IN_GfxResize

A window resize from gfx: the SDL_WINDOWEVENT_RESIZED case of IN_ProcessEvents, plus minimize.
gfx reports a size change of any origin (the initial size at show, display scaling) where SDL's
RESIZED is the user's alone, so only a size other than the last one counts, and only when
r_allowResize lets the user resize the window, as it does for the SDL window. win32 reports a
minimized window as 0x0 (gfx has no minimize event). qtrue when the delayed vid_restart is due.
===============
*/
static qboolean IN_GfxResize( unsigned int width, unsigned int height )
{
	if( width == 0 || height == 0 )
	{
		Cvar_SetValue( "com_minimized", 1 );
		return qfalse;
	}
	if( Cvar_VariableIntegerValue( "com_minimized" ) )
		Cvar_SetValue( "com_minimized", 0 );

	if( gfxInput.width == width && gfxInput.height == height )
		return qfalse;
	gfxInput.width = width;
	gfxInput.height = height;

	if( !Cvar_VariableIntegerValue( "r_allowResize" ) )
		return qfalse;

	// ignore this event on fullscreen
	if( cls.glconfig.isFullscreen )
		return qfalse;

	// check if size actually changed
	if( cls.glconfig.vidWidth == (int)width && cls.glconfig.vidHeight == (int)height )
		return qfalse;

	Cvar_SetValue( "r_customwidth", width );
	Cvar_SetValue( "r_customheight", height );
	Cvar_Set( "r_mode", "-1" );

	return qtrue;
}

/*
===============
IN_GfxProcessEvents

IN_ProcessEvents on gfx: pump the window, then hand each queued event to the client as the SDL
path does - key and char events, mouse motion as deltas while the mouse is active, buttons and
wheel as keys, focus as com_unfocused, resize, close as quit.
===============
*/
qboolean IN_GfxProcessEvents( struct gfx_window *window, int eventTime, qboolean mouseActive )
{
	struct gfx_event e;
	qboolean restart = qfalse;

	gfx_window_poll_events( window );

	if( gfxInput.dropped )
	{
		Com_DPrintf( "IN_ProcessEvents: %d gfx events dropped\n", gfxInput.dropped );
		gfxInput.dropped = 0;
	}

	while( IN_GfxNextEvent( &e ) )
	{
		switch( e.type )
		{
			case GFX_EVENT_KEY_DOWN:
			case GFX_EVENT_KEY_PRESS:
				{
					qboolean repeat;

					if( (unsigned int)e.key.key >= GFX_KEY_LAST )
						break;

					// a repeat of a key the client was told is up (released on focus loss and
					// still held when the focus came back) is its press
					repeat = ( e.type == GFX_EVENT_KEY_PRESS && gfxInput.keysDown[e.key.key] ) ? qtrue : qfalse;
					gfxInput.keysDown[e.key.key] = qtrue;
					IN_GfxKeyDown( eventTime, e.key.key, e.key.mods, repeat );
				}
				break;

			case GFX_EVENT_KEY_UP:
				// already released on focus loss
				if( (unsigned int)e.key.key >= GFX_KEY_LAST || !gfxInput.keysDown[e.key.key] )
					break;

				gfxInput.keysDown[e.key.key] = qfalse;
				IN_GfxKeyUp( eventTime, e.key.key, e.key.mods );
				break;

			case GFX_EVENT_CHARACTER:
				IN_GfxChar( eventTime, e.character.codepoint );
				break;

			case GFX_EVENT_MOUSE_MOVE:
				{
					int dx = e.pointer.x - gfxInput.mouseX;
					int dy = e.pointer.y - gfxInput.mouseY;

					gfxInput.mouseX = e.pointer.x;
					gfxInput.mouseY = e.pointer.y;

					if( mouseActive )
					{
						if( !dx && !dy )
							break;
						Com_QueueEvent( eventTime, SE_MOUSE, dx, dy, 0, NULL );
					}
				}
				break;

			case GFX_EVENT_MOUSE_DOWN:
			case GFX_EVENT_MOUSE_UP:
				Com_QueueEvent( eventTime, SE_KEY, IN_GfxButtonToQ3Key( e.mouse.button ),
					( e.type == GFX_EVENT_MOUSE_DOWN ? qtrue : qfalse ), 0, NULL );
				break;

			case GFX_EVENT_SCROLL:
				if( e.scroll.y > 0 )
				{
					Com_QueueEvent( eventTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
					Com_QueueEvent( eventTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
				}
				else if( e.scroll.y < 0 )
				{
					Com_QueueEvent( eventTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
					Com_QueueEvent( eventTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
				}
				break;

			case GFX_EVENT_FOCUS_IN:
				gfxInput.focused = qtrue;
				Cvar_SetValue( "com_unfocused", 0 );
				break;

			case GFX_EVENT_FOCUS_OUT:
				gfxInput.focused = qfalse;
				IN_GfxReleaseKeys( eventTime );
				Cvar_SetValue( "com_unfocused", 1 );
				break;

			case GFX_EVENT_RESIZE:
				if( IN_GfxResize( e.resize.width, e.resize.height ) )
					restart = qtrue;
				break;

			case GFX_EVENT_CLOSE:
				Cbuf_ExecuteText( EXEC_NOW, "quit Closed window\n" );
				break;

			default:
				break;
		}
	}

	return restart;
}
