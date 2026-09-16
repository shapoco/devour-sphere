// Floating upgrades (spinning additive solids) and the Lance beam.

#include <cmath>

#include "devoursphere/render/renderer.hpp"

namespace devoursphere::render {

using g3::vec3f;
using sim::FU;

static constexpr float PI = 3.14159265358979f;
static constexpr float BRAD_TO_RAD = 2.0f * PI / 65536.0f;

g2::Color upgradeColor(int kind) {
  switch (kind) {
    case (int)sim::UpgradeKind::SHIELD: return g2::makeColor(110, 210, 255);
    case (int)sim::UpgradeKind::OVERDRIVE: return g2::makeColor(255, 110, 50);
    case (int)sim::UpgradeKind::THRUSTER: return g2::makeColor(170, 240, 60);
    case (int)sim::UpgradeKind::EXTRA_CORE: return g2::makeColor(255, 110, 200);
    default: return g2::makeColor(200, 200, 200);
  }
}

// A convex solid; every triangle is wound counter-clockwise seen from
// outside (normal away from the centroid), so back-face culling works
void Renderer::putSolid(const vec3f *verts, int nv, const uint16_t *idx, int ni,
                        const g3::Material &m) {
  g3::Vertex v[12];
  uint16_t order[24];
  if (nv > 12 || ni > 24) return;
  vec3f center = {0, 0, 0};
  for (int i = 0; i < nv; i++) center = center + verts[i];
  center = center * (1.0f / nv);
  for (int i = 0; i < nv; i++) {
    v[i].position = verts[i];
    v[i].normal = {0, 1, 0};
    v[i].uv = {0, 0};
    v[i].color = g3::VERTEX_WHITE;
  }
  for (int t = 0; t + 2 < ni; t += 3) {
    const vec3f &a = verts[idx[t]], &b = verts[idx[t + 1]],
                &c = verts[idx[t + 2]];
    vec3f n = g3::cross(b - a, c - a);
    vec3f out = (a + b + c) * (1.0f / 3) - center;
    bool flip = g3::dot(n, out) < 0;
    order[t] = idx[t];
    order[t + 1] = flip ? idx[t + 2] : idx[t + 1];
    order[t + 2] = flip ? idx[t + 1] : idx[t + 2];
  }
  g3::VertexBuffer vb = {(uint16_t)nv, v};
  g3::Primitive prim = {g3::PrimitiveType::TRIANGLES, &vb, (uint16_t)ni, order,
                        &m};
  g3d_.putPrimitive(prim);
  kites_ += ni / 6;
}

static vec3f rotateAxis(const vec3f &v, const vec3f &axis, float angle) {
  float c = std::cos(angle), s = std::sin(angle);
  return v * c + g3::cross(axis, v) * s + axis * (g3::dot(axis, v) * (1 - c));
}

void Renderer::drawFloatingUpgrades() {
  const sim::Game &g = *game_;
  // Size follows the player's visual scale
  float s =
      sim::fragmentHalfSize(sim::log2Floor(g.player().size)) / (float)FU * 1.6f;
  for (int i = 0; i < sim::MAX_FLOATING_UPGRADES; i++) {
    const sim::FloatingUpgrade &u = g.floatingUpgrades[i];
    if (!u.alive) continue;
    vec3f up = {u.n.x / 1073741824.0f, u.n.y / 1073741824.0f,
                u.n.z / 1073741824.0f};
    g2::Color col = upgradeColor(u.kind);
    if (g3::dot(up, camUnit_) < cosHorizon_ - 0.02f) {
      // Beyond the horizon: a marker in the upgrade's color
      if (markerCount_ >= MAX_MARKERS) continue;
      vec3f d = up - camUnit_ * g3::dot(up, camUnit_);
      float len = g3::length(d);
      if (len < 1e-5f) continue;
      d = d * (1.0f / len);
      vec3f hp =
          camUnit_ * std::cos(horizonAngle_) + d * std::sin(horizonAngle_);
      vec3f world = sphereCenter_ + hp * ((float)sim::SPHERE_RADIUS / FU);
      float sx, sy;
      if (!project(world, sx, sy)) continue;
      if (sx < 4 || sx >= w_ - 4 || sy < 4 || sy >= h_ - 4) continue;
      Marker2D &mk = markers_[markerCount_++];
      mk.x = (int16_t)sx;
      mk.y = (int16_t)sy;
      mk.color = col;
      mk.kind = u.kind;
      mk.carrier = false;
      continue;
    }
    vec3f pos = toLocal(sim::scaleToLength(u.n, u.r)) + up * (s * 0.5f);
    vec3f rel = pos - cam_.eye;
    if (g3::dot(rel, viewDir_) < -2.0f) continue;
    float d = g3::length(rel);
    if (s * focalPx_ / (d > 0.1f ? d : 0.1f) < 1.5f) {
      putPoint3(pos, col, palette_[PAL_LINE]);
      continue;
    }
    // Spinning local basis: a, b in the tangent plane, up
    vec3f helper = std::fabs(up.x) < 0.9f ? vec3f{1, 0, 0} : vec3f{0, 1, 0};
    vec3f a0 = g3::normalize(g3::cross(up, helper));
    float ang = u.spin * BRAD_TO_RAD;
    vec3f a = rotateAxis(a0, up, ang);
    // slow nod around `a` so the top faces show
    vec3f upT = rotateAxis(up, a, 0.35f);
    vec3f bT = g3::cross(upT, a);

    switch ((sim::UpgradeKind)u.kind) {
      case sim::UpgradeKind::SHIELD: {
        // Octahedron stretched along its vertical axis
        const vec3f v[6] = {pos + upT * (s * 1.6f),
                            pos - upT * (s * 1.6f),
                            pos + a * s,
                            pos + bT * s,
                            pos - a * s,
                            pos - bT * s};
        static const uint16_t idx[24] = {0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 2,
                                         1, 3, 2, 1, 4, 3, 1, 5, 4, 1, 2, 5};
        putSolid(v, 6, idx, 24, palette_[PAL_UP_SHIELD]);
        break;
      }
      case sim::UpgradeKind::OVERDRIVE: {
        // Tetrahedron
        vec3f v[4] = {pos + upT * (s * 1.3f)};
        for (int k = 0; k < 3; k++) {
          float t = k * (2 * PI / 3);
          v[k + 1] = pos - upT * (s * 0.45f) +
                     (a * std::cos(t) + bT * std::sin(t)) * (s * 1.1f);
        }
        static const uint16_t idx[12] = {0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2};
        putSolid(v, 4, idx, 12, palette_[PAL_UP_OVERDRIVE]);
        break;
      }
      case sim::UpgradeKind::THRUSTER: {
        // Dart: a tetrahedron stretched along a horizontal axis (an
        // arrowhead that sweeps around as it spins)
        vec3f v[4] = {pos + a * (s * 2.2f)};
        for (int k = 0; k < 3; k++) {
          float t = k * (2 * PI / 3) + PI / 2;
          v[k + 1] = pos - a * (s * 0.8f) +
                     (upT * std::cos(t) + bT * std::sin(t)) * (s * 0.7f);
        }
        static const uint16_t idx[12] = {0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2};
        putSolid(v, 4, idx, 12, palette_[PAL_UP_THRUSTER]);
        break;
      }
      case sim::UpgradeKind::EXTRA_CORE: {
        // Stellated octahedron: two interpenetrating tetrahedra (8 spikes)
        vec3f v[8];
        v[0] = pos + upT * (s * 1.4f);
        v[4] = pos - upT * (s * 1.4f);
        for (int k = 0; k < 3; k++) {
          float t = k * (2 * PI / 3);
          v[k + 1] = pos - upT * (s * 0.47f) +
                     (a * std::cos(t) + bT * std::sin(t)) * (s * 1.3f);
          v[k + 5] = pos + upT * (s * 0.47f) +
                     (a * std::cos(t + PI / 3) + bT * std::sin(t + PI / 3)) *
                         (s * 1.3f);
        }
        static const uint16_t idx[24] = {0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2,
                                         4, 6, 5, 4, 7, 6, 4, 5, 7, 5, 6, 7};
        putSolid(v, 8, idx, 24, palette_[PAL_UP_CORE]);
        break;
      }
      default: break;
    }
  }
}

// The Lance: a thick additive beam ahead of the player with a bright core
void Renderer::drawLance() {
  const sim::Game &g = *game_;
  if (!g.lanceActive()) return;
  const sim::Entity &p = g.player();
  vec3f up = {p.frame.n.x / 1073741824.0f, p.frame.n.y / 1073741824.0f,
              p.frame.n.z / 1073741824.0f};
  vec3f fwd = {p.frame.t.x / 1073741824.0f, p.frame.t.y / 1073741824.0f,
               p.frame.t.z / 1073741824.0f};
  vec3f right = g3::cross(fwd, up);
  float len = g.lanceLength() / (float)FU;
  float width = p.bodyRadius / (float)FU;
  float flicker = 0.85f + 0.15f * std::sin(time_ * 60.0f);
  vec3f start = fwd * (p.coreY / (float)FU);
  vec3f center = start + fwd * (len * 0.5f);
  putQuad(center + up * 0.1f, fwd, right, len * 0.5f, width * flicker,
          palette_[PAL_LANCE]);
  putQuad(center + up * 0.15f, fwd, right, len * 0.5f, width * 0.3f * flicker,
          palette_[PAL_LANCE_CORE]);
}

}  // namespace devoursphere::render
