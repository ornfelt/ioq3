/*
gfx_shader.h - loads renderer_gfx's compiled gfx shaders (new file, no renderergl1 counterpart;
the port of ioq3-rs's code/gfx/gfx_shader.rs).
*/

#ifndef GFX_SHADER_H
#define GFX_SHADER_H

#include "../qcommon/q_shared.h"
#include "gfx/window.h"
#include "gfx/device.h"

// a linked vertex + fragment shader pair
typedef struct {
	struct gfx_shader		*vertex;
	struct gfx_shader		*fragment;
	struct gfx_shader_state	*state;
} gfxShaderProgram_t;

qboolean GfxShader_LoadProgram( struct gfx_device *device, enum gfx_device_backend deviceBackend,
	enum gfx_window_backend windowBackend, const char *vertex, const char *fragment,
	gfxShaderProgram_t *program, char *error, int errorSize );
void GfxShader_DeleteProgram( struct gfx_device *device, gfxShaderProgram_t *program );

#endif
