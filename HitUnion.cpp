#include "HitUnion.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace hitunion {

namespace {

struct Poly {
    std::vector<Pt> v;  // counter-clockwise
    float x0, y0, x1, y1;
};

struct Edge {
    Pt a, b;
    int poly;
    float ylo, yhi;
    int dir;                  // +1 going up, -1 going down, 0 flat
    std::vector<float> cuts;  // where other borders cross this edge (0..1)
};

// Above this many corners in one joined group, the fill is drawn shape by
// shape (overlaps a bit darker) so the game stays fast. The outline is
// still joined.
constexpr size_t kFillCornerBudget = 3000;

float area2(std::vector<Pt> const& v) {
    double s = 0.0;
    for (size_t i = 0; i < v.size(); i++) {
        Pt const& a = v[i];
        Pt const& b = v[(i + 1) % v.size()];
        s += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return static_cast<float>(s);
}

bool boxesTouch(Poly const& a, Poly const& b) {
    float e = 0.01f;
    return a.x0 <= b.x1 + e && b.x0 <= a.x1 + e && a.y0 <= b.y1 + e && b.y0 <= a.y1 + e;
}

// strictly inside a counter-clockwise convex polygon
bool inside(Poly const& p, Pt q) {
    if (q.x <= p.x0 || q.x >= p.x1 || q.y <= p.y0 || q.y >= p.y1) return false;
    size_t n = p.v.size();
    for (size_t i = 0; i < n; i++) {
        Pt const& a = p.v[i];
        Pt const& b = p.v[(i + 1) % n];
        float cr = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
        if (cr <= 1e-5f) return false;
    }
    return true;
}

// where two segments cross: t on the first, u on the second (both 0..1)
bool crossAt(Pt a, Pt b, Pt c, Pt d, float& t, float& u) {
    float rx = b.x - a.x, ry = b.y - a.y, sx = d.x - c.x, sy = d.y - c.y;
    float den = rx * sy - ry * sx;
    if (std::fabs(den) < 1e-9f) return false;  // parallel
    float qx = c.x - a.x, qy = c.y - a.y;
    t = (qx * sy - qy * sx) / den;
    u = (qx * ry - qy * rx) / den;
    float e = 1e-4f;
    return t >= -e && t <= 1.f + e && u >= -e && u <= 1.f + e;
}

float xAt(Edge const& e, float y) {
    if (e.b.y == e.a.y) return e.a.x;
    return e.a.x + (y - e.a.y) * (e.b.x - e.a.x) / (e.b.y - e.a.y);
}

void addPiece(Fill& fill, Pt const* p, unsigned n) {
    fill.pts.insert(fill.pts.end(), p, p + n);
    fill.counts.push_back(n);
}

int findRoot(std::vector<int>& parent, int i) {
    while (parent[static_cast<size_t>(i)] != i) {
        parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
        i = parent[static_cast<size_t>(i)];
    }
    return i;
}

// One group of shapes that touch each other.
void mergeGroup(std::vector<Poly> const& polys, Fill& fill, std::vector<Seg>& outline) {
    // 1) edges
    std::vector<Edge> edges;
    std::vector<size_t> firstEdge(polys.size() + 1, 0);
    size_t corners = 0;
    for (size_t i = 0; i < polys.size(); i++) {
        firstEdge[i] = edges.size();
        auto const& v = polys[i].v;
        corners += v.size();
        for (size_t k = 0; k < v.size(); k++) {
            Edge e;
            e.a = v[k];
            e.b = v[(k + 1) % v.size()];
            e.poly = static_cast<int>(i);
            e.ylo = std::min(e.a.y, e.b.y);
            e.yhi = std::max(e.a.y, e.b.y);
            e.dir = e.b.y > e.a.y ? 1 : (e.b.y < e.a.y ? -1 : 0);
            edges.push_back(std::move(e));
        }
    }
    firstEdge[polys.size()] = edges.size();

    // 2) where borders of different shapes cross. Shapes are sorted left to
    //    right so each one only looks at the shapes that can reach it.
    std::vector<size_t> byX(polys.size());
    std::iota(byX.begin(), byX.end(), size_t{0});
    std::sort(byX.begin(), byX.end(), [&](size_t a, size_t b) { return polys[a].x0 < polys[b].x0; });
    std::vector<float> sortedX0(polys.size());
    float widest = 0.f;
    for (size_t k = 0; k < byX.size(); k++) {
        sortedX0[k] = polys[byX[k]].x0;
        widest = std::max(widest, polys[byX[k]].x1 - polys[byX[k]].x0);
    }
    std::vector<float> ys;
    ys.reserve(edges.size() * 2);
    for (auto const& e : edges) ys.push_back(e.a.y);
    for (size_t k = 0; k < byX.size(); k++) {
        size_t i = byX[k];
        for (size_t m = k + 1; m < byX.size() && sortedX0[m] <= polys[i].x1 + 0.01f; m++) {
            size_t j = byX[m];
            if (!boxesTouch(polys[i], polys[j])) continue;
            // only edges inside the area where the two boxes overlap can cross
            float bx0 = std::max(polys[i].x0, polys[j].x0) - 0.01f, bx1 = std::min(polys[i].x1, polys[j].x1) + 0.01f;
            float by0 = std::max(polys[i].y0, polys[j].y0) - 0.01f, by1 = std::min(polys[i].y1, polys[j].y1) + 0.01f;
            auto inBox = [&](Edge const& e) {
                return std::max(e.a.x, e.b.x) >= bx0 && std::min(e.a.x, e.b.x) <= bx1 && e.yhi >= by0 && e.ylo <= by1;
            };
            for (size_t ei = firstEdge[i]; ei < firstEdge[i + 1]; ei++) {
                if (!inBox(edges[ei])) continue;
                for (size_t ej = firstEdge[j]; ej < firstEdge[j + 1]; ej++) {
                    if (!inBox(edges[ej])) continue;
                    float t = 0.f, u = 0.f;
                    if (!crossAt(edges[ei].a, edges[ei].b, edges[ej].a, edges[ej].b, t, u)) continue;
                    edges[ei].cuts.push_back(t);
                    edges[ej].cuts.push_back(u);
                    ys.push_back(edges[ei].a.y + t * (edges[ei].b.y - edges[ei].a.y));
                }
            }
        }
    }

    // 3) fill: cut everything into flat strips where nothing crosses; inside a
    //    strip, "inside any shape" is a row of trapezoids.
    if (corners > kFillCornerBudget) {
        for (auto const& p : polys) addPiece(fill, p.v.data(), static_cast<unsigned>(p.v.size()));
    } else {
        std::sort(ys.begin(), ys.end());
        std::vector<float> uy;
        for (float y : ys)
            if (uy.empty() || y - uy.back() > 1e-3f) uy.push_back(y);

        std::vector<size_t> order;
        for (size_t i = 0; i < edges.size(); i++)
            if (edges[i].dir != 0) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return edges[a].ylo < edges[b].ylo; });

        struct Trap {
            size_t left, right;
            Pt p[4];  // bottom-left, bottom-right, top-right, top-left
        };
        std::vector<Trap> traps;
        std::vector<size_t> open, nextOpen, active;
        std::vector<std::pair<float, size_t>> xs;
        size_t next = 0;
        for (size_t k = 0; k + 1 < uy.size(); k++) {
            float y0 = uy[k], y1 = uy[k + 1], ym = (y0 + y1) * 0.5f;
            while (next < order.size() && edges[order[next]].ylo < ym) active.push_back(order[next++]);
            active.erase(std::remove_if(active.begin(), active.end(), [&](size_t e) { return edges[e].yhi <= ym; }),
                         active.end());
            xs.clear();
            for (size_t e : active) xs.push_back({xAt(edges[e], ym), e});
            std::sort(xs.begin(), xs.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

            nextOpen.clear();
            int wind = 0;
            size_t leftEdge = 0;
            for (auto const& [x, e] : xs) {
                int before = wind;
                wind += edges[e].dir;
                if (before == 0 && wind != 0) leftEdge = e;
                if (before == 0 || wind != 0) continue;
                size_t L = leftEdge, R = e;
                Pt tl{xAt(edges[L], y1), y1}, tr{xAt(edges[R], y1), y1};
                // same two sides as the piece right below? then just make it taller
                bool grown = false;
                for (size_t oi : open) {
                    Trap& t = traps[oi];
                    if (t.left == L && t.right == R && t.p[2].y == y0) {
                        t.p[2] = tr;
                        t.p[3] = tl;
                        nextOpen.push_back(oi);
                        grown = true;
                        break;
                    }
                }
                if (!grown) {
                    Trap t{L, R, {{xAt(edges[L], y0), y0}, {xAt(edges[R], y0), y0}, tr, tl}};
                    traps.push_back(t);
                    nextOpen.push_back(traps.size() - 1);
                }
            }
            open.swap(nextOpen);
        }
        for (auto const& t : traps) {
            // drop repeated corners (a trapezoid that comes to a point is a triangle)
            Pt q[4];
            unsigned n = 0;
            for (int i = 0; i < 4; i++) {
                Pt const& p = t.p[i];
                if (n && std::fabs(p.x - q[n - 1].x) < 1e-4f && std::fabs(p.y - q[n - 1].y) < 1e-4f) continue;
                q[n++] = p;
            }
            if (n > 2 && std::fabs(q[n - 1].x - q[0].x) < 1e-4f && std::fabs(q[n - 1].y - q[0].y) < 1e-4f) n--;
            if (n >= 3) addPiece(fill, q, n);
        }
    }

    // 4) outline: the parts of each border that don't lie inside another shape
    float const eps = 0.02f;
    std::vector<size_t> nearby;
    for (auto& e : edges) {
        float len = std::hypot(e.b.x - e.a.x, e.b.y - e.a.y);
        if (len < 1e-4f) continue;
        nearby.clear();
        float ex0 = std::min(e.a.x, e.b.x) - 1.f, ex1 = std::max(e.a.x, e.b.x) + 1.f;
        auto from = static_cast<size_t>(std::lower_bound(sortedX0.begin(), sortedX0.end(), ex0 - widest) - sortedX0.begin());
        for (size_t k = from; k < byX.size() && sortedX0[k] <= ex1; k++) {
            size_t j = byX[k];
            if (static_cast<int>(j) == e.poly) continue;
            auto const& p = polys[j];
            if (p.x0 > ex1 || p.x1 < ex0 || p.y0 > e.yhi + 1.f || p.y1 < e.ylo - 1.f) continue;
            nearby.push_back(j);
        }
        if (nearby.empty()) {  // nothing near it: keep the whole edge
            outline.push_back({e.a, e.b});
            continue;
        }
        e.cuts.push_back(0.f);
        e.cuts.push_back(1.f);
        std::sort(e.cuts.begin(), e.cuts.end());
        float nx = (e.b.y - e.a.y) / len, ny = -(e.b.x - e.a.x) / len;  // points outward
        bool runOpen = false;
        Pt runStart{}, runEnd{};
        for (size_t c = 0; c + 1 < e.cuts.size(); c++) {
            float t0 = std::clamp(e.cuts[c], 0.f, 1.f), t1 = std::clamp(e.cuts[c + 1], 0.f, 1.f);
            if ((t1 - t0) * len < 1e-3f) continue;
            float tm = (t0 + t1) * 0.5f;
            Pt m{e.a.x + (e.b.x - e.a.x) * tm + nx * eps, e.a.y + (e.b.y - e.a.y) * tm + ny * eps};
            bool covered = false;
            for (size_t j : nearby)
                if (inside(polys[j], m)) {
                    covered = true;
                    break;
                }
            if (!covered) {
                if (!runOpen) {
                    runOpen = true;
                    runStart = {e.a.x + (e.b.x - e.a.x) * t0, e.a.y + (e.b.y - e.a.y) * t0};
                }
                runEnd = {e.a.x + (e.b.x - e.a.x) * t1, e.a.y + (e.b.y - e.a.y) * t1};
            } else if (runOpen) {
                outline.push_back({runStart, runEnd});
                runOpen = false;
            }
        }
        if (runOpen) outline.push_back({runStart, runEnd});
    }
}

}  // namespace

void merge(std::vector<std::vector<Pt>> const& input, Fill& fill, std::vector<Seg>& outline) {
    fill.clear();
    outline.clear();

    // tidy up: counter-clockwise, no tiny shapes, bounding boxes
    std::vector<Poly> polys;
    polys.reserve(input.size());
    for (auto const& in : input) {
        if (in.size() < 3) continue;
        float a = area2(in);
        if (std::fabs(a) < 1e-3f) continue;
        Poly p;
        p.v = in;
        if (a < 0.f) std::reverse(p.v.begin(), p.v.end());
        p.x0 = p.x1 = p.v[0].x;
        p.y0 = p.y1 = p.v[0].y;
        for (auto const& q : p.v) {
            p.x0 = std::min(p.x0, q.x);
            p.x1 = std::max(p.x1, q.x);
            p.y0 = std::min(p.y0, q.y);
            p.y1 = std::max(p.y1, q.y);
        }
        polys.push_back(std::move(p));
    }
    if (polys.empty()) return;

    // split into groups of shapes that touch (each group is joined on its own,
    // which is much faster than one big job for the whole screen)
    size_t n = polys.size();
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    std::vector<size_t> byX(n);
    std::iota(byX.begin(), byX.end(), size_t{0});
    std::sort(byX.begin(), byX.end(), [&](size_t a, size_t b) { return polys[a].x0 < polys[b].x0; });
    for (size_t k = 0; k < n; k++) {
        size_t i = byX[k];
        for (size_t m = k + 1; m < n && polys[byX[m]].x0 <= polys[i].x1 + 0.01f; m++) {
            size_t j = byX[m];
            if (!boxesTouch(polys[i], polys[j])) continue;
            int ri = findRoot(parent, static_cast<int>(i)), rj = findRoot(parent, static_cast<int>(j));
            if (ri != rj) parent[static_cast<size_t>(ri)] = rj;
        }
    }
    std::vector<std::vector<size_t>> groups;
    std::vector<int> groupOf(n, -1);
    for (size_t i = 0; i < n; i++) {
        auto r = static_cast<size_t>(findRoot(parent, static_cast<int>(i)));
        if (groupOf[r] < 0) {
            groupOf[r] = static_cast<int>(groups.size());
            groups.emplace_back();
        }
        groups[static_cast<size_t>(groupOf[r])].push_back(i);
    }

    std::vector<Poly> sub;
    for (auto const& g : groups) {
        if (g.size() == 1) {  // alone: it already is its own outline and fill
            auto const& p = polys[g[0]];
            addPiece(fill, p.v.data(), static_cast<unsigned>(p.v.size()));
            for (size_t k = 0; k < p.v.size(); k++) outline.push_back({p.v[k], p.v[(k + 1) % p.v.size()]});
            continue;
        }
        sub.clear();
        for (size_t i : g) sub.push_back(polys[i]);
        mergeGroup(sub, fill, outline);
    }
}

}  // namespace hitunion
