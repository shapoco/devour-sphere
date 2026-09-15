#ifndef DEVOURSPHERE_RENDER_RENDERER_HPP
#define DEVOURSPHERE_RENDER_RENDERER_HPP

// Draws the game state with ShapoGFX. Platform independent: the platform
// layer supplies the target surface (a whole frame buffer or a band of it).
//
// Frame structure:
//   beginFrame()  computes the camera and builds the whole 3D scene: stars
//                 (points), the sphere wireframe (lines),
//                 entities, floating fragments and bullets (triangles),
//                 debris and dash dust (lines)
//   renderBand()  draws the rows [y, y + h) of the frame into a surface:
//                 black background, the 3D scene, then the 2D overlays
//                 (enemy gauges, horizon markers) and the HUD
//   endFrame()
//
// Everything but the HUD goes through the scanline 3D renderer, so a device
// without a frame buffer can render band by band.
//
// The 3D renderer works in float, in "fragment units" (FU) relative to the
// player's position, so precision stays high anywhere on the sphere.

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/game.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"
#include "shapoco/gfx3d/gfx3d.hpp"

namespace devoursphere::render {

namespace g2 = shapoco::gfx2d;
namespace g3 = shapoco::gfx3d;

// Health gauge drawn over an enemy
struct Gauge2D {
  int16_t x, y, w;
  uint8_t ratio;  // 0..255
};

// Marker on the horizon for an enemy beyond it
struct Marker2D {
  int16_t x, y;
  g2::Color color;
  uint8_t kind;  // 0 = enemy (triangle), else sim::UpgradeKind icon
};

// Icon of an upgrade kind, centered at (cx, cy), about 14 px tall
void drawUpgradeIcon(g2::Graphics2D &g, int kind, int cx, int cy, g2::Color c);

// A piece of debris: a small spinning wireframe triangle that shrinks away
struct Debris {
  g3::vec3f pos, vel, axis;  // FU relative to the render origin
  float angle, spin;         // radians, radians per second
  float size, life, life0;   // FU, seconds
  g2::Color color;
};

// Pickup flash: a big triangle facing the camera that spins and shrinks
// onto the player
struct Pickup {
  float age;    // seconds
  float angle;  // radians
  float duration;
  float scale;  // radius multiplier
  g2::Color color;
};

// Dust streak of the dash effect (world static; streams towards the camera)
struct Dust {
  g3::vec3f pos;
  float age;
};

// Colors of the upgrade kinds (index = sim::UpgradeKind)
g2::Color upgradeColor(int kind);

struct Camera {
  g3::vec3f eye, target, up;  // FU, relative to the player's position
  float fovY;                 // radians
};

struct RenderStats {
  int lines, points, entitiesDrawn, kites;
  g3::Stats gfx;
};

class Renderer {
 public:
  static constexpr int PALETTE_SIZE = 24;
  static constexpr int MAX_SPHERE_LEVEL = 7;
  static constexpr int MAX_GAUGES = 64;
  static constexpr int MAX_MARKERS = 32;
  static constexpr int MAX_DEBRIS = 64;
  static constexpr int MAX_DUST = 64;

  // arena: working memory of the 3D renderer (192 KB or more recommended)
  void init(int width, int height, void *arena, size_t arenaSize);

  // dt: seconds since the previous frame (camera smoothing and effects)
  void beginFrame(const sim::Game &game, float dt);
  // Draw the rows [y, y + h) of the frame into dst starting at row dstY
  void renderBand(const g2::Surface &dst, int y, int h, int dstY = 0);
  void endFrame();

  RenderStats stats() const;
  const Camera &camera() const { return cam_; }
  int width() const { return w_; }
  int height() const { return h_; }

  // Internal (public for the traversal helper): one face of the sphere
  void subdivideFace(const g3::vec3f &a, const g3::vec3f &b, const g3::vec3f &c,
                     int level);

 private:
  int w_ = 0, h_ = 0;
  g3::Graphics3D g3d_;
  const sim::Game *game_ = nullptr;
  float time_ = 0;
  uint32_t rng_ = 0x1234567u;

  // Camera
  Camera cam_ = {};
  bool camValid_ = false;
  float camDist_ = 0, camHeight_ = 0, camFov_ = 0, camRoll_ = 0;
  float camAhead_ = 0, camDown_ = 0;  // look target: ahead / below the player
  float camNominal_ = 11;  // camera distance without dash/brake (sphere LOD)
  g3::mat4f view_ = g3::mat4f::identity();
  g3::mat4f proj_ = g3::mat4f::identity();
  g3::mat4f viewProj_ = g3::mat4f::identity();
  float focalPx_ = 1;
  g3::vec3f viewDir_ = {0, 0, -1};
  sim::Vec3 origin_ = {};  // world units of the render origin
  bool originValid_ = false;
  g3::vec3f sphereCenter_ = {};  // FU relative to the origin
  g3::vec3f camUnit_ = {};       // unit vector sphere center -> eye
  float cosHorizon_ = 0;
  float horizonAngle_ = 0;
  float cullCos_[MAX_SPHERE_LEVEL + 1] = {};
  int sphereShift_ = 0;        // level reduction used for the current frame
  int sphereExtra_ = 0;        // extra reduction kept between frames (budget)
  bool sphereDryRun_ = false;  // count edges instead of emitting them
  int sphereCount_ = 0;

  // Per-frame bookkeeping
  uint32_t edgeKeys_[2048];  // edge dedupe hash table
  int lineCount_ = 0, pointCount_ = 0, entitiesDrawn_ = 0, kites_ = 0;
  Gauge2D gauges_[MAX_GAUGES];
  int gaugeCount_ = 0;
  Marker2D markers_[MAX_MARKERS];
  int markerCount_ = 0;

  // Effects (float, render side only)
  Debris debris_[MAX_DEBRIS];
  int debrisCount_ = 0;
  Dust dust_[MAX_DUST];
  int dustCount_ = 0;
  static constexpr int MAX_PICKUPS = 4;
  Pickup pickups_[MAX_PICKUPS];
  int pickupCount_ = 0;
  uint32_t lastPickupTick_ = 0xFFFFFFFFu;
  float dustSpawnAcc_ = 0;
  uint32_t lastEffectTick_ = 0xFFFFFFFFu;
  float flash_[sim::MAX_ENTITIES] = {};  // seconds left of the white flash

  // Materials
  enum Palette : int {
    PAL_PLAYER = 0,     // teal, like the floating fragments
    PAL_ENEMY_BIG,      // bigger than the player: pink
    PAL_ENEMY_SMALL,    // not bigger than the player: light blue
    PAL_FRAGMENT,       // floating fragments (teal)
    PAL_CORE,           // white
    PAL_BULLET_PLAYER,  // additive orange
    PAL_BULLET_ENEMY,   // additive pink-red
    PAL_LASER_PLAYER,
    PAL_LINE,      // vertex colored lines and points (wireframe, stars, debris)
    PAL_LINE_ADD,  // vertex colored additive lines (dash dust)
    PAL_FRAGMENT_WHITE,  // floating fragments the player cannot eat (heal only)
    PAL_FLASH_RED,       // the player's hit flash
    PAL_UP_SHIELD,       // floating upgrades (additive solids)
    PAL_UP_OVERDRIVE,
    PAL_UP_THRUSTER,
    PAL_UP_CORE,
    PAL_LANCE,  // the Lance beam
    PAL_LANCE_CORE,
  };
  g3::Material palette_[PALETTE_SIZE];

  // renderer.cpp
  float frand();  // 0..1
  void updateCamera(float dt);
  g3::vec3f toLocal(const sim::Vec3 &worldUnits) const;
  bool project(const g3::vec3f &p, float &sx, float &sy) const;
  void putLine3(const g3::vec3f &a, const g3::vec3f &b, g2::Color ca,
                g2::Color cb, const g3::Material &m);
  void putLineLoop3(const g3::vec3f *pts, int n, g2::Color c,
                    const g3::Material &m);
  void putPoint3(const g3::vec3f &p, g2::Color c, const g3::Material &m);
  void putKite(const g3::vec3f &c, const g3::vec3f &dir, const g3::vec3f &perp,
               float s, float tipLen, const g3::Material &m);
  void putQuad(const g3::vec3f &c, const g3::vec3f &dir, const g3::vec3f &perp,
               float halfLen, float halfWidth, const g3::Material &m);
  void buildScene();
  void drawEntity(const sim::Entity &c, const g3::vec3f &pos, float px,
                  bool full, bool blink);
  void drawFloatingFragments();
  void drawBullets();
  void drawStars();
  void drawPresenceAuras();
  // upgrades.cpp
  void drawFloatingUpgrades();
  void drawLance();
  void putSolid(const g3::vec3f *verts, int nv, const uint16_t *idx, int ni,
                const g3::Material &m);
  const g3::Material &materialForEntity(const sim::Entity &c) const;
  g2::Color colorForEntity(const sim::Entity &c) const;

  // sphere.cpp
  void buildSphere();
  int countSphereLines(int shift, const int *order);
  void emitEdge(const g3::vec3f &a, const g3::vec3f &b, int level);

  // effects.cpp
  void shiftEffects(const g3::vec3f &delta);
  void spawnDebris(const g3::vec3f &pos, int count, float size,
                   g2::Color color);
  void collectEffects();
  void updateEffects(float dt);
  void drawEffects();

  // hud.cpp
  void drawHud(g2::Graphics2D &g, int offsetY);
  void drawUpgradeStatus(g2::Graphics2D &g, int offsetY);
  void drawCenteredText(g2::Graphics2D &g, int y, const char *text,
                        g2::Color color);
};

}  // namespace devoursphere::render

#endif
