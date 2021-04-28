#include "demo_lines.h"

#include "../../StereoKitC/stereokit.h"
#include "../../StereoKitC/stereokit_ui.h"
using namespace sk;

#include <vector>
#include <string>
using namespace std;

///////////////////////////////////////////

sound_t        mic_input = {};
vector<string> mic_device_names;
string         mic_active = "";

pose_t         window_pose;

sprite_t   mic_sprite;
mesh_t     mic_visual_mesh;
material_t mic_visual_mat;
float      mic_intensity = 0;
float      mic_intensity_dest = 0;

float *sample_buffer   = nullptr;
size_t sample_capacity = 0;
size_t sample_count    = 0;

///////////////////////////////////////////

void demo_mic_init() {
	mic_input = mic_start();

	for (size_t i = 0; i < mic_device_count(); i++) {
		mic_device_names.push_back(mic_device_name(i));
	}

	window_pose = pose_t{ {0.5f,0,-0.5f}, quat_lookat(vec3_zero, {-1,0,1}) };

	mic_sprite      = sprite_create_file("mic_icon.png", sprite_type_single);
	mic_visual_mesh = mesh_find       (default_id_mesh_sphere);
	mic_visual_mat  = material_copy_id(default_id_material_unlit);
	material_set_transparency(mic_visual_mat, transparency_blend);
}

///////////////////////////////////////////

void switch_mic(string mic) {
	mic_active = mic;
	if (mic == "") mic_input = mic_start(nullptr);
	else           mic_input = mic_start(mic.c_str());
	if (mic_input == nullptr) log_warn("Failed to set mic!");
}

///////////////////////////////////////////

void demo_mic_update() {
	ui_window_begin("Mic devices", window_pose);
	bool32_t toggle_val = mic_active == "";
	if (ui_toggle("Default", toggle_val) && toggle_val)
		switch_mic("");

	for (size_t i = 0; i < mic_device_names.size(); i++) {
		toggle_val = mic_active == mic_device_names[i];
		if (ui_toggle(mic_device_names[i].c_str(), toggle_val) && toggle_val)
			switch_mic(mic_device_names[i]);
	}
	ui_window_end();

	if (mic_input != nullptr) {
		size_t unread = sound_unread_s(mic_input);
		if (unread > sample_capacity) {
			sample_capacity = unread;
			free(sample_buffer);
			sample_buffer = (float*)malloc(sizeof(float) * sample_capacity);
		}
		sample_count = sound_read_s(mic_input, sample_buffer, sample_capacity);
		if (sample_count > 0) {
			float avg = 0;
			for (size_t i = 0; i < sample_count; i++) {
				avg += fabsf(sample_buffer[i]);
			}
			avg = avg / sample_count;
			avg = 1 - avg;
			mic_intensity_dest = 1-(avg*avg);
		}

		mic_intensity = mic_intensity + (mic_intensity_dest - mic_intensity) * time_elapsedf() * 16;
		float    scale = 0.1f + 0.1*mic_intensity;
		color128 color = { 1,1,1, fmaxf(0.1f,mic_intensity) };
		render_add_mesh(mic_visual_mesh, mic_visual_mat, matrix_trs({0,0,-0.5f}, quat_identity, vec3_one*scale), color);
		sprite_draw(mic_sprite, matrix_trs({ -0.03f,0.03f,-0.5f }, quat_identity, vec3_one * 0.06f));
	} else {
		render_add_mesh(mic_visual_mesh, mic_visual_mat, matrix_trs({0,0,-0.5f}, quat_identity, vec3_one*0.1f), { 1,0,0, 0.1f });
		sprite_draw(mic_sprite, matrix_trs({ -0.03f,0.03f,-0.5f }, quat_identity, vec3_one * 0.06f));
	}
}

///////////////////////////////////////////

void demo_mic_shutdown() {
	mic_stop();

	sprite_release  (mic_sprite);
	mesh_release    (mic_visual_mesh);
	material_release(mic_visual_mat);
}