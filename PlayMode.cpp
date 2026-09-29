#include "PlayMode.hpp"

#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "BoxProgram.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <random>
#include <array>

// cube mesh, vao is bound to the box material
static GLuint cube_vao = 0;
static Load< MeshBuffer > cube_meshes(LoadTagDefault, []() -> MeshBuffer const *
{
	MeshBuffer const *ret = new MeshBuffer(data_path("cube.pnct"));
	cube_vao = ret->make_vao_for_program(box_program->program);
	return ret;
});

PlayMode::PlayMode(Client &client_) : client(client_)
{
	{	// cube at the origin
		scene.transforms.emplace_back();
		Scene::Transform *cube_transform = &scene.transforms.back();
		cube_transform->name = "Cube";

		Mesh const &mesh = cube_meshes->lookup("Cube");	// mesh name from cube.blend
		scene.drawables.emplace_back(cube_transform);
		Scene::Drawable &drawable = scene.drawables.back();
		drawable.pipeline = box_program_pipeline;
		drawable.pipeline.vao = cube_vao;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
	}

	{	// camera 15m straight above the origin, looking down
		scene.transforms.emplace_back();
		Scene::Transform *camera_transform = &scene.transforms.back();
		camera_transform->name = "Camera";

		float pitch = glm::radians(0.0f);	// camera looks along its local -z already, so 0 is straight down; only tilt around x
		camera_transform->rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
		camera_transform->position = glm::vec3(0.0f, 0.0f, 15.0f);

		scene.cameras.emplace_back(camera_transform);
		camera = &scene.cameras.back();
	}
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.repeat) {
			//ignore repeats
		} else if (evt.key.key == SDLK_A) {
			controls.left.downs += 1;
			controls.left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_D) {
			controls.right.downs += 1;
			controls.right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			controls.up.downs += 1;
			controls.up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_S) {
			controls.down.downs += 1;
			controls.down.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			controls.jump.downs += 1;
			controls.jump.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_A) {
			controls.left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_D) {
			controls.right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_W) {
			controls.up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_S) {
			controls.down.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			controls.jump.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//queue data for sending to server:
	controls.send_controls_message(&client.connection);

	//reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.jump.downs = 0;

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
			bool handled_message;
			try {
				do {
					handled_message = false;
					if (game.recv_state_message(c)) handled_message = true;
				} while (handled_message);
			} catch (std::exception const &e) {
				std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
				//quit the game:
				throw e;
			}
		}
	}, 0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size)
{
	{	// clear to the gray background
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClearDepth(1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	switch (game.state)
	{
		case Game::State::Waiting:
			draw_waiting(drawable_size);
			break;
		case Game::State::Playing:
			{	// 3d scene
				camera->aspect = float(drawable_size.x) / float(drawable_size.y);
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS);
				scene.draw(*camera);
			}
			break;
	}

	GL_ERRORS();
}

void PlayMode::draw_waiting(glm::uvec2 const &drawable_size)
{
	glDisable(GL_DEPTH_TEST);

	glm::u8vec4 const white = glm::u8vec4(0xff, 0xff, 0xff, 0xff);
	constexpr float line_gap = 0.5f;	// space between title and subtitle, in title line heights

	// horizontally center one line on its measured width
	auto centered = [&drawable_size](TextRenderer &font, std::string const &text, float baseline_y, glm::u8vec4 const &color)
	{
		float x = 0.5f * (float(drawable_size.x) - font.measure(text));
		font.draw(text, drawable_size, glm::vec2(x, baseline_y), color);
	};

	// title baseline on the vertical center, subtitle hangs below it
	float y = 0.5f * float(drawable_size.y);
	centered(title, "SoCoopBan", y, white);

	y -= title.descender() + line_gap * title.line_height() + subtitle.ascender();
	// the only way to be waiting is being the first of two players, so this is always 1/2
	centered(subtitle, "Waiting for player 1/2", y, white);
}
