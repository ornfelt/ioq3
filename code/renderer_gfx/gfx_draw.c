/*
gfx_draw.c - renderer_gfx's draw submission on gfx (new file, no renderergl1 counterpart: it
stands for what the GL driver does with renderergl1's fixed-function draws; the port of
ioq3-rs's code/renderer_gfx/gfx_draw.rs). It owns the gfx objects every draw goes through:

- one shader program (shaders/generic.vs.gfxs / generic.fs.gfxs) for every draw: tr_shade does
  the per-vertex work on the CPU, so the shaders only transform, apply GL's texture environments
  per unit (a uniform turns a unit off; the unit then samples a white texture) and the alpha test.
  One program keeps the vertex code of every pass the same, which depthFunc GL_EQUAL passes need;
- one vertex layout (gfxVertex_t), its input layout and attributes state over the rings;
- ring buffers for vertices, indices and uniforms, created once at GLimp_Init (gfx's stream
  buffers: written in place, never reallocated). They wrap when full; their sizes keep many frames
  between a write and the next write to the same bytes, so the GPU is done with them;
- the pipeline cache: blend, depth-stencil and rasterizer states and pipelines created the first
  time a state combination is drawn, then found by their small keys (a hash of the pipeline key
  per draw, and no lookup at all for a draw with the key of the one before).

Nothing here allocates once GLimp_Init is done; the tables are fixed arrays.
*/

#include "tr_local.h"
#include "gfx_draw.h"
#include "gfx_shader.h"

// vertex ring: about 500000 vertices, far more than a frame of q3 draws
#define GFX_DRAW_VERTEX_RING_SIZE		( 16 << 20 )
// index ring: about 2 million indices
#define GFX_DRAW_INDEX_RING_SIZE		( 8 << 20 )
// uniform ring: tens of thousands of draws at the usual 256-byte uniform alignment
#define GFX_DRAW_UNIFORM_RING_SIZE		( 8 << 20 )

// the smallest uniform-offset alignment any gfx device asks for is at most this
#define GFX_DRAW_MIN_UNIFORM_STRIDE		256

// pipelines (a power of two, kept at most 3/4 full): q3's shaders make a few hundred keys at most
#define GFX_DRAW_PIPELINE_SLOTS			2048
#define GFX_DRAW_MAX_PIPELINES			( GFX_DRAW_PIPELINE_SLOTS / 4 * 3 )
// the states pipelines share, found by a linear search when a pipeline is made
#define GFX_DRAW_MAX_STATES				256

#define GFX_DRAW_VERTEX_SHADER			"generic"
#define GFX_DRAW_FRAGMENT_SHADER		"generic"

/*
A ring of one gfx stream buffer, written without a sync on every device (gfx's d3d11 device
appends with D3D11_MAP_WRITE_NO_OVERWRITE and renames the buffer when a write starts it over at
offset 0).
*/
typedef struct {
	struct gfx_buffer	*buffer;
	uint32_t			size;
	uint32_t			offset;
} gfxDrawRing_t;

typedef struct {
	gfxPipelineKey_t		key;
	struct gfx_pipeline		*pipeline;	// NULL: gfx refused the key, its draws are dropped
	qboolean				used;
} gfxDrawPipelineSlot_t;

typedef struct {
	gfxBlendKey_t			key;
	struct gfx_blend_state	*state;
} gfxDrawBlendState_t;

typedef struct {
	gfxDepthStencilKey_t	key;
	struct gfx_depth_stencil_state *state;
} gfxDrawDepthStencilState_t;

typedef struct {
	gfxRasterizerKey_t		key;
	struct gfx_rasterizer_state *state;
} gfxDrawRasterizerState_t;

typedef struct {
	struct gfx_device		*device;
	gfxShaderProgram_t		program;
	struct gfx_input_layout	*inputLayout;
	struct gfx_attributes_state *attributes;
	gfxDrawRing_t			vertices;
	gfxDrawRing_t			indices;
	gfxDrawRing_t			uniforms;
	uint32_t				uniformStride;

	// the uniforms of the last draw and where they went, reused while they stay the same
	qboolean				haveLastUniforms;
	gfxUniforms_t			lastUniforms;
	uint32_t				lastUniformsAt;

	// what this frame last bound: a draw with the same skips the lookup and the gfx call; reset
	// each frame (GfxDraw_BeginFrame) and by GfxDraw_BindState
	qboolean				pipelineBound;
	gfxPipelineKey_t		boundKey;
	qboolean				uniformsBound;
	uint32_t				boundUniformsAt;

	gfxDrawPipelineSlot_t	pipelines[GFX_DRAW_PIPELINE_SLOTS];
	int						numPipelines;
	gfxDrawBlendState_t		blendStates[GFX_DRAW_MAX_STATES];
	int						numBlendStates;
	gfxDrawDepthStencilState_t depthStencilStates[GFX_DRAW_MAX_STATES];
	int						numDepthStencilStates;
	gfxDrawRasterizerState_t rasterizerStates[GFX_DRAW_MAX_STATES];
	int						numRasterizerStates;
	qboolean				warnedFull;

	// a 1x1 white texture for a texture unit with nothing usable bound (GL samples an
	// incomplete texture as opaque black; renderergl1 always binds an image, so this only
	// covers errors and units that are off)
	struct gfx_texture		*white;
} gfxDraw_t;

static gfxDraw_t gfxDraw;

/*
==============================================================================

Rings

==============================================================================
*/

static qboolean GfxDraw_CreateRing( gfxDrawRing_t *ring, enum gfx_buffer_type type, uint32_t size )
{
	struct gfx_buffer_create_info info;

	Com_Memset( ring, 0, sizeof( *ring ) );
	info.type = type;
	info.data = NULL;
	info.size = size;
	info.usage = GFX_BUFFER_STREAM;
	if ( !gfx_create_buffer( gfxDraw.device, &info, &ring->buffer ) ) {
		ring->buffer = NULL;
		return qfalse;
	}
	ring->size = size;
	return qtrue;
}

static void GfxDraw_DeleteRing( gfxDrawRing_t *ring )
{
	if ( ring->buffer ) {
		gfx_delete_buffer( gfxDraw.device, ring->buffer );
	}
	Com_Memset( ring, 0, sizeof( *ring ) );
}

/*
===============
GfxDraw_Push

Writes size bytes at the next align-aligned offset of a ring, wrapping to the start when they do
not fit before the end; *at gets where they went. qfalse when they are larger than the ring.
===============
*/
static qboolean GfxDraw_Push( gfxDrawRing_t *ring, const void *data, uint32_t size, uint32_t align, uint32_t *at )
{
	uint32_t offset;

	if ( size > ring->size ) {
		return qfalse;
	}
	offset = ( ring->offset + align - 1 ) / align * align;
	if ( offset > ring->size - size ) {
		offset = 0;
	}
	gfx_set_buffer_data( gfxDraw.device, ring->buffer, data, size, offset );
	ring->offset = offset + size;
	*at = offset;
	return qtrue;
}

/*
==============================================================================

Init / shutdown

==============================================================================
*/

/*
===============
GfxDraw_CreateWhite
===============
*/
static qboolean GfxDraw_CreateWhite( void )
{
	static const byte white[4] = { 255, 255, 255, 255 };
	struct gfx_texture_create_info info;

	Com_Memset( &info, 0, sizeof( info ) );
	info.type = GFX_TEXTURE_2D;
	info.usage = GFX_TEXTURE_SAMPLED;
	info.format = GFX_FORMAT_R8G8B8A8_UNORM;
	info.levels = 1;
	info.width = 1;
	info.height = 1;
	info.depth = 1;
	info.addressing_s = GFX_TEXTURE_ADDRESSING_REPEAT;
	info.addressing_t = GFX_TEXTURE_ADDRESSING_REPEAT;
	info.addressing_r = GFX_TEXTURE_ADDRESSING_REPEAT;
	info.min_filtering = GFX_FILTERING_NEAREST;
	info.mag_filtering = GFX_FILTERING_NEAREST;
	info.mip_filtering = GFX_FILTERING_NONE;
	if ( !gfx_create_texture( gfxDraw.device, &info, &gfxDraw.white ) ) {
		gfxDraw.white = NULL;
		return qfalse;
	}
	gfx_set_texture_data( gfxDraw.device, gfxDraw.white, 0, 0, 1, 1, 1, sizeof( white ), white );
	return qtrue;
}

/*
===============
GfxDraw_CreateVertexInput

The input layout of the program (gfxVertex_t's fields in the shader's input order) and the
attributes state over the vertex and index rings
===============
*/
static qboolean GfxDraw_CreateVertexInput( void )
{
	struct gfx_input_layout_bind binds[4];
	struct gfx_input_layout_create_info layoutInfo;
	struct gfx_attribute_bind attributeBind;
	struct gfx_attributes_state_create_info attributesInfo;
	static const enum gfx_format formats[4] = {
		GFX_FORMAT_R32G32B32_SFLOAT, GFX_FORMAT_R8G8B8A8_UNORM, GFX_FORMAT_R32G32_SFLOAT, GFX_FORMAT_R32G32_SFLOAT
	};
	static const uint32_t offsets[4] = {
		offsetof( gfxVertex_t, xyz ), offsetof( gfxVertex_t, rgba ), offsetof( gfxVertex_t, st0 ), offsetof( gfxVertex_t, st1 )
	};
	int i;

	for ( i = 0; i < ARRAY_LEN( binds ); i++ ) {
		binds[i].buffer = 0;
		binds[i].format = formats[i];
		binds[i].stride = sizeof( gfxVertex_t );
		binds[i].offset = offsets[i];
		binds[i].step_mode = GFX_STEP_VERTEX;
	}
	layoutInfo.shader_state = gfxDraw.program.state;
	layoutInfo.binds = binds;
	layoutInfo.count = ARRAY_LEN( binds );
	if ( !gfx_create_input_layout( gfxDraw.device, &layoutInfo, &gfxDraw.inputLayout ) ) {
		gfxDraw.inputLayout = NULL;
		return qfalse;
	}

	attributeBind.buffer = gfxDraw.vertices.buffer;
	attributesInfo.binds = &attributeBind;
	attributesInfo.count = 1;
	attributesInfo.index_type = GFX_INDEX_UINT32;
	attributesInfo.index_buffer = gfxDraw.indices.buffer;
	if ( !gfx_create_attributes_state( gfxDraw.device, &attributesInfo, &gfxDraw.attributes ) ) {
		gfxDraw.attributes = NULL;
		return qfalse;
	}
	return qtrue;
}

/*
===============
GfxDraw_Init

Loads the shaders and creates the rings and the vertex input (GLimp_Init). qfalse: error names
what failed, and nothing is left made.
===============
*/
qboolean GfxDraw_Init( struct gfx_device *device, enum gfx_device_backend deviceBackend,
	enum gfx_window_backend windowBackend, char *error, int errorSize )
{
	Com_Memset( &gfxDraw, 0, sizeof( gfxDraw ) );
	gfxDraw.device = device;

	if ( !GfxDraw_CreateRing( &gfxDraw.vertices, GFX_BUFFER_VERTEXES, GFX_DRAW_VERTEX_RING_SIZE )
		|| !GfxDraw_CreateRing( &gfxDraw.indices, GFX_BUFFER_INDICES, GFX_DRAW_INDEX_RING_SIZE )
		|| !GfxDraw_CreateRing( &gfxDraw.uniforms, GFX_BUFFER_UNIFORM, GFX_DRAW_UNIFORM_RING_SIZE )
		|| !GfxDraw_CreateWhite() ) {
		Com_sprintf( error, errorSize, "cannot create the ring buffers" );
		GfxDraw_Shutdown();
		return qfalse;
	}

	if ( !GfxShader_LoadProgram( device, deviceBackend, windowBackend, GFX_DRAW_VERTEX_SHADER,
			GFX_DRAW_FRAGMENT_SHADER, &gfxDraw.program, error, errorSize ) ) {
		GfxDraw_Shutdown();
		return qfalse;
	}
	if ( !GfxDraw_CreateVertexInput() ) {
		Com_sprintf( error, errorSize, "cannot create the vertex input" );
		GfxDraw_Shutdown();
		return qfalse;
	}

	gfxDraw.uniformStride = MAX( gfx_get_uniform_buffer_size( device, sizeof( gfxUniforms_t ) ), GFX_DRAW_MIN_UNIFORM_STRIDE );
	return qtrue;
}

/*
===============
GfxDraw_Shutdown

Releases every gfx object, pipelines before the states and programs they use
===============
*/
void GfxDraw_Shutdown( void )
{
	struct gfx_device *device = gfxDraw.device;
	int i;

	if ( !device ) {
		return;
	}
	for ( i = 0; i < GFX_DRAW_PIPELINE_SLOTS; i++ ) {
		if ( gfxDraw.pipelines[i].pipeline ) {
			gfx_delete_pipeline( device, gfxDraw.pipelines[i].pipeline );
		}
	}
	for ( i = 0; i < gfxDraw.numBlendStates; i++ ) {
		gfx_delete_blend_state( device, gfxDraw.blendStates[i].state );
	}
	for ( i = 0; i < gfxDraw.numDepthStencilStates; i++ ) {
		gfx_delete_depth_stencil_state( device, gfxDraw.depthStencilStates[i].state );
	}
	for ( i = 0; i < gfxDraw.numRasterizerStates; i++ ) {
		gfx_delete_rasterizer_state( device, gfxDraw.rasterizerStates[i].state );
	}
	if ( gfxDraw.attributes ) {
		gfx_delete_attributes_state( device, gfxDraw.attributes );
	}
	if ( gfxDraw.inputLayout ) {
		gfx_delete_input_layout( device, gfxDraw.inputLayout );
	}
	GfxShader_DeleteProgram( device, &gfxDraw.program );
	if ( gfxDraw.white ) {
		gfx_delete_texture( device, gfxDraw.white );
	}
	GfxDraw_DeleteRing( &gfxDraw.vertices );
	GfxDraw_DeleteRing( &gfxDraw.indices );
	GfxDraw_DeleteRing( &gfxDraw.uniforms );

	Com_Memset( &gfxDraw, 0, sizeof( gfxDraw ) );
}

/*
===============
GfxDraw_BeginFrame

A new frame: nothing counts as bound any more (devices with per-frame command lists start with no
bindings)
===============
*/
void GfxDraw_BeginFrame( void )
{
	gfxDraw.pipelineBound = qfalse;
	gfxDraw.uniformsBound = qfalse;
}

/*
==============================================================================

The pipeline cache

==============================================================================
*/

/*
===============
GfxDraw_HashKey

FNV-1a over the key's bytes (it has no padding, gfx_draw.h)
===============
*/
static uint32_t GfxDraw_HashKey( const gfxPipelineKey_t *key )
{
	const byte *bytes = (const byte *)key;
	uint32_t hash = 2166136261u;
	int i;

	for ( i = 0; i < sizeof( *key ); i++ ) {
		hash = ( hash ^ bytes[i] ) * 16777619u;
	}
	return hash;
}

/*
===============
GfxDraw_BlendFunctionAlpha

A blend factor as it applies to the alpha channel: a color factor's alpha component is the
matching alpha factor's (GL_DST_COLOR scales alpha by Ad), and D3D takes only the latter for the
alpha blend
===============
*/
static enum gfx_blend_function GfxDraw_BlendFunctionAlpha( enum gfx_blend_function function )
{
	switch ( function ) {
	case GFX_BLEND_SRC_COLOR:			return GFX_BLEND_SRC_ALPHA;
	case GFX_BLEND_ONE_MINUS_SRC_COLOR:	return GFX_BLEND_ONE_MINUS_SRC_ALPHA;
	case GFX_BLEND_DST_COLOR:			return GFX_BLEND_DST_ALPHA;
	case GFX_BLEND_ONE_MINUS_DST_COLOR:	return GFX_BLEND_ONE_MINUS_DST_ALPHA;
	default:							return function;
	}
}

static struct gfx_blend_state *GfxDraw_BlendState( const gfxBlendKey_t *key )
{
	struct gfx_blend_state_create_info info;
	gfxDrawBlendState_t *entry;
	int i;

	for ( i = 0; i < gfxDraw.numBlendStates; i++ ) {
		if ( !memcmp( &gfxDraw.blendStates[i].key, key, sizeof( *key ) ) ) {
			return gfxDraw.blendStates[i].state;
		}
	}
	if ( gfxDraw.numBlendStates == GFX_DRAW_MAX_STATES ) {
		return NULL;
	}

	Com_Memset( &info, 0, sizeof( info ) );
	info.enabled = key->enabled != 0;
	info.src_color = key->enabled ? (enum gfx_blend_function)key->src : GFX_BLEND_ONE;
	info.dst_color = key->enabled ? (enum gfx_blend_function)key->dst : GFX_BLEND_ZERO;
	info.src_alpha = GfxDraw_BlendFunctionAlpha( info.src_color );
	info.dst_alpha = GfxDraw_BlendFunctionAlpha( info.dst_color );
	info.equation_color = GFX_EQUATION_ADD;
	info.equation_alpha = GFX_EQUATION_ADD;
	info.color_mask = (enum gfx_color_mask)key->colorMask;

	entry = &gfxDraw.blendStates[gfxDraw.numBlendStates];
	if ( !gfx_create_blend_state( gfxDraw.device, &info, &entry->state ) ) {
		return NULL;
	}
	entry->key = *key;
	gfxDraw.numBlendStates++;
	return entry->state;
}

static struct gfx_depth_stencil_state *GfxDraw_DepthStencilState( const gfxDepthStencilKey_t *key )
{
	struct gfx_depth_stencil_state_create_info info;
	struct gfx_stencil_create_info stencil;
	gfxDrawDepthStencilState_t *entry;
	int i;

	for ( i = 0; i < gfxDraw.numDepthStencilStates; i++ ) {
		if ( !memcmp( &gfxDraw.depthStencilStates[i].key, key, sizeof( *key ) ) ) {
			return gfxDraw.depthStencilStates[i].state;
		}
	}
	if ( gfxDraw.numDepthStencilStates == GFX_DRAW_MAX_STATES ) {
		return NULL;
	}

	Com_Memset( &stencil, 0, sizeof( stencil ) );
	if ( key->stencilEnabled ) {
		stencil.fail = (enum gfx_stencil_operation)key->stencilFail;
		stencil.zfail = (enum gfx_stencil_operation)key->stencilZFail;
		stencil.pass = (enum gfx_stencil_operation)key->stencilPass;
		stencil.compare_func = (enum gfx_compare_function)key->stencilFunc;
		stencil.compare_mask = key->stencilFuncMask;
		stencil.write_mask = key->stencilWriteMask;
		stencil.reference = key->stencilRef;
	} else {
		stencil.fail = GFX_STENCIL_KEEP;
		stencil.zfail = GFX_STENCIL_KEEP;
		stencil.pass = GFX_STENCIL_KEEP;
		stencil.compare_func = GFX_CMP_ALWAYS;
		stencil.compare_mask = 0xFF;
		stencil.write_mask = 0xFF;
	}

	Com_Memset( &info, 0, sizeof( info ) );
	info.depth_compare = (enum gfx_compare_function)key->depthFunc;
	info.depth_write = key->depthWrite != 0;
	info.depth_test = key->depthTest != 0;
	info.stencil_enabled = key->stencilEnabled != 0;
	info.front = stencil;
	info.back = stencil;

	entry = &gfxDraw.depthStencilStates[gfxDraw.numDepthStencilStates];
	if ( !gfx_create_depth_stencil_state( gfxDraw.device, &info, &entry->state ) ) {
		return NULL;
	}
	entry->key = *key;
	gfxDraw.numDepthStencilStates++;
	return entry->state;
}

static struct gfx_rasterizer_state *GfxDraw_RasterizerState( const gfxRasterizerKey_t *key )
{
	struct gfx_rasterizer_state_create_info info;
	gfxDrawRasterizerState_t *entry;
	int i;

	for ( i = 0; i < gfxDraw.numRasterizerStates; i++ ) {
		if ( !memcmp( &gfxDraw.rasterizerStates[i].key, key, sizeof( *key ) ) ) {
			return gfxDraw.rasterizerStates[i].state;
		}
	}
	if ( gfxDraw.numRasterizerStates == GFX_DRAW_MAX_STATES ) {
		return NULL;
	}

	Com_Memset( &info, 0, sizeof( info ) );
	info.fill_mode = (enum gfx_fill_mode)key->fill;
	info.cull_mode = (enum gfx_cull_mode)key->cull;
	info.front_face = GFX_FRONT_CCW;
	info.scissor = key->scissor != 0;
	info.depth_clamp = false;
	info.multisample = true;

	entry = &gfxDraw.rasterizerStates[gfxDraw.numRasterizerStates];
	if ( !gfx_create_rasterizer_state( gfxDraw.device, &info, &entry->state ) ) {
		return NULL;
	}
	entry->key = *key;
	gfxDraw.numRasterizerStates++;
	return entry->state;
}

static struct gfx_pipeline *GfxDraw_CreatePipeline( const gfxPipelineKey_t *key )
{
	struct gfx_pipeline_create_info info;
	struct gfx_pipeline *pipeline;
	static const enum gfx_primitive_type primitives[] = {
		GFX_PRIMITIVE_TRIANGLES, GFX_PRIMITIVE_LINES, GFX_PRIMITIVE_POINTS
	};

	Com_Memset( &info, 0, sizeof( info ) );
	info.shader_state = gfxDraw.program.state;
	info.blend_state = GfxDraw_BlendState( &key->blend );
	info.depth_stencil_state = GfxDraw_DepthStencilState( &key->depthStencil );
	info.rasterizer_state = GfxDraw_RasterizerState( &key->rasterizer );
	info.input_layout = gfxDraw.inputLayout;
	info.primitive = primitives[key->primitive];
	if ( !info.blend_state || !info.depth_stencil_state || !info.rasterizer_state ) {
		return NULL;
	}
	if ( !gfx_create_pipeline( gfxDraw.device, &info, &pipeline ) ) {
		return NULL;
	}
	return pipeline;
}

/*
===============
GfxDraw_Pipeline

The pipeline for a key, created with its states the first time; NULL for a key gfx refused
(reported once, then its draws are dropped) or when the cache is full
===============
*/
static struct gfx_pipeline *GfxDraw_Pipeline( const gfxPipelineKey_t *key )
{
	uint32_t slot = GfxDraw_HashKey( key ) & ( GFX_DRAW_PIPELINE_SLOTS - 1 );
	gfxDrawPipelineSlot_t *entry;

	for ( ;; ) {
		entry = &gfxDraw.pipelines[slot];
		if ( !entry->used ) {
			break;
		}
		if ( !memcmp( &entry->key, key, sizeof( *key ) ) ) {
			return entry->pipeline;
		}
		slot = ( slot + 1 ) & ( GFX_DRAW_PIPELINE_SLOTS - 1 );
	}

	if ( gfxDraw.numPipelines == GFX_DRAW_MAX_PIPELINES ) {
		if ( !gfxDraw.warnedFull ) {
			gfxDraw.warnedFull = qtrue;
			ri.Printf( PRINT_WARNING, "gfx: more than %d pipelines, the draws of new ones are dropped\n", GFX_DRAW_MAX_PIPELINES );
		}
		return NULL;
	}

	entry->used = qtrue;
	entry->key = *key;
	entry->pipeline = GfxDraw_CreatePipeline( key );
	gfxDraw.numPipelines++;
	if ( !entry->pipeline ) {
		ri.Printf( PRINT_WARNING, "gfx: cannot create a pipeline, its draws are dropped\n" );
	}
	return entry->pipeline;
}

/*
===============
GfxDraw_BindState

Binds the pipeline of a key without drawing: gfx applies its depth / color write masks and
scissor test, which glClear obeys (gfx_gl.c's qglClear)
===============
*/
void GfxDraw_BindState( const gfxPipelineKey_t *key )
{
	struct gfx_pipeline *pipeline;

	if ( !gfxDraw.device ) {
		return;
	}
	gfxDraw.pipelineBound = qfalse;
	pipeline = GfxDraw_Pipeline( key );
	if ( pipeline ) {
		gfx_bind_pipeline( gfxDraw.device, pipeline );
	}
}

/*
==============================================================================

Draws

==============================================================================
*/

/*
===============
GfxDraw_Draw

Draws numVertices vertices with a draw state: indexed by indices (numIndices of them, relative to
the first vertex; rebased here, in place, onto where the vertices went in the ring) when given,
else in order
===============
*/
void GfxDraw_Draw( const gfxDrawState_t *state, const gfxVertex_t *vertices, int numVertices,
	uint32_t *indices, int numIndices )
{
	struct gfx_device *device = gfxDraw.device;
	struct gfx_pipeline *pipeline = NULL;
	struct gfx_texture *textures[2];
	qboolean samePipeline;
	uint32_t vertexAt, indexAt = 0, uniformAt, baseVertex;
	int i;

	if ( !device || numVertices <= 0 ) {
		return;
	}
	samePipeline = gfxDraw.pipelineBound && !memcmp( &gfxDraw.boundKey, &state->key, sizeof( state->key ) );
	if ( !samePipeline ) {
		pipeline = GfxDraw_Pipeline( &state->key );
		if ( !pipeline ) {
			return;
		}
	}

	if ( !GfxDraw_Push( &gfxDraw.vertices, vertices, (uint32_t)numVertices * sizeof( gfxVertex_t ), sizeof( gfxVertex_t ), &vertexAt ) ) {
		return;
	}
	baseVertex = vertexAt / sizeof( gfxVertex_t );

	if ( indices ) {
		// gfx's draw has no base vertex (GFX_STATUS.md, gfx gaps)
		for ( i = 0; i < numIndices; i++ ) {
			indices[i] += baseVertex;
		}
		if ( !GfxDraw_Push( &gfxDraw.indices, indices, (uint32_t)numIndices * sizeof( uint32_t ), sizeof( uint32_t ), &indexAt ) ) {
			return;
		}
	}

	if ( gfxDraw.haveLastUniforms && !memcmp( &gfxDraw.lastUniforms, &state->uniforms, sizeof( state->uniforms ) ) ) {
		uniformAt = gfxDraw.lastUniformsAt;
	} else {
		if ( !GfxDraw_Push( &gfxDraw.uniforms, &state->uniforms, sizeof( state->uniforms ), gfxDraw.uniformStride, &uniformAt ) ) {
			return;
		}
		gfxDraw.haveLastUniforms = qtrue;
		gfxDraw.lastUniforms = state->uniforms;
		gfxDraw.lastUniformsAt = uniformAt;
	}

	if ( !samePipeline ) {
		gfx_bind_pipeline( device, pipeline );
		gfxDraw.pipelineBound = qtrue;
		gfxDraw.boundKey = state->key;
	}
	gfx_bind_attributes_state( device, gfxDraw.attributes, gfxDraw.inputLayout );
	if ( !gfxDraw.uniformsBound || gfxDraw.boundUniformsAt != uniformAt ) {
		gfx_bind_constant( device, 0, gfxDraw.uniforms.buffer, sizeof( gfxUniforms_t ), uniformAt );
		gfxDraw.uniformsBound = qtrue;
		gfxDraw.boundUniformsAt = uniformAt;
	}
	textures[0] = state->textures[0] ? state->textures[0] : gfxDraw.white;
	textures[1] = state->textures[1] ? state->textures[1] : gfxDraw.white;
	gfx_bind_samplers( device, 0, 2, textures );
	if ( state->key.primitive == GFX_PRIMITIVE_KEY_LINES ) {
		gfx_set_line_width( device, state->lineWidth );
	}
	if ( indices ) {
		gfx_draw_indexed( device, (uint32_t)numIndices, indexAt / sizeof( uint32_t ) );
	} else {
		gfx_draw( device, (uint32_t)numVertices, baseVertex );
	}
}
