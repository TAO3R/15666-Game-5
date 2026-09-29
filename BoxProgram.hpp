#pragma once

#include "GL.hpp"
#include "Load.hpp"
#include "Scene.hpp"

// box material: flat COLOR albedo with a fixed light so the faces are distinguishable
struct BoxProgram {
	BoxProgram();
	~BoxProgram();

	GLuint program = 0;

	// attribute locations
	GLuint Position_vec4 = -1U;
	GLuint Normal_vec3 = -1U;

	// uniform locations
	GLuint CLIP_FROM_OBJECT_mat4 = -1U;
	GLuint LIGHT_FROM_NORMAL_mat3 = -1U;
	GLuint COLOR_vec3 = -1U;	// albedo, linear rgb; set per drawable through pipeline.set_uniforms
};

extern Load< BoxProgram > box_program;

// pipeline template, copy this into a drawable then fill vao/type/start/count
extern Scene::Drawable::Pipeline box_program_pipeline;
