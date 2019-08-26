#include "text.h"

#include "material.h"
#include "assets.h"

#include <vector>
using namespace std;

vector<_text_style_t> text_styles;
vector<text_buffer_t> text_buffers;

void text_buffer_ensure_capacity(text_buffer_t &buffer, size_t characters) {
	if (buffer.vert_count + characters * 4 <= buffer.vert_cap)
		return;

	buffer.vert_cap = buffer.vert_count + characters * 4;
	buffer.verts    = (vert_t *)realloc(buffer.verts, sizeof(vert_t) * buffer.vert_cap);

	// regenerate indices
	int       quads = buffer.vert_cap / 4;
	uint16_t *inds  = (uint16_t *)malloc(quads * 6 * sizeof(uint16_t));
	for (size_t i = 0; i < quads; i++) {
		int q = i * 4;
		int c = i * 6;
		inds[c+0] = q+2;
		inds[c+1] = q+1;
		inds[c+2] = q;

		inds[c+3] = q+3;
		inds[c+4] = q+2;
		inds[c+5] = q;
	}
	mesh_set_inds(buffer.mesh, inds, quads * 6);
	free(inds);
}

text_style_t text_make_style(font_t font, material_t material, text_align_ align) {
	uint32_t       id     = font->header.id << 16 | material->header.id;
	uint32_t       index  = 0;
	text_buffer_t *buffer = nullptr;
	
	// Find or make a buffer for this style
	for (size_t i = 0; i < text_buffers.size(); i++) {
		if (text_buffers[i].id == id) {
			buffer = &text_buffers[i];
			index  = i;
			break;
		}
	}
	if (buffer == nullptr) {
		text_buffers.push_back({});
		index  = text_buffers.size() - 1;
		buffer = &text_buffers[index];

		char name[26];
		assets_unique_name("auto/txt_buf/", name, _countof(name));
		buffer->mesh = mesh_create(name);
		buffer->id   = id;
	}

	// Create the style
	_text_style_t style;
	style.font         = font;
	style.material     = material;
	style.buffer_index = index;
	style.align        = align;
	text_styles.push_back(style);

	material_set_texture   (material, "diffuse", font_get_tex(font));
	material_set_cull      (material, material_cull_none);
	material_set_alpha_mode(material, material_alpha_test);
	return text_styles.size() - 1;
}

vec2 text_line_size(text_style_t style, const char *text) {
	font_t      font = text_styles[style].font;
	const char *curr = text;
	float       x    = 0;
	while (*curr != '\0' && *curr != '\n') {
		char         currch = *curr;
		font_char_t &ch     = font->characters[(int)currch];

		// Do spacing for whitespace characters
		switch (currch) {
		case '\t': x += font->characters[(int)' '].xadvance * 4; break;
		default:   x += ch.xadvance; break;
		}
		curr += 1;
	}
	return vec2{ x, 1 };
}
vec2 text_size(text_style_t style, const char *text) {
	font_t font = text_styles[style].font;
	const char *curr = text;
	float x = 0;
	float y = 0;
	float max_x = 0;
	int line_ct = 0;
	while (*curr != '\0') {
		char currch = *curr;
		curr += 1;
		line_ct += 1;
		font_char_t &ch = font->characters[(int)currch];

		// Do spacing for whitespace characters
		switch (currch) {
		case '\t': x += font->characters[(int)' '].xadvance * 4; break;
		case '\n': if (x > max_x) max_x = x; x = 0; y -= 1; line_ct = 0; break;
		default:   x += ch.xadvance; break;
		}
	}
	if (x > max_x) max_x = x;
	if (line_ct > 0)
		y -= 1;
	y = fminf(-1, y);
	return vec2{ max_x, fabsf(y) };
}

void text_add(text_style_t style, transform_t &transform, const char *text, float off_x, float off_y) {
	_text_style_t &style_data = text_styles[style];
	text_buffer_t &buffer     = text_buffers[style_data.buffer_index];
	vec2           size       = text_size(style, text);

	// Resize array if we need more room for this text
	size_t length = strlen(text);
	text_buffer_ensure_capacity(buffer, length);
	
	vec3    normal  = transform_local_to_world_dir(transform, -vec3_forward);
	color32 col     = {255,255,255,255};
	const char*curr = text;
	vec2    line_sz = text_line_size(style, curr);
	float   align_x = 0;
	if (style_data.align & text_align_x_center) align_x = -(line_sz.x / 2.f);
	if (style_data.align & text_align_x_right)  align_x = -line_sz.x;
	float   x       = off_x + align_x;
	float   y       = off_y-1;
	if (style_data.align & text_align_y_center) y += (size.y / 2.f);
	if (style_data.align & text_align_y_bottom) y += size.y;
	size_t  offset  = buffer.vert_count;
	while (*curr != '\0') {
		char currch = *curr;
		curr += 1;
		font_char_t &ch = style_data.font->characters[(int)currch];

		// Do spacing for whitespace characters
		switch (currch) {
		case '\t': x += style_data.font->characters[(int)' '].xadvance * 4; continue;
		case ' ':  x += ch.xadvance; continue;
		case '\n': {
			line_sz = text_line_size(style, curr);
			align_x = 0;
			if (style_data.align & text_align_x_center) align_x = -(line_sz.x / 2.f);
			if (style_data.align & text_align_x_right)  align_x = -line_sz.x;
			x = off_x + align_x;
			y -= 1;
		} continue;
		default:break;
		}
		
		// Add a character quad
		buffer.verts[offset + 0] = { transform_local_to_world(transform, vec3{x + ch.x0, y + ch.y0, 0}), normal, vec2{ch.u0, ch.v0}, col };
		buffer.verts[offset + 1] = { transform_local_to_world(transform, vec3{x + ch.x1, y + ch.y0, 0}), normal, vec2{ch.u1, ch.v0}, col };
		buffer.verts[offset + 2] = { transform_local_to_world(transform, vec3{x + ch.x1, y + ch.y1, 0}), normal, vec2{ch.u1, ch.v1}, col };
		buffer.verts[offset + 3] = { transform_local_to_world(transform, vec3{x + ch.x0, y + ch.y1, 0}), normal, vec2{ch.u0, ch.v1}, col };

		buffer.vert_count += 4;
		x += ch.xadvance;
		offset += 4;
	}
}

void text_render_style(text_style_t style) {
	text_buffer_t &buffer = text_buffers[text_styles[style].buffer_index];
	transform_t tr;
	transform_initialize(tr);

	mesh_set_verts(buffer.mesh, buffer.verts, buffer.vert_count);
	
	render_add_mesh(buffer.mesh, text_styles[style].material, tr);
	buffer.vert_count = 0;
}