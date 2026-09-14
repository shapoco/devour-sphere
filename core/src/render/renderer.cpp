// Camera, scene construction and band rendering.

#include <cmath>
#include <cstring>

#include "devoursphere/render/renderer.hpp"

namespace devoursphere::render {

using g3::vec3f;
using sim::PU;

static constexpr float PI = 3.14159265358979f;
static constexpr float Z_NEAR = 0.4f;    // PU
static constexpr float Z_FAR = 1800.0f;  // PU
static constexpr float PLANET_R = (float)sim::PLANET_RADIUS / PU;

// Palette indices
enum : int {
  PAL_ENEMY0 = 0,  // ENEMY_HUE_COUNT entries
  PAL_PLAYER = sim::ENEMY_HUE_COUNT,
  PAL_GRAY,
  PAL_PART,
  PAL_CORE,
  PAL_BULLET_PLAYER,
  PAL_BULLET_ENEMY,
  PAL_LASER_PLAYER,
};

static g3::colorf toColorf(g2::Color c) {
  return {g2::colorR(c) / 255.0f, g2::colorG(c) / 255.0f,
          g2::colorB(c) / 255.0f, 1.0f};
}

static g3::Material flatMaterial(g2::Color c) {
  g3::Material m = {};
  m.diffuse = toColorf(c);
  m.ambient = m.diffuse;
  m.texture = nullptr;
  m.blendMode = g3::BlendMode::NONE;
  m.flags = g3::MaterialFlags::DOUBLE_SIDED;
  return m;
}

static g3::Material addMaterial(g2::Color c, float opacity) {
  g3::Material m = flatMaterial(c);
  m.diffuse.a = opacity;
  m.blendMode = g3::BlendMode::ADD;
  return m;
}

static g2::Color hueColor(int hue, int s, int v) {
  return g2::makeColorHsv(hue * 360 / 256, s, v);
}

void Renderer::init(int width, int height, void *arena, size_t arenaSize) {
  w_ = width;
  h_ = height;
  g3d_.init((int16_t)width, (int16_t)height, arena, arenaSize);
  g3d_.disableClear();

  for (int i = 0; i < sim::ENEMY_HUE_COUNT; i++) {
    palette_[PAL_ENEMY0 + i] =
        flatMaterial(hueColor(sim::ENEMY_HUES[i], 235, 235));
  }
  palette_[PAL_PLAYER] = flatMaterial(hueColor(sim::PLAYER_HUE, 200, 255));
  palette_[PAL_GRAY] = flatMaterial(g2::makeColor(110, 112, 120));
  palette_[PAL_PART] = flatMaterial(hueColor(sim::PART_HUE, 220, 220));
  palette_[PAL_CORE] = flatMaterial(g2::makeColor(255, 255, 255));
  palette_[PAL_BULLET_PLAYER] = addMaterial(g2::makeColor(255, 150, 30), 1.0f);
  palette_[PAL_BULLET_ENEMY] = addMaterial(g2::makeColor(255, 60, 110), 1.0f);
  palette_[PAL_LASER_PLAYER] = addMaterial(g2::makeColor(255, 190, 80), 1.0f);

  for (int h = 0; h < 256; h++) {
    int best = 0, bestD = 1000;
    for (int i = 0; i < sim::ENEMY_HUE_COUNT; i++) {
      int d = std::abs(h - sim::ENEMY_HUES[i]);
      if (d > 128) d = 256 - d;
      if (d < bestD) bestD = d, best = i;
    }
    hueToPalette_[h] = (uint8_t)best;
  }
  camValid_ = false;
  time_ = 0;
}

// ---------------------------------------------------------------------------
// Coordinate helpers

static inline vec3f q30ToF(const sim::Vec3 &v) {
  constexpr float k = 1.0f / (float)(1 << 30);
  return {v.x * k, v.y * k, v.z * k};
}

vec3f Renderer::toLocal(const sim::Vec3 &worldUnits) const {
  constexpr float k = 1.0f / PU;
  return {(worldUnits.x - origin_.x) * k, (worldUnits.y - origin_.y) * k,
          (worldUnits.z - origin_.z) * k};
}

bool Renderer::project(const vec3f &p, float &sx, float &sy) const {
  float w;
  vec3f c = viewProj_.transformPoint4(p, w);
  if (w < Z_NEAR) return false;
  float iw = 1.0f / w;
  sx = (c.x * iw * 0.5f + 0.5f) * w_;
  sy = (0.5f - c.y * iw * 0.5f) * h_;
  return true;
}

// Liang-Barsky clip of a 2D segment against a rectangle
static bool clipSegment(float &x0, float &y0, float &x1, float &y1, float xmin,
                        float ymin, float xmax, float ymax) {
  float dx = x1 - x0, dy = y1 - y0;
  float t0 = 0, t1 = 1;
  const float p[4] = {-dx, dx, -dy, dy};
  const float q[4] = {x0 - xmin, xmax - x0, y0 - ymin, ymax - y0};
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) {
      if (q[i] < 0) return false;
    } else {
      float t = q[i] / p[i];
      if (p[i] < 0) {
        if (t > t1) return false;
        if (t > t0) t0 = t;
      } else {
        if (t < t0) return false;
        if (t < t1) t1 = t;
      }
    }
  }
  float nx0 = x0 + dx * t0, ny0 = y0 + dy * t0;
  float nx1 = x0 + dx * t1, ny1 = y0 + dy * t1;
  x0 = nx0, y0 = ny0, x1 = nx1, y1 = ny1;
  return true;
}

void Renderer::addLine(const vec3f &a, const vec3f &b, g2::Color color) {
  if (lineCount_ >= MAX_LINES) return;
  float wa, wb;
  vec3f ca = viewProj_.transformPoint4(a, wa);
  vec3f cb = viewProj_.transformPoint4(b, wb);
  if (wa < Z_NEAR && wb < Z_NEAR) return;
  if (wa < Z_NEAR || wb < Z_NEAR) {
    // Clip against the near plane in homogeneous space
    float t = (Z_NEAR - wa) / (wb - wa);
    vec3f cm = g3::lerp(ca, cb, t);
    if (wa < Z_NEAR)
      ca = cm, wa = Z_NEAR;
    else
      cb = cm, wb = Z_NEAR;
  }
  float x0 = (ca.x / wa * 0.5f + 0.5f) * w_,
        y0 = (0.5f - ca.y / wa * 0.5f) * h_;
  float x1 = (cb.x / wb * 0.5f + 0.5f) * w_,
        y1 = (0.5f - cb.y / wb * 0.5f) * h_;
  if (!clipSegment(x0, y0, x1, y1, -4, -4, (float)w_ + 4, (float)h_ + 4))
    return;
  Line2D &l = lines_[lineCount_++];
  l.x0 = (int16_t)std::lround(x0), l.y0 = (int16_t)std::lround(y0);
  l.x1 = (int16_t)std::lround(x1), l.y1 = (int16_t)std::lround(y1);
  l.color = color;
}

void Renderer::addPoint(const vec3f &p, int size, g2::Color color) {
  if (pointCount_ >= MAX_POINTS) return;
  float sx, sy;
  if (!project(p, sx, sy)) return;
  if (sx < -2 || sy < -2 || sx >= w_ + 2 || sy >= h_ + 2) return;
  Point2D &pt = points_[pointCount_++];
  pt.x = (int16_t)sx, pt.y = (int16_t)sy;
  pt.size = (uint8_t)size;
  pt.color = color;
}

// ---------------------------------------------------------------------------
// Camera

static vec3f rotateAroundAxis(const vec3f &v, const vec3f &axis, float angle) {
  float c = std::cos(angle), s = std::sin(angle);
  return v * c + g3::cross(axis, v) * s + axis * (g3::dot(axis, v) * (1 - c));
}

void Renderer::updateCamera(float dt) {
  const sim::Game &g = *game_;
  const sim::Creature &p = g.player();
  origin_ = sim::scaleToLength(p.frame.n, p.r);
  vec3f up = q30ToF(p.frame.n);
  vec3f fwd = q30ToF(p.frame.t);
  float bodyR = p.bodyRadius / (float)PU;

  float wantDist = 3.2f * bodyR + 5.0f;
  if (wantDist < 7.0f) wantDist = 7.0f;
  float wantHeight = wantDist * 0.42f;
  float wantFov = 62.0f * PI / 180.0f;
  float wantRoll = 0;
  sim::GameState st = g.state();
  if (st == sim::GameState::PLAYING) {
    if (p.dashing) {
      wantDist *= 0.72f;
      wantHeight *= 0.8f;
      wantFov = 50.0f * PI / 180.0f;
    } else if (p.braking) {
      wantDist *= 1.35f;
      wantHeight *= 1.3f;
      wantFov = 76.0f * PI / 180.0f;
    }
    wantRoll = p.turn * (p.braking ? 14.0f : 9.0f) * PI / 180.0f;
  } else if (st == sim::GameState::LAUNCH) {
    float t = g.stateTimer() / (float)sim::TICK_RATE;
    wantDist *= 1.0f + t * 1.5f;
    wantHeight *= 1.0f + t * 0.8f;
    wantFov = 70.0f * PI / 180.0f;
  } else if (st == sim::GameState::TITLE ||
             st == sim::GameState::WEAPON_SELECT) {
    // Cinematic orbit around the attract-mode player
    wantDist *= 2.2f;
    wantHeight *= 1.6f;
    fwd = rotateAroundAxis(fwd, up, time_ * 0.25f);
  } else if (st == sim::GameState::DEAD) {
    wantDist *= 1.6f;
    wantHeight *= 2.0f;
  }

  if (!camValid_) {
    camDist_ = wantDist;
    camHeight_ = wantHeight;
    camFov_ = wantFov;
    camRoll_ = wantRoll;
    camValid_ = true;
  } else {
    float k = 1.0f - std::exp(-dt * 5.0f);
    camDist_ += (wantDist - camDist_) * k;
    camHeight_ += (wantHeight - camHeight_) * k;
    camFov_ += (wantFov - camFov_) * k;
    camRoll_ += (wantRoll - camRoll_) * k;
  }

  vec3f eye = fwd * (-camDist_) + up * camHeight_;
  // Look at a point slightly ahead of and below the player so that the
  // planet surface fills the lower part of the screen
  vec3f target = fwd * (bodyR * 0.5f + 1.0f) - up * (camHeight_ * 0.45f);
  vec3f dir = g3::normalize(target - eye);
  vec3f upR = rotateAroundAxis(up, dir, camRoll_);
  cam_.eye = eye;
  cam_.target = target;
  cam_.up = upR;
  cam_.fovY = camFov_;

  float aspect = (float)w_ / (float)h_;
  view_ = g3::mat4f::lookAt(eye, target, upR);
  proj_ = g3::mat4f::perspective(camFov_, aspect, Z_NEAR, Z_FAR);
  viewProj_ = proj_ * view_;
  g3d_.setPerspectiveProjection(camFov_, aspect, Z_NEAR, Z_FAR);
  focalPx_ = (h_ * 0.5f) / std::tan(camFov_ * 0.5f);
  viewDir_ = dir;

  planetCenter_ = toLocal({0, 0, 0});
  vec3f rel = eye - planetCenter_;
  float d = g3::length(rel);
  camUnit_ = rel * (1.0f / d);
  cosHorizon_ = PLANET_R / d;
  if (cosHorizon_ > 1) cosHorizon_ = 1;
  horizonAngle_ = std::acos(cosHorizon_);
  // Angular radius of a subdivided icosahedron face (level 0: ~37.4 deg)
  float faceAngle = 0.6524f;
  for (int l = 0; l <= MAX_PLANET_LEVEL; l++) {
    float a = horizonAngle_ + faceAngle;
    cullCos_[l] = a >= PI ? -1.0f : std::cos(a);
    faceAngle *= 0.5f;
  }
}

// ---------------------------------------------------------------------------
// Scene

const g3::Material &Renderer::materialForCreature(
    const sim::Creature &c) const {
  if (c.isPlayer) return palette_[PAL_PLAYER];
  const sim::Creature &p = game_->player();
  if (!sim::canAttack(p.size, c.size)) return palette_[PAL_GRAY];
  return palette_[PAL_ENEMY0 + hueToPalette_[c.hue]];
}

void Renderer::putKite(const vec3f &c, const vec3f &dir, const vec3f &perp,
                       float s, float tipLen, const g3::Material &m) {
  g3::Vertex v[4];
  const vec3f pos[4] = {c + dir * tipLen, c + perp * s, c - dir * s,
                        c - perp * s};
  for (int i = 0; i < 4; i++) {
    v[i].position = pos[i];
    v[i].normal = {0, 1, 0};
    v[i].uv = {0, 0};
    v[i].color = g3::VERTEX_WHITE;
  }
  static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  g3::VertexBuffer vb = {4, v};
  g3::Primitive prim = {g3::PrimitiveType::TRIANGLES, &vb, 6, idx, &m};
  g3d_.putPrimitive(prim);
  kites_++;
}

void Renderer::putQuad(const vec3f &c, const vec3f &dir, const vec3f &perp,
                       float halfLen, float halfWidth, const g3::Material &m) {
  g3::Vertex v[4];
  const vec3f pos[4] = {c + dir * halfLen + perp * halfWidth,
                        c + dir * halfLen - perp * halfWidth,
                        c - dir * halfLen - perp * halfWidth,
                        c - dir * halfLen + perp * halfWidth};
  for (int i = 0; i < 4; i++) {
    v[i].position = pos[i];
    v[i].normal = {0, 1, 0};
    v[i].uv = {0, 0};
    v[i].color = g3::VERTEX_WHITE;
  }
  static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  g3::VertexBuffer vb = {4, v};
  g3::Primitive prim = {g3::PrimitiveType::TRIANGLES, &vb, 6, idx, &m};
  g3d_.putPrimitive(prim);
  kites_++;
}

void Renderer::drawCreature(const sim::Creature &c, const vec3f &pos, bool full,
                            const g3::Material &m, bool blink) {
  vec3f up = q30ToF(c.frame.n);
  vec3f fwd = q30ToF(c.frame.t);
  vec3f right = g3::cross(fwd, up);
  const float k = 1.0f / PU;
  float coreY = c.coreY * k, coreHalf = c.coreHalf * k;
  float focusY = c.layoutFocusY * k;

  if (!blink) {
    if (full) {
      for (int i = 0; i < c.partCount; i++) {
        const sim::Part &p = c.parts[i];
        float s = sim::partHalfSize(p.sizeLog2) * k;
        float lx = p.x * k * 1.4f, ly = p.y * k;  // widen like wings
        for (int side = -1; side <= 1; side += 2) {
          float x = lx * side;
          float dx = x, dy = ly - focusY;
          float len = std::sqrt(dx * dx + dy * dy);
          if (len < 1e-4f) dx = 0, dy = -1, len = 1;
          dx /= len, dy /= len;
          vec3f dir = right * dx + fwd * dy;
          vec3f perp = right * (-dy) + fwd * dx;
          putKite(pos + right * x + fwd * ly, dir, perp, s, s * 2.5f, m);
        }
      }
    } else {
      // Far away: one kite the size of the body
      float s = c.bodyRadius * k * 0.5f;
      putKite(pos + fwd * (coreY - s), fwd * -1.0f, right, s, s * 2.0f, m);
    }
  }
  // Core (white, pointing forward)
  putKite(pos + fwd * coreY, fwd, right, coreHalf, coreHalf * 2.5f,
          palette_[PAL_CORE]);
  creaturesDrawn_++;
}

void Renderer::drawFloatingParts() {
  const sim::Game &g = *game_;
  const g3::Material &m = palette_[PAL_PART];
  g2::Color pointColor = hueColor(sim::PART_HUE, 200, 200);
  for (int i = 0; i < sim::MAX_FLOATING_PARTS; i++) {
    const sim::FloatingPart &fp = g.floatingParts[i];
    if (!fp.alive) continue;
    vec3f up = q30ToF(fp.n);
    if (g3::dot(up, camUnit_) < cosHorizon_ - 0.01f) continue;
    vec3f pos = toLocal(sim::scaleToLength(fp.n, fp.r));
    vec3f rel = pos - cam_.eye;
    float d = g3::length(rel);
    if (g3::dot(rel, viewDir_) < -2.0f) continue;
    float s = sim::partHalfSize(fp.sizeLog2) / (float)PU;
    float px = s * focalPx_ / (d > 0.1f ? d : 0.1f);
    if (px < 1.0f) {
      addPoint(pos, 1, pointColor);
      continue;
    }
    // Tangent frame spun by the part's own angle
    vec3f helper = std::fabs(up.x) < 0.9f ? vec3f{1, 0, 0} : vec3f{0, 1, 0};
    vec3f a = g3::normalize(g3::cross(up, helper));
    vec3f b = g3::cross(up, a);
    float ang = fp.spin * (2 * PI / 65536.0f);
    vec3f dir = a * std::cos(ang) + b * std::sin(ang);
    vec3f perp = g3::cross(up, dir);
    putKite(pos, dir, perp, s, s * 2.5f, m);
  }
}

void Renderer::drawBullets() {
  const sim::Game &g = *game_;
  for (int i = 0; i < sim::MAX_BULLETS; i++) {
    const sim::Bullet &b = g.bullets[i];
    if (!b.alive) continue;
    vec3f up = q30ToF(b.frame.n);
    if (g3::dot(up, camUnit_) < cosHorizon_ - 0.01f) continue;
    vec3f pos = toLocal(sim::scaleToLength(b.frame.n, b.r));
    vec3f rel = pos - cam_.eye;
    if (g3::dot(rel, viewDir_) < -2.0f) continue;
    vec3f fwd = q30ToF(b.frame.t);
    vec3f right = g3::cross(fwd, up);
    float base = sim::partHalfSize(sim::log2Floor(b.ownerSize)) / (float)PU;
    switch (b.kind) {
      case sim::Weapon::VULCAN: {
        const g3::Material &m =
            palette_[b.fromPlayer ? PAL_BULLET_PLAYER : PAL_BULLET_ENEMY];
        float s = base * 0.3f + 0.08f;
        putKite(pos, fwd, right, s, s * 2.2f, m);
        break;
      }
      case sim::Weapon::LASER: {
        const g3::Material &m =
            palette_[b.fromPlayer ? PAL_LASER_PLAYER : PAL_BULLET_ENEMY];
        putQuad(pos, fwd, right, base * 3.0f + 1.0f, base * 0.12f + 0.06f, m);
        break;
      }
      case sim::Weapon::MISSILE: {
        const g3::Material &m =
            palette_[b.fromPlayer ? PAL_BULLET_PLAYER : PAL_BULLET_ENEMY];
        float s = base * 0.45f + 0.1f;
        putKite(pos, fwd, right, s, s * 2.5f, m);
        break;
      }
    }
  }
}

void Renderer::drawParticles() {
  const sim::Game &g = *game_;
  uint32_t t = g.tickCount();
  for (int i = 0; i < sim::MAX_PARTICLES; i++) {
    const sim::Particle &p = g.particles[i];
    if (!p.alive) continue;
    vec3f up = q30ToF(p.n);
    if (g3::dot(up, camUnit_) < cosHorizon_ - 0.01f) continue;
    vec3f pos = toLocal(sim::scaleToLength(p.n, p.r));
    vec3f rel = pos - cam_.eye;
    float d = g3::length(rel);
    if (g3::dot(rel, viewDir_) < -1.0f) continue;
    int tw = (int)(((t + (uint32_t)i * 7u) >> 1) & 3);
    int v = 170 + tw * 28;
    int size = d < 40.0f ? 2 : 1;
    addPoint(pos, size, g2::makeColor(v, v, 255));
  }
}

void Renderer::buildScene() {
  const sim::Game &g = *game_;
  g3d_.beginScene();
  g3d_.lookAt(cam_.eye, cam_.target, cam_.up);
  g3d_.disableParallelLight();
  g3d_.disableEnvironmentLight();

  // Visible creatures sorted by distance
  struct Vis {
    float d, px;
    int16_t idx;
  };
  Vis vis[sim::MAX_CREATURES];
  int n = 0;
  for (int i = 0; i < sim::MAX_CREATURES; i++) {
    const sim::Creature &c = g.creatures[i];
    if (!c.alive) continue;
    vec3f up = q30ToF(c.frame.n);
    float bodyR = c.bodyRadius / (float)PU;
    if (g3::dot(up, camUnit_) < cosHorizon_ - 0.02f) continue;
    vec3f pos = toLocal(sim::scaleToLength(c.frame.n, c.r));
    vec3f rel = pos - cam_.eye;
    float d = g3::length(rel);
    if (g3::dot(rel, viewDir_) < -bodyR) continue;
    float px = bodyR * focalPx_ / (d > 0.1f ? d : 0.1f);
    if (px < 0.8f) {
      const g3::Material &m = materialForCreature(c);
      addPoint(pos, 1,
               g2::makeColorF(m.diffuse.r * 0.7f, m.diffuse.g * 0.7f,
                              m.diffuse.b * 0.7f));
      continue;
    }
    // insertion sort by distance
    int k = n++;
    while (k > 0 && vis[k - 1].d > d) {
      vis[k] = vis[k - 1];
      k--;
    }
    vis[k] = {d, px, (int16_t)i};
  }

  int triBudget = g3d_.getStats().triCapacity * 3 / 4;
  for (int k = 0; k < n; k++) {
    const sim::Creature &c = g.creatures[vis[k].idx];
    int fullTris = (1 + 2 * c.partCount) * 2;
    bool full = vis[k].px >= 6.0f && triBudget >= fullTris;
    triBudget -= full ? fullTris : 4;
    vec3f pos = toLocal(sim::scaleToLength(c.frame.n, c.r));
    bool blink = c.invincible > 0 && ((g.tickCount() >> 2) & 1);
    drawCreature(c, pos, full, materialForCreature(c), blink);
  }

  drawFloatingParts();
  drawBullets();
  g3d_.endScene();
}

// ---------------------------------------------------------------------------
// Frame

void Renderer::beginFrame(const sim::Game &game, float dt) {
  game_ = &game;
  time_ += dt;
  lineCount_ = 0;
  pointCount_ = 0;
  creaturesDrawn_ = 0;
  kites_ = 0;
  updateCamera(dt);
  buildStars();
  buildPlanet();
  drawParticles();
  buildScene();
  g3d_.beginRender();
}

void Renderer::renderBand(const g2::Surface &dst, int y, int h, int dstY) {
  g2::Graphics2D g(dst);
  g.setClipRect(0, dstY, w_, h);
  g.clear(g2::makeColor(0, 0, 4));
  int oy = dstY - y;
  for (int i = 0; i < pointCount_; i++) {
    const Point2D &p = points_[i];
    if (p.y + p.size <= y || p.y >= y + h) continue;
    if (p.size <= 1) {
      g.setPixel(p.x, p.y + oy, p.color);
    } else {
      g.fillRect(p.x, p.y + oy, p.size, p.size, p.color);
    }
  }
  for (int i = 0; i < lineCount_; i++) {
    const Line2D &l = lines_[i];
    int lo = l.y0 < l.y1 ? l.y0 : l.y1, hi = l.y0 < l.y1 ? l.y1 : l.y0;
    if (hi < y || lo >= y + h) continue;
    g.drawLine(l.x0, l.y0 + oy, l.x1, l.y1 + oy, l.color);
  }
  g3d_.render(0, (int16_t)y, (int16_t)w_, (int16_t)h, dst, 0, (int16_t)dstY);
  drawHud(g, oy);
}

void Renderer::endFrame() { g3d_.endRender(); }

RenderStats Renderer::stats() const {
  RenderStats s;
  s.lines = lineCount_;
  s.points = pointCount_;
  s.creaturesDrawn = creaturesDrawn_;
  s.kites = kites_;
  s.gfx = g3d_.getStats();
  return s;
}

}  // namespace devoursphere::render
