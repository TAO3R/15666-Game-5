#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b) {
		if (b.downs & 0x80) {
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t( (b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f) ) );
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(jump);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 5) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");
	
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	auto recv_button = [](uint8_t byte, Button *button) {
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255) {
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4+0], &left);
	recv_button(recv_buffer[4+1], &right);
	recv_button(recv_buffer[4+2], &up);
	recv_button(recv_buffer[4+3], &down);
	recv_button(recv_buffer[4+4], &jump);

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}


//-----------------------------------------

Game::Game()
{
	{	// hard-coded test level, 9 x 5 (both odd, so the camera sits over the middle cell)
		// index = y * width + x
		board.width = 9;
		board.height = 5;

		board.common_obstacle = { 4, 13, 31, 40 };	// wall down the middle at x = 4, gap at (4, 2)
		board.player_1_obstacle = { 20, 29 };		// (2, 2), (2, 3)
		board.player_2_obstacle = { 15, 24 };		// (6, 1), (6, 2)

		board.player_1_start = 10;	// (1, 1)
		board.player_2_start = 34;	// (7, 3)
	}

	{	// sanity check: everything on the board, nothing stacked
		uint32_t cell_count = uint32_t(board.width) * uint32_t(board.height);
		std::vector< bool > used(cell_count, false);
		auto place = [&](uint32_t cell)
		{
			assert(cell < cell_count && "board cell out of range");
			assert(!used[cell] && "two things placed on the same board cell");
			used[cell] = true;
		};

		for (uint32_t c : board.common_obstacle) { place(c); }
		for (uint32_t c : board.player_1_obstacle) { place(c); }
		for (uint32_t c : board.player_2_obstacle) { place(c); }
		place(board.player_1_start);
		place(board.player_2_start);
	}
}

Player *Game::spawn_player()
{
	// take whichever number is free, so a rejoining player fills the empty slot
	bool has_1 = false;
	for (auto const &p : players)
	{
		if (p.number == 1) { has_1 = true; }
	}

	players.emplace_back();
	Player &player = players.back();
	player.number = has_1 ? 2 : 1;
	player.cell = player.number == 1 ? board.player_1_start : board.player_2_start;

	return &player;
}

void Game::remove_player(Player *player) {
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi) {
		if (&*pi == player) {
			players.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

void Game::update(float elapsed) {
	{	// state machine
		// back to waiting if someone leaves mid game
		state = players.size() == MaxPlayers ? State::Playing : State::Waiting;
	}

	// movement is not implemented yet

	for (auto &p : players)
	{	// reset 'downs' since controls have been handled
		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
		p.controls.jump.downs = 0;
	}
}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer

	// game state
	connection.send(uint8_t(state));

	{	// board
		// cell lists are sent as [count, cells...]
		auto send_cells = [&](std::vector< uint32_t > const &cells)
		{
			connection.send(uint32_t(cells.size()));
			for (uint32_t c : cells) { connection.send(c); }
		};

		connection.send(board.width);
		connection.send(board.height);
		send_cells(board.common_obstacle);
		send_cells(board.player_1_obstacle);
		send_cells(board.player_2_obstacle);
		connection.send(board.player_1_start);
		connection.send(board.player_2_start);
	}

	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.number);
		connection.send(player.cell);
	};

	//player count:
	connection.send(uint8_t(players.size()));
	if (connection_player) send_player(*connection_player);
	for (auto const &player : players) {
		if (&player == connection_player) continue;
		send_player(player);
	}

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-3] = uint8_t(size);
	connection.send_buffer[mark-2] = uint8_t(size >> 8);
	connection.send_buffer[mark-1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	//copy bytes from buffer and advance position:
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	{	// game state
		uint8_t s;
		read(&s);
		if (s > uint8_t(State::Playing)) { throw std::runtime_error("Unknown game state " + std::to_string(int(s)) + "."); }
		state = State(s);
	}

	{	// board
		auto read_cells = [&](std::vector< uint32_t > *cells)
		{
			uint32_t count;
			read(&count);
			// each cell is 4 bytes, so a bogus count runs out of message before it runs out of memory
			if (count > (size - at) / sizeof(uint32_t)) { throw std::runtime_error("Cell list longer than state message."); }
			cells->resize(count);
			for (uint32_t &c : *cells) { read(&c); }
		};

		read(&board.width);
		read(&board.height);
		read_cells(&board.common_obstacle);
		read_cells(&board.player_1_obstacle);
		read_cells(&board.player_2_obstacle);
		read(&board.player_1_start);
		read(&board.player_2_start);
	}

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i) {
		players.emplace_back();
		Player &player = players.back();
		read(&player.number);
		read(&player.cell);
	}

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}
