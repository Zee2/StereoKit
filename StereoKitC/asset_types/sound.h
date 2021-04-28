#pragma once

#include "../libraries/miniaudio.h"
#include "../libraries/tinycthread.h"
#include "assets.h"

namespace sk {

struct ring_buffer_t {
	float  *data;
	size_t  capacity;
	size_t  cursor;
	size_t  start;
	size_t  count;
};

typedef enum sound_type_ {
	sound_type_none,
	sound_type_decode,
	sound_type_buffer,
	sound_type_stream,
} sound_type_;

struct _sound_t {
	asset_header_t header;
	sound_type_    type;
	ma_decoder     decoder;
	ring_buffer_t  buffer;
	mtx_t          data_lock;
};

void sound_destroy(sound_t sound);

void   ring_buffer_write(ring_buffer_t *buffer, const float *data, size_t data_size);
size_t ring_buffer_read (ring_buffer_t *buffer, float *out_data, size_t out_data_size);

}