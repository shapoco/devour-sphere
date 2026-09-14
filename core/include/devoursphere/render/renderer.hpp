#ifndef DEVOURSPHERE_RENDER_RENDERER_HPP
#define DEVOURSPHERE_RENDER_RENDERER_HPP

// Draws the game state with ShapoGFX. Platform independent: the platform
// layer supplies the target surface (a whole frame buffer or a band of it).
//
// Frame structure:
//   beginFrame()  computes the camera, the 2D line/point lists (planet
//                 wireframe, stars, energy particles) and builds the 3D scene
//                 (creatures, floating parts, bullets)
//   renderBand()  draws the rows [y, y + h) of the frame into a surface:
//                 background + lines + points, then the 3D scene, then the HUD
//   endFrame()
//
// The 3D renderer works in float, in "part units" (PU) relative to the
// player's position, so precision stays high anywhere on the planet.

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/game.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"
#include "shapoco/gfx3d/gfx3d.hpp"

namespace devoursphere::render {

namespace g2 = shapoco::gfx2d;
namespace g3 = shapoco::gfx3d;

struct Line2D {
  int16_t x0, y0, x1, y1;
  g2::Color color;
};

struct Point2D {
  int16_t x, y;
  uint8_t size;
  g2::Color color;
};

struct Camera {
  g3::vec3f eye, target, up;  // PU, relative to the player's position
  float fovY;                 // radians
};

struct RenderStats {
  int lines, points, creaturesDrawn, kites;
  g3::Stats gfx;
};

class Renderer {
 public:
  static constexpr int MAX_LINES = 3072;
  static constexpr int MAX_POINTS = 768;
  static constexpr int PALETTE_SIZE = 16;
  static constexpr int MAX_PLANET_LEVEL = 7;

  // arena: working memory of the 3D renderer (128 KB or more recommended)
  void init(int width, int height, void *arena, size_t arenaSize);

  // dt: seconds since the previous frame (camera smoothing only)
  void beginFrame(const sim::Game &game, float dt);
  // Draw the rows [y, y + h) of the frame into dst starting at row dstY
  void renderBand(const g2::Surface &dst, int y, int h, int dstY = 0);
  void endFrame();

  RenderStats stats() const;
  const Camera &camera() const { return cam_; }
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  int w_ = 0, h_ = 0;
  g3::Graphics3D g3d_;
  const sim::Game *game_ = nullptr;
  float time_ = 0;

  // Camera
  Camera cam_ = {};
  bool camValid_ = false;
  float camDist_ = 0, camHeight_ = 0, camFov_ = 0, camRoll_ = 0;
  g3::mat4f view_ = g3::mat4f::identity();
  g3::mat4f proj_ = g3::mat4f::identity();
  g3::mat4f viewProj_ = g3::mat4f::identity();
  float focalPx_ = 1;
  g3::vec3f viewDir_ = {0, 0, -1};
  sim::Vec3 origin_ = {};        // world units of the render origin
  g3::vec3f planetCenter_ = {};  // PU relative to the origin
  g3::vec3f camUnit_ = {};       // unit vector planet center -> eye
  float cosHorizon_ = 0;
  float horizonAngle_ = 0;
  float cullCos_[MAX_PLANET_LEVEL + 1] = {};

  // 2D lists
  Line2D lines_[MAX_LINES];
  int lineCount_ = 0;
  Point2D points_[MAX_POINTS];
  int pointCount_ = 0;
  uint32_t edgeKeys_[4096];  // edge dedupe hash table (per frame)
  int creaturesDrawn_ = 0, kites_ = 0;

  // Materials
  g3::Material palette_[PALETTE_SIZE];
  uint8_t hueToPalette_[256];

  // renderer.cpp
  void updateCamera(float dt);
  g3::vec3f toLocal(const sim::Vec3 &worldUnits) const;
  bool project(const g3::vec3f &p, float &sx, float &sy) const;
  void addLine(const g3::vec3f &a, const g3::vec3f &b, g2::Color c);
  void addPoint(const g3::vec3f &p, int size, g2::Color c);
  void buildScene();
  void putKite(const g3::vec3f &c, const g3::vec3f &dir, const g3::vec3f &perp,
               float s, float tipLen, const g3::Material &m);
  void putQuad(const g3::vec3f &c, const g3::vec3f &dir, const g3::vec3f &perp,
               float halfLen, float halfWidth, const g3::Material &m);
  void drawCreature(const sim::Creature &c, const g3::vec3f &pos, bool full,
                    const g3::Material &m, bool blink);
  void drawFloatingParts();
  void drawBullets();
  void drawParticles();
  const g3::Material &materialForCreature(const sim::Creature &c) const;

  // planet.cpp
  void buildPlanet();
  void buildStars();
  void subdivideFace(const g3::vec3f &a, const g3::vec3f &b, const g3::vec3f &c,
                     int level);
  void emitEdge(const g3::vec3f &a, const g3::vec3f &b, int level);

  // hud.cpp
  void drawHud(g2::Graphics2D &g, int offsetY);
  void drawCenteredText(g2::Graphics2D &g, int y, const char *text,
                        g2::Color color);
};

}  // namespace devoursphere::render

#endif
