// Render-side effects: debris (spinning wireframe triangles) spawned from
// the simulation's effect events, and the dust streaks of the dash.

#include <cmath>

#include "devoursphere/render/renderer.hpp"

namespace devoursphere::render {

using g3::vec3f;
using sim::FU;

static constexpr float PI = 3.14159265358979f;
static const g2::Color DEBRIS_RED = g2::makeColor(255, 70, 60);
static const g2::Color DEBRIS_GRAY = g2::makeColor(180, 185, 195);

// The effects are stored relative to the render origin (the player); when
// the origin moves, keep them in place
void Renderer::shiftEffects(const vec3f &delta) {
  for (int i = 0; i < debrisCount_; i++)
    debris_[i].pos = debris_[i].pos + delta;
  for (int i = 0; i < dustCount_; i++) dust_[i].pos = dust_[i].pos + delta;
}

void Renderer::spawnDebris(const vec3f &pos, int count, float size,
                           g2::Color color) {
  for (int n = 0; n < count; n++) {
    int slot;
    if (debrisCount_ < MAX_DEBRIS) {
      slot = debrisCount_++;
    } else {
      slot = (int)(frand() * MAX_DEBRIS) % MAX_DEBRIS;  // recycle
    }
    Debris &d = debris_[slot];
    vec3f dir = {frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1};
    if (g3::length(dir) < 0.01f) dir = {1, 0, 0};
    dir = g3::normalize(dir);
    d.pos = pos + dir * (size * frand());
    d.vel = dir * ((6.0f + 10.0f * frand()) * (0.5f + size));
    vec3f axis = {frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1};
    if (g3::length(axis) < 0.01f) axis = {0, 0, 1};
    d.axis = g3::normalize(axis);
    d.angle = frand() * 2 * PI;
    d.spin = (6.0f + 10.0f * frand()) * (frand() < 0.5f ? -1.0f : 1.0f);
    d.size = size * (0.6f + 0.8f * frand());
    d.life = d.life0 = 0.7f + 0.6f * frand();
    d.color = color;
  }
}

// Turn the simulation's effect events of the last tick into debris
void Renderer::collectEffects() {
  const sim::Game &g = *game_;
  // Only once per simulation tick (a frame may be rendered several times)
  if (g.tickCount() == lastEffectTick_) return;
  lastEffectTick_ = g.tickCount();
  // Debris scale follows the player's visual size
  float base =
      sim::fragmentHalfSize(sim::log2Floor(g.player().size)) / (float)FU;
  for (int i = 0; i < g.effectCount(); i++) {
    const sim::EffectEvent &e = g.effects()[i];
    vec3f pos = toLocal(sim::scaleToLength(e.n, e.r));
    bool mine = e.kind == sim::EffectKind::PLAYER_HIT ||
                e.kind == sim::EffectKind::PLAYER_DRAINED;
    int count = 2;
    if (e.kind != sim::EffectKind::PLAYER_DRAINED &&
        e.kind != sim::EffectKind::ENEMY_DRAINED) {
      int k = sim::log2Floor((uint32_t)(e.size > 0 ? e.size : 1));
      count = 1 + (k > 5 ? 5 : k) / 3;
    }
    spawnDebris(pos, count, base * 0.6f, mine ? DEBRIS_RED : DEBRIS_GRAY);
    // ... and the body flashes white
    if (e.entity >= 0 && e.entity < sim::MAX_ENTITIES) {
      flash_[e.entity] = 0.12f;
    }
  }
}

void Renderer::updateEffects(float dt) {
  if (dt > 0.1f) dt = 0.1f;
  for (int i = 0; i < sim::MAX_ENTITIES; i++) {
    if (flash_[i] > 0) flash_[i] -= dt;
  }
  // Debris: fly, slow down, spin, shrink and vanish
  for (int i = 0; i < debrisCount_;) {
    Debris &d = debris_[i];
    d.life -= dt;
    if (d.life <= 0) {
      debris_[i] = debris_[--debrisCount_];
      continue;
    }
    d.pos = d.pos + d.vel * dt;
    d.vel = d.vel * std::exp(-dt * 1.5f);
    d.angle += d.spin * dt;
    i++;
  }

  // Dust: spawned ahead of the player while dashing, world static, streams
  // past the camera as the player moves
  const sim::Entity &p = game_->player();
  float dash = p.dashLevel / 256.0f;
  bool dashing =
      p.alive && dash > 0.05f && game_->state() == sim::GameState::PLAYING;
  if (dashing) {
    vec3f up = {p.frame.n.x / 1073741824.0f, p.frame.n.y / 1073741824.0f,
                p.frame.n.z / 1073741824.0f};
    vec3f fwd = {p.frame.t.x / 1073741824.0f, p.frame.t.y / 1073741824.0f,
                 p.frame.t.z / 1073741824.0f};
    vec3f right = g3::cross(fwd, up);
    float reach = camDist_ * 4.0f + 10.0f;
    dustSpawnAcc_ += dt * 90.0f * dash;  // more dust the faster the dash
    while (dustSpawnAcc_ >= 1.0f && dustCount_ < MAX_DUST) {
      dustSpawnAcc_ -= 1.0f;
      Dust &d = dust_[dustCount_++];
      d.pos = fwd * (reach * (0.4f + frand())) +
              right * (reach * (frand() - 0.5f) * 1.2f) +
              up * (camHeight_ * (frand() * 2.5f - 0.5f));
      d.age = 0;
    }
    if (dustSpawnAcc_ > 1.0f) dustSpawnAcc_ = 1.0f;
  } else {
    dustSpawnAcc_ = 0;
  }
  for (int i = 0; i < dustCount_;) {
    Dust &d = dust_[i];
    d.age += dt;
    vec3f rel = d.pos - cam_.eye;
    if (d.age > 4.0f || g3::dot(rel, viewDir_) < 0.5f) {
      dust_[i] = dust_[--dustCount_];
      continue;
    }
    i++;
  }
}

void Renderer::drawEffects() {
  // Debris: wireframe triangles
  for (int i = 0; i < debrisCount_; i++) {
    const Debris &d = debris_[i];
    float s = d.size * (d.life / d.life0);
    vec3f helper = std::fabs(d.axis.x) < 0.9f ? vec3f{1, 0, 0} : vec3f{0, 1, 0};
    vec3f u = g3::normalize(g3::cross(d.axis, helper));
    vec3f v = g3::cross(d.axis, u);
    vec3f pts[3];
    for (int k = 0; k < 3; k++) {
      float a = d.angle + k * (2 * PI / 3);
      pts[k] = d.pos + (u * std::cos(a) + v * std::sin(a)) * s;
    }
    putLineLoop3(pts, 3, d.color, palette_[PAL_LINE]);
  }

  // Dust: streaks along the player's motion, bright head, fading tail
  const sim::Entity &p = game_->player();
  if (dustCount_ > 0) {
    vec3f fwd = {p.frame.t.x / 1073741824.0f, p.frame.t.y / 1073741824.0f,
                 p.frame.t.z / 1073741824.0f};
    float speedFUs = p.speed * (float)sim::TICK_RATE / FU;
    float len = speedFUs * 0.18f + 1.0f;
    g2::Color head = g2::makeColor(150, 190, 255);
    g2::Color tail = g2::makeColor(0, 0, 0);
    for (int i = 0; i < dustCount_; i++) {
      const Dust &d = dust_[i];
      putLine3(d.pos, d.pos + fwd * len, head, tail, palette_[PAL_LINE_ADD]);
    }
  }
}

}  // namespace devoursphere::render
