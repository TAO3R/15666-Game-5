#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"
#include "Scene.hpp"
#include "Text.hpp"
#include "data_path.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <array>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking for local player:
	Player::Controls controls;

	//latest game state (from server):
	Game game;

	//last message from server:
	std::string server_message;

	//connection to server:
	Client &client;

	// 3d scene, client side rendering only
	Scene scene;
	Scene::Transform camera_transform;	// lives outside the scene so rebuilding the board doesn't touch it
	Scene::Camera camera{&camera_transform};

	// board rendering
	GameBoard built_board;	// the board the current scene was built from
	void build_board_scene(GameBoard const &board);	// clears the scene and lays out floor, walls, obstacles
	std::array< Scene::Transform *, 2 > player_transforms = { nullptr, nullptr };	// indexed by player number - 1
	std::array< Scene::Drawable *, 2 > player_drawables = { nullptr, nullptr };		// hidden (count = 0) while that player is absent

	// text
	TextRenderer title{data_path("UESC_Display_Font.otf"), 96};
	TextRenderer subtitle{data_path("UESC_Display_Font.otf"), 32};
	void draw_waiting(glm::uvec2 const &drawable_size);	// title screen while waiting for the other player

};
