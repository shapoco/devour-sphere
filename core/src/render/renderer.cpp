// Camera, scene construction and band rendering.

#include <cmath>
#include <cstring>

#include "devoursphere/render/renderer.hpp"

namespace devoursphere::render {

using g3::vec3f;
using sim::FU;

static constexpr float PI = 3.14159265358979f;
static constexpr float Z_NEAR = 0.4f;    // FU
static constexpr float Z_FAR = 1800.0f;  // FU
static constexpr float SPHERE_R = (float)sim::SPHERE_RADIUS / FU;
static constexpr float BRAD_TO_RAD = 2.0f * PI / 65536.0f;
static constexpr float TILT_MAX = 35.0f * PI / 180.0f;  // fragment dihedral

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

// White material whose color comes from the vertices
static g3::Material vertexColorMaterial(bool additive) {
  g3::Material m = flatMaterial(g2::makeColor(255, 255, 255));
  m.flags |= g3::MaterialFlags::VERTEX_COLOR;
  if (additive) m.blendMode = g3::BlendMode::ADD;
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

  palette_[PAL_PLAYER] = flatMaterial(hueColor(sim::FRAGMENT_HUE, 220, 230));
  palette_[PAL_ENEMY_BIG] = flatMaterial(g2::makeColor(255, 90, 170));
  palette_[PAL_ENEMY_SMALL] = flatMaterial(g2::makeColor(110, 200, 255));
  palette_[PAL_FRAGMENT] = flatMaterial(hueColor(sim::FRAGMENT_HUE, 220, 220));
  palette_[PAL_CORE] = flatMaterial(g2::makeColor(255, 255, 255));
  palette_[PAL_BULLET_PLAYER] =
      addMaterial(hueColor(sim::FRAGMENT_HUE, 200, 255), 1.0f);
  palette_[PAL_BULLET_ENEMY] = addMaterial(g2::makeColor(255, 60, 110), 1.0f);
  palette_[PAL_LASER_PLAYER] =
      addMaterial(hueColor(sim::FRAGMENT_HUE, 160, 255), 1.0f);
  palette_[PAL_LINE] = vertexColorMaterial(false);
  palette_[PAL_LINE_ADD] = vertexColorMaterial(true);
  palette_[PAL_FRAGMENT_WHITE] = flatMaterial(g2::makeColor(235, 240, 245));

  camValid_ = false;
  originValid_ = false;
  debrisCount_ = 0;
  dustCount_ = 0;
  dustSpawnAcc_ = 0;
  lastEffectTick_ = 0xFFFFFFFFu;
  for (int i = 0; i < sim::MAX_ENTITIES; i++) flash_[i] = 0;
  time_ = 0;
}

float Renderer::frand() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x;
  return (x >> 8) * (1.0f / 16777216.0f);
}

// ---------------------------------------------------------------------------
// Coordinate helpers

static inline vec3f q30ToF(const sim::Vec3 &v) {
  constexpr float k = 1.0f / (float)(1 << 30);
  return {v.x * k, v.y * k, v.z * k};
}

vec3f Renderer::toLocal(const sim::Vec3 &worldUnits) const {
  constexpr float k = 1.0f / FU;
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

static vec3f rotateAroundAxis(const vec3f &v, const vec3f &axis, float angle) {
  float c = std::cos(angle), s = std::sin(angle);
  return v * c + g3::cross(axis, v) * s + axis * (g3::dot(axis, v) * (1 - c));
}

// ---------------------------------------------------------------------------
// Primitive helpers

void Renderer::putLine3(const vec3f &a, const vec3f &b, g2::Color ca,
                        g2::Color cb, const g3::Material &m) {
  g3::Vertex v[2];
  v[0].position = a;
  v[1].position = b;
  for (int i = 0; i < 2; i++) {
    v[i].normal = {0, 1, 0};
    v[i].uv = {0, 0};
  }
  v[0].color = ca;
  v[1].color = cb;
  static const uint16_t idx[2] = {0, 1};
  g3::VertexBuffer vb = {2, v};
  g3::Primitive prim = {g3::PrimitiveType::LINES, &vb, 2, idx, &m};
  g3d_.putPrimitive(prim);
  lineCount_++;
}

void Renderer::putLineLoop3(const vec3f *pts, int n, g2::Color c,
                            const g3::Material &m) {
  if (n < 2 || n > 8) return;
  g3::Vertex v[8];
  uint16_t idx[8];
  for (int i = 0; i < n; i++) {
    v[i].position = pts[i];
    v[i].normal = {0, 1, 0};
    v[i].uv = {0, 0};
    v[i].color = c;
    idx[i] = (uint16_t)i;
  }
  g3::VertexBuffer vb = {(uint16_t)n, v};
  g3::Primitive prim = {g3::PrimitiveType::LINE_LOOP, &vb, (uint16_t)n, idx,
                        &m};
  g3d_.putPrimitive(prim);
  lineCount_ += n;
}

void Renderer::putPoint3(const vec3f &p, g2::Color c, const g3::Material &m) {
  g3::Vertex v[1];
  v[0].position = p;
  v[0].normal = {0, 1, 0};
  v[0].uv = {0, 0};
  v[0].color = c;
  static const uint16_t idx[1] = {0};
  g3::VertexBuffer vb = {1, v};
  g3::Primitive prim = {g3::PrimitiveType::POINTS, &vb, 1, idx, &m};
  g3d_.putPrimitive(prim);
  pointCount_++;
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

// ---------------------------------------------------------------------------
// Camera

void Renderer::updateCamera(float dt) {
  const sim::Game &g = *game_;
  const sim::Entity &p = g.player();
  sim::Vec3 newOrigin = sim::scaleToLength(p.frame.n, p.r);
  if (originValid_) {
    // Effects are stored relative to the origin: keep them where they are
    sim::Vec3 d = origin_ - newOrigin;
    shiftEffects({d.x * (1.0f / FU), d.y * (1.0f / FU), d.z * (1.0f / FU)});
  }
  origin_ = newOrigin;
  originValid_ = true;
  vec3f up = q30ToF(p.frame.n);
  vec3f fwd = q30ToF(p.frame.t);
  // Body size estimate from the size and the fragment count, so that the
  // camera does not follow the jitter of the fragment layout
  float bodyR = sim::fragmentHalfSize(sim::log2Floor(p.size)) / (float)FU *
                (2.0f + 0.2f * p.fragmentCount);

  // High and far behind the player, looking down at roughly 45 degrees
  float wantDist = 3.5f * bodyR + 8.0f;
  if (wantDist < 11.0f) wantDist = 11.0f;
  float nominalDist = wantDist;  // without dash / brake (sphere LOD basis)
  float wantHeight = wantDist * 1.0f;
  float wantFov = 70.0f * PI / 180.0f;
  float wantRoll = 0;
  float wantAhead = bodyR * 0.5f + 1.0f;  // look target ahead of the player
  float wantDown = 0.12f;                 // ... and below (x camera height)
  sim::GameState st = g.state();
  if (st == sim::GameState::PLAYING) {
    float dash = p.dashLevel / 256.0f;
    if (dash > 0) {
      // Close behind and low, looking along the heading; blended in as the
      // dash builds up
      wantDist *= 1.0f - 0.45f * dash;
      wantHeight *= 1.0f - 0.78f * dash;
      wantFov = (70.0f - 10.0f * dash) * PI / 180.0f;
      wantAhead = wantAhead + (wantDist * 4.0f - wantAhead) * dash;
      wantDown *= 1.0f - dash;
    }
    if (p.braking) {
      wantDist *= 1.25f;
      wantHeight *= 1.25f;
      wantFov = 82.0f * PI / 180.0f;
    }
    wantRoll =
        (p.turnLevel / 256.0f) * (p.braking ? 14.0f : 9.0f) * PI / 180.0f;
  } else if (st == sim::GameState::LAUNCH) {
    float t = g.stateTimer() / (float)sim::TICK_RATE;
    wantDist *= 1.0f + t * 1.5f;
    nominalDist = wantDist;
    wantHeight *= 1.0f + t * 0.8f;
    wantFov = 70.0f * PI / 180.0f;
  } else if (st == sim::GameState::TITLE ||
             st == sim::GameState::WEAPON_SELECT) {
    // Cinematic orbit around the attract-mode player
    wantDist *= 2.2f;
    nominalDist = wantDist;
    wantHeight *= 1.6f;
    fwd = rotateAroundAxis(fwd, up, time_ * 0.25f);
  } else if (st == sim::GameState::DEAD) {
    wantDist *= 1.6f;
    nominalDist = wantDist;
    wantHeight *= 2.0f;
  }

  if (!camValid_) {
    camDist_ = wantDist;
    camNominal_ = nominalDist;
    camHeight_ = wantHeight;
    camFov_ = wantFov;
    camRoll_ = wantRoll;
    camAhead_ = wantAhead;
    camDown_ = wantDown;
    camValid_ = true;
  } else {
    float k = 1.0f - std::exp(-dt * 5.0f);
    camDist_ += (wantDist - camDist_) * k;
    camNominal_ += (nominalDist - camNominal_) * k;
    camHeight_ += (wantHeight - camHeight_) * k;
    camFov_ += (wantFov - camFov_) * k;
    camRoll_ += (wantRoll - camRoll_) * k;
    camAhead_ += (wantAhead - camAhead_) * k;
    camDown_ += (wantDown - camDown_) * k;
  }

  vec3f eye = fwd * (-camDist_) + up * camHeight_;
  // Look at a point ahead of (and normally slightly below) the player so
  // that the sphere surface fills the lower part of the screen
  vec3f target = fwd * camAhead_ - up * (camHeight_ * camDown_);
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

  sphereCenter_ = toLocal({0, 0, 0});
  vec3f rel = eye - sphereCenter_;
  float d = g3::length(rel);
  camUnit_ = rel * (1.0f / d);
  cosHorizon_ = SPHERE_R / d;
  if (cosHorizon_ > 1) cosHorizon_ = 1;
  horizonAngle_ = std::acos(cosHorizon_);
  // Angular radius of a subdivided icosahedron face (level 0: ~37.4 deg)
  float faceAngle = 0.6524f;
  for (int l = 0; l <= MAX_SPHERE_LEVEL; l++) {
    float a = horizonAngle_ + faceAngle;
    cullCos_[l] = a >= PI ? -1.0f : std::cos(a);
    faceAngle *= 0.5f;
  }
}

// ---------------------------------------------------------------------------
// Scene

const g3::Material &Renderer::materialForEntity(const sim::Entity &c) const {
  if (c.isPlayer) return palette_[PAL_PLAYER];
  const sim::Entity &p = game_->player();
  return palette_[c.size > p.size ? PAL_ENEMY_BIG : PAL_ENEMY_SMALL];
}

g2::Color Renderer::colorForEntity(const sim::Entity &c) const {
  const g3::Material &m = materialForEntity(c);
  return g2::makeColorF(m.diffuse.r, m.diffuse.g, m.diffuse.b);
}

// px: projected body radius in pixels; full: draw every fragment (otherwise
// an outline of the body)
void Renderer::drawEntity(const sim::Entity &c, const vec3f &pos, float px,
                          bool full, bool blink) {
  vec3f up = q30ToF(c.frame.n);
  vec3f fwd = q30ToF(c.frame.t);
  vec3f right = g3::cross(fwd, up);
  const float k = 1.0f / FU;
  float coreY = c.coreY * k, coreHalf = c.coreHalf * k;
  float focusY = c.layoutFocusY * k;
  const g3::Material &m = materialForEntity(c);

  // Bank into the turn: roll the body around the heading
  if (c.bank != 0) {
    float bank = c.bank * BRAD_TO_RAD;
    right = rotateAroundAxis(right, fwd, bank);
    up = rotateAroundAxis(up, fwd, bank);
  }
  // Hit flash: the whole body turns white for a moment
  int idx = (int)(&c - game_->entities);
  bool flashing = idx >= 0 && idx < sim::MAX_ENTITIES && flash_[idx] > 0;
  const g3::Material &bodyMat = flashing ? palette_[PAL_CORE] : m;

  if (blink) {
    entitiesDrawn_++;
    return;
  }
  if (full) {
    // Dihedral: fragments tilt outwards (around the heading) the farther
    // they are from the body's axis, so the body looks like it has volume
    float bodyR = c.bodyRadius * k * 1.4f;
    if (bodyR < 0.1f) bodyR = 0.1f;
    for (int i = 0; i < c.fragmentCount; i++) {
      const sim::Fragment &p = c.fragments[i];
      float s = sim::fragmentHalfSize(p.sizeLog2) * k;
      float lx = p.x * k * 1.4f, ly = p.y * k;  // widen like wings
      for (int side = -1; side <= 1; side += 2) {
        float x = lx * side;
        float t = std::fabs(x) / bodyR;
        if (t > 1) t = 1;
        // rotating +right around fwd by a positive angle moves it towards
        // -up, so the sign follows the side to raise the outer edge
        float tilt = -side * t * TILT_MAX;
        vec3f rightT = rotateAroundAxis(right, fwd, tilt);
        float dx = x, dy = ly - focusY;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) dx = 0, dy = -1, len = 1;
        dx /= len, dy /= len;
        vec3f dir = rightT * dx + fwd * dy;
        vec3f perp = rightT * (-dy) + fwd * dx;
        putKite(pos + right * x + fwd * ly, dir, perp, s, s * 2.5f, bodyMat);
      }
    }
    // Core (white, pointing forward)
    putKite(pos + fwd * coreY, fwd, right, coreHalf, coreHalf * 2.5f,
            palette_[PAL_CORE]);
  } else {
    // Far away: the outline of a body-sized kite (lines stay visible even
    // edge-on), plus the core when it is more than a few pixels
    float s = c.bodyRadius * k * 0.5f;
    vec3f center = pos + fwd * (coreY - s);
    const vec3f pts[4] = {center - fwd * (s * 2.0f), center + right * s,
                          center + fwd * s, center - right * s};
    putLineLoop3(pts, 4, colorForEntity(c), palette_[PAL_LINE]);
    if (px >= 4.0f) {
      putKite(pos + fwd * coreY, fwd, right, coreHalf, coreHalf * 2.5f,
              palette_[PAL_CORE]);
    }
  }
  entitiesDrawn_++;
}

void Renderer::drawFloatingFragments() {
  const sim::Game &g = *game_;
  g2::Color pointColor = hueColor(sim::FRAGMENT_HUE, 200, 200);
  g2::Color whitePoint = g2::makeColor(235, 240, 245);
  uint32_t playerSize = g.player().size;
  for (int i = 0; i < sim::MAX_FLOATING_FRAGMENTS; i++) {
    const sim::FloatingFragment &fp = g.floatingFragments[i];
    if (!fp.alive) continue;
    // Fragments too small to join the player's body only heal: white
    bool edible = (1u << fp.sizeLog2) * sim::FOOD_NOTICE_RATIO >= playerSize;
    const g3::Material &m =
        palette_[edible ? PAL_FRAGMENT : PAL_FRAGMENT_WHITE];
    vec3f up = q30ToF(fp.n);
    if (g3::dot(up, camUnit_) < cosHorizon_ - 0.01f) continue;
    vec3f pos = toLocal(sim::scaleToLength(fp.n, fp.r));
    vec3f rel = pos - cam_.eye;
    float d = g3::length(rel);
    if (g3::dot(rel, viewDir_) < -2.0f) continue;
    float s = sim::fragmentHalfSize(fp.sizeLog2) / (float)FU;
    float px = s * focalPx_ / (d > 0.1f ? d : 0.1f);
    if (px < 1.0f) {
      putPoint3(pos, edible ? pointColor : whitePoint, palette_[PAL_LINE]);
      continue;
    }
    // Tangent frame spun by the fragment's own angle, tilted a little
    // towards the camera like the entities
    vec3f toCam = rel * (-1.0f / (d > 0.1f ? d : 0.1f));
    float elev = std::fabs(g3::dot(toCam, up));
    float t = (0.55f - elev) / 0.55f;
    if (t > 0) up = g3::normalize(up + toCam * ((t > 1 ? 1 : t) * 0.8f));
    vec3f helper = std::fabs(up.x) < 0.9f ? vec3f{1, 0, 0} : vec3f{0, 1, 0};
    vec3f a = g3::normalize(g3::cross(up, helper));
    vec3f b = g3::cross(up, a);
    float ang = fp.spin * BRAD_TO_RAD;
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
    float base = sim::fragmentHalfSize(sim::log2Floor(b.ownerSize)) / (float)FU;
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
        // Long beam (the hit test sweeps the path of the last tick, so it
        // can be drawn as long as it flies)
        putQuad(pos, fwd, right, base * 12.0f + 4.0f, base * 0.12f + 0.06f, m);
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

void Renderer::drawStars() {
  constexpr int STARS = 80;
  constexpr float DIST = 1500.0f;  // inside the far plane
  g3d_.setPointSize(1);
  for (int i = 0; i < STARS; i++) {
    uint32_t h = (uint32_t)(i + 1) * 2654435761u;
    uint32_t h2 = h * 40503u + 12345u;
    float z = ((h & 0xFFFF) / 32768.0f) - 1.0f;  // -1..1
    float phi = ((h >> 16) / 65536.0f) * 6.2831853f;
    float r = std::sqrt(1.0f - z * z);
    vec3f dir = {r * std::cos(phi), r * std::sin(phi), z};
    // Only stars in front of the camera and above the sphere's limb
    if (g3::dot(dir, viewDir_) < 0.3f) continue;
    if (g3::dot(dir, camUnit_) < -cosHorizon_ + 0.05f) continue;
    int v8 = 90 + (int)((h2 >> 8) % 120);
    putPoint3(cam_.eye + dir * DIST,
              g2::makeColor(v8, v8, v8 + 20 > 255 ? 255 : v8 + 20),
              palette_[PAL_LINE]);
  }
}

// An enemy just behind the player, outside the screen, heading at the
// player: warn with a red glow at the bottom of the screen
void Renderer::updateRearWarning() {
  const sim::Game &g = *game_;
  const sim::Entity &p = g.player();
  rearWarning_ = 0;
  if (!p.alive || g.state() != sim::GameState::PLAYING) return;
  vec3f fwd = q30ToF(p.frame.t);
  constexpr float RANGE = 45.0f;  // FU
  for (int i = 0; i < sim::MAX_ENTITIES; i++) {
    const sim::Entity &c = g.entities[i];
    if (!c.alive || c.isPlayer) continue;
    vec3f pos = toLocal(sim::scaleToLength(c.frame.n, c.r));
    float d = g3::length(pos);
    if (d > RANGE || d < 0.5f) continue;
    if (g3::dot(pos, fwd) > 0) continue;  // ahead of the player
    float sx, sy;
    if (project(pos, sx, sy) && sx >= 0 && sx < w_ && sy >= 0 && sy < h_) {
      continue;  // visible on screen: no warning needed
    }
    vec3f toPlayer = pos * (-1.0f / d);
    float align = g3::dot(q30ToF(c.frame.t), toPlayer);
    if (align < 0.6f) continue;
    float v = (align - 0.6f) / 0.4f * (1.0f - d / RANGE);
    if (v > rearWarning_) rearWarning_ = v;
  }
  if (rearWarning_ > 1) rearWarning_ = 1;
}

// Additive red gradient over the bottom quarter of the screen
void Renderer::drawRearWarning(const g2::Surface &dst, int y, int h, int dstY) {
  if (rearWarning_ <= 0.02f || dst.format != g2::PixelFormat::RGB565BE) return;
  int top = h_ * 3 / 4;
  for (int row = y; row < y + h; row++) {
    if (row < top || row >= h_) continue;
    float t = (float)(row - top) / (float)(h_ - top);  // 0 at the top edge
    int r5 = (int)(rearWarning_ * t * t * 31.0f + 0.5f);
    if (r5 <= 0) continue;
    uint8_t *line =
        (uint8_t *)dst.pixels + (size_t)(row - y + dstY) * dst.stride;
    g2::CursorRgb565BE cur;
    cur.init(line, 0);
    for (int x = 0; x < w_; x++) {
      uint32_t px = cur.read();
      cur.write(g2::addSaturateRgb565((uint16_t)px, (uint32_t)r5, 0, 0));
      cur.next();
    }
  }
}

void Renderer::buildScene() {
  const sim::Game &g = *game_;
  g3d_.beginScene();
  g3d_.lookAt(cam_.eye, cam_.target, cam_.up);
  g3d_.disableParallelLight();
  g3d_.disableEnvironmentLight();
  g3d_.setDepthBias(0.0f);

  drawStars();
  buildSphere();

  // Visible entities sorted by distance
  struct Vis {
    float d, px;
    int16_t idx;
  };
  Vis vis[sim::MAX_ENTITIES];
  int n = 0;
  for (int i = 0; i < sim::MAX_ENTITIES; i++) {
    const sim::Entity &c = g.entities[i];
    if (!c.alive) continue;
    vec3f up = q30ToF(c.frame.n);
    float bodyR = c.bodyRadius / (float)FU;
    if (g3::dot(up, camUnit_) < cosHorizon_ - 0.02f) {
      // Beyond the horizon: opponents of a comparable size (1/4 .. 4x) get
      // a marker on the horizon in their direction
      uint32_t ps = g.player().size;
      if (c.isPlayer || markerCount_ >= MAX_MARKERS || c.size * 4 < ps ||
          c.size > ps * 4) {
        continue;
      }
      vec3f d = up - camUnit_ * g3::dot(up, camUnit_);
      float len = g3::length(d);
      if (len < 1e-5f) continue;
      d = d * (1.0f / len);
      vec3f hp =
          camUnit_ * std::cos(horizonAngle_) + d * std::sin(horizonAngle_);
      vec3f world = sphereCenter_ + hp * SPHERE_R;
      float sx, sy;
      if (!project(world, sx, sy)) continue;
      if (sx < 4 || sx >= w_ - 4 || sy < 4 || sy >= h_ - 4) continue;
      Marker2D &mk = markers_[markerCount_++];
      mk.x = (int16_t)sx;
      mk.y = (int16_t)sy;
      mk.color = colorForEntity(c);
      continue;
    }
    vec3f pos = toLocal(sim::scaleToLength(c.frame.n, c.r));
    vec3f rel = pos - cam_.eye;
    float d = g3::length(rel);
    if (g3::dot(rel, viewDir_) < -bodyR) continue;
    float px = bodyR * focalPx_ / (d > 0.1f ? d : 0.1f);
    if (px < 0.8f) {
      putPoint3(pos, colorForEntity(c), palette_[PAL_LINE]);
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

  // Triangle budget: what is left after the wireframe, minus a reserve for
  // fragments, bullets and effects
  g3::Stats st = g3d_.getStats();
  int triBudget = st.triCapacity - st.triCount - 220;
  for (int k = 0; k < n; k++) {
    const sim::Entity &c = g.entities[vis[k].idx];
    int fullTris = (1 + 2 * c.fragmentCount) * 2;
    bool full = vis[k].px >= 6.0f && triBudget >= fullTris;
    triBudget -= full ? fullTris : 6;
    vec3f pos = toLocal(sim::scaleToLength(c.frame.n, c.r));
    bool blink = c.invincible > 0 && ((g.tickCount() >> 2) & 1);
    drawEntity(c, pos, vis[k].px, full, blink);
    // Health gauge over enemies
    if (!c.isPlayer && vis[k].px >= 2.5f && gaugeCount_ < MAX_GAUGES) {
      float bodyR = c.bodyRadius / (float)FU;
      vec3f up = q30ToF(c.frame.n);
      float sx, sy;
      if (project(pos + up * (bodyR * 0.3f + 0.5f), sx, sy)) {
        int w = (int)(vis[k].px * 1.6f);
        w = w < 14 ? 14 : (w > 48 ? 48 : w);
        if (sx > -w && sx < w_ + w && sy > -8 && sy < h_ + 8) {
          Gauge2D &gg = gauges_[gaugeCount_++];
          gg.x = (int16_t)(sx - w / 2);
          gg.y = (int16_t)(sy - 6);
          gg.w = (int16_t)w;
          int ratio = c.hpMax > 0 ? (int)((int64_t)c.hp * 255 / c.hpMax) : 0;
          gg.ratio = (uint8_t)(ratio < 0 ? 0 : (ratio > 255 ? 255 : ratio));
        }
      }
    }
  }

  drawFloatingFragments();
  drawBullets();
  drawEffects();
  g3d_.endScene();
}

// ---------------------------------------------------------------------------
// Frame

void Renderer::beginFrame(const sim::Game &game, float dt) {
  game_ = &game;
  time_ += dt;
  lineCount_ = 0;
  pointCount_ = 0;
  gaugeCount_ = 0;
  markerCount_ = 0;
  entitiesDrawn_ = 0;
  kites_ = 0;
  updateCamera(dt);
  updateRearWarning();
  collectEffects();
  updateEffects(dt);
  buildScene();
  g3d_.beginRender();
}

void Renderer::renderBand(const g2::Surface &dst, int y, int h, int dstY) {
  g2::Graphics2D g(dst);
  g.setClipRect(0, dstY, w_, h);
  g.clear(g2::makeColor(0, 0, 4));
  g3d_.render(0, (int16_t)y, (int16_t)w_, (int16_t)h, dst, 0, (int16_t)dstY);
  drawRearWarning(dst, y, h, dstY);
  int oy = dstY - y;
  for (int i = 0; i < markerCount_; i++) {
    const Marker2D &mk = markers_[i];
    // Downward triangle just above the horizon point
    int x = mk.x, yb = mk.y + oy - 3, yt = yb - 7;
    if (yb < dstY || yt >= dstY + h) continue;
    g.fillTriangle(x - 5, yt, x + 5, yt, x, yb, mk.color);
    g.drawTriangle(x - 5, yt, x + 5, yt, x, yb, g2::makeColor(0, 0, 0, 120));
  }
  for (int i = 0; i < gaugeCount_; i++) {
    const Gauge2D &gg = gauges_[i];
    if (gg.y + 3 <= y || gg.y >= y + h) continue;
    g.fillRect(gg.x - 1, gg.y + oy - 1, gg.w + 2, 5,
               g2::makeColor(0, 0, 0, 170));
    int fill = gg.w * gg.ratio / 255;
    g2::Color c = gg.ratio > 128 ? g2::makeColor(90, 230, 140)
                                 : (gg.ratio > 50 ? g2::makeColor(240, 200, 60)
                                                  : g2::makeColor(240, 70, 60));
    if (fill > 0) g.fillRect(gg.x, gg.y + oy, fill, 3, c);
  }
  drawHud(g, oy);
}

void Renderer::endFrame() { g3d_.endRender(); }

RenderStats Renderer::stats() const {
  RenderStats s;
  s.lines = lineCount_;
  s.points = pointCount_;
  s.entitiesDrawn = entitiesDrawn_;
  s.kites = kites_;
  s.gfx = g3d_.getStats();
  return s;
}

}  // namespace devoursphere::render
