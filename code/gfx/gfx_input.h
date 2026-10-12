/*
gfx_input.h - the client's keyboard, text, mouse and window events from renderer_gfx's gfx window
(new file, USE_GFX; no counterpart: it stands in for the window half of code/sdl/sdl_input.c).

sdl_input.c calls these while GfxGLimp_GetWindow() is not NULL and keeps its SDL code for the GL
renderers and for joysticks / gamepads. Include after "../client/client.h".
*/

#ifndef GFX_INPUT_H
#define GFX_INPUT_H

struct gfx_window;

// routes the window's events to the client and resets the state of the previous window
void		IN_GfxInit( struct gfx_window *window );

// gfx's answer to SDL_GetWindowFlags( window ) & SDL_WINDOW_INPUT_FOCUS
qboolean	IN_GfxHasFocus( void );

// IN_ActivateMouse / IN_DeactivateMouse on gfx, for a mouse that is available; the caller keeps
// mouseActive
void		IN_GfxActivateMouse( struct gfx_window *window, qboolean isFullscreen, qboolean mouseActive );
void		IN_GfxDeactivateMouse( struct gfx_window *window );

// IN_ProcessEvents on gfx: pumps the window and queues its events for the client; qtrue when a
// resize asks for the delayed vid_restart
qboolean	IN_GfxProcessEvents( struct gfx_window *window, int eventTime, qboolean mouseActive );

// sdl_input.c's console key test, shared with this file
qboolean	IN_IsConsoleKey( keyNum_t key, int character );

#endif
