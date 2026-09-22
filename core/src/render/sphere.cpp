// Sphere wireframe: an adaptively subdivided icosahedron drawn as 2D lines.
//
// Everything here is integer: the surface points are Q30 unit vectors (the
// simulation's own Vec3 arithmetic, whose normalize is cheap even on a core
// without an FPU), and the projection uses the integer camera that
// updateCamera() derives from the float one (CameraQ). The traversal used
// to be float and took four normalizes and a handful of dot products per
// face; on the RP2040 that was 22 ms of every frame.

#include <cmath>
#include <cstring>

#include "devoursphere/render/renderer.hpp"

namespace devoursphere::render {

using sim::FU;
using sim::Vec3;

static constexpr float SPHERE_R = (float)sim::SPHERE_RADIUS / FU;
static constexpr float FADE_NEAR = 60.0f, FADE_FAR = 900.0f;  // FU
// Subdivision: the level a piece of the surface is drawn at is decided by
// how big its faces come out on the screen, and by nothing else -- not by
// where the player happens to be, which is no business of the camera's.
//
// For a point of the surface at an angle psi from the point the camera is
// directly above, the distance to the eye and the tilt away from it both
// follow from psi alone (C is the eye's distance from the centre, R the
// sphere's radius):
//
//   d(psi)^2 = C^2 + R^2 - 2 C R cos psi      cos i = (C cos psi - R) / d
//
// and a cell of edge s covers about s * focal * sqrt(cos i) / d pixels --
// the square root because it is foreshortened along one axis only. Asking
// for cells of SPHERE_TARGET_PX pixels therefore gives the wanted level as
// a function of cos psi alone, so the test stays what it has always been:
// one dot product per face, now against the camera's own direction, and a
// comparison against thresholds worked out once a frame.
//
// Two things fall out of it. Near the surface the distance runs from a few
// FU underfoot to the horizon, so the mesh grades steeply; seen from far
// away, d and cos i barely vary across everything visible, the thresholds
// fall outside the visible cap and the mesh comes out even -- which is
// what a sphere seen from far away wants, and grading it there only looks
// wrong. And the level is naturally finest right below the camera, not
// around a third party.
//
// The pixels the cells are asked to cover is the one number steered: the
// line count of the last frame nudges it towards the budget. The mesh
// answers continuously (a face at a time crosses a threshold), so there is
// no level of the whole sphere to step and nothing to flicker.
static constexpr float ICO_EDGE = 1.05146f;  // level 0 edge, x sphere radius
// How big a cell of the mesh wants to be on the screen. The budget below
// only ever makes them bigger than this.
static constexpr float SPHERE_TARGET_PX = Renderer::SPHERE_TARGET_PX_INIT;
static constexpr float SPHERE_TARGET_MAX = 200.0f;
// Coarsest level while the player is in flight: the far sphere at level 2
// still reads as a polygon (a 24-gon limb), level 3 as a circle
static constexpr int FLIGHT_MIN_LEVEL = 3;
// Below this share of the budget the cells are let back towards the size
// they want; above the budget itself they are made bigger until they fit.
// The gap between the two is what keeps a mesh that is up against the
// budget from breathing in and out.
static constexpr int WIRE_TARGET_PCT = 50;

// Integer units of this file: a surface point is a Q30 unit vector; a
// position relative to the sphere center is in 1/16 world units (the sphere
// radius 2^17 units is 2^21 of them, so a unit vector becomes a position by
// a shift); a screen coordinate is in 1/16 pixel.
static constexpr int POS_SHIFT = 4;  // 1/16 unit
static constexpr int Q30_TO_POS = sim::Q30_SHIFT - sim::SPHERE_RADIUS_SHIFT -
                                  POS_SHIFT;  // u >> 9 = u * R in 1/16 units
static constexpr int32_t Z_NEAR_POS =
    (int32_t)(0.4f * FU * (1 << POS_SHIFT));  // Z_NEAR of renderer.cpp

static constexpr int32_t q30(float v) { return (int32_t)(v * 1073741824.0f); }

static const Vec3 ICO_VERTS[12] = {
    {q30(-0.525731f), q30(0.850651f), 0},  {q30(0.525731f), q30(0.850651f), 0},
    {q30(-0.525731f), q30(-0.850651f), 0}, {q30(0.525731f), q30(-0.850651f), 0},
    {0, q30(-0.525731f), q30(0.850651f)},  {0, q30(0.525731f), q30(0.850651f)},
    {0, q30(-0.525731f), q30(-0.850651f)}, {0, q30(0.525731f), q30(-0.850651f)},
    {q30(0.850651f), 0, q30(-0.525731f)},  {q30(0.850651f), 0, q30(0.525731f)},
    {q30(-0.850651f), 0, q30(-0.525731f)}, {q30(-0.850651f), 0, q30(0.525731f)},
};
static const uint8_t ICO_FACES[20][3] = {
    {0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
    {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
    {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
    {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1},
};

// Unit vector halfway between two unit vectors (halved first: the sum of
// two Q30 ones can overflow an int32)
static inline Vec3 midpointQ(const Vec3 &a, const Vec3 &b) {
  return sim::normalizeQ30({(a.x >> 1) + (b.x >> 1), (a.y >> 1) + (b.y >> 1),
                            (a.z >> 1) + (b.z >> 1)});
}
static inline Vec3 sum3(const Vec3 &a, const Vec3 &b, const Vec3 &c) {
  return {(a.x >> 2) + (b.x >> 2) + (c.x >> 2),
          (a.y >> 2) + (b.y >> 2) + (c.y >> 2),
          (a.z >> 2) + (b.z >> 2) + (c.z >> 2)};
}

// Brightness (0..255) of a wireframe vertex: fades with the distance from
// the eye. The color is the ramp WIRE_DIM..WIRE_BRIGHT at that brightness.
static constexpr g2::Color WIRE_DIM = g2::makeColor(6, 14, 40);
static constexpr g2::Color WIRE_BRIGHT = g2::makeColor(70, 130, 235);
// `dist` in 1/16 world units, `offset` (same units) is where the fade
// starts: the altitude of a flying player, so that the far sphere keeps its
// brightness and only fades with the depth beyond its nearest point
static int wireBrightness(int32_t dist, int32_t offset, int level) {
  constexpr int32_t NEAR = (int32_t)(FADE_NEAR * FU * 16),
                    FAR = (int32_t)(FADE_FAR * FU * 16);
  dist -= offset;
  if (dist < 0) dist = 0;
  int32_t t200 = ((FAR - dist) / 64) * 200 / ((FAR - NEAR) / 64);  // 32-bit
  if (t200 < 0) t200 = 0;
  if (t200 > 200) t200 = 200;
  int it = t200 + (level <= 1 ? 55 : (level == 2 ? 30 : 0));
  if (it > 255) it = 255;
  return it;
}

// The position (1/16 units, relative to the sphere center) of a surface
// point, and its distance from the eye
static inline Vec3 surfacePos(const Vec3 &u) {
  return {u.x >> Q30_TO_POS, u.y >> Q30_TO_POS, u.z >> Q30_TO_POS};
}

// Screen coordinates (1/16 px) of a position relative to the sphere center.
// False when the point is not in front of the near plane.
DEVOURSPHERE_HOT_ATTR bool Renderer::projectQ(const Vec3 &pos, int32_t &sx,
                                              int32_t &sy) const {
  const Vec3 d = {pos.x - camQ_.eye.x, pos.y - camQ_.eye.y,
                  pos.z - camQ_.eye.z};
  const int32_t zv = sim::dotQ30(d, camQ_.fwd);  // 1/16 units, forward
  if (zv < Z_NEAR_POS) return false;
  const int32_t xv = sim::dotQ30(d, camQ_.right);
  const int32_t yv = sim::dotQ30(d, camQ_.up);
  // focal / z in Q15: focal16 (~2^12) * (2^31 / zv) >> 16
  const int32_t f =
      (int32_t)(((int64_t)camQ_.focal16 * (INT32_MAX / zv)) >> 16);
  sx = camQ_.cx16 + (int32_t)(((int64_t)xv * f) >> 15);
  sy = camQ_.cy16 - (int32_t)(((int64_t)yv * f) >> 15);
  return true;
}

// One chord of the wireframe, clipped against the horizon.
DEVOURSPHERE_HOT_ATTR void Renderer::emitChord(const Vec3 &a, const Vec3 &b,
                                               int level) {
  if (!sphereDryRun_ && lineCount_ >= wireBudget_) return;
  // Clip against the horizon: the far side of the sphere is never drawn
  const int32_t da = sim::dotQ30(a, camQ_.unit) - camQ_.cosHorizon;
  const int32_t db = sim::dotQ30(b, camQ_.unit) - camQ_.cosHorizon;
  if (da < 0 && db < 0) return;
  Vec3 ua = a, ub = b;
  if (da < 0 || db < 0) {
    // Where the chord crosses the horizon plane, t = da / (da - db) in Q16
    const int32_t t = (int32_t)(((int64_t)da << 16) / ((int64_t)da - db));
    const Vec3 h = {
        (a.x >> 1) + (int32_t)(((int64_t)((b.x >> 1) - (a.x >> 1)) * t) >> 16),
        (a.y >> 1) + (int32_t)(((int64_t)((b.y >> 1) - (a.y >> 1)) * t) >> 16),
        (a.z >> 1) + (int32_t)(((int64_t)((b.z >> 1) - (a.z >> 1)) * t) >> 16)};
    const Vec3 m = sim::normalizeQ30(h);
    if (da < 0)
      ua = m;
    else
      ub = m;
  }
  sphereCount_++;
  if (sphereDryRun_) return;
  addWireSegment(ua, ub, level);
}

// Project a chord to the screen and keep it as a 2D segment. A chord with an
// end behind the near plane is dropped, which is what the 3D pipeline did
// with such a line; the rest is clipped to the screen (Liang-Barsky) so the
// band drawing never has to test a coordinate.
DEVOURSPHERE_HOT_ATTR void Renderer::addWireSegment(const Vec3 &ua,
                                                    const Vec3 &ub, int level) {
  if (wireCount_ >= MAX_WIRE) return;
  const Vec3 pa = surfacePos(ua), pb = surfacePos(ub);
  int32_t x0, y0, x1, y1;
  if (!projectQ(pa, x0, y0) || !projectQ(pb, x1, y1)) return;
  // Brightness from the distance to the eye: |d| = sqrt(d.d / 2^16) * 2^8
  const Vec3 da = {pa.x - camQ_.eye.x, pa.y - camQ_.eye.y, pa.z - camQ_.eye.z};
  const Vec3 db = {pb.x - camQ_.eye.x, pb.y - camQ_.eye.y, pb.z - camQ_.eye.z};
  const int32_t distA =
      (int32_t)sim::isqrt32((uint32_t)(sim::length2_64(da) >> 16)) << 8;
  const int32_t distB =
      (int32_t)sim::isqrt32((uint32_t)(sim::length2_64(db) >> 16)) << 8;
  const int ba = wireBrightness(distA, wireFadeOffset_, level),
            bb = wireBrightness(distB, wireFadeOffset_, level);

  int ba2 = ba, bb2 = bb;
  if (!clipSegment16(x0, y0, x1, y1, ba2, bb2)) return;
  WireSeg &seg = wire_[wireCount_++];
  seg.x0 = (int16_t)x0;
  seg.y0 = (int16_t)y0;
  seg.x1 = (int16_t)x1;
  seg.y1 = (int16_t)y1;
  seg.b0 = (uint8_t)ba2;
  seg.b1 = (uint8_t)bb2;
  lineCount_++;
}

bool Renderer::clipSegment16(int32_t &x0, int32_t &y0, int32_t &x1, int32_t &y1,
                             int &b0, int &b1) const {
  const int32_t dx = x1 - x0, dy = y1 - y0;
  const int32_t W = w_ << 4, H = h_ << 4;
  if (x0 >= 0 && x0 <= W && y0 >= 0 && y0 <= H && x1 >= 0 && x1 <= W &&
      y1 >= 0 && y1 <= H) {
    return true;
  }
  // Liang-Barsky. The division is 64-bit, but only segments that touch
  // the screen edge get here
  int32_t t0 = 0, t1 = 1 << 16;  // Q16
  auto clip = [&](int32_t p, int32_t q) {
    if (p == 0) return q >= 0;
    const int32_t r = (int32_t)(((int64_t)q << 16) / p);
    if (p < 0) {
      if (r > t1) return false;
      if (r > t0) t0 = r;
    } else {
      if (r < t0) return false;
      if (r < t1) t1 = r;
    }
    return true;
  };
  if (!clip(-dx, x0) || !clip(dx, W - x0) || !clip(-dy, y0) ||
      !clip(dy, H - y0)) {
    return false;
  }
  const int32_t ox = x0, oy = y0, ob = b0;
  x0 = ox + (int32_t)(((int64_t)dx * t0) >> 16);
  y0 = oy + (int32_t)(((int64_t)dy * t0) >> 16);
  x1 = ox + (int32_t)(((int64_t)dx * t1) >> 16);
  y1 = oy + (int32_t)(((int64_t)dy * t1) >> 16);
  b0 = ob + (((b1 - ob) * t0) >> 16);
  b1 = ob + (((b1 - ob) * t1) >> 16);
  return true;
}

bool Renderer::projectUnitsQ(const Vec3 &p, int32_t &sx, int32_t &sy) const {
  // Relative to the sphere center in 1/16 units, like the surface points
  const Vec3 pos = {(p.x + origin_.x) << POS_SHIFT,
                    (p.y + origin_.y) << POS_SHIFT,
                    (p.z + origin_.z) << POS_SHIFT};
  return projectQ(pos, sx, sy);
}

// Draw a-b as the 2^depth chords the mesh actually has along it, bisecting
// the way subdivideFace() does so that the pieces meet the neighbouring
// faces' vertices exactly. `depth` is how far the finer of the two sides
// went, so the line follows the surface instead of cutting the corner: no
// T-junction, and no second, straighter line drawn over it.
//
// Iterative rather than recursive, and the array of pending ends lives in
// emitSplitEdge() rather than here: the sphere recursion is already seven
// frames deep where it draws, and the boards' stacks are 4 KB and cannot be
// grown. The deepest faces are also the ones whose lines need no splitting
// at all, so that path costs nothing.
void Renderer::emitEdge(const Vec3 &a, const Vec3 &b, int depth, int level) {
  if (depth <= 0) {
    emitChord(a, b, level);
    return;
  }
  // The recursion cannot report more than MAX_SPHERE_LEVEL, but the depth
  // arrives through a four-bit field and indexes an array on the stack, so
  // it is clamped rather than trusted
  if (depth > MAX_SPHERE_LEVEL) depth = MAX_SPHERE_LEVEL;
  emitSplitEdge(a, b, depth, level);
}

// An in-order walk of the bisection tree. `pending[l]` holds the right-hand
// end still owed at tree level l, and a bit of `rightDone` says whether the
// node at that level has had its right half drawn; every leaf is at level
// `depth`, so the chords come out left to right and their ends are exactly
// the vertices the neighbouring faces normalized into place.
void Renderer::emitSplitEdge(const Vec3 &a, const Vec3 &b, int depth,
                             int level) {
  Vec3 pending[MAX_SPHERE_LEVEL + 1];
  Vec3 cur = a, end = b;
  uint32_t rightDone = 0;
  int lvl = 0;
  for (;;) {
    while (lvl < depth) {  // down to the left-most leaf below here
      pending[lvl] = end;
      end = midpointQ(cur, end);
      lvl++;
    }
    emitChord(cur, end, level + depth);
    // Climb out of every subtree whose right half is already drawn
    while (lvl > 0 && ((rightDone >> (lvl - 1)) & 1)) {
      rightDone &= ~(1u << (lvl - 1));
      lvl--;
    }
    if (lvl == 0) return;
    lvl--;
    rightDone |= 1u << lvl;  // now taking this node's right half
    cur = end;
    end = pending[lvl];
    lvl++;
  }
}

// The subdivision level wanted at a point of the surface. The thresholds
// are the cosines at which the wanted level steps, already widened by the
// face's own angular radius so that a face takes the level its nearest
// corner asks for (see sphereConstants).
int Renderer::wantLevel(const Vec3 &center, int level) const {
  const int32_t cosPsi = sim::dotQ30(center, camQ_.unit);
  for (int k = SPHERE_LEVEL_STEPS - 1; k >= 0; k--) {
    if (cosPsi > camQ_.cosStep[level][k]) return camQ_.levelMin + k + 1;
  }
  return camQ_.levelMin;
}

// The per-traversal constants: where the wanted level steps, for the mesh
// the camera is looking at now and the size the cells are being asked to
// come out (sphereTarget_).
//
// In units of the sphere's radius, with c = C / R and u = cos psi, the
// pixels a level-L cell covers are (ICO_EDGE / 2^L) * focal * sqrt(g) with
//
//   g(u) = cos i / d^2 = (c u - 1) / (c^2 + 1 - 2 c u)^(3/2)
//
// so the level wanted at u is K + log2(g) / 2 for K = log2(ICO_EDGE *
// focal / target), and the step between L and L - 1 sits where g reaches
// G = 2^(2 (L - 0.5 - K)). g falls from 1 / (c - 1)^2 under the camera to
// zero at the horizon, so each step is found by halving the interval --
// on the squared form, which is a polynomial and needs no square roots.
void Renderer::sphereConstants() {
  const float c = 1.0f + camAltitude_ / SPHERE_R;
  const float uh = 1.0f / c;  // the horizon
  const float K = std::log2(ICO_EDGE * camFocalNominal_ / sphereTarget_);
  // The finest level, at the point right below the camera. Rounding it
  // would flip the whole mesh whenever the camera sat between two levels,
  // so it only moves once it is well past one (the thresholds themselves
  // are continuous, and it is only the range of levels they cover).
  const float topF = K - std::log2(c - 1.0f);
  int top = sphereTop_;
  if (topF > (float)top + 0.65f || topF < (float)top - 0.65f) {
    top = (int)std::floor(topF + 0.5f);
  }
  if (top > MAX_SPHERE_LEVEL) top = MAX_SPHERE_LEVEL;
  if (top < 1) top = 1;
  sphereTop_ = top;
  int min = top - SPHERE_LEVEL_STEPS;
  if (min < 1) {
    min = 1;
    if (top < min) top = min;
  }
  if (inFlight_ && min < FLIGHT_MIN_LEVEL) {
    // Far from the sphere the whole limb is the picture, and a coarse one
    // reads as a polygon rather than a circle
    min = FLIGHT_MIN_LEVEL;
    if (top < min) top = min;
  }
  camQ_.levelMin = min;
  // G at the step above the coarsest level, then four times that for each
  // step after it (a level is two in the pixels, four in g)
  float G = std::exp2(2.0f * ((float)(min + 1) - 0.5f - K));
  for (int k = 0; k < SPHERE_LEVEL_STEPS; k++) {
    float u;
    if (min + k + 1 > top) {
      u = 2.0f;  // beyond the finest level this frame: never
    } else {
      // Halve the interval on (c u - 1)^2 = G^2 (c^2 + 1 - 2 c u)^3, whose
      // left side grows with u and right side shrinks
      float lo = uh, hi = 1.0f;
      const float G2 = G * G;
      for (int i = 0; i < 12; i++) {
        const float m = (lo + hi) * 0.5f;
        const float l = c * m - 1.0f;
        const float d2 = c * c + 1.0f - 2.0f * c * m;
        if (l * l > G2 * d2 * d2 * d2) {
          hi = m;
        } else {
          lo = m;
        }
      }
      u = hi;
    }
    for (int level = 0; level <= MAX_SPHERE_LEVEL; level++) {
      // Widen by the face's own angular radius: cos(psi + a), with the
      // sine of psi from the cosine
      const float a = 0.6524f / (float)(1 << level);
      float t;
      if (u > 1.0f) {
        t = 2.0f;
      } else {
        const float su = std::sqrt(1.0f - u * u);
        t = u * std::cos(a) - su * std::sin(a);
      }
      camQ_.cosStep[level][k] =
          t >= 1.0f ? INT32_MAX : (t <= -1.0f ? INT32_MIN : q30(t));
    }
    G *= 4.0f;
  }
}

// A face draws the three lines that separate its own four children, and
// nothing else.
//
// Every line of the mesh separates two faces, and those two always share an
// ancestor; the lowest one has them in different children, so it -- and only
// it -- draws that line. Nothing is ever emitted twice, so there is no
// dedupe table and no hashing. A face's own three edges are drawn by an
// ancestor, not by itself, and the thirty edges of the icosahedron, which
// have no ancestor, by traverseSphere().
//
// The node is also the one place that knows how far the mesh went on either
// side of the lines it draws: each is shared by the middle child and one
// outer child, both of which have just returned. That is what removes the
// T-junctions -- the line is drawn subdivided to match the finer side
// instead of cutting straight across it.
//
// Returns how deeply the mesh is subdivided along this face's own three
// edges, which is what its parent needs for the same decision.
DEVOURSPHERE_HOT_ATTR Renderer::EdgeDepths Renderer::subdivideFace(
    const Vec3 &a, const Vec3 &b,
                                             const Vec3 &c, int level) {
  if (!sphereDryRun_ && lineCount_ >= wireBudget_) return 0;
  const Vec3 center = sim::normalizeQ30(sum3(a, b, c));
  // Horizon: skip faces entirely on the far side of the sphere, and the
  // screen's own four sides: most of what is within the horizon of a low
  // camera is behind it or off to the side, and subdividing that finely
  // (it is the closest surface there is) would spend the whole budget on
  // faces that are never drawn
  if (sim::dotQ30(center, camQ_.unit) < camQ_.cullCos[level]) return 0;
  for (int k = 0; k < 4; k++) {
    if (sim::dotQ30(center, camQ_.cullN[k]) < camQ_.cullPlane[k][level]) {
      return 0;
    }
  }
  if (level >= wantLevel(center, level) || level >= MAX_SPHERE_LEVEL) {
    return 0;  // a leaf: its edges belong to an ancestor
  }

  const Vec3 ab = midpointQ(a, b);
  const Vec3 bc = midpointQ(b, c);
  const Vec3 ca = midpointQ(c, a);
  // child 0 = (a, ab, ca), 1 = (ab, b, bc), 2 = (ca, bc, c), 3 = (ab, bc, ca)
  const Vec3 *tri[4][3] = {
      {&a, &ab, &ca}, {&ab, &b, &bc}, {&ca, &bc, &c}, {&ab, &bc, &ca}};
  // Children nearest to the camera first, so that when the line budget
  // runs out only the far faces end up coarse
  int32_t key[4];
  int order[4] = {0, 1, 2, 3};
  for (int i = 0; i < 4; i++) {
    key[i] =
        -sim::dotQ30(sum3(*tri[i][0], *tri[i][1], *tri[i][2]), camQ_.unit);
  }
  for (int i = 1; i < 4; i++) {
    int o = order[i], j = i - 1;
    while (j >= 0 && key[order[j]] > key[o]) order[j + 1] = order[j], j--;
    order[j + 1] = o;
  }
  EdgeDepths d[4];
  for (int i = 0; i < 4; i++) {
    int o = order[i];
    d[o] = subdivideFace(*tri[o][0], *tri[o][1], *tri[o][2], level + 1);
  }

  // The three lines between the children. Each is an edge of the middle
  // child and of one outer child, so both sides are known right here.
  auto deeper = [](int p, int q) { return p > q ? p : q; };
  emitEdge(ab, bc, deeper(edgeDepth(d[3], 0), edgeDepth(d[1], 2)), level + 1);
  emitEdge(bc, ca, deeper(edgeDepth(d[3], 1), edgeDepth(d[2], 0)), level + 1);
  emitEdge(ca, ab, deeper(edgeDepth(d[3], 2), edgeDepth(d[0], 1)), level + 1);

  // Each of this face's own edges is covered by two of the children
  return makeEdgeDepths(
      1 + deeper(edgeDepth(d[0], 0), edgeDepth(d[1], 0)),   // a-b
      1 + deeper(edgeDepth(d[1], 1), edgeDepth(d[2], 1)),   // b-c
      1 + deeper(edgeDepth(d[2], 2), edgeDepth(d[0], 2)));  // c-a
}

// One traversal of the whole sphere: the twenty faces of the icosahedron,
// and then its own thirty edges. Those have no ancestor to draw them, so
// they are drawn here, each subdivided to the deeper of what the two faces
// sharing it reported.
void Renderer::traverseSphere(const int *order) {
  sphereConstants();
  std::memset(icoEdgeDepth_, 0xFF, sizeof(icoEdgeDepth_));
  for (int i = 0; i < 20; i++) {
    int f = order[i];
    faceDepth_[f] =
        subdivideFace(ICO_VERTS[ICO_FACES[f][0]], ICO_VERTS[ICO_FACES[f][1]],
                      ICO_VERTS[ICO_FACES[f][2]], 0);
  }
  emitIcoEdges();
}

// Out of line so that none of this is on the stack while the subdivision
// recurses: traverseSphere() is a frame every one of those calls sits on.
void Renderer::emitIcoEdges() {
  for (int f = 0; f < 20; f++) {
    for (int k = 0; k < 3; k++) {
      int u = ICO_FACES[f][k], v = ICO_FACES[f][(k + 1) % 3];
      if (u > v) {
        int t = u;
        u = v;
        v = t;
      }
      uint8_t &slot = icoEdgeDepth_[u][v];
      uint8_t got = (uint8_t)edgeDepth(faceDepth_[f], k);
      if (slot == 0xFF || got > slot) slot = got;
    }
  }
  for (int u = 0; u < 12; u++) {
    for (int v = u + 1; v < 12; v++) {
      if (icoEdgeDepth_[u][v] == 0xFF) continue;  // not an edge
      emitEdge(ICO_VERTS[u], ICO_VERTS[v], icoEdgeDepth_[u][v], 0);
    }
  }
}

int Renderer::countSphereLines(const int *order) {
  sphereDryRun_ = true;
  sphereCount_ = 0;
  wireCount_ = 0;
  traverseSphere(order);
  sphereDryRun_ = false;
  return wireCount_;
}

void Renderer::buildSphere() {
  // Top-level faces nearest to the camera first, so that if the budget
  // runs out it is the far ones that are left coarse
  int32_t key[20];
  int order[20];
  for (int f = 0; f < 20; f++) {
    key[f] = -sim::dotQ30(
        sum3(ICO_VERTS[ICO_FACES[f][0]], ICO_VERTS[ICO_FACES[f][1]],
             ICO_VERTS[ICO_FACES[f][2]]),
        camQ_.unit);
    order[f] = f;
  }
  for (int i = 1; i < 20; i++) {
    int o = order[i], j = i - 1;
    while (j >= 0 && key[order[j]] > key[o]) order[j + 1] = order[j], j--;
    order[j + 1] = o;
  }

  // The whole array high up in flight (there is little else to draw), the
  // screen's share of it on the surface; blended over the first 100 FU of
  // altitude so that the landing does not change the budget in one step
  int wireLines = ui_.wireLines;
  if (inFlight_ && MAX_WIRE > wireLines) {
    float high = altExcess_ * (1.0f / 100.0f);
    if (high > 1) high = 1;
    wireLines += (int)((MAX_WIRE - wireLines) * high);
  }
  wireBudget_ = wireLines;
  const int wireLimit = wireLines - 80 * ui_.scale8 / 8;
  const int wireTarget = wireLimit * WIRE_TARGET_PCT / 100;
  // The cells want to be SPHERE_TARGET_PX across, and that is the whole of
  // the picture's side of it: the budget is a ceiling, not a target. When
  // a frame draws more lines than it may, the cells grow until it fits;
  // once there is room to spare they ease back towards the size they want.
  // (Steering at the budget instead would turn the mesh fine whenever
  // little of the sphere is on the screen, which is exactly when the count
  // is low for reasons that have nothing to do with how it should look --
  // the launch takes the sphere out of the picture altogether.)
  auto steer = [&](int drawn, float damp) {
    if (drawn > wireLimit) {
      float step = std::sqrt((float)drawn / (float)wireLimit);
      if (damp < 1.0f) step = std::sqrt(step);
      if (step > 1.3f) step = 1.3f;
      sphereTarget_ *= step;
      if (sphereTarget_ > SPHERE_TARGET_MAX) sphereTarget_ = SPHERE_TARGET_MAX;
      return true;
    }
    if (sphereTarget_ > SPHERE_TARGET_PX && drawn < wireTarget) {
      // Half a second to give back most of whatever the budget took
      sphereTarget_ += (SPHERE_TARGET_PX - sphereTarget_) * 0.03f;
      if (sphereTarget_ < SPHERE_TARGET_PX) sphereTarget_ = SPHERE_TARGET_PX;
      return true;
    }
    return false;
  };
  if (sphereCountValid_) {
    // The lines the previous frame kept (off-screen chords are walked but
    // not drawn, and it is the kept ones the budget is about)
    steer(wireDrawn_, 0.5f);
  } else {
    // First frame after init() or after a sphere switch: nothing to go on,
    // so count a few trials instead of letting the picture settle on screen
    for (int i = 0; i < 3; i++) {
      if (!steer(countSphereLines(order), 1.0f)) break;
    }
  }
  sphereCount_ = 0;
  wireCount_ = 0;
  traverseSphere(order);
  wireDrawn_ = wireCount_;
  sphereCountValid_ = true;
}

// The 16-bit word that a pixel of color c occupies in memory, for the two
// formats the fast path writes directly (the "native" pixel of RGB565BE is
// byte-swapped in memory: see CursorRgb565BE::write)
static bool directWord(g2::PixelFormat f) {
  return f == g2::PixelFormat::RGB565BE || f == g2::PixelFormat::ARGB4444;
}
static uint16_t memoryWord(g2::PixelFormat f, g2::Color c) {
  const uint16_t native = (uint16_t)g2::colorToNative(f, c);
  return f == g2::PixelFormat::RGB565BE ? g2::bswap16(native) : native;
}

// One wireframe segment into one band. Integer DDA over the major axis
// from the segment's own start, so the pieces drawn into neighbouring bands
// join exactly; the pixel range that falls into the band is worked out
// first rather than walked, which matters for a long line crossing many
// bands. Coordinates are 1/16 pixel, the DDA runs in 1/4096 of that.
// The integer DDA of a screen segment (1/16 px, brightness 0..255 at each
// end): calls plot(px, py, b) for each pixel of the segment that lies in
// the rows [y, y + h) of the frame. Always counted from the segment's
// start, so the pieces drawn in adjacent bands join exactly.
template <typename Plot>
static void walkSegment(int X0, int Y0, int X1, int Y1, int B0, int B1, int y,
                        int h, int w, Plot plot) {
  const bool xMajor =
      (X1 - X0 < 0 ? X0 - X1 : X1 - X0) >= (Y1 - Y0 < 0 ? Y0 - Y1 : Y1 - Y0);
  if (!xMajor) {  // walk y: swap the axes and swap back when plotting
    int t = X0;
    X0 = Y0;
    Y0 = t;
    t = X1;
    X1 = Y1;
    Y1 = t;
  }
  if (X0 > X1) {  // walk in the positive direction of the major axis
    int t = X0;
    X0 = X1;
    X1 = t;
    t = Y0;
    Y0 = Y1;
    Y1 = t;
    t = B0;
    B0 = B1;
    B1 = t;
  }
  const int dx = X1 - X0, dy = Y1 - Y0;
  // Pixels whose center (p * 16 + 8) lies within [X0, X1] along the major
  // axis
  const int p0 = (X0 + 7) >> 4, p1 = (X1 - 8) >> 4;
  const int n = p1 - p0 + 1;
  if (n <= 0) return;
  // Minor coordinate and brightness per major pixel, in 1/16 px x 2^12
  const int dMinor = dx > 0 ? (int)(((int64_t)dy << 16) / dx) : 0;
  const int dBright = n > 1 ? ((B1 - B0) << 16) / (n - 1) : 0;
  int minor0 = (Y0 << 12) + (((p0 * 16 + 8 - X0) * dMinor) >> 4);
  // The pixels of the segment that fall into the band
  int kLo = 0, kHi = n - 1;
  if (xMajor) {
    const int bandLo = y << 16, bandHi = ((y + h) << 16) - 1;
    if (dMinor == 0) {
      const int row = minor0 >> 16;
      if (row < y || row >= y + h) return;
    } else {
      // Solve for the pixels whose row spans the band; the per-pixel test
      // below takes care of the rounding at both ends
      int kA = (bandLo - minor0) / dMinor;
      int kB = (bandHi - minor0) / dMinor;
      if (kA > kB) {
        const int t = kA;
        kA = kB;
        kB = t;
      }
      kA -= 1;
      kB += 1;
      if (kA > kLo) kLo = kA;
      if (kB < kHi) kHi = kB;
      if (kLo > kHi) return;
    }
  } else {
    // y is the major axis here: only the band's rows
    const int kA = y - p0, kB = y + h - 1 - p0;
    if (kA > kLo) kLo = kA;
    if (kB < kHi) kHi = kB;
    if (kLo > kHi) return;
  }
  int minor = minor0 + kLo * dMinor;
  int bright = (B0 << 16) + kLo * dBright;
  for (int k = kLo; k <= kHi; k++, minor += dMinor, bright += dBright) {
    const int major = p0 + k;
    const int m = minor >> 16;  // pixel along the minor axis
    const int px = xMajor ? major : m, py = xMajor ? m : major;
    if (py >= y && py < y + h && px >= 0 && px < w) {
      const int b = bright >> 16;
      plot(px, py, b < 0 ? 0 : (b > 255 ? 255 : b));
    }
  }
}

DEVOURSPHERE_HOT_ATTR void Renderer::drawWireSegment(const g2::Surface &dst,
                                                     const WireSeg &s, int y,
                                                     int h, int dstY) {
  if (directWord(dst.format)) {
    walkSegment(s.x0, s.y0, s.x1, s.y1, s.b0, s.b1, y, h, w_,
                [&](int px, int py, int b) {
                  uint16_t *row = (uint16_t *)dst.linePtr(py - y + dstY);
                  row[px] = wireNative_[b >> 3];
                });
  } else {
    g2::Graphics2D g(dst);
    walkSegment(s.x0, s.y0, s.x1, s.y1, s.b0, s.b1, y, h, w_,
                [&](int px, int py, int b) {
                  g.fillRect(px, py - y + dstY, 1, 1,
                             g2::lerpColor(WIRE_DIM, WIRE_BRIGHT, b));
                });
  }
}

DEVOURSPHERE_HOT_ATTR void Renderer::drawLines2D(const g2::Surface &dst, int y,
                                                 int h, int dstY,
                                                 Layer2D layer) {
  const bool wide = directWord(dst.format);
  g2::Graphics2D g(dst);
  const g2::Color black = g2::makeColor(0, 0, 0);
  for (int i = 0; i < lineCount2D_; i++) {
    const LineSeg &s = lines_[i];
    if ((s.layer & 1) != layer) continue;
    const int lo = s.y0 < s.y1 ? s.y0 : s.y1;
    const int hi = s.y0 < s.y1 ? s.y1 : s.y0;
    if ((hi >> 4) < y || (lo >> 4) >= y + h) continue;
    const g2::Color color = unpackRgb565(s.rgb565);
    if (s.layer & L2D_ADD) {
      // Added onto the frame, per pixel (the dust: a gradient to black,
      // which under addition is a fade to transparent)
      walkSegment(s.x0, s.y0, s.x1, s.y1, s.b0, s.b1, y, h, w_,
                  [&](int px, int py, int b) {
                    g.fillRect(px, py - y + dstY, 1, 1,
                               g2::lerpColor(black, color, b),
                               g2::BlendMode::ADD);
                  });
      continue;
    }
    if (s.b0 == 255 && s.b1 == 255) {
      // Flat: one native word for the whole segment
      if (wide) {
        const uint16_t word = memoryWord(dst.format, color);
        walkSegment(s.x0, s.y0, s.x1, s.y1, 255, 255, y, h, w_,
                    [&](int px, int py, int) {
                      uint16_t *row = (uint16_t *)dst.linePtr(py - y + dstY);
                      row[px] = word;
                    });
      } else {
        walkSegment(s.x0, s.y0, s.x1, s.y1, 255, 255, y, h, w_,
                    [&](int px, int py, int) {
                      g.fillRect(px, py - y + dstY, 1, 1, color);
                    });
      }
    } else {
      // A gradient towards black (the dash dust): per pixel
      if (wide) {
        walkSegment(s.x0, s.y0, s.x1, s.y1, s.b0, s.b1, y, h, w_,
                    [&](int px, int py, int b) {
                      uint16_t *row = (uint16_t *)dst.linePtr(py - y + dstY);
                      row[px] = memoryWord(dst.format,
                                           g2::lerpColor(black, color, b));
                    });
      } else {
        walkSegment(s.x0, s.y0, s.x1, s.y1, s.b0, s.b1, y, h, w_,
                    [&](int px, int py, int b) {
                      g.fillRect(px, py - y + dstY, 1, 1,
                                 g2::lerpColor(black, color, b));
                    });
      }
    }
  }
}

// The stars and the wireframe of one band, before the 3D layers
DEVOURSPHERE_HOT_ATTR void Renderer::drawBackdropBand(const g2::Surface &dst,
                                                      int y, int h, int dstY) {
  if (!wireNativeValid_ || wireNativeFormat_ != dst.format) {
    for (int i = 0; i < 32; i++) {
      wireNative_[i] = memoryWord(
          dst.format, g2::lerpColor(WIRE_DIM, WIRE_BRIGHT, i * 8 + 4));
    }
    wireNativeFormat_ = dst.format;
    wireNativeValid_ = true;
  }
  g2::Graphics2D g(dst);
  const bool wide = directWord(dst.format);
  for (int i = 0; i < starCount_; i++) {
    const StarPt &st = stars_[i];
    if (st.y < y || st.y >= y + h) continue;
    if (wide) {
      uint16_t *row = (uint16_t *)dst.linePtr(st.y - y + dstY);
      row[st.x] = memoryWord(dst.format, st.c);
    } else {
      g.fillRect(st.x, st.y - y + dstY, 1, 1, st.c);
    }
  }
  for (int i = 0; i < pointCount2D_; i++) {
    const StarPt &pt = points_[i];
    if (pt.y < y || pt.y >= y + h) continue;
    if (wide) {
      uint16_t *row = (uint16_t *)dst.linePtr(pt.y - y + dstY);
      row[pt.x] = memoryWord(dst.format, pt.c);
    } else {
      g.fillRect(pt.x, pt.y - y + dstY, 1, 1, pt.c);
    }
  }
  for (int i = 0; i < wireCount_; i++) {
    const WireSeg &seg = wire_[i];
    const int lo = seg.y0 < seg.y1 ? seg.y0 : seg.y1;
    const int hi = seg.y0 < seg.y1 ? seg.y1 : seg.y0;
    if ((hi >> 4) < y || (lo >> 4) >= y + h) continue;
    drawWireSegment(dst, seg, y, h, dstY);
  }
  drawLines2D(dst, y, h, dstY, L2D_UNDER);
}

}  // namespace devoursphere::render
