/*
gfx_draw.h - renderer_gfx's draw submission on gfx (new file, no renderergl1 counterpart: it
stands for what the GL driver does with renderergl1's fixed-function draws). gfx_gl.c turns GL's
state at a draw into a gfxDrawState_t (a pipeline key, the uniforms, the textures) and the
vertices into gfxVertex_t; gfx_draw.c owns the gfx objects that draw them.
*/

#ifndef GFX_DRAW_H
#define GFX_DRAW_H

#include "../qcommon/q_shared.h"
#include "gfx/window.h"
#include "gfx/device.h"

// one vertex as the shaders read it (shaders/generic.vs.gfxs' inputs 0-3): 32 bytes, no padding
typedef struct {
	float		xyz[3];
	byte		rgba[4];
	float		st0[2];
	float		st1[2];
} gfxVertex_t;

// the Uniforms block of the shaders (std140: a mat4 and three vec4s)
typedef struct {
	float		mvp[16];		// projection * modelview, column-major as GL keeps them
	float		params[4];		// x: the alpha test (GFX_ALPHA_TEST_*), y: its reference,
								// z / w: unit 0 / 1's texture environment (GFX_TEX_ENV_*)
	float		clipPlane[4];	// GL_CLIP_PLANE0 in the vertices' coordinates (0 0 0 1 when off)
	float		depth[4];		// x: glPolygonOffset as a normalized-device depth bias (0 when
								// off); y: 1 when the device's clip space has z in [0, w] (D3D,
								// Vulkan), so generic.vs remaps GL's z; z, w unused
} gfxUniforms_t;

#define GFX_ALPHA_TEST_NONE		0.0f
#define GFX_ALPHA_TEST_GREATER	1.0f
#define GFX_ALPHA_TEST_LESS		2.0f
#define GFX_ALPHA_TEST_GEQUAL	3.0f

#define GFX_TEX_ENV_OFF			-1.0f
#define GFX_TEX_ENV_MODULATE	0.0f
#define GFX_TEX_ENV_REPLACE		1.0f
#define GFX_TEX_ENV_DECAL		2.0f
#define GFX_TEX_ENV_ADD			3.0f

// the primitive a draw is made of (immediate-mode strips, fans and quads become triangles)
typedef enum {
	GFX_PRIMITIVE_KEY_TRIANGLES,
	GFX_PRIMITIVE_KEY_LINES,
	GFX_PRIMITIVE_KEY_POINTS
} gfxPrimitiveKey_t;

/*
The pipeline key: GL's state at a draw, reduced to what gfx bakes into a pipeline, as gfx's enum
values. Every member is a byte, so a key has no padding and is compared and hashed as bytes; a
part that is off (blend, stencil) keeps its other bytes 0. Fill one with Com_Memset 0 first.
*/
typedef struct {
	byte		enabled;
	byte		src;			// gfx_blend_function, of the color (the alpha one is derived)
	byte		dst;
	byte		colorMask;		// gfx_color_mask bits
} gfxBlendKey_t;

typedef struct {
	byte		depthTest;
	byte		depthWrite;
	byte		depthFunc;		// gfx_compare_function
	byte		stencilEnabled;
	byte		stencilFunc;	// gfx_compare_function
	byte		stencilRef;
	byte		stencilFuncMask;
	byte		stencilWriteMask;
	byte		stencilFail;	// gfx_stencil_operation
	byte		stencilZFail;
	byte		stencilPass;
	byte		pad;
} gfxDepthStencilKey_t;

typedef struct {
	byte		cull;			// gfx_cull_mode
	byte		fill;			// gfx_fill_mode
	byte		scissor;
	byte		pad;
} gfxRasterizerKey_t;

typedef struct {
	gfxBlendKey_t		blend;
	gfxDepthStencilKey_t depthStencil;
	gfxRasterizerKey_t	rasterizer;
	byte				primitive;	// gfxPrimitiveKey_t
	byte				pad[3];
} gfxPipelineKey_t;

// GL's state at one draw, as gfx_gl.c hands it over
typedef struct {
	gfxPipelineKey_t	key;
	gfxUniforms_t		uniforms;
	struct gfx_texture	*textures[2];	// units 0 and 1 (NULL: a white texture)
	float				lineWidth;
} gfxDrawState_t;

qboolean GfxDraw_Init( struct gfx_device *device, enum gfx_device_backend deviceBackend,
	enum gfx_window_backend windowBackend, char *error, int errorSize );
void GfxDraw_Shutdown( void );
void GfxDraw_BeginFrame( void );
void GfxDraw_BindState( const gfxPipelineKey_t *key );
void GfxDraw_Draw( const gfxDrawState_t *state, const gfxVertex_t *vertices, int numVertices,
	uint32_t *indices, int numIndices );

#endif
