#include "PlayMode.hpp"

#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "BoxProgram.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/color_space.hpp>
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

// board layout
static constexpr float CellSize = 2.0f;	// cube.pnct is 2m on a side, one cube per cell
static constexpr float CameraHeight = 15.0f;

// colors are written as the sRGB hex you see on screen, the framebuffer does the sRGB encoding
static glm::vec3 srgb(uint32_t hex)
{
	return glm::convertSRGBToLinear(glm::vec3((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff) / 255.0f);
}
static glm::vec3 const FloorColorA = srgb(0xadd8e6);	// light blue
static glm::vec3 const FloorColorB = srgb(0xd6ecf3);	// lighter blue
static glm::vec3 const WallColor = srgb(0xffffff);	// perimeter and common obstacles
static std::array< glm::vec3, 2 > const PlayerColors = { srgb(0xff0000), srgb(0x00c000) };	// player 1 red, player 2 green, also used by their obstacles

// center of cell (x, y) in world space, grid starts at the origin
static glm::vec3 cell_center(int32_t x, int32_t y, float z)
{
	return glm::vec3(CellSize * float(x), CellSize * float(y), z);
}

PlayMode::PlayMode(Client &client_) : client(client_)
{
	{	// camera straight above the board, looking down
		// real position is set in build_board_scene once the board size is known
		camera_transform.name = "Camera";

		float pitch = glm::radians(0.0f);	// camera looks along its local -z already, so 0 is straight down; only tilt around x
		camera_transform.rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
		camera_transform.position = glm::vec3(0.0f, 0.0f, CameraHeight);
	}
}

void PlayMode::build_board_scene(GameBoard const &board)
{
	scene.drawables.clear();	// drawables point at transforms, so clear them first
	scene.transforms.clear();
	player_transforms = { nullptr, nullptr };
	player_drawables = { nullptr, nullptr };

	Mesh const &mesh = cube_meshes->lookup("Cube");	// mesh name from cube.blend

	// copy the cube to 'position' with a flat color
	auto add_cube = [&](std::string const &name, glm::vec3 const &position, glm::vec3 const &scale, glm::vec3 const &color) -> Scene::Drawable &
	{
		scene.transforms.emplace_back();
		Scene::Transform *transform = &scene.transforms.back();
		transform->name = name;
		transform->position = position;
		transform->scale = scale;

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();
		drawable.pipeline = box_program_pipeline;
		drawable.pipeline.vao = cube_vao;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
		drawable.pipeline.set_uniforms = [color]()
		{
			glUniform3fv(box_program->COLOR_vec3, 1, glm::value_ptr(color));
		};
		return drawable;
	};

	int32_t w = int32_t(board.width);
	int32_t h = int32_t(board.height);
	glm::vec3 const full = glm::vec3(1.0f);

	{	// floor on z = -1, checkerboard so neighbors never share a color
		for (int32_t y = 0; y < h; y++)
		{
			for (int32_t x = 0; x < w; x++)
			{
				add_cube("Floor", cell_center(x, y, -1.0f), full, (x + y) % 2 == 0 ? FloorColorA : FloorColorB);
			}
		}
	}

	{	// perimeter walls on z = 1, one ring outside the w x h area
		for (int32_t y = -1; y <= h; y++)
		{
			for (int32_t x = -1; x <= w; x++)
			{
				bool on_ring = (x == -1 || x == w || y == -1 || y == h);
				if (on_ring) { add_cube("Wall", cell_center(x, y, 1.0f), full, WallColor); }
			}
		}
	}

	{	// obstacles on z = 1, standing on the floor
		auto add_obstacles = [&](std::string const &name, std::vector< uint32_t > const &cells, glm::vec3 const &color)
		{
			for (uint32_t c : cells)
			{
				add_cube(name, cell_center(int32_t(c % board.width), int32_t(c / board.width), 1.0f), full, color);
			}
		};
		add_obstacles("CommonObstacle", board.common_obstacle, WallColor);
		add_obstacles("Player1Obstacle", board.player_1_obstacle, PlayerColors[0]);
		add_obstacles("Player2Obstacle", board.player_2_obstacle, PlayerColors[1]);
	}

	{	// players, 0.8 scale so z = 0.8 puts their bottom on the floor
		// positions are synced every frame in update(), hidden until that player shows up
		for (uint32_t i = 0; i < 2; i++)
		{
			Scene::Drawable &drawable = add_cube("Player" + std::to_string(i + 1), glm::vec3(0.0f), glm::vec3(0.8f), PlayerColors[i]);
			drawable.pipeline.count = 0;
			player_transforms[i] = drawable.transform;
			player_drawables[i] = &drawable;
		}
	}

	{	// center the camera on the board
		// even size lands between the two middle cells, odd size on the middle cell
		camera_transform.position.x = 0.5f * CellSize * float(w - 1);
		camera_transform.position.y = 0.5f * CellSize * float(h - 1);
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

	{	// rebuild the scene only when the server sends a different board
		if (game.board.width && game.board.height && !(game.board == built_board))
		{
			build_board_scene(game.board);
			built_board = game.board;
		}
	}

	if (player_drawables[0])
	{	// sync player cubes with the latest server state
		GLuint mesh_count = cube_meshes->lookup("Cube").count;
		for (uint32_t i = 0; i < 2; i++)
		{
			player_drawables[i]->pipeline.count = 0;
		}
		for (auto const &p : game.players)
		{
			if (p.number < 1 || p.number > 2) { continue; }
			uint32_t i = p.number - 1;
			player_transforms[i]->position = cell_center(int32_t(p.cell % built_board.width), int32_t(p.cell / built_board.width), 0.8f);
			player_drawables[i]->pipeline.count = mesh_count;
		}
	}
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
				camera.aspect = float(drawable_size.x) / float(drawable_size.y);
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS);
				scene.draw(camera);
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
	centered(subtitle, "Waiting  for  player  1/2", y, white);
}
