#include "../../StereoKitC/stereokit.h"

#include <vector>
using namespace std;
vector<solid_t> phys_objs;

transform_t floor_tr;
transform_t tr;
model_t     gltf;
model_t     box;

int main() {
	if (!sk_init("StereoKit C", sk_runtime_flatscreen))
		return 1;

	// Create a PBR floor material
	tex2d_t    tex_color = tex2d_create_file("../../Examples/Assets/floor_color.jpg");
	tex2d_t    tex_norm  = tex2d_create_file("../../Examples/Assets/floor_normal.jpg");
	material_t floor_mat = material_create("app/material_floor", shader_find("default/shader_pbr"));
	material_set_texture(floor_mat, "diffuse", tex_color);
	material_set_texture(floor_mat, "normal",  tex_norm);
	tex2d_release(tex_color);
	tex2d_release(tex_norm);

	// Procedurally create a cube model
	mesh_t mesh_cube = mesh_gen_cube("app/mesh_cube", { 1,1,1 }, 0);
	box  = model_create_mesh("app/model_cube", mesh_cube, floor_mat);
	mesh_release(mesh_cube);

	// Load a gltf model
	gltf = model_create_file("../../Examples/Assets/DamagedHelmet.gltf");

	// Build a physical floor!
	transform_set(floor_tr, { 0,-1.5f,0 }, vec3{ 5,1,5 }, { 0,0,0,1 });
	solid_t floor = solid_create(floor_tr._position, floor_tr._rotation, solid_type_immovable);
	solid_add_box (floor, floor_tr._scale);

	while (sk_step( []() {
		// Do hand input
		if (input_hand(handed_right).state & input_state_justpinch) {
			solid_t new_obj = solid_create({ 0,3,0 }, { 0,0,0,1 });
			solid_add_sphere(new_obj, 0.45f, 40);
			solid_add_box   (new_obj, vec3{1,1,1}*0.35f, 40);
			phys_objs.push_back(new_obj);
		}

		// Render solid helmets
		transform_set_scale(tr, vec3{ 1,1,1 }*0.25f);
		for (size_t i = 0; i < phys_objs.size(); i++) {
			solid_get_transform(phys_objs[i], tr);
			render_add_model   (gltf, tr);
		}
		
		// Render floor
		render_add_model(box, floor_tr);
	}));

	// Release everything
	for (size_t i = 0; i < phys_objs.size(); i++)
		solid_release(phys_objs[i]);
	solid_release(floor);
	model_release(gltf);
	material_release(floor_mat);
	model_release(box);

	sk_shutdown();
	return 0;
}