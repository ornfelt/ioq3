/*
gfx_gl.h - renderer_gfx's GL calls on the gfx device (new file). gfx_gl.c replaces the qgl
function pointers of code/renderercommon/qgl.h, and their loading in code/sdl/sdl_glimp.c, for
renderer_gfx: tr_local.h declares the qgl names as gfx_gl.c's functions, so the back end's call
sites stay as in renderergl1.
*/

#ifndef GFX_GL_H
#define GFX_GL_H

#include "tr_local.h"
#include "gfx_draw.h"

// the GL extensions renderer_gfx emulates: GLimp_Init and gfxinfo read this list as they read
// GL_EXTENSIONS
#define GFX_GL_EXTENSIONS	"GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_texture_env_add " \
	"GL_EXT_texture_filter_anisotropic GL_SGIS_texture_edge_clamp"

// texture units (GL_MAX_TEXTURE_UNITS_ARB): renderergl1 multitextures with two
#define GFX_GL_TEXTURE_UNITS		2
// GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: what every gfx device takes (gfx has no capability query)
#define GFX_GL_MAX_ANISOTROPY		16
// glPushMatrix depth of GL_MODELVIEW / GL_PROJECTION (GL guarantees 32 / 2; the renderer pushes
// at most one)
#define GFX_GL_MATRIX_STACK_DEPTH	4
// texture names (glGenTextures): one per image, with room for names freed and made again
#define GFX_GL_MAX_TEXTURES			( MAX_DRAWIMAGES * 2 )
// glBegin / glEnd vertices: R_DrawStripElements feeds at most a tess' indexes into one strip
#define GFX_GL_IMMEDIATE_VERTICES	SHADER_MAX_INDEXES
// mip levels of a texture: GL_MAX_TEXTURE_SIZE 16384 has 15
#define GFX_GL_MAX_LEVELS			16

// a client array (glVertexPointer / glColorPointer / glTexCoordPointer): where the caller's
// data is, read when a draw is issued
typedef struct {
	qboolean	enabled;
	GLint		size;
	GLenum		type;
	GLsizei		stride;
	const void	*ptr;
} gfxGlClientArray_t;

// the sampling a gfx texture is created with: GL's texture parameters
typedef struct {
	GLenum		wrapS;
	GLenum		wrapT;
	GLenum		minFilter;
	GLenum		magFilter;
	uint32_t	anisotropy;		// 0: off
} gfxGlSampling_t;

// a texture name and the parameters set on it
typedef struct {
	qboolean	live;
	GLsizei		width;
	GLsizei		height;
	GLenum		internalFormat;	// glTexImage2D's of level 0 (the RGBA8 upload is reduced to it)
	GLenum		wrapS;
	GLenum		wrapT;
	GLenum		minFilter;
	GLenum		magFilter;
	float		anisotropy;
	struct gfx_texture *texture;	// the gfx texture, made from the staged levels
	gfxGlSampling_t made;		// the sampling it was made with
} gfxGlTexture_t;

/*
The levels glTexImage2D gave the texture being uploaded, RGBA8, until its gfx texture is made:
renderergl1 sets filter and wrap after the images, so the gfx texture is made when another texture
is bound or uploaded, or at the next draw. One texture is staged at a time; the buffer grows to the
largest image and is kept until GLimp_Shutdown.
*/
typedef struct {
	GLuint		name;			// 0: nothing staged
	int			levels;
	int			levelOffsets[GFX_GL_MAX_LEVELS];
	int			used;			// bytes of the buffer the levels take
	byte		*buffer;
	int			size;
} gfxGlStaging_t;

// a matrix and its glPushMatrix stack
typedef struct {
	float		top[16];
	float		saved[GFX_GL_MATRIX_STACK_DEPTH][16];
	int			depth;
} gfxGlMatrixStack_t;

// GL's state as the back end has set it, and the gfx window and device it draws to
typedef struct {
	struct gfx_window	*window;
	struct gfx_device	*device;
	enum gfx_device_backend deviceBackend;
	// the device's clip space keeps z in [0, w] (D3D, Vulkan), not GL's [-w, w]: uDepth.y, with
	// which generic.vs remaps GL's projection
	qboolean	depthZeroToOne;
	GLint		maxTextureSize;
	GLenum		error;

	float		clearColor[4];
	float		clearDepth;
	byte		clearStencil;
	GLint		viewport[4];
	float		depthRange[2];
	GLint		scissor[4];

	unsigned	caps;			// GFX_GL_CAP_* bits
	qboolean	texture2D[GFX_GL_TEXTURE_UNITS];
	GLenum		blendSrc;
	GLenum		blendDst;
	GLenum		depthFunc;
	qboolean	depthMask;
	qboolean	colorMask[4];
	GLenum		cullFace;
	GLenum		alphaFunc;
	float		alphaRef;
	GLenum		polygonMode;
	float		polygonOffset[2];	// factor, units
	GLenum		stencilFunc;
	GLint		stencilRef;
	GLuint		stencilFuncMask;
	GLuint		stencilWriteMask;
	GLenum		stencilOp[3];		// fail, zfail, zpass
	float		clipPlane0[4];		// in eye coordinates, as GL keeps it
	float		lineWidth;

	GLenum		matrixMode;
	gfxGlMatrixStack_t modelview;
	gfxGlMatrixStack_t projection;
	float		mvp[16];			// projection * modelview, while mvpValid
	qboolean	mvpValid;

	int			activeTexture;
	int			clientActiveTexture;
	GLuint		boundTextures[GFX_GL_TEXTURE_UNITS];
	GLenum		texEnv[GFX_GL_TEXTURE_UNITS];
	gfxGlTexture_t textures[GFX_GL_MAX_TEXTURES];
	int			numTextures;	// names in use are below this
	gfxGlStaging_t staging;

	gfxGlClientArray_t vertexArray;
	gfxGlClientArray_t colorArray;
	gfxGlClientArray_t texCoordArrays[GFX_GL_TEXTURE_UNITS];
	qboolean	locked;			// glLockArraysEXT: the arrays stay the same until the unlock
	GLint		lockedFirst;
	GLsizei		lockedCount;

	qboolean	inBegin;		// between glBegin and glEnd
	GLenum		immediateMode;
	gfxVertex_t	immediate[GFX_GL_IMMEDIATE_VERTICES];
	int			numImmediate;
	vec4_t		currentColor;
	byte		currentRgba[4];	// currentColor as the vertices take it
	vec2_t		currentSt[GFX_GL_TEXTURE_UNITS];

	int			overbrightPasses;	// GLimp_SetGamma's overbright shift (drawn in phase 5)

	qboolean	warnedImmediate;
	qboolean	warnedReadPixels;
	qboolean	warnedResample;
	qboolean	warnedSubImage;
	qboolean	warnedDrawRange;
} gfxGl_t;

// GL capabilities (glEnable / glDisable) as bits of gfxGl.caps; GL_TEXTURE_2D is per unit
#define GFX_GL_CAP_BLEND				( 1 << 0 )
#define GFX_GL_CAP_DEPTH_TEST			( 1 << 1 )
#define GFX_GL_CAP_CULL_FACE			( 1 << 2 )
#define GFX_GL_CAP_ALPHA_TEST			( 1 << 3 )
#define GFX_GL_CAP_SCISSOR_TEST			( 1 << 4 )
#define GFX_GL_CAP_STENCIL_TEST			( 1 << 5 )
#define GFX_GL_CAP_POLYGON_OFFSET_FILL	( 1 << 6 )
#define GFX_GL_CAP_CLIP_PLANE0			( 1 << 7 )

extern gfxGl_t gfxGl;

qboolean GfxGL_Init( struct gfx_window *window, enum gfx_device_backend backend, char *error, int errorSize );
void GfxGL_Shutdown( void );
void GfxGL_BeginFrame( void );

// the GL_ARB_multitexture / GL_EXT_compiled_vertex_array entry points GLimp_Init puts into
// qgl.h's pointers (sdl_glimp.c gets them from the driver)
void APIENTRY GfxGL_ActiveTextureARB( GLenum texture );
void APIENTRY GfxGL_ClientActiveTextureARB( GLenum texture );
void APIENTRY GfxGL_MultiTexCoord2fARB( GLenum target, GLfloat s, GLfloat t );
void APIENTRY GfxGL_LockArraysEXT( GLint first, GLsizei count );
void APIENTRY GfxGL_UnlockArraysEXT( void );

#endif
