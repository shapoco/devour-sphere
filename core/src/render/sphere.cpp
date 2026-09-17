// Sphere wireframe: an adaptively subdivided icosahedron drawn as 3D lines.

#include <cmath>
#include <cstring>

#include "devoursphere/render/renderer.hpp"

namespace devoursphere::render {

using g3::vec3f;
using sim::FU;

static constexpr float SPHERE_R = (float)sim::SPHERE_RADIUS / FU;
static constexpr float FADE_NEAR = 60.0f, FADE_FAR = 900.0f;  // FU
// Subdivision: fixed levels chosen by the distance from the player's
// position on the surface (not by the screen size, which would make the
// mesh flicker as the camera moves)
// The radii scale with the nominal camera distance (11 FU for the smallest
// player) and every level drops by one each time that distance doubles, so
// the on-screen density and the line count stay about the same as the
// player grows
static constexpr int BASE_LEVEL = 4;  // edges of ~34 FU at 11 FU
static constexpr float LEVEL5_RADIUS = 6.0f, LEVEL6_RADIUS = 2.5f;  // x nominal
static constexpr float NOMINAL_DIST0 = 11.0f;
// How much one more subdivision level multiplies the visible edge count by
static constexpr int SPHERE_LEVEL_FANOUT = 3;
// Budget of the triangle buffer for the wireframe (UiMetrics::wireLines:
// 1100 on the reference screen, less on a smaller one). The edges are
// counted in a dry run first; when they exceed the limit every level is
// lowered by one for the whole sphere (uniform, no holes) until they fit.

static const vec3f ICO_VERTS[12] = {
    {-0.525731f, 0.850651f, 0},  {0.525731f, 0.850651f, 0},
    {-0.525731f, -0.850651f, 0}, {0.525731f, -0.850651f, 0},
    {0, -0.525731f, 0.850651f},  {0, 0.525731f, 0.850651f},
    {0, -0.525731f, -0.850651f}, {0, 0.525731f, -0.850651f},
    {0.850651f, 0, -0.525731f},  {0.850651f, 0, 0.525731f},
    {-0.850651f, 0, -0.525731f}, {-0.850651f, 0, 0.525731f},
};
static const uint8_t ICO_FACES[20][3] = {
    {0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
    {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
    {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
    {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1},
};

// Color of a wireframe vertex: fades with the distance from the eye
static g2::Color wireColor(float d, int level) {
  float t = (FADE_FAR - d) / (FADE_FAR - FADE_NEAR);
  t = g3::clamp01(t);
  int it = (int)(t * 200) + (level <= 1 ? 55 : (level == 2 ? 30 : 0));
  if (it > 255) it = 255;
  g2::Color dim = g2::makeColor(6, 14, 40);
  g2::Color bright = g2::makeColor(70, 130, 235);
  return g2::lerpColor(dim, bright, it);
}

// One chord of the wireframe, clipped against the horizon.
void Renderer::emitChord(const vec3f &a, const vec3f &b, int level) {
  if (!sphereDryRun_ && lineCount_ >= ui_.wireLines) return;
  // Clip against the horizon: the far side of the sphere is never drawn
  float da = g3::dot(a, camUnit_) - cosHorizon_;
  float db = g3::dot(b, camUnit_) - cosHorizon_;
  if (da < 0 && db < 0) return;
  vec3f ua = a, ub = b;
  if (da < 0 || db < 0) {
    float t = da / (da - db);  // where the chord crosses the horizon plane
    vec3f m = g3::normalize(g3::lerp(a, b, t));
    if (da < 0)
      ua = m;
    else
      ub = m;
  }
  sphereCount_++;
  if (sphereDryRun_) return;
  vec3f pa = sphereCenter_ + ua * SPHERE_R;
  vec3f pb = sphereCenter_ + ub * SPHERE_R;
  putLine3(pa, pb, wireColor(g3::length(pa - cam_.eye), level),
           wireColor(g3::length(pb - cam_.eye), level), palette_[PAL_LINE]);
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
void Renderer::emitEdge(const vec3f &a, const vec3f &b, int depth, int level) {
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
void Renderer::emitSplitEdge(const vec3f &a, const vec3f &b, int depth,
                             int level) {
  vec3f pending[MAX_SPHERE_LEVEL + 1];
  vec3f cur = a, end = b;
  uint32_t rightDone = 0;
  int lvl = 0;
  for (;;) {
    while (lvl < depth) {  // down to the left-most leaf below here
      pending[lvl] = end;
      end = g3::normalize(cur + end);
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

// The subdivision level wanted at a point of the surface: finer near the
// player's position on it.
int Renderer::wantLevel(const vec3f &center, int level) const {
  vec3f playerUnit = g3::normalize(sphereCenter_ * -1.0f);
  float cosDist = g3::dot(center, playerUnit);
  float faceAngle = 0.6524f / (float)(1 << level);  // angular radius
  float ang = std::acos(cosDist > 1 ? 1 : (cosDist < -1 ? -1 : cosDist));
  float dist = (ang - faceAngle) * SPHERE_R;  // FU to the nearest point
  int shift = sphereShift_;
  float unit = camNominal_ / (float)(1 << shift);  // radii in FU
  int want = BASE_LEVEL - shift;
  if (dist < LEVEL6_RADIUS * unit) {
    want += 2;
  } else if (dist < LEVEL5_RADIUS * unit) {
    want += 1;
  }
  if (want < 1) want = 1;
  return want;
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
Renderer::EdgeDepths Renderer::subdivideFace(const vec3f &a, const vec3f &b,
                                             const vec3f &c, int level) {
  if (!sphereDryRun_ && lineCount_ >= ui_.wireLines) return 0;
  vec3f center = g3::normalize(a + b + c);
  // Horizon: skip faces entirely on the far side of the sphere
  if (g3::dot(center, camUnit_) < cullCos_[level]) return 0;
  if (level >= wantLevel(center, level) || level >= MAX_SPHERE_LEVEL) {
    return 0;  // a leaf: its edges belong to an ancestor
  }

  vec3f ab = g3::normalize(a + b);
  vec3f bc = g3::normalize(b + c);
  vec3f ca = g3::normalize(c + a);
  // child 0 = (a, ab, ca), 1 = (ab, b, bc), 2 = (ca, bc, c), 3 = (ab, bc, ca)
  const vec3f *tri[4][3] = {
      {&a, &ab, &ca}, {&ab, &b, &bc}, {&ca, &bc, &c}, {&ab, &bc, &ca}};
  // Children nearest to the player first, so that when the line budget
  // runs out only the far faces end up coarse
  vec3f playerUnit = g3::normalize(sphereCenter_ * -1.0f);
  float key[4];
  int order[4] = {0, 1, 2, 3};
  for (int i = 0; i < 4; i++) {
    vec3f cen = *tri[i][0] + *tri[i][1] + *tri[i][2];
    key[i] = -g3::dot(cen, playerUnit);
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

int Renderer::countSphereLines(int shift, const int *order) {
  sphereShift_ = shift;
  sphereDryRun_ = true;
  sphereCount_ = 0;
  traverseSphere(order);
  sphereDryRun_ = false;
  return sphereCount_;
}

void Renderer::buildSphere() {
  // Top-level faces nearest to the player first
  vec3f playerUnit = g3::normalize(sphereCenter_ * -1.0f);
  float key[20];
  int order[20];
  for (int f = 0; f < 20; f++) {
    vec3f cen = ICO_VERTS[ICO_FACES[f][0]] + ICO_VERTS[ICO_FACES[f][1]] +
                ICO_VERTS[ICO_FACES[f][2]];
    key[f] = -g3::dot(cen, playerUnit);
    order[f] = f;
  }
  for (int i = 1; i < 20; i++) {
    int o = order[i], j = i - 1;
    while (j >= 0 && key[order[j]] > key[o]) order[j + 1] = order[j], j--;
    order[j + 1] = o;
  }

  // Base level reduction from the camera distance, plus what the budget
  // needs (kept between frames with hysteresis so that it does not flicker)
  float scale = camNominal_ / NOMINAL_DIST0;
  int base = 0;
  while (scale >= 2.0f && base < BASE_LEVEL) scale *= 0.5f, base++;
  const int wireLimit = ui_.wireLines - 80 * ui_.scale8 / 8;
  const int wireRelax = wireLimit * 6 / 10;  // hysteresis
  int shift;
  if (sphereCountValid_) {
    // Steer the level from what the previous frame actually emitted rather
    // than by trial traversals: counting the edges costs as much as drawing
    // them, and the camera moves smoothly enough that reacting a frame late
    // is invisible. emitEdge() caps the line count either way, so a level
    // that is briefly too fine cannot overflow the buffer.
    if (sphereCount_ > wireLimit) {
      sphereExtra_ += (sphereCount_ > 2 * wireLimit) ? 2 : 1;
    } else if (sphereCount_ * SPHERE_LEVEL_FANOUT < wireRelax &&
               sphereExtra_ > 0) {
      // Only go finer when the next level down would still fit: one level
      // multiplies the edge count by roughly this much
      sphereExtra_--;
    }
    shift = base + sphereExtra_;
    if (shift < base) shift = base;
    if (shift > BASE_LEVEL) shift = BASE_LEVEL;
  } else {
    // First frame after init(): no previous count to go on, so search
    shift = base + sphereExtra_;
    if (shift > BASE_LEVEL) shift = BASE_LEVEL;
    int count = countSphereLines(shift, order);
    while (count > wireLimit && shift < BASE_LEVEL) {
      shift++;
      count = countSphereLines(shift, order);
    }
  }
  sphereExtra_ = shift - base;
  sphereShift_ = shift;
  sphereCount_ = 0;
  traverseSphere(order);
  sphereCountValid_ = true;
}

}  // namespace devoursphere::render
