#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"
#include "Scene.hpp"
#include "Text.hpp"
#include "data_path.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

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
	Scene::Camera *camera = nullptr;

	// text
	TextRenderer title{data_path("UESC_Display_Font.otf"), 48};

};
