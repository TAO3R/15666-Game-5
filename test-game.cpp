// assert-based check for the grid movement / push rules in Game::try_move and Game::update
// built with the default targets, run: dist/test-game

#include "Game.hpp"

#include <cassert>
#include <iostream>

// 7 x 1 corridor, cells 0..6 left to right; each case sets its own obstacles
static void setup(Game &game, Player *&p1, Player *&p2)
{
	game.board = GameBoard();
	game.board.width = 7;
	game.board.height = 1;
	p1 = &game.players.front();
	p2 = &game.players.back();
	p1->cell = 0;
	p2->cell = 6;
}

int main()
{
	Game game;
	Player *p1 = game.spawn_player();
	Player *p2 = game.spawn_player();
	assert(p1->number == 1 && p2->number == 2);

	{	// free floor: just walk
		setup(game, p1, p2);
		game.try_move(*p1, 1, 0);
		assert(p1->cell == 1);
	}

	{	// edge of the level blocks
		setup(game, p1, p2);
		game.try_move(*p1, -1, 0);
		assert(p1->cell == 0);
	}

	{	// push a chain of two own obstacles
		setup(game, p1, p2);
		game.board.player_1_obstacle = { 1, 2 };
		game.try_move(*p1, 1, 0);
		assert(p1->cell == 1 && game.board.player_1_obstacle == std::vector< uint32_t >({ 2, 3 }));
	}

	{	// chain with the other player's obstacle in it doesn't move at all
		setup(game, p1, p2);
		game.board.player_1_obstacle = { 1 };
		game.board.player_2_obstacle = { 2 };
		game.try_move(*p1, 1, 0);
		assert(p1->cell == 0 && game.board.player_1_obstacle[0] == 1 && game.board.player_2_obstacle[0] == 2);
	}

	{	// chain with a common obstacle behind it doesn't move
		setup(game, p1, p2);
		game.board.player_1_obstacle = { 1, 2 };
		game.board.common_obstacle = { 3 };
		game.try_move(*p1, 1, 0);
		assert(p1->cell == 0 && game.board.player_1_obstacle == std::vector< uint32_t >({ 1, 2 }));
	}

	{	// chain can't be pushed out of the level
		setup(game, p1, p2);
		p2->cell = 0;
		p1->cell = 4;
		game.board.player_1_obstacle = { 5, 6 };
		game.try_move(*p1, 1, 0);
		assert(p1->cell == 4 && game.board.player_1_obstacle == std::vector< uint32_t >({ 5, 6 }));
	}

	{	// chain can't be pushed into the other player
		setup(game, p1, p2);
		game.board.player_1_obstacle = { 1 };
		p2->cell = 2;
		game.try_move(*p1, 1, 0);
		assert(p1->cell == 0 && game.board.player_1_obstacle[0] == 1);
	}

	{	// players block each other
		setup(game, p1, p2);
		p2->cell = 1;
		game.try_move(*p1, 1, 0);
		assert(p1->cell == 0 && p2->cell == 1);
	}

	{	// can't push the other player's obstacle alone either
		setup(game, p1, p2);
		game.board.player_2_obstacle = { 1 };
		game.try_move(*p1, 1, 0);
		assert(p1->cell == 0 && game.board.player_2_obstacle[0] == 1);
	}

	{	// vertical push uses y * width + x
		game.board = GameBoard();
		game.board.width = 3;
		game.board.height = 3;
		p1->cell = 1;	// (1, 0)
		p2->cell = 0;
		game.board.player_1_obstacle = { 4 };	// (1, 1)
		game.try_move(*p1, 0, 1);
		assert(p1->cell == 4 && game.board.player_1_obstacle[0] == 7);
	}

	{	// clear detection, freeze once cleared, space resets
		setup(game, p1, p2);
		game.board.player_1_obstacle = { 2 };
		game.board.player_1_goal = { 3 };
		game.board.player_2_goal = { 6 };	// player 2 already stands on it
		game.initial_board = game.board;
		game.initial_board.player_1_start = 0;
		game.initial_board.player_2_start = 6;

		game.state = Game::State::Playing;
		game.update(Game::Tick);
		assert(game.state == Game::State::Playing);

		p1->controls.right.downs = 1;	// 0 -> 1, free
		game.update(Game::Tick);
		p1->controls.right.downs = 1;	// 1 -> 2, pushes obstacle 2 -> 3 onto the goal
		game.update(Game::Tick);
		assert(p1->cell == 2 && game.board.player_1_obstacle[0] == 3);
		assert(game.state == Game::State::Cleared);

		p1->controls.left.downs = 1;	// frozen
		game.update(Game::Tick);
		assert(p1->cell == 2 && game.state == Game::State::Cleared);

		p2->controls.jump.downs = 1;	// either player resets
		game.update(Game::Tick);
		assert(p1->cell == 0 && game.board.player_1_obstacle[0] == 2 && game.state == Game::State::Playing);
	}

	{	// someone leaving resets the level and goes back to waiting
		p1->controls.right.downs = 1;
		game.update(Game::Tick);
		assert(p1->cell == 1);
		game.remove_player(p2);
		game.update(Game::Tick);
		assert(game.state == Game::State::Waiting && p1->cell == 0);
	}

	std::cout << "test-game: all checks passed" << std::endl;
	return 0;
}
