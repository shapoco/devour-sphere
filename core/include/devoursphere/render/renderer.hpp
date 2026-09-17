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

#include "devoursphere/profile.hpp"
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
  uint8_t kind;     // 0 = enemy (triangle), else sim::UpgradeKind icon
  uint8_t outline;  // enemy: 0 = dark edge, else white edge brightness
};

// Icon of an upgrade kind, centered at (cx, cy), 14 px tall at scale8 = 8:
// its outline as a polygon (star-shaped around the center, up to 8 points;
// returns the point count, 0 for an unknown kind) and a filled drawing
int upgradeIconPolygon(int kind, int cx, int cy, g2::vec2i *pts,
                       int scale8 = 8);
void drawUpgradeIcon(g2::Graphics2D &g, int kind, int cx, int cy, g2::Color c,
                     int scale8 = 8);

// The screen size the HUD layout is written against; every distance below is
// that layout's pixel value scaled by UiMetrics::scale8
constexpr int UI_REF_W = 480, UI_REF_H = 320;

// How the HUD adapts to the frame buffer size (computed by init()). The
// layout is expressed in the reference screen's pixels and multiplied by
// `scale8` (in 1/8 units); text that still does not fit is shortened or
// dropped. Full-width overlays are placed at fractions of the height
// instead, so they follow the aspect ratio.
struct UiMetrics {
  int scale8 = 8;        // UI scale in 1/8 of the reference screen
  int fontMult = 1;      // integer magnification of the fonts
  bool compact = false;  // below 3/4 of the reference: smaller fonts
  bool tiny = false;     // below 1/2: minimal HUD
  int margin = 8;        // distance from the screen edge
  int gaugeW = 120;      // player health gauge
  int gaugeH = 8;
  int iconScale8 = 8;    // upgrade icons (they stop shrinking at 1/2)
  int markerScale8 = 8;  // horizon markers
  int wireLines = 1100;  // budget for the sphere wireframe
};

// The control hints on the title screen. Every platform has its own input
// device, so the strings come from the front end; the defaults describe a PC
// keyboard. Each line has a shorter alternative used when the screen is too
// narrow for the first one (and is dropped when neither fits). The platform
// owns the storage: pass string literals or something that outlives the
// renderer.
struct ControlHints {
  const char *move = "MOVE: ARROWS / WASD    A: SPACE / IJKL";
  const char *moveAlt = "ARROWS: MOVE   SPACE: FIRE";
  const char *dash = "UP: DASH   DOWN: BRAKE";
  const char *dashAlt = "UP / DOWN: DASH / BRAKE";
};

// What the HUD needs from the simulation, taken once per frame by
// beginFrame(). The HUD is drawn inside renderBand(), so without this a
// platform that advances the simulation on another core while the bands are
// rasterized would read the state as it changes underneath.
struct HudState {
  uint32_t tickCount = 0;
  uint32_t score = 0, highScore = 0;
  uint32_t events = 0;
  int32_t playerHp = 0, playerHpMax = 1;
  int stateTimer = 0;
  int sphereLevel = 1, spheresCleared = 0;
  int playerRank = 1, aliveEntities = 1;
  int selectedWeapon = 0, playerWeapon = 0;
  int cores = 0;
  int upgradeLevel[sim::UPGRADE_KINDS] = {};
  sim::GameState state = sim::GameState::TITLE;
};

// Font roles of the HUD (the fonts themselves live in hud.cpp)
enum class HudFont : uint8_t { SMALL, MEDIUM, LARGE, TITLE };

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

// Where beginFrame() spends its time (see devoursphere/profile.hpp). Slots:
//   0 camera   1 effects (collect + update)
//   2 stars and the sphere wireframe   3 entities (markers, sorting, bodies)
//   4 the rest of the scene (floating fragments, bullets, effects, overlays)
//   5 ShapoGFX beginRender (the depth sort)
// and where renderBand() spends its, summed over the bands of a frame:
//   6 the 3D rasterizer   7 the 2D work (clear, gauges, HUD)
enum FramePhase {
  FP_CAMERA = 0,
  FP_EFFECTS,
  FP_SPHERE,
  FP_ENTITIES,
  FP_SCENE_REST,
  FP_SORT,
  FP_BAND_3D,
  FP_BAND_2D,
  FP_COUNT
};

class Renderer {
 public:
  static constexpr int PALETTE_SIZE = 24;
  static constexpr int MAX_SPHERE_LEVEL = 7;
  static constexpr int MAX_GAUGES = 64;
  static constexpr int MAX_MARKERS = 40;
  static constexpr int MAX_ENEMY_MARKERS = 32;  // the rest is kept for upgrades
  static constexpr int MAX_DEBRIS = 64;
  static constexpr int MAX_DUST = 64;

  // arena: working memory of the 3D renderer. The triangle buffer it is
  // divided into is what binds: about 37 KB on a 32-bit target holds the
  // whole scene at 240x240 without the renderer thinning it, and the peak
  // frame only uses half of that. 64 KB is comfortable there; the WASM front
  // end keeps 256 KB because it has the room and draws bigger screens.
  //
  // spanCapacity: spans held per scanline, 0 for the ShapoGFX default (a
  // quarter of what is left after the fixed part, which is far more than
  // this scene needs -- see impl/xiamocon/SPEC.md). Whatever it saves goes
  // to the triangle buffer.
  void init(int width, int height, void *arena, size_t arenaSize,
            int spanCapacity = 0);

  // The control hints of the title screen (init() keeps them, so this can be
  // called once at start up whatever the frame buffer size does afterwards)
  void setControlHints(const ControlHints &hints) { hints_ = hints; }

  // Take the effects the simulation raised during the last tick. A platform
  // that runs several ticks per frame must call this after every tick: the
  // simulation only keeps the events of the current tick, so the ones it
  // catches up on would be lost otherwise. beginFrame() does the same for the
  // tick it is given, and both skip a tick that was already collected.
  void pollEffects(const sim::Game &game);

  // dt: seconds since the previous frame (camera smoothing and effects)
  void beginFrame(const sim::Game &game, float dt);
  // Draw the rows [y, y + h) of the frame into dst starting at row dstY
  void renderBand(const g2::Surface &dst, int y, int h, int dstY = 0);
  void endFrame();

  RenderStats stats() const;
  const PhaseTimer &frameProfile() const { return frameProfile_; }
  void resetFrameProfile() { frameProfile_.reset(); }
  const UiMetrics &uiMetrics() const { return ui_; }
  const Camera &camera() const { return cam_; }
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  int w_ = 0, h_ = 0;
  UiMetrics ui_;
  ControlHints hints_;  // set by the platform, kept across init()
  HudState hud_;        // snapshot for renderBand(), taken in beginFrame()
  // A distance of the reference layout in the pixels of this screen
  int ui(int refPx) const { return refPx * ui_.scale8 / 8; }
  // A vertical position of the reference layout as a fraction of the height
  // (full-width overlays follow the aspect ratio instead of the UI scale)
  int uiY(int refY) const { return refY * h_ / UI_REF_H; }
  int ui(int refPx, int minPx) const {
    int v = ui(refPx);
    return v < minPx ? minPx : v;
  }
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
  int sphereCount_ = 0;        // edges of the last pass (drives the level)
  bool sphereCountValid_ = false;

  // Per-frame bookkeeping
  // How deeply the mesh is subdivided along each of a face's three edges
  // ([0] = v0-v1, [1] = v1-v2, [2] = v2-v0), four bits each so that the
  // whole thing rides back from subdivideFace() in a register. A leaf
  // reports zero on all three.
  using EdgeDepths = uint16_t;
  static constexpr int edgeDepth(EdgeDepths e, int i) {
    return (e >> (i * 4)) & 0xF;
  }
  static constexpr EdgeDepths makeEdgeDepths(int e0, int e1, int e2) {
    return (EdgeDepths)((e0 & 0xF) | ((e1 & 0xF) << 4) | ((e2 & 0xF) << 8));
  }
  // Subdivision depth of each icosahedron edge (255: the two vertices are
  // not an edge of it), taken from the deeper of the two faces that share
  // it, and what each of the 20 faces reported
  uint8_t icoEdgeDepth_[12][12];
  EdgeDepths faceDepth_[20];
  int lineCount_ = 0, pointCount_ = 0, entitiesDrawn_ = 0, kites_ = 0;
  // The integer camera the backdrop is projected with (sphere.cpp):
  // derived from the float one at the end of updateCamera(). Vectors are
  // Q30; the eye is in 1/16 world units relative to the sphere center; the
  // focal length and the screen center are in 1/16 pixel.
  struct CameraQ {
    sim::Vec3 right, up, fwd;  // view basis
    sim::Vec3 unit;            // sphere center -> eye (camUnit_)
    sim::Vec3 player;          // sphere center -> player (for the LOD)
    sim::Vec3 eye;
    int32_t focal16, cx16, cy16;
    int32_t cosHorizon;
    int32_t cullCos[MAX_SPHERE_LEVEL + 1];
    int32_t cosLevel6[MAX_SPHERE_LEVEL + 1];  // per traversal (sphereConstants)
    int32_t cosLevel5[MAX_SPHERE_LEVEL + 1];
  };
  CameraQ camQ_ = {};
  bool projectQ(const sim::Vec3 &pos, int32_t &sx, int32_t &sy) const;
  // The stars' directions (Q30) and colors, fixed at init()
  sim::Vec3 starDir_[120];
  g2::Color starColor_[120];
  bool starsValid_ = false;
  // Visible entities of the frame, sorted by distance. A member rather than
  // a local: 256 of these is 3 KB, which is most of the 4 KB stack a core
  // gets on RP2350 (the stacks live in SCRATCH_X / SCRATCH_Y and cannot be
  // grown), and buildScene() would overflow it.
  struct Vis {
    float d, px;
    int16_t idx;
  };
  Vis vis_[sim::MAX_ENTITIES];

  // The backdrop -- the stars and the sphere wireframe -- is not put through
  // the 3D pipeline. Both layers carry no depth and everything in the world
  // is in front of them, so beginFrame() projects them to screen segments
  // and points, and renderBand() draws those straight into the band before
  // the 3D layers. What it saves is a vertex transform, a primitive setup
  // and a record for every line, and a span per scanline the line crosses:
  // on a Cortex-M0+ that was 240 us per line in beginFrame() and most of
  // the rasterization time.
  struct WireSeg {
    int16_t x0, y0, x1, y1;  // screen, 1/16 pixel, clipped to the screen
    uint8_t b0, b1;          // brightness at each end (wireBrightness())
  };
  static constexpr int MAX_WIRE = 1100;  // the largest UiMetrics::wireLines
  WireSeg wire_[MAX_WIRE];
  int wireCount_ = 0;
  struct StarPt {
    int16_t x, y;
    g2::Color c;
  };
  static constexpr int MAX_STARS = 120;
  StarPt stars_[MAX_STARS];
  int starCount_ = 0;
  // Native pixels of the wireframe ramp, 32 steps of brightness, built for
  // the format of the band being drawn
  uint16_t wireNative_[32] = {};
  g2::PixelFormat wireNativeFormat_ = g2::PixelFormat::GRAY1;
  bool wireNativeValid_ = false;
  void addWireSegment(const sim::Vec3 &ua, const sim::Vec3 &ub, int level);
  void drawBackdropBand(const g2::Surface &dst, int y, int h, int dstY);
  void drawWireSegment(const g2::Surface &dst, const WireSeg &s, int y, int h,
                       int dstY);

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
  // Score as shown on the HUD: it rolls up towards the real score (a fixed
  // fraction of the remaining gap per second, so it keeps pace with the
  // faster gains on later spheres) and snaps down when the score drops
  double scoreShown_ = 0;
  PhaseTimer frameProfile_;
  static constexpr float SCORE_ROLL_PER_SEC = 4.0f;    // fraction of the gap
  static constexpr float SCORE_ROLL_MIN_PER_SEC = 30;  // points
  void updateScoreDisplay(float dt);
  uint32_t scoreShown() const { return (uint32_t)scoreShown_; }

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
  // Horizon marker of an enemy beyond the horizon; `always`: shown whatever
  // its size and distance (carriers of upgrades)
  void addEnemyMarker(const sim::Entity &c, bool always);
  void drawHealthWarning();
  void drawMarkers();
  // Screen-space drawing inside the 3D scene: a plane facing the camera at
  // a given view-space depth, and the world position of a screen pixel on it
  struct ScreenPlane {
    g3::vec3f right, up;
    float tanX, tanY;
  };
  ScreenPlane screenPlane() const;
  g3::vec3f screenToWorld(const ScreenPlane &sp, float sx, float sy,
                          float depth) const;
  // upgrades.cpp
  void drawFloatingUpgrades();
  void putSolid(const g3::vec3f *verts, int nv, const uint16_t *idx, int ni,
                const g3::Material &m);
  const g3::Material &materialForEntity(const sim::Entity &c) const;
  g2::Color colorForEntity(const sim::Entity &c) const;

  // sphere.cpp
  void buildSphere();
  int countSphereLines(int shift, const int *order);
  // Subdivide one face and draw the lines separating its children; returns
  // how deeply the mesh ended up subdivided along the face's own edges. See
  // sphere.cpp for why that is all a face draws.
  EdgeDepths subdivideFace(const sim::Vec3 &a, const sim::Vec3 &b,
                           const sim::Vec3 &c, int level);
  void sphereConstants();
  int wantLevel(const sim::Vec3 &center, int level) const;
  // Draw a-b as 2^depth chords lying on the sphere
  void emitEdge(const sim::Vec3 &a, const sim::Vec3 &b, int depth, int level);
  void emitSplitEdge(const sim::Vec3 &a, const sim::Vec3 &b, int depth,
                     int level);
  void emitChord(const sim::Vec3 &a, const sim::Vec3 &b, int level);
  void traverseSphere(const int *order);
  void emitIcoEdges();

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
  void setHudFont(g2::Graphics2D &g, HudFont role) const;
  void drawCenteredText(g2::Graphics2D &g, int y, const char *text,
                        g2::Color color);
  // Centered text that is replaced by `alt` when it does not fit, and
  // dropped when `alt` does not fit either (or is null). Returns what was
  // drawn, 0 for nothing.
  const char *drawCenteredFit(g2::Graphics2D &g, int y, const char *text,
                              const char *alt, g2::Color color);
  int textFits(g2::Graphics2D &g, const char *text) const;
};

}  // namespace devoursphere::render

#endif
