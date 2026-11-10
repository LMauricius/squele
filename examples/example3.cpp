// Combining views: a map computes owned results, and an extend zips sources
// with function results into items with named members.

#include "squele/core/ledger.hpp"
#include "squele/queries/extend.hpp"
#include "squele/queries/map.hpp"
#include "squele/queries/sort.hpp"
#include "squele/views/slotStore.hpp"

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

using namespace squele;

struct Player {
    std::string name;
    int points;
};

// Sorts players by points, highest first
struct ByPoints {
    bool operator()(const Player &a, const Player &b) const {
        return a.points > b.points;
    }
};

// A named item: the members follow the arguments of the Extend
struct Ranked {
    const Player *player;
    int rank; // the result of RankOf
};

// A function result per item: the player's bucket of 100 points
struct RankOf {
    int operator()(const Player &p) const { return p.points / 100; }
};

int main() {
    auto players = std::make_shared<SlotStore<Player>>();
    players->emplace("ada", 320);
    auto k_bob = players->emplace("bob", 150);
    players->emplace("cy", 280);

    Ledger ledger;
    // A map owns its results: one name length per player, recomputed only
    // when that player changes
    auto lengths = ledger.get(
        Map{From{players}, [](const Player &p) { return int(p.name.size()); }});

    // An extend over the sorted players: each item points at a player and
    // holds its rank bucket. Item types other than pair/tuple are named as
    // the first template argument
    using Sorted = Sort<From<SlotStore<Player>>, ByPoints>;
    const Sorted sorted{From{players}, ByPoints{}};
    auto board = ledger.get(Extend<Ranked, Sorted, RankOf>{sorted, RankOf{}});

    auto print = [&] {
        for (auto it = board->begin(); it != board->end(); ++it) {
            const Ranked &r = *it;
            std::printf("%u. %s (%d points, bucket %d)\n", it.index() + 1,
                        r.player->name.c_str(), r.player->points, r.rank);
        }
    };
    print(); // ada 320, cy 280, bob 150

    // bob overtakes everyone; only bob's item is rebuilt, the others move
    players->modify(k_bob).points = 400;
    board->sync();
    lengths->sync();
    print(); // bob 400, ada 320, cy 280
    assert((*board)[0].player->name == "bob" && (*board)[0].rank == 4);
    assert((*lengths)[k_bob] == 3);
    return 0;
}
