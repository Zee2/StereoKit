#pragma once

#include "stereokit.h"
#include "font.h"

struct _text_style_t {
	font_t      font;
	material_t  material;
	uint32_t    buffer_index;
	text_align_ align;
};
struct text_buffer_t {
	mesh_t   mesh;
	vert_t  *verts;
	uint32_t id;
	int      vert_count;
	int      vert_cap;
};

vec2 text_line_size(text_style_t style, const char *text);