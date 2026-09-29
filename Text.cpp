#include "Text.hpp"
#include "ColorTextureProgram.hpp"

#include "Load.hpp"
#include "gl_errors.hpp"

#include <hb-ft.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <stdexcept>

namespace {
	// what color_texture_program wants per vertex
	struct Vertex {
		glm::vec2 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	static_assert(sizeof(Vertex) == 2*4 + 4 + 2*4, "Vertex is packed");
}

// all TextRenderer::draw calls share one buffer + vertex array, same pattern as DrawLines.cpp
static GLuint vertex_buffer = 0;
static GLuint vertex_buffer_for_color_texture_program = 0;

static Load< void > setup_buffers(LoadTagDefault, []()
{
	{	// vertex buffer, filled on every draw
		glGenBuffers(1, &vertex_buffer);
	}

	{	// vertex array mapping buffer for color_texture_program
		glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);
		glBindVertexArray(vertex_buffer_for_color_texture_program);

		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

		// binding a vec2 to a vec4 attribute is fine, z gets 0.0 and w gets 1.0
		glVertexAttribPointer(color_texture_program->Position_vec4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Position));
		glEnableVertexAttribArray(color_texture_program->Position_vec4);

		glVertexAttribPointer(color_texture_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Color));
		glEnableVertexAttribArray(color_texture_program->Color_vec4);

		glVertexAttribPointer(color_texture_program->TexCoord_vec2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, TexCoord));
		glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}

	GL_ERRORS();
});

TextRenderer::TextRenderer(std::string const &font_path, uint32_t pixel_size)
{
	if (FT_Init_FreeType(&library)) { throw std::runtime_error("failed to init freetype"); }
	if (FT_New_Face(library, font_path.c_str(), 0, &face)) { throw std::runtime_error("failed to load font '" + font_path + "'"); }
	if (FT_Set_Pixel_Sizes(face, 0, pixel_size)) { throw std::runtime_error("failed to set font pixel size"); }
	font = hb_ft_font_create_referenced(face);
}

TextRenderer::~TextRenderer()
{
	for (auto const &[index, glyph] : cache)
	{
		if (glyph.tex) { glDeleteTextures(1, &glyph.tex); }
	}
	if (font) { hb_font_destroy(font); }
	if (face) { FT_Done_Face(face); }
	if (library) { FT_Done_FreeType(library); }
}

float TextRenderer::line_height() const
{
	return face->size->metrics.height / 64.0f;
}

float TextRenderer::ascender() const
{
	return face->size->metrics.ascender / 64.0f;
}

float TextRenderer::descender() const
{
	return -face->size->metrics.descender / 64.0f;	// freetype reports this as negative
}

TextRenderer::Glyph const &TextRenderer::get(uint32_t glyph_index)
{
	auto found = cache.find(glyph_index);
	if (found != cache.end()) { return found->second; }

	if (FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER)) { throw std::runtime_error("failed to render glyph"); }
	FT_Bitmap const &bitmap = face->glyph->bitmap;

	Glyph glyph;
	glyph.size = glm::ivec2(bitmap.width, bitmap.rows);
	glyph.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top);

	if (bitmap.width && bitmap.rows)
	{
		glGenTextures(1, &glyph.tex);
		glBindTexture(GL_TEXTURE_2D, glyph.tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);	// rows of an 8-bit bitmap aren't 4-byte aligned
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, bitmap.width, bitmap.rows, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.buffer);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		GLint swizzle[4] = { GL_ONE, GL_ONE, GL_ONE, GL_RED };	// coverage shows up as alpha
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	return cache.emplace(glyph_index, glyph).first->second;
}

std::vector< TextRenderer::Quad > TextRenderer::shape(std::string const &utf8)
{
	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, utf8.c_str(), int(utf8.size()), 0, int(utf8.size()));
	hb_buffer_guess_segment_properties(buf);	// direction / script / language from the text itself
	hb_shape(font, buf, nullptr, 0);

	unsigned int count = 0;
	hb_glyph_info_t const *infos = hb_buffer_get_glyph_infos(buf, &count);
	hb_glyph_position_t const *positions = hb_buffer_get_glyph_positions(buf, &count);

	std::vector< Quad > quads;
	quads.reserve(count);
	glm::vec2 pen = glm::vec2(0.0f);
	for (unsigned int i = 0; i < count; ++i)
	{
		Glyph const &glyph = get(infos[i].codepoint);	// after shaping, 'codepoint' is a glyph index
		if (glyph.tex)
		{
			Quad quad;
			quad.tex = glyph.tex;
			quad.min = pen
				+ glm::vec2(positions[i].x_offset, positions[i].y_offset) / 64.0f
				+ glm::vec2(float(glyph.bearing.x), float(glyph.bearing.y - glyph.size.y));
			quad.max = quad.min + glm::vec2(glyph.size);
			quads.emplace_back(quad);
		}
		pen += glm::vec2(positions[i].x_advance, positions[i].y_advance) / 64.0f;
	}

	hb_buffer_destroy(buf);
	return quads;
}

float TextRenderer::measure(std::string const &utf8)
{
	// same shaping pass as shape(), but only the advances are kept
	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, utf8.c_str(), int(utf8.size()), 0, int(utf8.size()));
	hb_buffer_guess_segment_properties(buf);
	hb_shape(font, buf, nullptr, 0);

	unsigned int count = 0;
	hb_glyph_position_t const *positions = hb_buffer_get_glyph_positions(buf, &count);

	float width = 0.0f;
	for (unsigned int i = 0; i < count; ++i) { width += positions[i].x_advance / 64.0f; }

	hb_buffer_destroy(buf);
	return width;
}

void TextRenderer::draw(std::vector< Quad > const &quads, glm::mat4 const &clip_from_pixel, glm::u8vec4 const &color)
{
	if (quads.empty()) { return; }

	{	// two triangles per quad
		// v is flipped because freetype's bitmap row 0 is the top row, while quad y is up
		std::vector< Vertex > attribs;
		attribs.reserve(quads.size() * 6);
		for (auto const &quad : quads)
		{
			Vertex const bl = { glm::vec2(quad.min.x, quad.min.y), color, glm::vec2(0.0f, 1.0f) };
			Vertex const br = { glm::vec2(quad.max.x, quad.min.y), color, glm::vec2(1.0f, 1.0f) };
			Vertex const tl = { glm::vec2(quad.min.x, quad.max.y), color, glm::vec2(0.0f, 0.0f) };
			Vertex const tr = { glm::vec2(quad.max.x, quad.max.y), color, glm::vec2(1.0f, 0.0f) };
			attribs.insert(attribs.end(), { bl, br, tl,  br, tr, tl });
		}

		// upload every glyph's vertices at once; only the texture binding changes between draws
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, attribs.size() * sizeof(attribs[0]), attribs.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	glUseProgram(color_texture_program->program);
	glUniformMatrix4fv(color_texture_program->CLIP_FROM_OBJECT_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_pixel));
	glBindVertexArray(vertex_buffer_for_color_texture_program);
	glActiveTexture(GL_TEXTURE0);	// color_texture_program's sampler is pinned to unit 0

	// glyph coverage arrives as alpha (see the swizzle in get()), so it needs blending
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// ponytail: one texture bind + draw call per glyph; pack glyphs into an atlas if this ever shows up in a profile
	for (size_t i = 0; i < quads.size(); ++i)
	{
		glBindTexture(GL_TEXTURE_2D, quads[i].tex);
		glDrawArrays(GL_TRIANGLES, GLsizei(i * 6), 6);
	}

	glDisable(GL_BLEND);	// nothing else in this project blends, so leave it off
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);

	GL_ERRORS();
}

void TextRenderer::draw(std::string const &utf8, glm::uvec2 const &drawable_size, glm::vec2 const &origin, glm::u8vec4 const &color)
{
	draw(shape(utf8), clip_from_pixel(drawable_size, origin), color);
}
