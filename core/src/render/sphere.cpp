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
// Budget of the triangle buffer for the wireframe. The edges are counted in
// a dry run first; when they exceed WIRE_LIMIT every level is lowered by one
// for the whole sphere (uniform, no holes) until they fit.
static constexpr int MAX_WIRE_LINES = 1100;
static constexpr int WIRE_LIMIT = MAX_WIRE_LINES - 80;
static constexpr int WIRE_RELAX = WIRE_LIMIT * 6 / 10;  // hysteresis

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

static uint32_t hashVec(const vec3f &v) {
  // Quantize to ~1e-4 so both faces sharing an edge produce the same key
  int32_t x = (int32_t)std::lround(v.x * 8192.0f);
  int32_t y = (int32_t)std::lround(v.y * 8192.0f);
  int32_t z = (int32_t)std::lround(v.z * 8192.0f);
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^
               (uint32_t)z * 83492791u;
  return h ? h : 1;
}

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

void Renderer::emitEdge(const vec3f &a, const vec3f &b, int level) {
  if (!sphereDryRun_ && lineCount_ >= MAX_WIRE_LINES) return;
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
  uint32_t ha = hashVec(ua), hb = hashVec(ub);
  uint32_t key = ha < hb ? (ha * 2654435761u) ^ hb : (hb * 2654435761u) ^ ha;
  if (key == 0) key = 1;
  constexpr uint32_t MASK = 2047;
  uint32_t slot = key & MASK;
  for (int probe = 0; probe < 32; probe++) {
    uint32_t k = edgeKeys_[slot];
    if (k == key) return;  // already drawn
    if (k == 0) {
      edgeKeys_[slot] = key;
      break;
    }
    slot = (slot + 1) & MASK;
  }
  sphereCount_++;
  if (sphereDryRun_) return;
  vec3f pa = sphereCenter_ + ua * SPHERE_R;
  vec3f pb = sphereCenter_ + ub * SPHERE_R;
  putLine3(pa, pb, wireColor(g3::length(pa - cam_.eye), level),
           wireColor(g3::length(pb - cam_.eye), level), palette_[PAL_LINE]);
}

void Renderer::subdivideFace(const vec3f &a, const vec3f &b, const vec3f &c,
                             int level) {
  if (!sphereDryRun_ && lineCount_ >= MAX_WIRE_LINES) return;
  vec3f center = g3::normalize(a + b + c);
  // Horizon: skip faces entirely on the far side of the sphere
  if (g3::dot(center, camUnit_) < cullCos_[level]) return;
  // Level wanted here: finer near the player's position on the surface
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
  if (level < want && level < MAX_SPHERE_LEVEL) {
    vec3f ab = g3::normalize(a + b);
    vec3f bc = g3::normalize(b + c);
    vec3f ca = g3::normalize(c + a);
    // Children nearest to the player first, so that when the line budget
    // runs out only the far faces end up coarse
    const vec3f *tri[4][3] = {
        {&a, &ab, &ca}, {&ab, &b, &bc}, {&ca, &bc, &c}, {&ab, &bc, &ca}};
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
    for (int i = 0; i < 4; i++) {
      int o = order[i];
      subdivideFace(*tri[o][0], *tri[o][1], *tri[o][2], level + 1);
    }
    return;
  }
  emitEdge(a, b, level);
  emitEdge(b, c, level);
  emitEdge(c, a, level);
}

// One traversal of the whole sphere (dry run: count only)
static void traverseSphere(Renderer &r,
                           void (Renderer::*fn)(const vec3f &, const vec3f &,
                                                const vec3f &, int),
                           const int *order) {
  for (int i = 0; i < 20; i++) {
    int f = order[i];
    (r.*fn)(ICO_VERTS[ICO_FACES[f][0]], ICO_VERTS[ICO_FACES[f][1]],
            ICO_VERTS[ICO_FACES[f][2]], 0);
  }
}

int Renderer::countSphereLines(int shift, const int *order) {
  sphereShift_ = shift;
  sphereDryRun_ = true;
  sphereCount_ = 0;
  std::memset(edgeKeys_, 0, sizeof(edgeKeys_));
  traverseSphere(*this, &Renderer::subdivideFace, order);
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
  int shift = base + sphereExtra_;
  if (shift > BASE_LEVEL) shift = BASE_LEVEL;
  int count = countSphereLines(shift, order);
  while (count > WIRE_LIMIT && shift < BASE_LEVEL) {
    shift++;
    count = countSphereLines(shift, order);
  }
  if (shift > base && shift - 1 >= base) {
    int relaxed = countSphereLines(shift - 1, order);
    if (relaxed < WIRE_RELAX) {
      shift--;
      count = relaxed;
    }
  }
  sphereExtra_ = shift - base;
  sphereShift_ = shift;
  sphereCount_ = 0;
  std::memset(edgeKeys_, 0, sizeof(edgeKeys_));
  traverseSphere(*this, &Renderer::subdivideFace, order);
}

}  // namespace devoursphere::render
