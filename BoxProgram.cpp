#include "BoxProgram.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

Scene::Drawable::Pipeline box_program_pipeline;

Load< BoxProgram > box_program(LoadTagEarly, []() -> BoxProgram const *
{
	BoxProgram *ret = new BoxProgram();

	{	// build the pipeline template
		box_program_pipeline.program = ret->program;
		box_program_pipeline.CLIP_FROM_OBJECT_mat4 = ret->CLIP_FROM_OBJECT_mat4;
		box_program_pipeline.LIGHT_FROM_NORMAL_mat3 = ret->LIGHT_FROM_NORMAL_mat3;
	}

	return ret;
});

BoxProgram::BoxProgram()
{
	program = gl_compile_program(
		// vertex shader
		"#version 330\n"
		"uniform mat4 CLIP_FROM_OBJECT;\n"
		"uniform mat3 LIGHT_FROM_NORMAL;\n"
		"in vec4 Position;\n"
		"in vec3 Normal;\n"
		"out vec3 normal;\n"
		"void main() {\n"
		"	gl_Position = CLIP_FROM_OBJECT * Position;\n"
		"	normal = LIGHT_FROM_NORMAL * Normal;\n"
		"}\n"
	,
		// fragment shader
		"#version 330\n"
		"in vec3 normal;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	vec3 albedo = vec3(1.0);\n"
		"	vec3 l = normalize(vec3(0.3, -0.5, 1.0));\n"	// fixed light direction in world space
		"	float e = 0.4 + 0.6 * max(0.0, dot(normalize(normal), l));\n"	// set e = 1.0 for flat white
		"	fragColor = vec4(e * albedo, 1.0);\n"
		"}\n"
	);

	{	// look up attribute and uniform locations
		Position_vec4 = glGetAttribLocation(program, "Position");
		Normal_vec3 = glGetAttribLocation(program, "Normal");

		CLIP_FROM_OBJECT_mat4 = glGetUniformLocation(program, "CLIP_FROM_OBJECT");
		LIGHT_FROM_NORMAL_mat3 = glGetUniformLocation(program, "LIGHT_FROM_NORMAL");
	}
}

BoxProgram::~BoxProgram()
{
	glDeleteProgram(program);
	program = 0;
}
