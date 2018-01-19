/*
paq_wav.h - v1.00 - public domain barebones wav loader
https://github.com/pennie-quinn/paq

	*** no warranty implied; use at your own risk ***

	Do this:
		#define WAV_IMPLEMENTATION
	before you include this file in _ONE_ C or C++ file to include
	the implementation.

	// i.e. something like this:
	#include ...
	#include ...
	#define WAV_IMPLEMENTATION
	#include "paq_wav.h"
	#include ...

	- You can #define WAV_ASSERT(x, msg) before the #include to avoid using
	  my assert.
	  - Or, you can #define WAV_NO_ASSERT if you're feeling lucky.

	- You can also #define WAV_MALLOC, WAV_REALLOC, and WAV_FREE to
	  avoid using malloc, realloc, and free.

	- You can #define several printf-style functions, if you want them:
	  - WAV_DBG(...) for verbose printouts
	  - WAV_ERR(...) for error messages

	- You can #define WAV_NO_STDIO if you don't want to load from files.


NOTES:
	- Really basic, only reads format and data chunks.
	- Load from a file path, FILE*, or memory block.
	- Load from arbitrary I/O callbacks (see: WAV_Callbacks).

	Full docs under "DOCUMENTATION" below.


CHANGELOG:
	- 1.00  (2018-01-19) first release



===============================   CONTRIBUTORS   ==============================
Pennie Quinn
	- core functionality



LICENSE

This software is dual-licensed to the public domain and under the following
license: you are granted a perpetual, irrevocable license to copy, modify,
publish, and distribute this file as you see fit.
*/


#ifdef __cplusplus
extern "C" {
#endif

#ifndef PAQ_WAV_H


/*
=============================== DOCUMENTATION =================================

Basic Usage
	WAV_Data Data;
	if (WAV_load("foo.wav", &Data)) {
		// loading succeeded
	}

	...
	...
	...

	WAV_free(&Data);

See "primary API" below for full interface.

===============================================================================

I/O Callbacks

Callbacks let the decoder read from _whatever_ source you want.

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

See: WAV_Callbacks

===============================================================================

File Format Reference
	Readily available from many sources online. MSDN C# example claims a 32bit
	wBitsPerSample, for some reason, despite every other spec using a ushort,
	and despite every .WAV file I've ever seen using a ushort (shrug).
*/


#include <memory.h> // memcpy, memset
#include <stdint.h>

#ifndef WAV_NO_STDIO
#	include <stdio.h>
#endif


//////////////////////////////////////////////////////////////////////////////
// macros / config
//
#define WAV_DEBUG 0

#define WAV_BOOL int

#ifdef inline
#	define WAV_DECL inline
#else
#	define WAV_DECL extern inline
#endif

#ifndef WAV_DBG
#	if WAV_DEBUG && !defined(WAV_NO_STDIO)
#		define WAV_DBG(...) printf(__VA_ARGS__)
#	else
#		define WAV_DBG(...)
#	endif
#endif

#ifndef WAV_ERR
#	if WAV_DEBUG && !defined(WAV_NO_STDIO)
#		define WAV_ERR(...) printf(__VA_ARGS__)
#	else
#		define WAV_ERR(...)
#	endif
#endif

#if !defined(WAV_NO_ASSERT) && !defined(WAV_ASSERT)
#	define WAV_ASSERT(expr, msg) \
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

#ifndef WAV_MALLOC
#	include <malloc.h>
#	define WAV_MALLOC malloc
#	define WAV_REALLOC realloc
#	define WAV_FREE free
#endif


//////////////////////////////////////////////////////////////////////////////
// flags, magic numbers
//
enum {
	WAV_8BIT        = 8,
	WAV_16BIT       = 16,
	WAV_FLOAT       = 32,
	WAV_MAGIC_RIFF  = ('R' << 24) | ('I' << 16) | ('I' << 8) | 'F',
	WAV_MAGIC_WAVE  = ('W' << 24) | ('A' << 16) | ('V' << 8) | 'E',
	WAV_MAGIC_FMT   = ('f' << 24) | ('m' << 16) | ('t' << 8) | ' ',
	WAV_MAGIC_DATA  = ('d' << 24) | ('a' << 16) | ('t' << 8) | 'a',
};


//////////////////////////////////////////////////////////////////////////////
// primary API - structs
//
typedef struct {
	uint16_t  wChannels;
	uint32_t  dwSamplesPerSec;
	uint32_t  dwAvgBytesPerSec;
	uint16_t  wBlockAlign;
	uint32_t  wBitsPerSample;
	uint32_t  dwSamples;
	int8_t  * data;
} WAV_Data;


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

} WAV_Callbacks;

#ifndef WAV_NO_STDIO
WAV_DECL WAV_BOOL WAV_load (const char *filename, WAV_Data *out);

WAV_DECL WAV_BOOL WAV_load_from_file (FILE *f, WAV_Data *out);
// the position of f will be left pointing immediately after the aseprite data.
#endif

WAV_DECL WAV_BOOL WAV_load_from_memory (const int8_t *buffer, int len, WAV_Data *out);

WAV_DECL WAV_BOOL WAV_load_from_callbacks (const WAV_Callbacks *io, void *user, WAV_Data *out);

WAV_DECL void     WAV_free(WAV_Data *Doc);


//////////////////////////////////////////////////////////////////////////////
// primary API - conversion
//
WAV_DECL void WAV_convert_to_8bit  (WAV_Data *Loaded);
WAV_DECL void WAV_convert_to_16bit (WAV_Data *Loaded);
WAV_DECL void WAV_convert_to_float (WAV_Data *Loaded);

#define PAQ_WAVE_H
#endif



//////////////////////////////////////////////////////////////////////////////
//                                                                          //
//                              IMPLEMENTATION                              //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////
#ifdef WAV_IMPLEMENTATION


//////////////////////////////////////////////////////////////////////////////
// context struct and functions
//

// we want to load from different sources without a lot of code duplication

typedef struct {
	WAV_Callbacks io;
	void *udata;

	int read_callbacks;
	int buflen;

	int8_t *buf;
	int8_t *buf_end;
	int8_t *buf_orig;
	int8_t *buf_orig_end;
} WAV__ctx;

// init decode from callbacks
static void WAV__start_callbacks(WAV__ctx *ctx, WAV_Callbacks *cb, void *user)
{
	ctx->io = *cb;
	ctx->udata = user;
	ctx->read_callbacks = 1;
}

// memory interface
static int WAV__mem_read(void *user, char *data, int size)
{
	WAV__ctx *C = (WAV__ctx *)user;
	int count = 0;
	for(; count < size; ++count) {
		if (C->buf >= C->buf_end) break;
		*(data++) = *(C->buf++);
	}
	return(count);
}

static void WAV__mem_skip(void *user, int bytes)
{
	WAV__ctx *C = (WAV__ctx *)user;
	for(; bytes; --bytes, ++C->buf) {
		if (C->buf >= C->buf_end) break;
	}
}

static int WAV__mem_eof(void *user)
{
	WAV__ctx *C = (WAV__ctx *)user;
	return(C->buf >= C->buf_end);
}

static int WAV__mem_tell(void *user)
{
	WAV__ctx *C = (WAV__ctx *)user;
	return((int)(C->buf - C->buf_orig));
}

static void WAV__mem_seek(void *user, int pos)
{
	WAV__ctx *C = (WAV__ctx *)user;
	C->buf = C->buf_orig + pos;
}

static WAV_Callbacks WAV__mem_callbacks = {
	WAV__mem_read,
	WAV__mem_skip,
	WAV__mem_eof,
	WAV__mem_tell,
	WAV__mem_seek
};

static void WAV__start_mem(WAV__ctx *ctx, int8_t *buf, int len)
{
	WAV__start_callbacks(ctx, &WAV__mem_callbacks, (void *)ctx);
	ctx->buf = ctx->buf_orig = buf;
	ctx->buf_end = ctx->buf_orig_end = buf + len;
}


// file interface
#ifndef WAV_NO_STDIO

static int WAV__file_read(void *user, char *data, int size)
{
	return((int)fread(data, 1, size, (FILE *)user));
}

static void WAV__file_skip(void *user, int bytes)
{
	fseek((FILE *)user, bytes, SEEK_CUR);
}

static int WAV__file_eof(void *user)
{
	return(feof((FILE *)user));
}

static int WAV__file_tell(void *user)
{
	return((int)ftell((FILE *)user));
}

static void WAV__file_seek(void *user, int pos)
{
	fseek((FILE *)user, pos, SEEK_SET);
}

static WAV_Callbacks WAV__file_callbacks = {
	WAV__file_read,
	WAV__file_skip,
	WAV__file_eof,
	WAV__file_tell,
	WAV__file_seek
};

// init decode from FILE
static void WAV__start_file(WAV__ctx *ctx, FILE *f)
{
	WAV__start_callbacks(ctx, &WAV__file_callbacks, (void *)f);
}

#endif // !WAV_NO_STDIO



//////////////////////////////////////////////////////////////////////////////
// generic reads
//
WAV__read8(WAV__ctx *c)
{
	uint8_t v;
	if (1 == c->io.read(c->udata, &v, 1)) return(v);
	return(0);
}

WAV__read16_le(WAV__ctx *c)
{
	uint8_t v[2];
	if (2 == c->io.read(c->udata, v, 2))
		return((v[1] << 8) | v[0]); // little-endian
	else return(0);
}

WAV__read32_le(WAV__ctx *c)
{
	uint8_t v[4];
	if (4 == c->io.read(c->udata, v, 4))
		return((v[3] << 24) | (v[2] << 16) | (v[1] << 8) | v[0]); // little-endian
	else return(0);
}

WAV__read16_be(WAV__ctx *c)
{
	uint8_t v[2];
	if (2 == c->io.read(c->udata, v, 2))
		return((v[0] << 8) | v[1]); // big-endian
	else return(0);
}

WAV__read32_be(WAV__ctx *c)
{
	uint8_t v[4];
	if (4 == c->io.read(c->udata, v, 4))
		return((v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3]); // big-endian
	else return(0);
}



//////////////////////////////////////////////////////////////////////////////
// decoder main
//
WAV_DECL WAV_BOOL
WAV__decode_main(WAV__ctx *F, WAV_Data *Doc)
{
	// check header
	if (WAV_MAGIC_RIFF == WAV__read32_le(F)) {
		WAV_ERR("RIFF header missing!\n");
		return(0);
	}

	// load size
	uint32_t FileSize = WAV__read32_le(F); // minus 8 for 'RIFF' and 'WAVE'

	// load RIFF chunk type -- "WAVE"
	if (WAV_MAGIC_WAVE == WAV__read32_le(F)) {
		WAV_ERR("WAVE header missing!\n");
		return(0);
	}

	// format chunk
	if (WAV_MAGIC_FMT == WAV__read32_le(F)) {
		WAV_ERR("fmt header missing!\n");
		return(0);
	}

	uint32_t FmtChunkSize = WAV__read32_le(F);

	if (1 != WAV__read16_le(F)) { // indicates PCM format
		WAV_ERR("chunkformat was not 1\n");
		return(0);
	}

	Doc->wChannels = WAV__read16_le(F);
	WAV_DBG("\twChannels:             %i\n", (int)Doc->wChannels);

	Doc->dwSamplesPerSec = WAV__read32_le(F);
	WAV_DBG("\tsamples per second:   %i\n", (int)Doc->dwSamplesPerSec);

	Doc->dwAvgBytesPerSec = WAV__read32_le(F);
	WAV_DBG("\tavg bytes per second: %i\n", (int)Doc->dwAvgBytesPerSec);

	Doc->wBlockAlign = WAV__read16_le(F);
	WAV_DBG("\tblock align:          %i\n", (int)Doc->wBlockAlign);

	Doc->wBitsPerSample = WAV__read16_le(F);
	WAV_DBG("\tbits per sample:      %i\n", (int)Doc->wBitsPerSample);

	// data chunk
	if (WAV_MAGIC_DATA == WAV__read32_le(F)) {
		WAV_ERR("data header missing!\n");
		return(0);
	}

	uint32_t DataChunkSize = WAV__read32_le(F);
	WAV_DBG("\tdata chunk size:      %i\n", (int)DataChunkSize);

	// sample data
	Doc->data = WAV_MALLOC(DataChunkSize);
	int BytesRead = F->io.read(F->udata, Doc->data, DataChunkSize);

	if (BytesRead != DataChunkSize) {
		WAV_ERR("only read %i of %i sample bytes\n", BytesRead, DataChunkSize);
		WAV_FREE(Doc->data);
		Doc->data = 0;
		return(0);
	}

	Doc->dwSamples = DataChunkSize / Doc->wChannels;

	return(1);
}



//////////////////////////////////////////////////////////////////////////////
// primary API - loading
//
#ifndef WAV_NO_STDIO
WAV_DECL WAV_BOOL
WAV_load (const char *filename, WAV_Data *out)
{
	FILE *F = fopen(filename, "rb");
	if (!F) {
		WAV_ERR("could not open file: %s\n", filename);
		return(0);
	}
	WAV__ctx Context = {0};
	WAV__start_file(&Context, F);
	int R = WAV__decode_main(&Context, out);
	fclose(F);
	return(R);
}

WAV_DECL WAV_BOOL
WAV_load_from_file (FILE *f, WAV_Data *out)
{
	WAV__ctx Context = {0};
	WAV__start_file(&Context, f);
	return (WAV__decode_main(&Context, out));
}
#endif

WAV_DECL WAV_BOOL
WAV_load_from_memory (const int8_t *buffer, int len, WAV_Data *out)
{
	WAV__ctx Context = {0};
	WAV__start_mem(&Context, (int8_t *)buffer, len);
	return(WAV__decode_main(&Context, out));
}

WAV_DECL WAV_BOOL
WAV_load_from_callbacks (const WAV_Callbacks *io, void *user, WAV_Data *out)
{
	WAV__ctx Context = {0};
	WAV__start_callbacks(&Context, (WAV_Callbacks *)io, user);
	return(WAV__decode_main(&Context, out));
}

WAV_DECL void
WAV_free(WAV_Data *Doc)
{
	WAV_FREE(Doc->data);
	memset(Doc, 0, sizeof(WAV_Data));
}


//////////////////////////////////////////////////////////////////////////////
// primary API - conversion
//
WAV_DECL void
WAV_convert_to_8bit  (WAV_Data *Loaded)
{
	WAV_ASSERT(Loaded && Loaded->data, "invalid arg");
	if (WAV_8BIT == Loaded->wBitsPerSample) return; // no conversion needed

	int8_t *NewData = (int8_t *)WAV_MALLOC(
		Loaded->dwSamples * Loaded->wChannels);
	int8_t *D = NewData;

	switch (Loaded->wBitsPerSample) {
		case WAV_16BIT:
		{
			int16_t  *P = (int16_t *)Loaded->data;
			for (int i=0; i < Loaded->dwSamples * Loaded->wChannels; ++i)
				*(D++) = (int8_t)(*(P++)/32767.0f * 127.0f);
		} break;
		case WAV_FLOAT:
		{
			float   *P = (float *)Loaded->data;
			for (int i=0; i < Loaded->dwSamples * Loaded->wChannels; ++i)
				*(D++) = (int8_t)(*(P++) * 127.0f);
		} break;
		default: WAV_ASSERT(0, "INVALID DEFAULT CASE"); break;
	}
	WAV_FREE(Loaded->data);
	Loaded->wBitsPerSample = WAV_8BIT;
	Loaded->data = (int8_t *)NewData;
}

WAV_DECL void
WAV_convert_to_16bit (WAV_Data *Loaded)
{
	WAV_ASSERT(Loaded && Loaded->data, "invalid arg");
	if (WAV_16BIT == Loaded->wBitsPerSample) return; // no conversion needed

	int16_t *NewData = (int16_t *)WAV_MALLOC(
		Loaded->dwSamples * Loaded->wChannels * 2);
	int16_t *D = NewData;

	switch (Loaded->wBitsPerSample) {
		case WAV_8BIT:
		{
			int8_t  *P = (int8_t *)Loaded->data;
			for (int i=0; i < Loaded->dwSamples * Loaded->wChannels; ++i)
				*(D++) = (int16_t)(*(P++)/127.0f * 32767.0f);
		} break;
		case WAV_FLOAT:
		{
			float   *P = (float *)Loaded->data;
			for (int i=0; i < Loaded->dwSamples * Loaded->wChannels; ++i)
				*(D++) = (int16_t)(*(P++) * 32767.0f);
		} break;
		default: WAV_ASSERT(0, "INVALID DEFAULT CASE"); break;
	}
	WAV_FREE(Loaded->data);
	Loaded->wBitsPerSample = WAV_16BIT;
	Loaded->data = (int8_t *)NewData;
}

WAV_DECL void
WAV_convert_to_float (WAV_Data *Loaded)
{
	WAV_ASSERT(Loaded && Loaded->data, "invalid arg");
	if (WAV_FLOAT == Loaded->wBitsPerSample) return; // no conversion needed

	float *NewData = (float *)WAV_MALLOC(
		Loaded->dwSamples * Loaded->wChannels * 4);
	float *D = NewData;

	switch (Loaded->wBitsPerSample) {
		case WAV_8BIT:
		{
			int8_t  *P = (int8_t *)Loaded->data;
			for (int i=0; i < Loaded->dwSamples * Loaded->wChannels; ++i)
				*(D++) = (*(P++)/127.0f);
		} break;
		case WAV_16BIT:
		{
			int16_t  *P = (int16_t *)Loaded->data;
			for (int i=0; i < Loaded->dwSamples * Loaded->wChannels; ++i)
				*(D++) = (*(P++)/32767.0f);
		} break;
		default: WAV_ASSERT(0, "INVALID DEFAULT CASE"); break;
	}
	WAV_FREE(Loaded->data);
	Loaded->wBitsPerSample = WAV_FLOAT;
	Loaded->data = (int8_t *)NewData;
}


#endif // WAV_IMPLEMENTATION

#ifdef __cplusplus
}
#endif
