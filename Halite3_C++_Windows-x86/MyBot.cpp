#include "hlt/game.hpp"
#include "hlt/constants.hpp"
#include "hlt/log.hpp"
#include <random>
#include <ctime>
#include <unordered_set>
#include <algorithm>
#include <cmath>

using namespace std;
using namespace hlt;

// Constants for game logic
constexpr int RETURN_THRESHOLD_TURNS = 25; // Turns before the game ends when ships should return to the shipyard.
constexpr double SHIP_FULL_THRESHOLD = 0.9; // Fraction of a ship's maximum halite considered "full".
constexpr double RETURN_HALITE_THRESHOLD = 0.5; // Fraction of max halite to prioritize ship return.
constexpr int RETURN_TO_BASE = 220; // Turn threshold for shipyard spawn logic.

// Comparator for sorting ships by halite amount in descending order.
struct ShipHaliteComparator {
    bool operator()(const shared_ptr<Ship>& a, const shared_ptr<Ship>& b) {
        return a->halite > b->halite;
    }
};

// Class for managing ships returning to the shipyard.
class ReturnScheduler {
private:
    unordered_set<int> returning_ships; // Track IDs of returning ships.
    shared_ptr<Ship> current_returning_ship; // Track the highest priority returning ship.

public:
    // Constructor
    ReturnScheduler() : current_returning_ship(nullptr) {}

    // Determine if a ship should return to the shipyard.
    bool should_return(shared_ptr<Ship> ship, int turn_number, const vector<shared_ptr<Ship>>& sorted_ships) {
        // Check if the ship is already returning.
        if (returning_ships.find(ship->id) != returning_ships.end()) {
            return true;
        }

        // Endgame condition: Start returning near the end of the game.
        if (turn_number >= constants::MAX_TURNS - RETURN_THRESHOLD_TURNS) {
            returning_ships.insert(ship->id);
            return true;
        }

        // Ship is considered full.
        if (ship->halite >= constants::MAX_HALITE * SHIP_FULL_THRESHOLD) {
            returning_ships.insert(ship->id);
            return true;
        }

        // Prioritize the ship with the most halite if none are returning.
        if (!current_returning_ship &&
            ship == sorted_ships[0] &&
            ship->halite >= constants::MAX_HALITE * RETURN_HALITE_THRESHOLD) {
            current_returning_ship = ship;
            returning_ships.insert(ship->id);
            return true;
        }

        return false;
    }

    // Update the status of a ship after it reaches the base.
    void update_status(shared_ptr<Ship> ship, const Position& base_pos) {
        if (ship->position == base_pos) {
            returning_ships.erase(ship->id);
            if (current_returning_ship && current_returning_ship->id == ship->id) {
                current_returning_ship = nullptr;
            }
        }
    }

    // Check if a specific ship is returning.
    bool is_returning(int ship_id) const {
        return returning_ships.find(ship_id) != returning_ships.end();
    }
};

int main(int argc, char* argv[]) {
    // Seed for random number generation.
    unsigned int rng_seed;
    if (argc > 1) {
        rng_seed = static_cast<unsigned int>(stoul(argv[1]));
    }
    else {
        rng_seed = static_cast<unsigned int>(time(nullptr));
    }
    mt19937 rng(rng_seed);

    // Initialize the game.
    Game game;
    game.ready("LaBeteDuMaroc");

    // Initialize the return scheduler.
    ReturnScheduler return_scheduler;

    // Main game loop.
    for (;;) {
        game.update_frame(); // Update the game state.
        shared_ptr<Player> me = game.me;
        unique_ptr<GameMap>& game_map = game.game_map;

        vector<Command> command_queue; // Queue for storing commands.
        unordered_set<Position> targeted_positions; // Track positions targeted by ships.

        // Sort ships by halite amount.
        vector<shared_ptr<Ship>> sorted_ships;
        for (const auto& ship_iterator : me->ships) {
            sorted_ships.push_back(ship_iterator.second);
        }
        sort(sorted_ships.begin(), sorted_ships.end(), ShipHaliteComparator());

        // Process each ship.
        for (const auto& ship : sorted_ships) {
            // Update ship status based on current position.
            return_scheduler.update_status(ship, me->shipyard->position);

            // Determine if the ship should return to base.
            if (return_scheduler.should_return(ship, game.turn_number, sorted_ships)) {
                Direction move_to_base = game_map->naive_navigate(ship, me->shipyard->position);
                Position target_position = ship->position.directional_offset(move_to_base);

                // Ensure no position conflict.
                if (targeted_positions.count(target_position) == 0) {
                    command_queue.push_back(ship->move(move_to_base));
                    targeted_positions.insert(target_position);
                }
                else {
                    command_queue.push_back(ship->stay_still());
                }
                continue;
            }

            // Collect halite if the current cell has sufficient resources.
            if (game_map->at(ship)->halite >= constants::MAX_HALITE / 10) {
                command_queue.push_back(ship->stay_still());
                continue;
            }

            // Search for the best nearby position with the most halite.
            Position best_position = ship->position;
            int max_halite = game_map->at(ship)->halite;

            for (const Direction& direction : ALL_CARDINALS) {
                Position pos = ship->position.directional_offset(direction);
                if (!game_map->at(pos)->is_occupied()) {
                    int halite = game_map->at(pos)->halite;
                    if (halite > max_halite) {
                        max_halite = halite;
                        best_position = pos;
                    }
                }
            }

            // Move to the best position or stay still if already optimal.
            if (best_position != ship->position) {
                Direction move = game_map->naive_navigate(ship, best_position);
                Position target_position = ship->position.directional_offset(move);

                // Ensure no position conflict.
                if (targeted_positions.count(target_position) == 0) {
                    command_queue.push_back(ship->move(move));
                    targeted_positions.insert(target_position);
                }
                else {
                    command_queue.push_back(ship->stay_still());
                }
            }
            else {
                command_queue.push_back(ship->stay_still());
            }
        }

        // Spawn a new ship if conditions allow.
        if (game.turn_number <= RETURN_TO_BASE &&
            me->halite >= constants::SHIP_COST &&
            !game_map->at(me->shipyard->position)->is_occupied()) {
            command_queue.push_back(me->shipyard->spawn());
        }

        // End turn with the queued commands.
        if (!game.end_turn(command_queue)) {
            break;
        }
    }

    return 0;
}
