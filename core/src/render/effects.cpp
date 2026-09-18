// Render-side effects: debris (spinning wireframe triangles) spawned from
// the simulation's effect events, and the dust streaks of the dash.

#include <cmath>

#include "devoursphere/render/renderer.hpp"

#include "trig.hpp"

namespace devoursphere::render {

using g3::vec3f;
using sim::FU;

static constexpr float PI = 3.14159265358979f;
static constexpr float PICKUP_DURATION = 0.3f;  // seconds
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
    axis = g3::normalize(axis);
    // The plane of the triangle, fixed here so that drawing it is a sine
    // and cosine pair and nine multiplies
    vec3f helper = std::fabs(axis.x) < 0.9f ? vec3f{1, 0, 0} : vec3f{0, 1, 0};
    d.u = g3::normalize(g3::cross(axis, helper));
    d.v = g3::cross(axis, d.u);
    d.angle = frand() * 2 * PI;
    d.spin = (6.0f + 10.0f * frand()) * (frand() < 0.5f ? -1.0f : 1.0f);
    d.size = size * (0.6f + 0.8f * frand());
    d.life = d.life0 = 0.7f + 0.6f * frand();
    d.color = color;
  }
}

// Take the effects of a tick the front end has just run. A platform that
// catches up several ticks in one frame calls this after every one of them:
// the simulation only holds the events of the current tick, so without this
// everything but the last tick of the batch would go unnoticed.
void Renderer::pollEffects(const sim::Game &game) {
  // Before the first frame there is no render origin to place the debris
  // against; beginFrame() collects that tick instead.
  if (!originValid_) return;
  game_ = &game;
  collectEffects();
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
                e.kind == sim::EffectKind::PLAYER_DRAINED ||
                e.kind == sim::EffectKind::PLAYER_KILLED;
    int count = 2;
    float size = base * 0.6f;
    if (e.kind == sim::EffectKind::ENTITY_KILLED ||
        e.kind == sim::EffectKind::PLAYER_KILLED) {
      // A death nearby: a burst of debris scaled by the victim's size
      float victim = sim::fragmentHalfSize(
                         sim::log2Floor((uint32_t)(e.size > 0 ? e.size : 1))) /
                     (float)FU;
      count = 14;
      size = victim * 0.8f;
    } else if (e.kind != sim::EffectKind::PLAYER_DRAINED &&
               e.kind != sim::EffectKind::ENEMY_DRAINED) {
      int k = sim::log2Floor((uint32_t)(e.size > 0 ? e.size : 1));
      count = 1 + (k > 5 ? 5 : k) / 3;
    }
    spawnDebris(pos, count, size, mine ? DEBRIS_RED : DEBRIS_GRAY);
    // ... and the body flashes white
    if (e.entity >= 0 && e.entity < sim::MAX_ENTITIES) {
      flash_[e.entity] = 0.12f;
    }
  }
}

void Renderer::updateEffects(float dt) {
  if (dt > 0.1f) dt = 0.1f;
  // Pickup flashes (one per tick at most)
  {
    const sim::Game &g = *game_;
    bool upgraded = (g.events() & sim::Event::PLAYER_UPGRADED) != 0;
    bool ate = (g.events() & sim::Event::PLAYER_ATE_FRAGMENT) != 0;
    if ((upgraded || ate) && g.tickCount() != lastPickupTick_ &&
        g.player().alive) {
      lastPickupTick_ = g.tickCount();
      int slot = pickupCount_ < MAX_PICKUPS ? pickupCount_++ : 0;
      Pickup &pk = pickups_[slot];
      pk.age = 0;
      pk.angle = frand() * 2 * PI;
      if (upgraded) {
        // Upgrades: a bigger, longer flash in the upgrade's color
        pk.duration = PICKUP_DURATION * 2.0f;
        pk.scale = 1.8f;
        pk.color = upgradeColor((int)g.lastUpgradeKind());
      } else {
        pk.duration = PICKUP_DURATION;
        pk.scale = 1.0f;
        pk.color = colorForEntity(g.player());
      }
    }
    for (int i = 0; i < pickupCount_;) {
      pickups_[i].age += dt;
      pickups_[i].angle += dt * 9.0f;
      if (pickups_[i].age >= pickups_[i].duration) {
        pickups_[i] = pickups_[--pickupCount_];
        continue;
      }
      i++;
    }
  }
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
  // Pickup flashes: triangles perpendicular to the view direction, in the
  // player's color, spinning and shrinking onto the player
  if (pickupCount_ > 0) {
    const sim::Entity &p = game_->player();
    vec3f camUp = {0, 0, 1};
    if (std::fabs(g3::dot(viewDir_, cam_.up)) < 0.99f) camUp = cam_.up;
    vec3f right = g3::normalize(g3::cross(viewDir_, camUp));
    vec3f up = g3::cross(right, viewDir_);
    float bodyR = sim::fragmentHalfSize(sim::log2Floor(p.size)) / (float)FU *
                  (2.0f + 0.2f * p.fragmentCount);
    for (int i = 0; i < pickupCount_; i++) {
      const Pickup &pk = pickups_[i];
      g2::Color color = pk.color;
      float t = 1.0f - pk.age / pk.duration;  // 1 -> 0
      float radius = bodyR * (0.5f + 5.0f * t) * pk.scale;
      vec3f pts[3];
      float c[3], s[3];
      triangleAngles(pk.angle, c, s);
      for (int k = 0; k < 3; k++) pts[k] = (right * c[k] + up * s[k]) * radius;
      putLoop2Df(pts, 3, color, L2D_OVER, palette_[PAL_LINE]);
    }
  }

  // Debris: wireframe triangles
  for (int i = 0; i < debrisCount_; i++) {
    const Debris &d = debris_[i];
    float size = d.size * (d.life / d.life0);
    vec3f pts[3];
    float c[3], s[3];
    triangleAngles(d.angle, c, s);
    for (int k = 0; k < 3; k++)
      pts[k] = d.pos + (d.u * c[k] + d.v * s[k]) * size;
    putLoop2Df(pts, 3, d.color, L2D_OVER, palette_[PAL_LINE]);
  }

  // Dust: streaks along the player's motion, bright head, fading tail. As
  // 2D lines the fade is towards black rather than additive to transparent;
  // the sky behind them is black anyway
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
      if (!addLine2Df(d.pos, d.pos + fwd * len, head, 255, 0, L2D_OVER)) {
        putLine3(d.pos, d.pos + fwd * len, head, tail, palette_[PAL_LINE_ADD]);
      }
    }
  }
}

}  // namespace devoursphere::render
