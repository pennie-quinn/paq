/*
paq_aseprite.h - v1.01 - public domain Aseprite file loader
https://github.com/pennie-quinn/paq

	*** no warranty implied; use at your own risk ***

	Do this:
		#define ASE_IMPLEMENTATION
	before you include this file in _ONE_ C or C++ file to include
	the implementation.

	// i.e. something like this:
	#include ...
	#include ...
	#define ASE_IMPLEMENTATION
	#include "paq_aseprite.h"
	#include ...

	- You can #define ASE_ASSERT(x, msg) before the #include to avoid using
	  my assert.
	  - Or, you can #define ASE_NO_ASSERT if you're feeling lucky.

	- You can also #define ASE_MALLOC, ASE_REALLOC, and ASE_FREE to
	  avoid using malloc, realloc, and free.

	- You can #define several printf-style functions, if you want them:
	  - ASE_DBG(...) for verbose printouts
	  - ASE_ERR(...) for error messages

	- You can #define ASE_NO_STDIO if you don't want to load from files.

	- You can #define ASE_UserData_Cel my_typename if you want to extend
	  ASE_Cel without editing the source code. This is useful for adding a
	  texture handle, atlas offset, etc. to loaded Cels.
	  - You can do the same with #define ASE_UserData_Sprite


NOTES:
	Intended for game developers who use Aseprite and would like
	a simpler art asset pipeline during development.

	- Various features of Aseprite are _NOT_ implemented, including:
		- slices, masks, paths
		- deprecated features
		- editor features (i.e. tag colors)

	- Decode from a filepath, FILE*, or memory block.
	- Decode with arbitrary I/O callbacks (see: ASE_Callbacks)

	Full docs under "DOCUMENTATION" below.


CHANGELOG:
	- 1.01  (2018-01-19) added userdata support, more usage code,
	                    fixed some errors in the documentation.
	- 1.00  (2018-01-13) first release



===============================   CONTRIBUTORS   ==============================
Pennie Quinn
	- core functionality

Sean Barrett
	- I borrowed his public domain ZLIB decoder from stb_image
	  https://github.com/nothings/stb



==================================   THANKS   =================================
David Capello, for:
	- creating Aseprite, (imho) the best software for pixel artists
	- making the Aseprite source code available

Sean Barrett, for:
	- the ZLIB decoder used here
	- making some of the best C libs a girl could want
	- releasing so much good code to the public domain


LICENSE

This software is dual-licensed to the public domain and under the following
license: you are granted a perpetual, irrevocable license to copy, modify,
publish, and distribute this file as you see fit.
*/


#ifdef __cplusplus
extern "C" {
#endif

#ifndef PAQ_ASE_H


/*
=============================== DOCUMENTATION =================================

Basic Usage
	ASE_Sprite sprite;
	if (ASE_load("mysprite.ase", &sprite)) {
		// loading succeeded
	}

	...
	...

	// rendering
	if (pframe) {
		for (int i=0; i < pframe->ncels; ++i) {
			ASE_Cel cel = pframe->cels + i;
			f32   alpha = cel->opacity / 255.0f;

			if (cel->is_linked) cel = ASE_get_linked_cel(&sprite, cel);
			if (!ASE_check_cel_visible(&sprite, cel)) continue;

			// draw the cel
			//    texture data:    sprite.depth, cel->data, cel->w, cel->h
			//                     (possibly) sprite.palette
			//    cel draw offset: cel->x, cel->y

			// NOTE: if you did #define ASE_PROVIDE_UDATA, you may have set a
			//       texture handle or something in cel->udata
		}
	}

	...
	...

	ASE_free(&sprite);

NOTE (ASE_get_next_frame):
	If a tag's loop type is ASE_LOOP_PINGPONG, this function can return
	_negative numbers_. Treat these as offsets from tag->to, i.e.

	...
	...
	int frame_i = 0;
	ASE_Frame *frame = 0;
	...
	...
	frame_i = ASE_get_next_frame(tag, frame_i);
	if (frame_i < 0)
		frame_p = sprite.frames + (tag->to + frame_i);
	else
		frame_p = sprite.frames + frame_i;
	...
	...

See "primary API" below for full interface.


Cel Data:
	Cel data is left in the format dictated by Sprite->depth.

===============================================================================

I/O Callbacks

Callbacks let the decoder read from _whatever_ source you want.

Currently, all decoding paths allocate temporary buffers of whatever size is
needed with ASE_MALLOC or ASE_REALLOC, so there is probably some avoidable
memory overhead...

The functions you must provide for this are:

	int read (void *user, char *out, int size);
		- fill 'out' with 'size' bytes
		- returns the number of bytes read

	void skip (void *user, int nbytes);
		- skip 'nbytes' bytes

	int eof  (void *user);
		- return nonzero if we have reached the end of the stream

	int (*tell) (void *user);
		// same behavior as ftell

	void (*seek) (void *user, int pos);
		// same behavior as fseek with SEEK_SET

See: ASE_Callbacks

===============================================================================

File Format Reference
	https://github.com/aseprite/aseprite/blob/master/docs/ase-file-specs.md

	This reference is seemingly incomplete, so I recommend reading the
	official decoder source:

	https://github.com/aseprite/aseprite/blob/master/src/dio/aseprite_decoder.cpp
*/


#include <memory.h> // memcpy, memset
#include <stdint.h>

#ifndef ASE_NO_STDIO
#	include <stdio.h>
#endif


//////////////////////////////////////////////////////////////////////////////
// macros / config
//
#define ASE_DEBUG 0

#define ASE_BOOL int

#ifdef inline
#	define ASE_DECL inline
#else
#	define ASE_DECL extern inline
#endif

#ifndef ASE_DBG
#	if ASE_DEBUG && !defined(ASE_NO_STDIO)
#		define ASE_DBG(...) printf(__VA_ARGS__)
#	else
#		define ASE_DBG(...)
#	endif
#endif

#ifndef ASE_ERR
#	if ASE_DEBUG && !defined(ASE_NO_STDIO)
#		define ASE_ERR(...) printf(__VA_ARGS__)
#	else
#		define ASE_ERR(...)
#	endif
#endif

#if !defined(ASE_NO_ASSERT) && !defined(ASE_ASSERT)
#	define ASE_ASSERT(expr, msg) \
	do{                      \
	if(!(expr)) {            \
	fprintf(stderr,          \
		"ASSERT(%s):\n"      \
		"\t msg: %s\n"       \
		"\tfile: %s\n"       \
		"\tfunc: %s\n"       \
		"\tline: %i\n",      \
		#expr,               \
		msg,                 \
		__FILE__,            \
		__FUNCTION__,        \
		__LINE__);           \
	abort();                 \
	}}while(0)
#endif

#ifndef ASE_MALLOC
#	include <malloc.h>
#	define ASE_MALLOC malloc
#	define ASE_REALLOC realloc
#	define ASE_FREE free
#endif



//////////////////////////////////////////////////////////////////////////////
// flags, magic numbers
//
#define ASE_FILE_MAGIC                      0xA5E0
#define ASE_FILE_FRAME_MAGIC                0xF1FA

#define ASE_FILE_FLAG_LAYER_WITH_OPACITY    1

#define ASE_DEPTH_RGBA      32
#define ASE_DEPTH_GRAYSCALE 16
#define ASE_DEPTH_INDEXED   8

#define ASE_FILE_CHUNK_FLI_COLOR2           4
#define ASE_FILE_CHUNK_FLI_COLOR            11
#define ASE_FILE_CHUNK_LAYER                0x2004
#define ASE_FILE_CHUNK_CEL                  0x2005
#define ASE_FILE_CHUNK_CEL_EXTRA            0x2006
#define ASE_FILE_CHUNK_MASK                 0x2016
#define ASE_FILE_CHUNK_PATH                 0x2017
#define ASE_FILE_CHUNK_FRAME_TAGS           0x2018
#define ASE_FILE_CHUNK_PALETTE              0x2019
#define ASE_FILE_CHUNK_USER_DATA            0x2020
// Deprecated chunk (used on dev versions only
// between v1.2-beta7 and v1.2-beta8)
#define ASE_FILE_CHUNK_SLICES               0x2021
#define ASE_FILE_CHUNK_SLICE                0x2022

#define ASE_FILE_LAYER_IMAGE                0
#define ASE_FILE_LAYER_GROUP                1

#define ASE_FILE_RAW_CEL                    0
#define ASE_FILE_LINK_CEL                   1
#define ASE_FILE_COMPRESSED_CEL             2

#define ASE_PALETTE_FLAG_HAS_NAME           1

#define ASE_USER_DATA_FLAG_HAS_TEXT         1
#define ASE_USER_DATA_FLAG_HAS_COLOR        2

#define ASE_CEL_EXTRA_FLAG_PRECISE_BOUNDS   1

#define ASE_SLICE_FLAG_HAS_CENTER_BOUNDS    1
#define ASE_SLICE_FLAG_HAS_PIVOT_POINT      2

#define ASE_LAYER_VISIBLE         1
#define ASE_LAYER_EDITABLE        2
#define ASE_LAYER_LOCKMOVE        4
#define ASE_LAYER_BACKGROUND      8
#define ASE_LAYER_PREFER_LINKED   16
#define ASE_LAYER_GROUP_COLLAPSED 32
#define ASE_LAYER_REFERENCE       64

#define ASE_BLEND_NORMAL      0
#define ASE_BLEND_MULTIPLY    1
#define ASE_BLEND_SCREEN      2
#define ASE_BLEND_OVERLAY     3
#define ASE_BLEND_DARKEN      4
#define ASE_BLEND_LIGHTEN     5
#define ASE_BLEND_COLORDODGE  6
#define ASE_BLEND_COLORBURN   7
#define ASE_BLEND_HARDLIGHT   8
#define ASE_BLEND_SOFTLIGHT   9
#define ASE_BLEND_DIFFERENCE  10
#define ASE_BLEND_EXCLUSION   11
#define ASE_BLEND_HUE         12
#define ASE_BLEND_SATURATION  13
#define ASE_BLEND_COLOR       14
#define ASE_BLEND_LUMINOSITY  15
#define ASE_BLEND_ADDITION    16
#define ASE_BLEND_SUBTRACT    17
#define ASE_BLEND_DIVIDE      18

#define ASE_LOOP_FORWARD      0
#define ASE_LOOP_REVERSE      1
#define ASE_LOOP_PINGPONG     2



//////////////////////////////////////////////////////////////////////////////
// primary API - structs
//


// pixels ///////////////////////////////////////////////////////////////////
typedef union { // RGBA
	uint32_t rgba;
	struct {
		uint8_t r,g,b,a;
	};
	uint8_t E[4];
} ASE_Pixel32;

typedef union { // Grayscale
	uint16_t hex;
	struct {
		uint8_t k,a;
	};
	uint8_t  E[2];
} ASE_Pixel16;

typedef uint8_t ASE_Pixel8; // Indexed


// document //////////////////////////////////////////////////////////////////
typedef struct {
	uint8_t     ncolors;
	ASE_Pixel32 colors[256];
} ASE_Palette;

typedef struct {
	char *   name;
	uint16_t flags;
	uint16_t type;

	uint16_t blendmode;
	uint8_t  opacity;

	int child_level;
	int parent;

	int visible;
} ASE_Layer;

typedef struct {
	uint16_t  layer;
	int16_t   x;
	int16_t   y;
	int16_t   w;
	int16_t   h;
	uint8_t   opacity;
	uint8_t * data;

	int       is_linked;
	int       frame;

#ifdef ASE_UserData_Cel
	ASE_UserData_Cel user;
#endif

} ASE_Cel;

typedef struct {
	uint16_t  duration;
	int       ncels;    // hope they don't send me death threats! ;)
	ASE_Cel * cels;
} ASE_Frame;

typedef struct {
	int16_t from;
	int16_t to;
	int16_t dir;
	char  * name;
} ASE_Tag;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t depth;

	ASE_Palette palette;

	int         nlayers;
	ASE_Layer * layers;

	int         nframes;
	ASE_Frame * frames;

	int            ntags;
	ASE_Tag * tags;

#ifdef ASE_UserData_Sprite
	ASE_UserData_Sprite user;
#endif

} ASE_Sprite;



//////////////////////////////////////////////////////////////////////////////
// primary API - loading
//

//
// load by filename, FILE*, memory, or callbacks
//

typedef struct {
	int (*read) (void *user, char *out, int size);
		// fill 'out' with 'size' bytes
		// returns the number of bytes read

	void (*skip) (void *user, int nbytes);
		// skip 'nbytes' bytes
		// returns the number of bytes skipped

	int (*eof)  (void *user);
		// return nonzero if we have reached the end of the stream

	int (*tell) (void *user);
		// same as ftell

	void (*seek) (void *user, int pos);
		// same as fseek

} ASE_Callbacks;

#ifndef ASE_NO_STDIO
ASE_DECL ASE_BOOL ASE_load (const char *filename, ASE_Sprite *out);

ASE_DECL ASE_BOOL ASE_load_from_file (FILE *f, ASE_Sprite *out);
// the position of f will be left pointing immediately after the aseprite data.
#endif

ASE_DECL ASE_BOOL ASE_load_from_memory (const uint8_t *buffer, int len, ASE_Sprite *out);

ASE_DECL ASE_BOOL ASE_load_from_callbacks (const ASE_Callbacks *io, void *user, ASE_Sprite *out);

ASE_DECL void     ASE_free(ASE_Sprite *Sprite);



//////////////////////////////////////////////////////////////////////////////
// primary API - conveniences
//
ASE_DECL ASE_Layer *ASE_get_layer_by_name (ASE_Sprite *sprite, const char *name);
ASE_DECL ASE_Tag   *ASE_get_tag_by_name (ASE_Sprite *sprite, const char *name);
ASE_DECL int        ASE_get_next_frame (ASE_Tag *tag, int frame);
ASE_DECL ASE_Cel   *ASE_get_linked_cel (ASE_Sprite *sprite, ASE_Cel *cel);
ASE_DECL int        ASE_check_cel_visible (ASE_Sprite *sprite, ASE_Cel *cel);

#define PAQ_ASE_H
#endif




//////////////////////////////////////////////////////////////////////////////
//                                                                          //
//                              IMPLEMENTATION                              //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////
#ifdef ASE_IMPLEMENTATION


//////////////////////////////////////////////////////////////////////////////
// string ops
//
ASE_DECL int
ASE_streq(const char *a, const char *b)
{
	for (; *a && *b; ++a, ++b) if (*a != *b) return(0);
	if (*a && !(*b)) return(0);
	if (*b && !(*a)) return(0);
	return(1);
}



//////////////////////////////////////////////////////////////////////////////
// context struct and functions
//

// we want to load from different sources without a lot of code duplication

typedef struct {
	ASE_Callbacks io;
	void *udata;

	int read_callbacks;
	int buflen;

	uint8_t *buf;
	uint8_t *buf_end;
	uint8_t *buf_orig;
	uint8_t *buf_orig_end;
} ASE__ctx;


// init decode from callbacks
static void ASE__start_callbacks(ASE__ctx *ctx, ASE_Callbacks *cb, void *user)
{
	ctx->io = *cb;
	ctx->udata = user;
	ctx->read_callbacks = 1;
}



// memory interface
static int ASE__mem_read(void *user, char *data, int size)
{
	ASE__ctx *C = (ASE__ctx *)user;
	int count = 0;
	for(; count < size; ++count) {
		if (C->buf >= C->buf_end) break;
		*(data++) = *(C->buf++);
	}
	return(count);
}

static void ASE__mem_skip(void *user, int bytes)
{
	ASE__ctx *C = (ASE__ctx *)user;
	for(; bytes; --bytes, ++C->buf) {
		if (C->buf >= C->buf_end) break;
	}
}

static int ASE__mem_eof(void *user)
{
	ASE__ctx *C = (ASE__ctx *)user;
	return(C->buf >= C->buf_end);
}

static int ASE__mem_tell(void *user)
{
	ASE__ctx *C = (ASE__ctx *)user;
	return((int)(C->buf - C->buf_orig));
}

static void ASE__mem_seek(void *user, int pos)
{
	ASE__ctx *C = (ASE__ctx *)user;
	C->buf = C->buf_orig + pos;
}

static ASE_Callbacks ASE__mem_callbacks = {
	ASE__mem_read,
	ASE__mem_skip,
	ASE__mem_eof,
	ASE__mem_tell,
	ASE__mem_seek
};

static void ASE__start_mem(ASE__ctx *ctx, uint8_t *buf, int len)
{
	ASE__start_callbacks(ctx, &ASE__mem_callbacks, (void *)ctx);
	ctx->buf = ctx->buf_orig = buf;
	ctx->buf_end = ctx->buf_orig_end = buf + len;
}


// file interface
#ifndef ASE_NO_STDIO

static int ASE__file_read(void *user, char *data, int size)
{
	return((int)fread(data, 1, size, (FILE *)user));
}

static void ASE__file_skip(void *user, int bytes)
{
	fseek((FILE *)user, bytes, SEEK_CUR);
}

static int ASE__file_eof(void *user)
{
	return(feof((FILE *)user));
}

static int ASE__file_tell(void *user)
{
	return((int)ftell((FILE *)user));
}

static void ASE__file_seek(void *user, int pos)
{
	fseek((FILE *)user, pos, SEEK_SET);
}

static ASE_Callbacks ASE__file_callbacks = {
	ASE__file_read,
	ASE__file_skip,
	ASE__file_eof,
	ASE__file_tell,
	ASE__file_seek
};

// init decode from FILE
static void ASE__start_file(ASE__ctx *ctx, FILE *f)
{
	ASE__start_callbacks(ctx, &ASE__file_callbacks, (void *)f);
}

#endif // !ASE_NO_STDIO



//////////////////////////////////////////////////////////////////////////////
// generic reads
//
ASE__read8(ASE__ctx *c)
{
	uint8_t v;
	if (1 == c->io.read(c->udata, &v, 1)) return(v);
	return(0);
}

ASE__read16(ASE__ctx *c)
{
	uint8_t v[2];
	if (2 == c->io.read(c->udata, v, 2))
		return((v[1] << 8) | v[0]); // little-endian
	else return(0);
}

ASE__read32(ASE__ctx *c)
{
	uint8_t v[4];
	if (4 == c->io.read(c->udata, v, 4))
		return((v[3] << 24) | (v[2] << 16) | (v[1] << 8) | v[0]); // little-endian
	else return(0);
}



//////////////////////////////////////////////////////////////////////////////
// pixels
//
ASE_DECL ASE_Pixel32
ASE_Pixel32_make(int r, int g, int b, int a)
{
	ASE_Pixel32 R = {0};
	R.r = r;
	R.g = g;
	R.b = b;
	R.a = a;
	return(R);
}

ASE_DECL ASE_Pixel16
ASE_Pixel16_make(int k, int a)
{
	ASE_Pixel16 R = {0};
	R.k = k;
	R.a = a;
	return(R);
}



//////////////////////////////////////////////////////////////////////////////
// document types
//
typedef struct {
	uint32_t size;
    uint16_t magic;
    uint16_t frames;
    uint16_t width;
    uint16_t height;
    uint16_t depth;       // 32, 16, or 8
    uint32_t flags;
    uint16_t speed;       // DEPRECATED: use "duration" of FrameHeader
    uint32_t next;
    uint32_t frit;
    uint8_t  transparent_index;
    uint8_t  _ignore[3];
    uint16_t ncolors;
    uint8_t  pixel_w;
    uint8_t  pixel_h;
} ASE_DOC_Header;

typedef struct {
	uint32_t size;
    uint16_t magic;
    uint16_t chunks;
    uint16_t duration;
} ASE_FrameHeader;

typedef struct {
	uint32_t  size;
	uint16_t  type;
	size_t    start;
} ASE_DOC_ChunkHeader;

typedef struct {
	uint16_t flags;
	uint16_t type;
	uint16_t child_level;
	uint16_t default_w;
	uint16_t default_h;
	uint16_t blendmode;
	uint8_t  opacity;
	uint8_t  _ignore[3];
	char *   name;
} ASE_LayerHeader;



//////////////////////////////////////////////////////////////////////////////
// document funcs
//
ASE_DECL char *
ASE_DOC_read_string(ASE__ctx *F)
{
	char *S = 0;
	uint16_t Length = ASE__read16(F);

	if (Length != EOF) {
		S = (char *)ASE_MALLOC(Length + 1);
		S[Length] = '\0';
		for (int i=0; i<Length; ++i) S[i] = ASE__read8(F);
	}

	return(S);
}

ASE_DECL ASE_BOOL
ASE_DOC_Header_read(ASE__ctx *F, ASE_DOC_Header *O)
{
	size_t Pos = F->io.tell(F->udata);

	O->size = ASE__read32(F);
	O->magic = ASE__read16(F);
	O->frames = ASE__read16(F);
	O->width = ASE__read16(F);
	O->height = ASE__read16(F);
	O->depth = ASE__read16(F);
	O->flags = ASE__read32(F);
	O->speed = ASE__read16(F);
	O->next = ASE__read32(F);
	O->frit = ASE__read32(F);
	O->transparent_index = ASE__read8(F);

	F->io.skip(F->udata, 3);

	O->ncolors = ASE__read16(F);
	O->pixel_h = ASE__read8(F);
	O->pixel_w = ASE__read8(F);

	if (O->magic != ASE_FILE_MAGIC) {
		ASE_ERR("invalid magic: 0x%04x -- is this an Aseprite file?\n", (int)O->magic);
		return(0);
	}
	if (O->depth != ASE_DEPTH_RGBA &&
	    O->depth != ASE_DEPTH_GRAYSCALE &&
	    O->depth != ASE_DEPTH_INDEXED)
	{
		ASE_ERR("invalid depth given: %i\n", O->depth);
		return(0);
	}
	if (0 == O->ncolors) { // older file quirk
		O->ncolors = 256;
	}
	if (!O->pixel_w || !O->pixel_h) {
		O->pixel_w = O->pixel_h = 1;
	}

	F->io.seek(F->udata, Pos + 128);
	return(1);
}

ASE_DECL void
ASE_FrameHeader_read(ASE__ctx *F, ASE_FrameHeader *O)
{
	size_t Pos = F->io.tell(F->udata);

	O->size     = ASE__read32(F);
	O->magic    = ASE__read16(F);
	O->chunks   = ASE__read16(F);
	O->duration = ASE__read16(F);

	F->io.skip(F->udata, 6);
}



//////////////////////////////////////////////////////////////////////////////
// chunk reading
//
ASE_DECL void
ASE_DOC_ChunkHeader_read(ASE__ctx *F, ASE_DOC_ChunkHeader *O)
{
	size_t Pos = F->io.tell(F->udata);
	O->size = ASE__read32(F);
	O->type = ASE__read16(F);
	O->start = Pos + 6;
}

ASE_DECL ASE_Palette
ASE_Palette_read(ASE__ctx *F, ASE_Palette *Prev)
{
	ASE_Palette R = {0};
	memcpy(&R, Prev, sizeof(R));

	int NewSize = ASE__read32(F); // ignore
	int From    = ASE__read32(F);
	int To      = ASE__read32(F);
	F->io.skip(F->udata, 8);

	// since we have a static palette, we don't need to resize or anything.
	// newsize is irrelevant for us lol

	ASE_DBG("\t\t--- palette chunk ---\n");
	ASE_DBG("\t\tfrom:  %i\n", (int)From);
	ASE_DBG("\t\tto:    %i\n", (int)To);
	ASE_DBG("\t\t   i  | r | g | b | a \n"
			"\t\t  --------------------\n");

	for (int i=From; i <= To; ++i) {
		int Flags = ASE__read16(F);
		R.colors[R.ncolors++].rgba = ASE__read32(F);

		// windows balogna: byte order (sigh)
		{
			uint8_t tmp = R.colors[R.ncolors-1].r;
			R.colors[R.ncolors-1].r = R.colors[R.ncolors-1].b;
			R.colors[R.ncolors-1].b = tmp;
		}

		ASE_DBG("\t\t  %03i: %3i %3i %3i %3i\n", i,
			(int)R.colors[R.ncolors-1].r,
			(int)R.colors[R.ncolors-1].g,
			(int)R.colors[R.ncolors-1].b,
			(int)R.colors[R.ncolors-1].a);

		// skip name
		if (Flags & ASE_PALETTE_FLAG_HAS_NAME) {
			char *Name = ASE_DOC_read_string(F);
			ASE_DBG("\t\tname: %s\n", Name);
			ASE_FREE(Name);
		}
	}

	return(R);
}


//////////////////////////////////////////////////////////////////////////////
// layers
//
ASE_DECL ASE_Layer *
ASE_DOC_AddLayer(ASE_Sprite *S)
{
	S->layers = (ASE_Layer *)ASE_REALLOC(S->layers, ++S->nlayers * sizeof(ASE_Layer));
	ASE_Layer *L = S->layers + (S->nlayers - 1);
	memset(L, 0, sizeof(ASE_Layer));

	ASE_DBG("\t\tlayer index: %i\n", (int)S->nlayers-1);

	return(L);
}

ASE_DECL ASE_Layer *
ASE_Layer_read(ASE__ctx *F,
	               ASE_DOC_Header *Header,
				   ASE_Sprite *Sprite,
				   ASE_Layer **PrevLayer,
				   int *CurrentLevel)
{
	// LAYER HEADER
	ASE_LayerHeader H = {0};
	H.flags = ASE__read16(F);
	H.type  = ASE__read16(F);
	H.child_level = ASE__read16(F);
	H.default_w = ASE__read16(F);
	H.default_h = ASE__read16(F);
	H.blendmode = ASE__read16(F);
	H.opacity = ASE__read8(F);
	F->io.skip(F->udata, 3);
	H.name = ASE_DOC_read_string(F);

	ASE_DBG("\t\t--- layer chunk ---\n");
	ASE_DBG("\t\tname:      %s\n", H.name);
	ASE_DBG("\t\ttype:      0x%04x\n", (int)H.type);
	ASE_DBG("\t\tclevel:    %i\n", (int)H.child_level);
	ASE_DBG("\t\tdefault_w: %i\n", (int)H.default_w);
	ASE_DBG("\t\tdefault_h: %i\n", (int)H.default_h);
	ASE_DBG("\t\tblendmode: %i\n", (int)H.blendmode);
	ASE_DBG("\t\topacity:   %i\n", (int)H.opacity);

	// LAYER DATA
	ASE_Layer *Layer = 0;

	// TYPE
	switch (H.type) {
	case ASE_FILE_LAYER_IMAGE:
		{
			Layer = ASE_DOC_AddLayer(Sprite);

			// only transparent layers have blendmode, opacity
			if (!(H.flags & ASE_LAYER_BACKGROUND)) {
				Layer->blendmode = H.blendmode;
				Layer->opacity   = H.opacity;
			}

			ASE_DBG("\t\t--- image layer ---\n");
		} break;

	case ASE_FILE_LAYER_GROUP:
		{
			Layer = ASE_DOC_AddLayer(Sprite);
			ASE_DBG("\t\t--- layer group ---\n");
		} break;

		// ? No default case ?
	}

	// DECODE
	if (Layer) {
		Layer->name = H.name;
		Layer->flags = H.flags;
		Layer->type = H.type;
		Layer->child_level = H.child_level;

		Layer->visible = (H.flags & ASE_LAYER_VISIBLE);

		// parent (groups)
		if (0 == H.child_level) {
			Layer->parent = -1; // root
		} else if (*CurrentLevel == H.child_level) {
			Layer->parent = (*PrevLayer)->parent;
		} else if (H.child_level > *CurrentLevel) {
			// PREVIOUS LAYER IS GROUP, ADD THIS LAYER
			Layer->parent = Sprite->nlayers - 2;
		} else if (H.child_level < *CurrentLevel) {
			// WALK BACK UP LAYER CHAIN UNTIL LEVEL REACHED
			int Parent = (*PrevLayer)->parent;
			if (Parent >= 0) {
				int Levels = *CurrentLevel - H.child_level;
				int ThisLayer = Sprite->nlayers - 1;
				while (Levels--) {
					ASE_Layer *PL = Sprite->layers + Parent;
					if (PL->parent == -1) break;
					Parent = PL->parent;
				}
			}
		}

		*PrevLayer = Layer;
		*CurrentLevel = H.child_level;

		ASE_DBG("\t\tparent:        %s\n", (Layer->parent <= -1)? "root" : Sprite->layers[Layer->parent].name);
		ASE_DBG("\t\tlevel:         %i\n", (int)Layer->child_level);
		ASE_DBG("\t\tcurrent_level: %i\n", (int)*CurrentLevel);
	} else {
		ASE_FREE(H.name);
	}

	return(Layer);
}



//////////////////////////////////////////////////////////////////////////////
// frames
//
ASE_DECL ASE_Frame *
ASE_DOC_AddFrame(ASE_Sprite *S)
{
	S->frames = (ASE_Frame *)ASE_REALLOC(S->frames, ++S->nframes * sizeof(ASE_Frame));
	ASE_Frame *A = S->frames + (S->nframes - 1);
	memset(A, 0, sizeof(ASE_Frame));
	ASE_DBG("\t\tframe index: %i\n", (int)S->nframes-1);
	return(A);
}



//////////////////////////////////////////////////////////////////////////////
// cels
//
ASE_DECL ASE_Cel *
ASE_DOC_AddCel(ASE_Frame *S)
{
	S->cels = (ASE_Cel *)ASE_REALLOC(S->cels, ++S->ncels * sizeof(ASE_Cel));
	ASE_Cel *L = S->cels + (S->ncels - 1);
	memset(L, 0, sizeof(ASE_Cel));
	ASE_DBG("\t\tcel index: %i\n", (int)S->ncels-1);
	return(L);
}


ASE_DECL void
ASE_DOC_read_raw_image_rgba      (ASE__ctx *F, ASE_Cel *Cel)
{
	Cel->data = (uint8_t *)ASE_MALLOC(Cel->w * Cel->h * 4);
	uint8_t *P = (uint8_t *)Cel->data;
	for (int i=0; i < Cel->w * Cel->h; ++i) {
		*(P++) = ASE__read8(F);
		*(P++) = ASE__read8(F);
		*(P++) = ASE__read8(F);
		*(P++) = ASE__read8(F);
	}
	ASE_DBG("\t\t--- (LOADED) ---\n");
}

ASE_DECL void
ASE_DOC_read_raw_image_grayscale (ASE__ctx *F, ASE_Cel *Cel)
{
	Cel->data = (uint8_t *)ASE_MALLOC(Cel->w * Cel->h * 2);
	uint8_t *P = Cel->data;
	for (int i=0; i < Cel->w * Cel->h; ++i) {
		*(P++) = ASE__read8(F);
		*(P++) = ASE__read8(F);
	}
	ASE_DBG("\t\t--- (LOADED) ---\n");
}

ASE_DECL void
ASE_DOC_read_raw_image_indexed   (ASE__ctx *F, ASE_Cel *Cel)
{
	Cel->data = (uint8_t *)ASE_MALLOC(Cel->w * Cel->h);
	uint8_t *P = Cel->data;
	for (int i=0; i < Cel->w * Cel->h; ++i) *(P++) = ASE__read8(F);
	ASE_DBG("\t\t--- (LOADED) ---\n");
}



//////////////////////////////////////////////////////////////////////////////
// ZLIB
//
#if !defined(STBI_INCLUDE_STB_IMAGE_H) || defined(STBI_NO_ZLIB) || (defined(STBI_NO_PNG) && !defined(STBI_SUPPORT_ZLIB))

// public domain zlib decode    v0.2  Sean Barrett 2006-11-18
//    simple implementation
//      - all input must be provided in an upfront buffer
//      - all output is written to a single output buffer (can malloc/realloc)
//    performance
//      - fast huffman


// header /////////////////////////////////////////////////////////////////////
#ifdef STB_IMAGE_STATIC
#define STBIDEF static
#else
#define STBIDEF extern
#endif

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen);
STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header);
STBIDEF char *stbi_zlib_decode_malloc(const char *buffer, int len, int *outlen);
STBIDEF int   stbi_zlib_decode_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

STBIDEF char *stbi_zlib_decode_noheader_malloc(const char *buffer, int len, int *outlen);
STBIDEF int   stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);


// implementation /////////////////////////////////////////////////////////////
#include <stdarg.h>
#include <stddef.h> // ptrdiff_t on osx
#include <stdlib.h>
#include <string.h>

typedef unsigned char stbi_uc;

#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR)
#include <math.h>  // ldexp
#endif

#ifndef ASE_NO_STDIO
#include <stdio.h>
#endif

#ifndef STBI_ASSERT
#include <assert.h>
#define STBI_ASSERT(x) assert(x)
#endif

#ifndef _MSC_VER
   #ifdef __cplusplus
   #define stbi_inline inline
   #else
   #define stbi_inline
   #endif
#else
   #define stbi_inline __forceinline
#endif

#ifdef _MSC_VER
typedef unsigned short stbi__uint16;
typedef   signed short stbi__int16;
typedef unsigned int   stbi__uint32;
typedef   signed int   stbi__int32;
#else
#include <stdint.h>
typedef uint16_t stbi__uint16;
typedef int16_t  stbi__int16;
typedef uint32_t stbi__uint32;
typedef int32_t  stbi__int32;
#endif

// should produce compiler error if size is wrong
typedef unsigned char validate_uint32[sizeof(stbi__uint32)==4 ? 1 : -1];

#ifdef _MSC_VER
#define STBI_NOTUSED(v)  (void)(v)
#else
#define STBI_NOTUSED(v)  (void)sizeof(v)
#endif

#ifdef _MSC_VER
#define STBI_HAS_LROTL
#endif

#ifdef STBI_HAS_LROTL
   #define stbi_lrot(x,y)  _lrotl(x,y)
#else
   #define stbi_lrot(x,y)  (((x) << (y)) | ((x) >> (32 - (y))))
#endif

#if defined(STBI_MALLOC) && defined(STBI_FREE) && (defined(STBI_REALLOC) || defined(STBI_REALLOC_SIZED))
// ok
#elif !defined(STBI_MALLOC) && !defined(STBI_FREE) && !defined(STBI_REALLOC) && !defined(STBI_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBI_MALLOC, STBI_FREE, and STBI_REALLOC (or STBI_REALLOC_SIZED)."
#endif

#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           ASE_MALLOC(sz)
#define STBI_REALLOC(p,newsz)     ASE_REALLOC(p,newsz)
#define STBI_FREE(p)              ASE_FREE(p)
#endif

#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

// this is not threadsafe
static const char *stbi__g_failure_reason;

STBIDEF const char *stbi_failure_reason(void)
{
   return stbi__g_failure_reason;
}

static int stbi__err(const char *str)
{
   stbi__g_failure_reason = str;
   return 0;
}

static void *stbi__malloc(size_t size)
{
    return STBI_MALLOC(size);
}

// stbi__err - error
// stbi__errpf - error returning pointer to float
// stbi__errpuc - error returning pointer to unsigned char

#ifdef STBI_NO_FAILURE_STRINGS
   #define stbi__err(x,y)  0
#elif defined(STBI_FAILURE_USERMSG)
   #define stbi__err(x,y)  stbi__err(y)
#else
   #define stbi__err(x,y)  stbi__err(x)
#endif

#define stbi__errpf(x,y)   ((float *)(size_t) (stbi__err(x,y)?NULL:NULL))
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))


// x86/x64 detection
#if defined(__x86_64__) || defined(_M_X64)
#define STBI__X64_TARGET
#elif defined(__i386) || defined(_M_IX86)
#define STBI__X86_TARGET
#endif

#if defined(__GNUC__) && (defined(STBI__X86_TARGET) || defined(STBI__X64_TARGET)) && !defined(__SSE2__) && !defined(STBI_NO_SIMD)
// NOTE: not clear do we actually need this for the 64-bit path?
// gcc doesn't support sse2 intrinsics unless you compile with -msse2,
// (but compiling with -msse2 allows the compiler to use SSE2 everywhere;
// this is just broken and gcc are jerks for not fixing it properly
// http://www.virtualdub.org/blog/pivot/entry.php?id=363 )
#define STBI_NO_SIMD
#endif

#if defined(__MINGW32__) && defined(STBI__X86_TARGET) && !defined(STBI_MINGW_ENABLE_SSE2) && !defined(STBI_NO_SIMD)
// Note that __MINGW32__ doesn't actually mean 32-bit, so we have to avoid STBI__X64_TARGET
//
// 32-bit MinGW wants ESP to be 16-byte aligned, but this is not in the
// Windows ABI and VC++ as well as Windows DLLs don't maintain that invariant.
// As a result, enabling SSE2 on 32-bit MinGW is dangerous when not
// simultaneously enabling "-mstackrealign".
//
// See https://github.com/nothings/stb/issues/81 for more information.
//
// So default to no SSE2 on 32-bit MinGW. If you've read this far and added
// -mstackrealign to your build settings, feel free to #define STBI_MINGW_ENABLE_SSE2.
#define STBI_NO_SIMD
#endif

#if !defined(STBI_NO_SIMD) && (defined(STBI__X86_TARGET) || defined(STBI__X64_TARGET))
#define STBI_SSE2
#include <emmintrin.h>

#ifdef _MSC_VER

#if _MSC_VER >= 1400  // not VC6
#include <intrin.h> // __cpuid
static int stbi__cpuid3(void)
{
   int info[4];
   __cpuid(info,1);
   return info[3];
}
#else
static int stbi__cpuid3(void)
{
   int res;
   __asm {
      mov  eax,1
      cpuid
      mov  res,edx
   }
   return res;
}
#endif

#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name

static int stbi__sse2_available()
{
   int info3 = stbi__cpuid3();
   return ((info3 >> 26) & 1) != 0;
}
#else // assume GCC-style if not VC++
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))

static int stbi__sse2_available()
{
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 408 // GCC 4.8 or later
   // GCC 4.8+ has a nice way to do this
   return __builtin_cpu_supports("sse2");
#else
   // portable way to do this, preferably without using GCC inline ASM?
   // just bail for now.
   return 0;
#endif
}
#endif
#endif

// ARM NEON
#if defined(STBI_NO_SIMD) && defined(STBI_NEON)
#undef STBI_NEON
#endif

#ifdef STBI_NEON
#include <arm_neon.h>
// assume GCC or Clang on ARM targets
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))
#endif

#ifndef STBI_SIMD_ALIGN
#define STBI_SIMD_ALIGN(type, name) type name
#endif


// fast-way is faster to check than jpeg huffman, but slow way is slower
#define STBI__ZFAST_BITS  9 // accelerate all cases in default tables
#define STBI__ZFAST_MASK  ((1 << STBI__ZFAST_BITS) - 1)

// zlib-style huffman encoding
// (jpegs packs from left, zlib from right, so can't share code)
typedef struct
{
   stbi__uint16 fast[1 << STBI__ZFAST_BITS];
   stbi__uint16 firstcode[16];
   int maxcode[17];
   stbi__uint16 firstsymbol[16];
   stbi_uc  size[288];
   stbi__uint16 value[288];
} stbi__zhuffman;

stbi_inline static int stbi__bitreverse16(int n)
{
  n = ((n & 0xAAAA) >>  1) | ((n & 0x5555) << 1);
  n = ((n & 0xCCCC) >>  2) | ((n & 0x3333) << 2);
  n = ((n & 0xF0F0) >>  4) | ((n & 0x0F0F) << 4);
  n = ((n & 0xFF00) >>  8) | ((n & 0x00FF) << 8);
  return n;
}

stbi_inline static int stbi__bit_reverse(int v, int bits)
{
   STBI_ASSERT(bits <= 16);
   // to bit reverse n bits, reverse 16 and shift
   // e.g. 11 bits, bit reverse and shift away 5
   return stbi__bitreverse16(v) >> (16-bits);
}

static int stbi__zbuild_huffman(stbi__zhuffman *z, stbi_uc *sizelist, int num)
{
   int i,k=0;
   int code, next_code[16], sizes[17];

   // DEFLATE spec for generating codes
   memset(sizes, 0, sizeof(sizes));
   memset(z->fast, 0, sizeof(z->fast));
   for (i=0; i < num; ++i)
      ++sizes[sizelist[i]];
   sizes[0] = 0;
   for (i=1; i < 16; ++i)
      if (sizes[i] > (1 << i))
         return stbi__err("bad sizes", "Corrupt PNG");
   code = 0;
   for (i=1; i < 16; ++i) {
      next_code[i] = code;
      z->firstcode[i] = (stbi__uint16) code;
      z->firstsymbol[i] = (stbi__uint16) k;
      code = (code + sizes[i]);
      if (sizes[i])
         if (code-1 >= (1 << i)) return stbi__err("bad codelengths","Corrupt PNG");
      z->maxcode[i] = code << (16-i); // preshift for inner loop
      code <<= 1;
      k += sizes[i];
   }
   z->maxcode[16] = 0x10000; // sentinel
   for (i=0; i < num; ++i) {
      int s = sizelist[i];
      if (s) {
         int c = next_code[s] - z->firstcode[s] + z->firstsymbol[s];
         stbi__uint16 fastv = (stbi__uint16) ((s << 9) | i);
         z->size [c] = (stbi_uc     ) s;
         z->value[c] = (stbi__uint16) i;
         if (s <= STBI__ZFAST_BITS) {
            int j = stbi__bit_reverse(next_code[s],s);
            while (j < (1 << STBI__ZFAST_BITS)) {
               z->fast[j] = fastv;
               j += (1 << s);
            }
         }
         ++next_code[s];
      }
   }
   return 1;
}

// zlib-from-memory implementation for PNG reading
//    because PNG allows splitting the zlib stream arbitrarily,
//    and it's annoying structurally to have PNG call ZLIB call PNG,
//    we require PNG read all the IDATs and combine them into a single
//    memory buffer

typedef struct
{
   stbi_uc *zbuffer, *zbuffer_end;
   int num_bits;
   stbi__uint32 code_buffer;

   char *zout;
   char *zout_start;
   char *zout_end;
   int   z_expandable;

   stbi__zhuffman z_length, z_distance;
} stbi__zbuf;

stbi_inline static stbi_uc stbi__zget8(stbi__zbuf *z)
{
   if (z->zbuffer >= z->zbuffer_end) return 0;
   return *z->zbuffer++;
}

static void stbi__fill_bits(stbi__zbuf *z)
{
   do {
      STBI_ASSERT(z->code_buffer < (1U << z->num_bits));
      z->code_buffer |= (unsigned int) stbi__zget8(z) << z->num_bits;
      z->num_bits += 8;
   } while (z->num_bits <= 24);
}

stbi_inline static unsigned int stbi__zreceive(stbi__zbuf *z, int n)
{
   unsigned int k;
   if (z->num_bits < n) stbi__fill_bits(z);
   k = z->code_buffer & ((1 << n) - 1);
   z->code_buffer >>= n;
   z->num_bits -= n;
   return k;
}

static int stbi__zhuffman_decode_slowpath(stbi__zbuf *a, stbi__zhuffman *z)
{
   int b,s,k;
   // not resolved by fast table, so compute it the slow way
   // use jpeg approach, which requires MSbits at top
   k = stbi__bit_reverse(a->code_buffer, 16);
   for (s=STBI__ZFAST_BITS+1; ; ++s)
      if (k < z->maxcode[s])
         break;
   if (s == 16) return -1; // invalid code!
   // code size is s, so:
   b = (k >> (16-s)) - z->firstcode[s] + z->firstsymbol[s];
   STBI_ASSERT(z->size[b] == s);
   a->code_buffer >>= s;
   a->num_bits -= s;
   return z->value[b];
}

stbi_inline static int stbi__zhuffman_decode(stbi__zbuf *a, stbi__zhuffman *z)
{
   int b,s;
   if (a->num_bits < 16) stbi__fill_bits(a);
   b = z->fast[a->code_buffer & STBI__ZFAST_MASK];
   if (b) {
      s = b >> 9;
      a->code_buffer >>= s;
      a->num_bits -= s;
      return b & 511;
   }
   return stbi__zhuffman_decode_slowpath(a, z);
}

static int stbi__zexpand(stbi__zbuf *z, char *zout, int n)  // need to make room for n bytes
{
   char *q;
   int cur, limit, old_limit;
   z->zout = zout;
   if (!z->z_expandable) return stbi__err("output buffer limit","Corrupt PNG");
   cur   = (int) (z->zout     - z->zout_start);
   limit = old_limit = (int) (z->zout_end - z->zout_start);
   while (cur + n > limit)
      limit *= 2;
   q = (char *) STBI_REALLOC_SIZED(z->zout_start, old_limit, limit);
   STBI_NOTUSED(old_limit);
   if (q == NULL) return stbi__err("outofmem", "Out of memory");
   z->zout_start = q;
   z->zout       = q + cur;
   z->zout_end   = q + limit;
   return 1;
}

static int stbi__zlength_base[31] = {
   3,4,5,6,7,8,9,10,11,13,
   15,17,19,23,27,31,35,43,51,59,
   67,83,99,115,131,163,195,227,258,0,0 };

static int stbi__zlength_extra[31]=
{ 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 };

static int stbi__zdist_base[32] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0};

static int stbi__zdist_extra[32] =
{ 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int stbi__parse_huffman_block(stbi__zbuf *a)
{
   char *zout = a->zout;
   for(;;) {
      int z = stbi__zhuffman_decode(a, &a->z_length);
      if (z < 256) {
         if (z < 0) return stbi__err("bad huffman code","Corrupt PNG"); // error in huffman codes
         if (zout >= a->zout_end) {
            if (!stbi__zexpand(a, zout, 1)) return 0;
            zout = a->zout;
         }
         *zout++ = (char) z;
      } else {
         stbi_uc *p;
         int len,dist;
         if (z == 256) {
            a->zout = zout;
            return 1;
         }
         z -= 257;
         len = stbi__zlength_base[z];
         if (stbi__zlength_extra[z]) len += stbi__zreceive(a, stbi__zlength_extra[z]);
         z = stbi__zhuffman_decode(a, &a->z_distance);
         if (z < 0) return stbi__err("bad huffman code","Corrupt PNG");
         dist = stbi__zdist_base[z];
         if (stbi__zdist_extra[z]) dist += stbi__zreceive(a, stbi__zdist_extra[z]);
         if (zout - a->zout_start < dist) return stbi__err("bad dist","Corrupt PNG");
         if (zout + len > a->zout_end) {
            if (!stbi__zexpand(a, zout, len)) return 0;
            zout = a->zout;
         }
         p = (stbi_uc *) (zout - dist);
         if (dist == 1) { // run of one byte; common in images.
            stbi_uc v = *p;
            if (len) { do *zout++ = v; while (--len); }
         } else {
            if (len) { do *zout++ = *p++; while (--len); }
         }
      }
   }
}

static int stbi__compute_huffman_codes(stbi__zbuf *a)
{
   static stbi_uc length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
   stbi__zhuffman z_codelength;
   stbi_uc lencodes[286+32+137];//padding for maximum single op
   stbi_uc codelength_sizes[19];
   int i,n;

   int hlit  = stbi__zreceive(a,5) + 257;
   int hdist = stbi__zreceive(a,5) + 1;
   int hclen = stbi__zreceive(a,4) + 4;

   memset(codelength_sizes, 0, sizeof(codelength_sizes));
   for (i=0; i < hclen; ++i) {
      int s = stbi__zreceive(a,3);
      codelength_sizes[length_dezigzag[i]] = (stbi_uc) s;
   }
   if (!stbi__zbuild_huffman(&z_codelength, codelength_sizes, 19)) return 0;

   n = 0;
   while (n < hlit + hdist) {
      int c = stbi__zhuffman_decode(a, &z_codelength);
      if (c < 0 || c >= 19) return stbi__err("bad codelengths", "Corrupt PNG");
      if (c < 16)
         lencodes[n++] = (stbi_uc) c;
      else if (c == 16) {
         c = stbi__zreceive(a,2)+3;
         memset(lencodes+n, lencodes[n-1], c);
         n += c;
      } else if (c == 17) {
         c = stbi__zreceive(a,3)+3;
         memset(lencodes+n, 0, c);
         n += c;
      } else {
         STBI_ASSERT(c == 18);
         c = stbi__zreceive(a,7)+11;
         memset(lencodes+n, 0, c);
         n += c;
      }
   }
   if (n != hlit+hdist) return stbi__err("bad codelengths","Corrupt PNG");
   if (!stbi__zbuild_huffman(&a->z_length, lencodes, hlit)) return 0;
   if (!stbi__zbuild_huffman(&a->z_distance, lencodes+hlit, hdist)) return 0;
   return 1;
}

static int stbi__parse_uncompressed_block(stbi__zbuf *a)
{
   stbi_uc header[4];
   int len,nlen,k;
   if (a->num_bits & 7)
      stbi__zreceive(a, a->num_bits & 7); // discard
   // drain the bit-packed data into header
   k = 0;
   while (a->num_bits > 0) {
      header[k++] = (stbi_uc) (a->code_buffer & 255); // suppress MSVC run-time check
      a->code_buffer >>= 8;
      a->num_bits -= 8;
   }
   STBI_ASSERT(a->num_bits == 0);
   // now fill header the normal way
   while (k < 4)
      header[k++] = stbi__zget8(a);
   len  = header[1] * 256 + header[0];
   nlen = header[3] * 256 + header[2];
   if (nlen != (len ^ 0xffff)) return stbi__err("zlib corrupt","Corrupt PNG");
   if (a->zbuffer + len > a->zbuffer_end) return stbi__err("read past buffer","Corrupt PNG");
   if (a->zout + len > a->zout_end)
      if (!stbi__zexpand(a, a->zout, len)) return 0;
   memcpy(a->zout, a->zbuffer, len);
   a->zbuffer += len;
   a->zout += len;
   return 1;
}

static int stbi__parse_zlib_header(stbi__zbuf *a)
{
   int cmf   = stbi__zget8(a);
   int cm    = cmf & 15;
   /* int cinfo = cmf >> 4; */
   int flg   = stbi__zget8(a);
   if ((cmf*256+flg) % 31 != 0) return stbi__err("bad zlib header","Corrupt PNG"); // zlib spec
   if (flg & 32) return stbi__err("no preset dict","Corrupt PNG"); // preset dictionary not allowed in png
   if (cm != 8) return stbi__err("bad compression","Corrupt PNG"); // DEFLATE required for png
   // window = 1 << (8 + cinfo)... but who cares, we fully buffer output
   return 1;
}

// @TODO: should statically initialize these for optimal thread safety
static stbi_uc stbi__zdefault_length[288], stbi__zdefault_distance[32];
static void stbi__init_zdefaults(void)
{
   int i;   // use <= to match clearly with spec
   for (i=0; i <= 143; ++i)     stbi__zdefault_length[i]   = 8;
   for (   ; i <= 255; ++i)     stbi__zdefault_length[i]   = 9;
   for (   ; i <= 279; ++i)     stbi__zdefault_length[i]   = 7;
   for (   ; i <= 287; ++i)     stbi__zdefault_length[i]   = 8;

   for (i=0; i <=  31; ++i)     stbi__zdefault_distance[i] = 5;
}

static int stbi__parse_zlib(stbi__zbuf *a, int parse_header)
{
   int final, type;
   if (parse_header)
      if (!stbi__parse_zlib_header(a)) return 0;
   a->num_bits = 0;
   a->code_buffer = 0;
   do {
      final = stbi__zreceive(a,1);
      type = stbi__zreceive(a,2);
      if (type == 0) {
         if (!stbi__parse_uncompressed_block(a)) return 0;
      } else if (type == 3) {
         return 0;
      } else {
         if (type == 1) {
            // use fixed code lengths
            if (!stbi__zdefault_distance[31]) stbi__init_zdefaults();
            if (!stbi__zbuild_huffman(&a->z_length  , stbi__zdefault_length  , 288)) return 0;
            if (!stbi__zbuild_huffman(&a->z_distance, stbi__zdefault_distance,  32)) return 0;
         } else {
            if (!stbi__compute_huffman_codes(a)) return 0;
         }
         if (!stbi__parse_huffman_block(a)) return 0;
      }
   } while (!final);
   return 1;
}

static int stbi__do_zlib(stbi__zbuf *a, char *obuf, int olen, int exp, int parse_header)
{
   a->zout_start = obuf;
   a->zout       = obuf;
   a->zout_end   = obuf + olen;
   a->z_expandable = exp;

   return stbi__parse_zlib(a, parse_header);
}

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer + len;
   if (stbi__do_zlib(&a, p, initial_size, 1, 1)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF char *stbi_zlib_decode_malloc(char const *buffer, int len, int *outlen)
{
   return stbi_zlib_decode_malloc_guesssize(buffer, len, 16384, outlen);
}

STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer + len;
   if (stbi__do_zlib(&a, p, initial_size, 1, parse_header)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF int stbi_zlib_decode_buffer(char *obuffer, int olen, char const *ibuffer, int ilen)
{
   stbi__zbuf a;
   a.zbuffer = (stbi_uc *) ibuffer;
   a.zbuffer_end = (stbi_uc *) ibuffer + ilen;
   if (stbi__do_zlib(&a, obuffer, olen, 0, 1))
      return (int) (a.zout - a.zout_start);
   else
      return -1;
}

STBIDEF char *stbi_zlib_decode_noheader_malloc(char const *buffer, int len, int *outlen)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(16384);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer+len;
   if (stbi__do_zlib(&a, p, 16384, 1, 0)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen)
{
   stbi__zbuf a;
   a.zbuffer = (stbi_uc *) ibuffer;
   a.zbuffer_end = (stbi_uc *) ibuffer + ilen;
   if (stbi__do_zlib(&a, obuffer, olen, 0, 0))
      return (int) (a.zout - a.zout_start);
   else
      return -1;
}

#endif // !STBI_INCLUDE_STB_IMAGE_H



//////////////////////////////////////////////////////////////////////////////
// cels (compressed)
//
ASE_DECL void
ASE_DOC_read_compressed_rgba(ASE__ctx *F,
	                         ASE_Cel *Cel,
							 size_t EndPos)
{
	// read compressed data
	int isize = EndPos - F->io.tell(F->udata);
	uint8_t *ibuffer = ASE_MALLOC(isize + 16);
	uint8_t *P = ibuffer;
	for (int i=0; i < isize; ++i) {
		*(P++) = ASE__read8(F);
	}
	{
		// alloc uncompressed data
		int osize = Cel->w * Cel->h * 4;
		uint8_t *obuffer = ASE_MALLOC(osize);

		// decode
		int Result = stbi_zlib_decode_buffer(
			(char *)obuffer, osize, (char *)ibuffer, isize);
		if (-1 == Result) {
			// failure!
			ASE_FREE(obuffer);
			ASE_ERR("ase: failed to load compressed cel!\n");
		} else {
			Cel->data = obuffer;
			ASE_DBG("\t\t--- (LOADED) ---\n");
		}
	}
	ASE_FREE(ibuffer);
}

ASE_DECL void
ASE_DOC_read_compressed_grayscale(ASE__ctx *F,
	                              ASE_Cel *Cel,
							      size_t EndPos)
{
	// read compressed data
	int isize = EndPos - F->io.tell(F->udata);
	uint8_t *ibuffer = ASE_MALLOC(isize + 16);
	uint8_t *P = ibuffer;
	for (int i=0; i < isize; ++i) {
		*(P++) = ASE__read8(F);
	}
	{
		// alloc uncompressed data
		int osize = Cel->w * Cel->h * 2;
		uint8_t *obuffer = ASE_MALLOC(osize);

		// decode
		int Result = stbi_zlib_decode_buffer(
			(char *)obuffer, osize, (char *)ibuffer, isize);
		if (-1 == Result) {
			// failure!
			ASE_FREE(obuffer);
			ASE_ERR("ase: failed to load compressed cel!\n");
		} else {
			Cel->data = obuffer;
			ASE_DBG("\t\t--- (LOADED) ---\n");
		}
	}
	ASE_FREE(ibuffer);
}

ASE_DECL void
ASE_DOC_read_compressed_indexed(ASE__ctx *F,
	                            ASE_Cel *Cel,
							    size_t EndPos)
{
	// read compressed data
	int isize = EndPos - F->io.tell(F->udata);
	uint8_t *ibuffer = ASE_MALLOC(isize + 16);
	uint8_t *P = ibuffer;
	for (int i=0; i < isize; ++i) {
		*(P++) = ASE__read8(F);
	}
	{
		// alloc uncompressed data
		int osize = Cel->w * Cel->h;
		uint8_t *obuffer = ASE_MALLOC(osize);

		// decode
		int Result = stbi_zlib_decode_buffer(
			(char *)obuffer, osize, (char *)ibuffer, isize);
		if (-1 == Result) { // failure
			ASE_FREE(obuffer);
			ASE_ERR("ase: failed to load compressed cel!\n");
		} else {
			Cel->data = obuffer;
			ASE_DBG("\t\t--- (LOADED) ---\n");
		}
	}
	ASE_FREE(ibuffer);
}



ASE_DECL ASE_Cel *
ASE_Cel_read(ASE__ctx *F,
	             ASE_Sprite *S,
				 int frame_index,
			     ASE_Frame *Frame,
			     size_t EndPos)
{
	ASE_DBG("\t\t--- CEL ---\n");

	// HEADER
	int layer   = ASE__read16(F);
	int x       = (int16_t)ASE__read16(F);
	int y       = (int16_t)ASE__read16(F);
	int opacity = ASE__read8(F);
	int type    = ASE__read16(F);
	F->io.skip(F->udata, 7);

	ASE_DBG("\t\tlayer:   %i  (%s)\n", layer, S->layers[layer].name);
	ASE_DBG("\t\tx:       %i\n", x);
	ASE_DBG("\t\ty:       %i\n", y);
	ASE_DBG("\t\topacity: %i\n", opacity);
	ASE_DBG("\t\ttype:    %i\n", type);

	ASE_Layer *L = 0;
	if (layer >= 0 && layer < S->nlayers) L = S->layers + layer;
	if (!L) {
		ASE_ERR("ase: frame %i missing layer %i\n", (int)frame_index, (int)layer);
		return(0);
	}
	if (L->type != ASE_FILE_LAYER_IMAGE) {
		ASE_ERR("ase: invalid .ase file (frame %i in layer %i which does not contain images)\n", (int)frame_index, (int)layer);
		return(0);
	}

	// create new frame
	ASE_Cel *Cel = ASE_DOC_AddCel(Frame);
	assert(Cel);
	Cel->layer = layer;
	Cel->x = x;
	Cel->y = y;
	Cel->opacity = opacity;

	switch (type) {
	case ASE_FILE_RAW_CEL:
		{
			ASE_DBG("\t\t--- RAW ---\n");

			int w = ASE__read16(F);
			int h = ASE__read16(F);

			Cel->w = w;
			Cel->h = h;

			ASE_DBG("\t\tw: %i\n", w);
			ASE_DBG("\t\th: %i\n", h);

			if (w > 0 && h > 0) {
				switch (S->depth) {
					case ASE_DEPTH_RGBA:      // RGBA
					{
						ASE_DOC_read_raw_image_rgba(F, Cel);
					} break;
					case ASE_DEPTH_GRAYSCALE: // Grayscale
					{
						ASE_DOC_read_raw_image_grayscale(F, Cel);
					} break;
					case ASE_DEPTH_INDEXED:   // Indexed
					{
						ASE_DOC_read_raw_image_indexed(F, Cel);
					} break;
				}
			} else {
				ASE_DBG("\t\t(cel has no area...?)\n");
			}
		} break;

	case ASE_FILE_LINK_CEL:
		{
			ASE_DBG("\t\t--- LINKED ---\n");

			/*
			NOTE:
				We are ignoring the edge case of the beta version
				with silly link attributes :)
				We are also assuming that a link is found.
			*/

			int frame = ASE__read16(F);

			Cel->is_linked = 1;
			Cel->frame = frame;

			ASE_DBG("\t\tframe: %i\n", frame);
		} break;

	case ASE_FILE_COMPRESSED_CEL:
		{
			ASE_DBG("\t\t--- COMPRESSED ---\n");

			// WORD      Width in pixels
		  	// WORD      Height in pixels
		  	// BYTE[]    "Raw Cel" data compressed with ZLIB method

			int w = ASE__read16(F);
			int h = ASE__read16(F);

			Cel->w = w;
			Cel->h = h;

			ASE_DBG("\t\tw: %i\n", w);
			ASE_DBG("\t\th: %i\n", h);

			// he uses C++ exceptions in his decoder... hoo boy.
			// i think we'll be fine. things are set up such that
			// you will just get an empty cel if loading fails.

			if (w > 0 && h > 0) {
				switch (S->depth) {
					case ASE_DEPTH_RGBA:      // RGBA
					{
						ASE_DOC_read_compressed_rgba(F, Cel, EndPos);
					} break;
					case ASE_DEPTH_GRAYSCALE: // Grayscale
					{
						ASE_DOC_read_compressed_grayscale(F, Cel, EndPos);
					} break;
					case ASE_DEPTH_INDEXED:   // Indexed
					{
						ASE_DOC_read_compressed_indexed(F, Cel, EndPos);
					} break;
				}
			} else {
				ASE_DBG("\t\t(cel has no area...?)\n");
			}
		} break;
	}

	if (!Cel) return(0);

	return(Cel);
}



//////////////////////////////////////////////////////////////////////////////
// frame tags
//
ASE_DECL ASE_Tag *
ASE_DOC_AddTag(ASE_Sprite *S)
{
	S->tags = ASE_REALLOC(S->tags, ++S->ntags * sizeof(ASE_Tag));
	ASE_Tag *L = S->tags + (S->ntags - 1);
	memset(L, 0, sizeof(ASE_Tag));
	ASE_DBG("\t\ttag index: %i\n", (int)S->ntags-1);
	return(L);
}

ASE_DECL void
ASE_Tags_read(ASE__ctx *F, ASE_Sprite *S)
{
	ASE_DBG("\t--- FRAME TAGS ---\n");

	size_t count = ASE__read16(F);

	ASE__read32(F); // 8 reserved bytes
	ASE__read32(F);

	for (size_t i=0; i<count; ++i) {
		ASE_DBG("\t\t--- TAG (%i) ---\n", i);

		int From = ASE__read16(F);
		int To   = ASE__read16(F);
		int AnimDirection = ASE__read8(F);

		if (ASE_LOOP_FORWARD != AnimDirection &&
		    ASE_LOOP_REVERSE != AnimDirection &&
			ASE_LOOP_PINGPONG != AnimDirection)
		{
			AnimDirection = ASE_LOOP_FORWARD;
		}

		ASE__read32(F); // 8 reserved bytes
		ASE__read32(F);

		F->io.skip(F->udata, 4); // skip tag color

		char *Name = ASE_DOC_read_string(F);

		ASE_DBG("\t\tfrom: %i\n", From);
		ASE_DBG("\t\tto:   %i\n", To);
		ASE_DBG("\t\tdir:  %i\n", AnimDirection);
		ASE_DBG("\t\tname: %s\n", Name);

		// allocate the tag
		ASE_Tag *Tag = ASE_DOC_AddTag(S);
		Tag->from = From;
		Tag->to   = To;
		Tag->dir  = AnimDirection;
		Tag->name = Name;
	}
}



//////////////////////////////////////////////////////////////////////////////
// decoder main
//
ASE_DECL ASE_BOOL
ASE__decode_main(ASE__ctx *F, ASE_Sprite *S)
{
	ASE_BOOL R = 1;

	// LOAD FILE HEADER
	ASE_DOC_Header Header = {0};
	ASE_BOOL Ok = ASE_DOC_Header_read(F, &Header);
	assert(Ok && "couldn't read the header!");

	// COPY TO SPRITE
	S->width = Header.width;
	S->height = Header.height;
	S->depth = Header.depth;

	ASE_DBG("--- aseprite document ---\n");
	ASE_DBG("frames:  %i\n", (int)Header.frames);
	ASE_DBG("width:   %i\n", (int)Header.width);
	ASE_DBG("height:  %i\n", (int)Header.height);
	ASE_DBG("depth:   %i\n", (int)Header.depth);
	ASE_DBG("ncolors: %i\n", (int)Header.ncolors);
	ASE_DBG("tcolor:  %i\n", (int)Header.transparent_index);


	ASE_Layer * LastLayer = 0;
	void * LastWithUserData = 0;
	void * LastCel = 0;
	int   CurrentLevel = -1; // root

	ASE_BOOL IgnoreOldColorChunks = 0;


	// LOOP OVER FRAMES
	for (int i=0; i < Header.frames; ++i) {
		size_t FrameHeaderStart = F->io.tell(F->udata);

		// LOAD FRAME HEADER
		ASE_FrameHeader FrameHeader = {0};
		ASE_FrameHeader_read(F, &FrameHeader);
		assert(FrameHeader.magic == ASE_FILE_FRAME_MAGIC);

		ASE_DBG("--- frame header (%i) ---\n", i);
		ASE_DBG("size:      %i\n", (int)FrameHeader.size);
		ASE_DBG("chunks:    %i\n", (int)FrameHeader.chunks);
		ASE_DBG("duration:  %i\n", (int)FrameHeader.duration);


		// FRAME
		ASE_Frame *Frame = ASE_DOC_AddFrame(S);
		assert(Frame);
		Frame->duration = FrameHeader.duration;


		// LOAD CHUNKS
		for (int j=0; j<FrameHeader.chunks; ++j) {
			size_t ChunkHeaderStart = F->io.tell(F->udata);

			// LOAD CHUNK HEADER
			ASE_DOC_ChunkHeader ChunkHeader = {0};
			ASE_DOC_ChunkHeader_read(F, &ChunkHeader);

			ASE_DBG("\t--- chunk header (%i) ---\n", j);
			ASE_DBG("\tsize:  %i\n", (int)ChunkHeader.size);
			ASE_DBG("\ttype:  0x%04x\n", (int)ChunkHeader.type);
			ASE_DBG("\tstart: %i\n", (int)ChunkHeader.start);


			// CHUNK TYPE
			switch (ChunkHeader.type) {
			case ASE_FILE_CHUNK_FLI_COLOR:  // FALLTHROUGH
			case ASE_FILE_CHUNK_FLI_COLOR2:
				{
					if (!IgnoreOldColorChunks) {
						// OLD COLOR CHUNKS -- SAFELY IGNORE?
					}
				} break;

			case ASE_FILE_CHUNK_PALETTE:
				{
					S->palette = ASE_Palette_read(F, &S->palette);
					IgnoreOldColorChunks = 1;
				} break;

			case ASE_FILE_CHUNK_LAYER:
				{
					ASE_Layer *Layer = 0;
					Layer = ASE_Layer_read(F,
						&Header, S, &LastLayer, &CurrentLevel);
					if (Layer) {
						LastWithUserData = Layer;
					}
				} break;

			case ASE_FILE_CHUNK_CEL:
				{
					ASE_Cel *Cel = ASE_Cel_read(F,
						S, i, Frame,
						ChunkHeaderStart + ChunkHeader.size);
					if (Cel) {
						LastCel = Cel;
						LastWithUserData = Cel->data;
					}
				} break;

			case ASE_FILE_CHUNK_CEL_EXTRA: break; // IGNORE
			case ASE_FILE_CHUNK_MASK:      break; // DEPRECATED
			case ASE_FILE_CHUNK_PATH:      break; // UNUSED

			case ASE_FILE_CHUNK_FRAME_TAGS:
				{
					ASE_Tags_read(F, S);
				} break;

			case ASE_FILE_CHUNK_SLICES:    break; // IGNORE
			case ASE_FILE_CHUNK_SLICE:     break; // IGNORE
			case ASE_FILE_CHUNK_USER_DATA: break; // IGNORE
			}

			// GOTO NEXT CHUNK HEADER
			F->io.seek(F->udata, ChunkHeaderStart + ChunkHeader.size);
		}

		// GOTO NEXT FRAME HEADER
		F->io.seek(F->udata, FrameHeaderStart + FrameHeader.size);
	}

	return(R);
}



//////////////////////////////////////////////////////////////////////////////
// primary API - loading
//
#ifndef ASE_NO_STDIO
ASE_DECL ASE_BOOL
ASE_load (const char *filename, ASE_Sprite *out)
{
	FILE *F = fopen(filename, "rb");
	if (!F) {
		ASE_ERR("could not open file: %s\n", filename);
		return(0);
	}
	ASE__ctx Context = {0};
	ASE__start_file(&Context, F);
	int R = ASE__decode_main(&Context, out);
	fclose(F);
	return(R);
}

ASE_DECL ASE_BOOL
ASE_load_from_file (FILE *f, ASE_Sprite *out)
{
	ASE__ctx Context = {0};
	ASE__start_file(&Context, f);
	return (ASE__decode_main(&Context, out));
}
#endif

ASE_DECL ASE_BOOL
ASE_load_from_memory (const uint8_t *buffer, int len, ASE_Sprite *out)
{
	ASE__ctx Context = {0};
	ASE__start_mem(&Context, (uint8_t *)buffer, len);
	return(ASE__decode_main(&Context, out));
}

ASE_DECL ASE_BOOL
ASE_load_from_callbacks (const ASE_Callbacks *io, void *user, ASE_Sprite *out)
{
	ASE__ctx Context = {0};
	ASE__start_callbacks(&Context, (ASE_Callbacks *)io, user);
	return(ASE__decode_main(&Context, out));
}

ASE_DECL void
ASE_free(ASE_Sprite *Sprite)
{
	if (!Sprite) return;

	for (int i=0; i < Sprite->nlayers; ++i) {
		ASE_Layer *I = Sprite->layers + i;
		ASE_FREE(I->name);
	}
	ASE_FREE(Sprite->layers);

	for (int i=0; i < Sprite->nframes; ++i) {
		ASE_Frame *I = Sprite->frames + i;
		for (int j=0; j < I->ncels; ++j) {
			ASE_Cel *J = I->cels + j;
			ASE_FREE(J->data);
		}
		ASE_FREE(I->cels);
	}
	ASE_FREE(Sprite->frames);

	for (int i=0; i < Sprite->ntags; ++i) {
		ASE_Tag *I = Sprite->tags + i;
		ASE_FREE(I->name);
	}
	ASE_FREE(Sprite->tags);

	memset(Sprite, 0, sizeof(ASE_Sprite));
}



//////////////////////////////////////////////////////////////////////////////
// primary API - conveniences
//
ASE_DECL ASE_Layer *
ASE_get_layer_by_name (ASE_Sprite *sprite, const char *name)
{
	ASE_Layer *A = sprite->layers;
	for (int i=0; i < sprite->nlayers; ++i, ++A) {
		if (ASE_streq(name, A->name)) return(A);
	}
	return(0);
}

ASE_DECL ASE_Tag *
ASE_get_tag_by_name (ASE_Sprite *sprite, const char *name)
{
	ASE_Tag *A = sprite->tags;
	for (int i=0; i < sprite->ntags; ++i, ++A) {
		if (ASE_streq(name, A->name)) return(A);
	}
	return(0);
}

ASE_DECL int
ASE_get_next_frame (ASE_Tag *tag, int frame)
{
	switch(tag->dir) {
	case ASE_LOOP_FORWARD:
		{
			if (++frame > tag->to) frame = tag->from;
		} break;
	case ASE_LOOP_REVERSE:
		{
			if (--frame < tag->from) frame = tag->to;
		} break;
	case ASE_LOOP_PINGPONG:
		{
			if (frame >= 0) {
				if (++frame > tag->to){
					frame = -1;
					if (0 == tag->to - tag->from) frame = 0;
				}
			} else {
				// treat a negative number as offset from tag->to
				if (--frame < tag->from) frame = 0;
			}
		} break;
	}
	return(frame);
}

ASE_DECL ASE_Cel *
ASE_get_linked_cel (ASE_Sprite *sprite, ASE_Cel *cel)
{
	ASE_Frame *lf = sprite->frames + cel->frame;
	for (int ic=0; ic < lf->ncels; ++ic) {
		ASE_Cel *cel2 = lf->cels + ic;
		if (cel2->layer == cel->layer) return(cel2);
	}
	return(0);
}

ASE_DECL int
ASE_check_cel_visible (ASE_Sprite *sprite, ASE_Cel *cel)
{
	return(sprite->layers[cel->layer].visible);
}



#endif // ASE_IMPLEMENTATION

#ifdef __cplusplus
}
#endif
