#pragma once
#include <vector>

// ---------------------------------------------------------------------------
//  Joins overlapping hit borders of the same kind into one shape: one even
//  fill (no darker overlaps) and one outline around the outside (no lines
//  inside). Plain C++ so it can be tested without the game.
// ---------------------------------------------------------------------------
namespace hitunion {

struct Pt {
    float x, y;
};

struct Seg {
    Pt a, b;  // a piece of the outline
};

// The fill, as convex pieces (counter-clockwise) that don't overlap.
struct Fill {
    std::vector<Pt> pts;          // all the corners, piece after piece
    std::vector<unsigned> counts; // how many corners each piece has
    void clear() {
        pts.clear();
        counts.clear();
    }
};

// `polys` are convex polygons (any winding). Fills `fill` with pieces that
// cover their union exactly once and `outline` with the union's border.
void merge(std::vector<std::vector<Pt>> const& polys, Fill& fill, std::vector<Seg>& outline);

}  // namespace hitunion
