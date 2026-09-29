#pragma once

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <vector>

struct Connection;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
	C2S_Controls = 1, //Greg!
	S2C_State = 's',
	//...
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down, jump;

		void send_controls_message(Connection *connection) const;

		//returns 'false' if no message or not a controls message,
		//returns 'true' if read a controls message,
		//throws on malformed controls message
		bool recv_controls_message(Connection *connection);
	} controls;

	//player state (sent from server):
	uint8_t number = 0;	// 1 or 2, decides start cell, color and which obstacles this player can push
	uint32_t cell = 0;	// grid index into the board, y * width + x
};

// level layout, only filled on the server; clients get it from the state message
// all cells are grid indices y * width + x, (0, 0) is the lower-left corner
struct GameBoard {
	uint8_t width = 0;
	uint8_t height = 0;

	std::vector< uint32_t > common_obstacle;
	std::vector< uint32_t > player_1_obstacle;	// seen by both players, only pushable by player 1
	std::vector< uint32_t > player_2_obstacle;	// seen by both players, only pushable by player 2

	uint32_t player_1_start = 0;
	uint32_t player_2_start = 0;

	// goals are floor cells; level clears when every goal has that player's obstacle or that player on it
	std::vector< uint32_t > player_1_goal;
	std::vector< uint32_t > player_2_goal;

	bool operator==(GameBoard const &) const = default;	// client rebuilds the scene only when the board changes
};

struct Game {
	// game state machine, decided on the server and sent to clients
	enum class State : uint8_t {
		Waiting = 0,	// waiting for MaxPlayers to join, scene is not drawn
		Playing = 1,
		Cleared = 2		// every goal is covered, scene is drawn with a "Level Clear" overlay
	} state = State::Waiting;
	inline static constexpr uint32_t MaxPlayers = 2;	// coop game, needs exactly two players

	// level
	GameBoard board;
	bool is_cleared() const;	// server side win check, see GameBoard goals

	std::list< Player > players; //(using list so they can have stable addresses)
	Player *spawn_player(); //add player the end of the players list (may also, e.g., play some spawn anim)
	void remove_player(Player *); //remove player from game (may also, e.g., play some despawn anim)

	Game();

	//state update function:
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
