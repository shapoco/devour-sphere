// Planet wireframe (adaptively subdivided icosahedron) and background stars.

#include <cmath>
#include <cstring>

#include "devoursphere/render/renderer.hpp"

namespace devoursphere::render {

using g3::vec3f;
using sim::PU;

static constexpr float PLANET_R = (float)sim::PLANET_RADIUS / PU;
static constexpr float ICO_EDGE = 1.0515f;  // edge length of a unit icosahedron
static constexpr float SUBDIVIDE_PX =
    56.0f;  // subdivide edges longer than this
static constexpr float FADE_NEAR = 60.0f, FADE_FAR = 900.0f;  // PU

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

void Renderer::emitEdge(const vec3f &a, const vec3f &b, int level) {
  uint32_t ha = hashVec(a), hb = hashVec(b);
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
  vec3f pa = planetCenter_ + a * PLANET_R;
  vec3f pb = planetCenter_ + b * PLANET_R;
  vec3f mid = (pa + pb) * 0.5f;
  float d = g3::length(mid - cam_.eye);
  float t = (FADE_FAR - d) / (FADE_FAR - FADE_NEAR);
  t = g3::clamp01(t);
  int it = (int)(t * 200) + (level <= 1 ? 55 : (level == 2 ? 30 : 0));
  if (it > 255) it = 255;
  g2::Color dim = g2::makeColor(6, 14, 40);
  g2::Color bright = g2::makeColor(70, 130, 235);
  addLine(pa, pb, g2::lerpColor(dim, bright, it));
}

void Renderer::subdivideFace(const vec3f &a, const vec3f &b, const vec3f &c,
                             int level) {
  if (lineCount_ >= MAX_LINES - 3) return;
  vec3f center = g3::normalize(a + b + c);
  // Horizon: skip faces entirely on the far side of the planet
  if (g3::dot(center, camUnit_) < cullCos_[level]) return;
  vec3f worldCenter = planetCenter_ + center * PLANET_R;
  vec3f rel = worldCenter - cam_.eye;
  float edgeLen = ICO_EDGE * PLANET_R / (float)(1 << level);
  // Faces entirely behind the camera
  if (g3::dot(rel, viewDir_) < -edgeLen) return;
  float d = g3::length(rel);
  float px = edgeLen * focalPx_ / (d > 1.0f ? d : 1.0f);
  if (level < MAX_PLANET_LEVEL && px > SUBDIVIDE_PX) {
    vec3f ab = g3::normalize(a + b);
    vec3f bc = g3::normalize(b + c);
    vec3f ca = g3::normalize(c + a);
    subdivideFace(a, ab, ca, level + 1);
    subdivideFace(ab, b, bc, level + 1);
    subdivideFace(ca, bc, c, level + 1);
    subdivideFace(ab, bc, ca, level + 1);
    return;
  }
  emitEdge(a, b, level);
  emitEdge(b, c, level);
  emitEdge(c, a, level);
}

void Renderer::buildPlanet() {
  std::memset(edgeKeys_, 0, sizeof(edgeKeys_));
  for (int f = 0; f < 20; f++) {
    subdivideFace(ICO_VERTS[ICO_FACES[f][0]], ICO_VERTS[ICO_FACES[f][1]],
                  ICO_VERTS[ICO_FACES[f][2]], 0);
  }
}

void Renderer::buildStars() {
  constexpr int STARS = 110;
  for (int i = 0; i < STARS; i++) {
    uint32_t h = (uint32_t)(i + 1) * 2654435761u;
    uint32_t h2 = h * 40503u + 12345u;
    float z = ((h & 0xFFFF) / 32768.0f) - 1.0f;  // -1..1
    float phi = ((h >> 16) / 65536.0f) * 6.2831853f;
    float r = std::sqrt(1.0f - z * z);
    vec3f dir = {r * std::cos(phi), r * std::sin(phi), z};
    // Rotate into view space and project as a point at infinity
    vec3f v = view_.transformDir(dir);
    if (v.z >= -0.05f) continue;
    float w;
    vec3f c = proj_.transformPoint4(v * 100.0f, w);
    float sx = (c.x / w * 0.5f + 0.5f) * w_;
    float sy = (0.5f - c.y / w * 0.5f) * h_;
    if (sx < 0 || sy < 0 || sx >= w_ || sy >= h_) continue;
    if (pointCount_ >= MAX_POINTS) break;
    int v8 = 90 + (int)((h2 >> 8) % 120);
    Point2D &p = points_[pointCount_++];
    p.x = (int16_t)sx, p.y = (int16_t)sy;
    p.size = 1;
    p.color = g2::makeColor(v8, v8, v8 + 20 > 255 ? 255 : v8 + 20);
  }
}

}  // namespace devoursphere::render
