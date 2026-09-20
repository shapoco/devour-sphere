// Camera, scene construction and band rendering.

#include <cmath>
#include <cstring>

#include "devoursphere/render/renderer.hpp"

#include "trig.hpp"

namespace devoursphere::render {

using g3::vec3f;
using sim::FU;

static constexpr float PI = 3.14159265358979f;
static constexpr float Z_NEAR = 0.4f;    // FU
static constexpr float Z_FAR = 1800.0f;  // FU
static constexpr float SPHERE_R = (float)sim::SPHERE_RADIUS / FU;
static constexpr float BRAD_TO_RAD = 2.0f * PI / 65536.0f;
// With this rank or better, markers of every bigger enemy are always shown
static constexpr int MARKER_ALWAYS_RANK = 5;

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

// How the HUD and the screen-space effects scale on this frame buffer. The
// UI scale is the smaller of the two axes against the reference screen, so
// nothing sticks out sideways on a narrow screen or vertically on a short
// one. The font magnification rounds it to an integer; icons and markers
// stop shrinking at half size (below that they turn into blobs).
static UiMetrics computeUi(int w, int h) {
  UiMetrics m;
  int sw = w * 8 / UI_REF_W, sh = h * 8 / UI_REF_H;
  m.scale8 = sw < sh ? sw : sh;
  if (m.scale8 < 2) m.scale8 = 2;
  if (m.scale8 > 32) m.scale8 = 32;
  m.tiny = m.scale8 < 4;
  m.compact = m.scale8 < 6;
  m.fontMult = (m.scale8 + 4) / 8;
  if (m.fontMult < 1) m.fontMult = 1;
  auto at = [&](int refPx, int minPx) {
    int v = refPx * m.scale8 / 8;
    return v < minPx ? minPx : v;
  };
  m.margin = at(8, 2);
  m.gaugeW = at(120, 24);
  m.gaugeH = at(8, 3);
  m.iconScale8 = m.scale8 < 4 ? 4 : m.scale8;
  m.markerScale8 = m.iconScale8;
  // The wireframe's line budget only ever goes down from the reference: a
  // small screen does not need 1100 lines, and the segment array (MAX_WIRE,
  // DEVOURSPHERE_MAX_WIRE) may be smaller than that on a small target
  m.wireLines = 1100 * m.scale8 / 8;
  if (m.wireLines > 1100) m.wireLines = 1100;
  if (m.wireLines < 220) m.wireLines = 220;
  if (m.wireLines > Renderer::MAX_WIRE) m.wireLines = Renderer::MAX_WIRE;
  return m;
}

void Renderer::init(int width, int height, void *arena, size_t arenaSize,
                    int spanCapacity) {
  w_ = width;
  h_ = height;
  ui_ = computeUi(width, height);
  g3::Config cfg =
      g3::defaultConfig((int16_t)width, (int16_t)height, arena, arenaSize);
  cfg.spanCapacity = spanCapacity;
  g3d_.init(cfg);
  g3d_.disableClear();

  palette_[PAL_PLAYER] = flatMaterial(hueColor(sim::FRAGMENT_HUE, 220, 230));
  palette_[PAL_ENEMY_BIG] = flatMaterial(g2::makeColor(255, 90, 170));
  palette_[PAL_ENEMY_SMALL] = flatMaterial(g2::makeColor(110, 200, 255));
  palette_[PAL_ENEMY_BOUNTY] = flatMaterial(g2::makeColor(255, 195, 60));
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
  palette_[PAL_FLASH_RED] = flatMaterial(g2::makeColor(255, 60, 60));
  // Solids: single sided (back faces culled) so that they do not add up
  // to a bright blob
  palette_[PAL_UP_SHIELD] = addMaterial(upgradeColor(1), 0.8f);
  palette_[PAL_UP_OVERDRIVE] = addMaterial(upgradeColor(2), 0.8f);
  palette_[PAL_UP_THRUSTER] = addMaterial(upgradeColor(3), 0.8f);
  palette_[PAL_UP_CORE] = addMaterial(upgradeColor(4), 0.8f);
  for (int i = PAL_UP_SHIELD; i <= PAL_UP_CORE; i++) {
    palette_[i].flags &= ~g3::MaterialFlags::DOUBLE_SIDED;
  }

  camValid_ = false;
  sphereCountValid_ = false;
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
// A float in [-1, 1] to Q30, saturating (1.0 is exactly representable)
static inline int32_t fToQ30(float v) {
  if (v >= 1.0f) return sim::Q30_ONE;
  if (v <= -1.0f) return -sim::Q30_ONE;
  return (int32_t)(v * (float)(1 << 30));
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

// Horizon margins of the integer pre-culls, Q30
static constexpr int32_t HORIZON_Q01 = (int32_t)(0.01f * (1 << 30));
static constexpr int32_t HORIZON_Q02 = (int32_t)(0.02f * (1 << 30));

// sin / cos from the simulation's table (1025 points, linear interpolation,
// error ~1e-6). libm's sinf / cosf are pure software on a core without an
// FPU and cost a few thousand cycles each; these cost a multiply and a
// lookup.

static vec3f rotateAroundAxis(const vec3f &v, const vec3f &axis, float angle) {
  float c = fastCos(angle), s = fastSin(angle);
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

// --- Integer geometry ------------------------------------------------------
// Visibility of a thing at `rel` (units from the eye): how far along the
// view direction it is, its distance, and whether it is at least a given
// size on the screen -- without a root or a division.
static inline int32_t dotUnits(const sim::Vec3 &v, const sim::Vec3 &q) {
  return (
      int32_t)(((int64_t)v.x * q.x + (int64_t)v.y * q.y + (int64_t)v.z * q.z) >>
               30);
}
static inline int64_t dist2Units(const sim::Vec3 &v) {
  constexpr int64_t MIN = (int64_t)(FU / 10) * (FU / 10);  // 0.1 FU floor
  const int64_t d2 =
      (int64_t)v.x * v.x + (int64_t)v.y * v.y + (int64_t)v.z * v.z;
  return d2 < MIN ? MIN : d2;
}
// s * focal / d >= px  <=>  (s * focal16)^2 >= (px16 * d)^2, with px16 the
// threshold in 1/16 pixels
static inline bool atLeastPx(int32_t sUnits, int64_t d2, int32_t focal16,
                             int32_t px16) {
  const int64_t sf = (int64_t)sUnits * focal16;
  return sf * sf >= d2 * px16 * px16;
}
// The body kites, the floating fragments and the bullets are built from the
// simulation's own integers -- positions in world units relative to the
// render origin, directions as Q30 unit vectors -- and handed to ShapoGFX as
// FixedVertex (16.16 FU, which is units << 8). On a core without an FPU
// this is what makes a kite cheap; the float path stays for the effects and
// the overlays.

static inline int32_t unitsToFix(int32_t units) {
  return units > 0x7FFFFF ? INT32_MAX
                          : (units < -0x7FFFFF ? INT32_MIN : units << 8);
}
static inline g3::FixedVertex fixedVertex(const sim::Vec3 &p, g2::Color c) {
  g3::FixedVertex v;
  v.position[0] = unitsToFix(p.x);
  v.position[1] = unitsToFix(p.y);
  v.position[2] = unitsToFix(p.z);
  v.normal[0] = 0;
  v.normal[1] = 32767;
  v.normal[2] = 0;
  v.uv[0] = v.uv[1] = 0;
  v.color = c;
  return v;
}
// Rotate the unit vector v around the unit axis by `brad` (Rodrigues, no
// renormalization: the inputs are unit vectors)
static inline sim::Vec3 rotQ30(const sim::Vec3 &v, const sim::Vec3 &axis,
                               uint16_t brad) {
  const int32_t c = sim::cosQ30(brad), s = sim::sinQ30(brad);
  const sim::Vec3 k = sim::crossQ30(axis, v);
  const int32_t d = sim::dotQ30(axis, v);
  return sim::scaleQ30(v, c) + sim::scaleQ30(k, s) +
         sim::scaleQ30(axis, sim::mulQ30(d, sim::Q30_ONE - c));
}

void Renderer::putKiteQ(const sim::Vec3 &c, const sim::Vec3 &dir,
                        const sim::Vec3 &perp, int32_t s, int32_t tipLen,
                        const g3::Material &m) {
  const sim::Vec3 dt = sim::scaleToLength(dir, tipLen);
  const sim::Vec3 ds = sim::scaleToLength(dir, s);
  const sim::Vec3 ps = sim::scaleToLength(perp, s);
  const g3::FixedVertex v[4] = {fixedVertex(c + dt, g3::VERTEX_WHITE),
                                fixedVertex(c + ps, g3::VERTEX_WHITE),
                                fixedVertex(c - ds, g3::VERTEX_WHITE),
                                fixedVertex(c - ps, g3::VERTEX_WHITE)};
  static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  g3::VertexBuffer vb = {4, nullptr, nullptr, v};
  g3::Primitive prim = {g3::PrimitiveType::TRIANGLES, &vb, 6, idx, &m};
  g3d_.putPrimitive(prim);
  kites_++;
}

void Renderer::putQuadQ(const sim::Vec3 &c, const sim::Vec3 &dir,
                        const sim::Vec3 &perp, int32_t halfLen,
                        int32_t halfWidth, const g3::Material &m) {
  const sim::Vec3 dl = sim::scaleToLength(dir, halfLen);
  const sim::Vec3 pw = sim::scaleToLength(perp, halfWidth);
  const g3::FixedVertex v[4] = {fixedVertex(c + dl + pw, g3::VERTEX_WHITE),
                                fixedVertex(c + dl - pw, g3::VERTEX_WHITE),
                                fixedVertex(c - dl - pw, g3::VERTEX_WHITE),
                                fixedVertex(c - dl + pw, g3::VERTEX_WHITE)};
  static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  g3::VertexBuffer vb = {4, nullptr, nullptr, v};
  g3::Primitive prim = {g3::PrimitiveType::TRIANGLES, &vb, 6, idx, &m};
  g3d_.putPrimitive(prim);
  kites_++;
}

void Renderer::putLineLoopQ(const sim::Vec3 *pts, int n, g2::Color c,
                            const g3::Material &m, Layer2D layer) {
  if (n < 2 || n > 8) return;
  if (lineCount2D_ + n <= MAX_LINES2D) {
    for (int i = 0; i < n; i++) {
      addLine2D(pts[i], pts[(i + 1) % n], c, 255, 255, layer);
    }
    return;
  }
  g3::FixedVertex v[8];
  uint16_t idx[8];
  for (int i = 0; i < n; i++) {
    v[i] = fixedVertex(pts[i], c);
    idx[i] = (uint16_t)i;
  }
  g3::VertexBuffer vb = {(uint16_t)n, nullptr, nullptr, v};
  g3::Primitive prim = {g3::PrimitiveType::LINE_LOOP, &vb, (uint16_t)n, idx,
                        &m};
  g3d_.putPrimitive(prim);
  lineCount_ += n;
}

// --- 2D lines and points (see renderer.hpp) --------------------------------

void Renderer::addLineScreen(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             g2::Color c, int b0, int b1, Layer2D layer) {
  if (lineCount2D_ >= MAX_LINES2D) return;
  if (!clipSegment16(x0, y0, x1, y1, b0, b1)) return;
  LineSeg &s = lines_[lineCount2D_++];
  s.x0 = (int16_t)x0;
  s.y0 = (int16_t)y0;
  s.x1 = (int16_t)x1;
  s.y1 = (int16_t)y1;
  s.b0 = (uint8_t)b0;
  s.b1 = (uint8_t)b1;
  s.layer = layer;
  s.rgb565 = packRgb565(c);
  lineCount_++;
}

bool Renderer::addLine2D(const sim::Vec3 &a, const sim::Vec3 &b, g2::Color c,
                         int b0, int b1, Layer2D layer) {
  if (lineCount2D_ >= MAX_LINES2D) return false;
  int32_t x0, y0, x1, y1;
  if (!projectUnitsQ(a, x0, y0) || !projectUnitsQ(b, x1, y1)) return true;
  addLineScreen(x0, y0, x1, y1, c, b0, b1, layer);
  return true;
}

bool Renderer::addLine2Df(const vec3f &a, const vec3f &b, g2::Color c, int b0,
                          int b1, Layer2D layer) {
  if (lineCount2D_ >= MAX_LINES2D) return false;
  const sim::Vec3 ua = {(int32_t)(a.x * FU), (int32_t)(a.y * FU),
                        (int32_t)(a.z * FU)};
  const sim::Vec3 ub = {(int32_t)(b.x * FU), (int32_t)(b.y * FU),
                        (int32_t)(b.z * FU)};
  return addLine2D(ua, ub, c, b0, b1, layer);
}

void Renderer::putLoop2Df(const vec3f *pts, int n, g2::Color c, Layer2D layer,
                          const g3::Material &fallback) {
  if (lineCount2D_ + n > MAX_LINES2D) {
    putLineLoop3(pts, n, c, fallback);
    return;
  }
  for (int i = 0; i < n; i++) {
    addLine2Df(pts[i], pts[(i + 1) % n], c, 255, 255, layer);
  }
}

bool Renderer::addPoint2D(const sim::Vec3 &p, g2::Color c) {
  if (pointCount2D_ >= MAX_POINTS2D) return false;
  int32_t sx, sy;
  if (!projectUnitsQ(p, sx, sy)) return true;
  const int px = sx >> 4, py = sy >> 4;
  if (px < 0 || px >= w_ || py < 0 || py >= h_) return true;
  StarPt &pt = points_[pointCount2D_++];
  pt.x = (int16_t)px;
  pt.y = (int16_t)py;
  pt.c = c;
  pointCount_++;
  return true;
}

// A filled triangle in screen pixels (frame y; oy maps it into the band)
// A filled triangle as one span per row (added onto the frame when
// `additive`, else overwriting)
static void fillTriangle2D(g2::Graphics2D &g, int oy, int ax, int ay, int bx,
                           int by, int cx, int cy, g2::Color c,
                           bool additive) {
  auto span = [&](int lo, int y, int n) {
    if (additive) {
      g.fillRect(lo, y, n, 1, c, g2::BlendMode::ADD);
    } else {
      g.fillRect(lo, y, n, 1, c);
    }
  };
  // Sort by y: a top, c bottom
  if (ay > by) {
    int t = ax;
    ax = bx;
    bx = t;
    t = ay;
    ay = by;
    by = t;
  }
  if (by > cy) {
    int t = bx;
    bx = cx;
    cx = t;
    t = by;
    by = cy;
    cy = t;
  }
  if (ay > by) {
    int t = ax;
    ax = bx;
    bx = t;
    t = ay;
    ay = by;
    by = t;
  }
  if (cy == ay) {
    int lo = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int hi = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    span(lo, ay + oy, hi - lo + 1);
    return;
  }
  for (int y = ay; y <= cy; y++) {
    // The long edge a-c and the short edge (a-b, then b-c), x in 16.16
    const int xl = ax + (int)(((int64_t)(cx - ax) * (y - ay)) / (cy - ay));
    int xs;
    if (y < by) {
      xs = by == ay ? bx
                    : ax + (int)(((int64_t)(bx - ax) * (y - ay)) / (by - ay));
    } else {
      xs = cy == by ? cx
                    : bx + (int)(((int64_t)(cx - bx) * (y - by)) / (cy - by));
    }
    const int lo = xl < xs ? xl : xs, hi = xl < xs ? xs : xl;
    span(lo, y + oy, hi - lo + 1);
  }
}

void Renderer::drawMarkers2D(g2::Graphics2D &g, int oy) {
  // Additive like the auras and the pickups unless blending is suppressed
  const bool additive = !DEVOURSPHERE_SUPPRESS_ALPHA;
  for (int i = 0; i < markerCount_; i++) {
    const Marker2D &mk = markers_[i];
    const int ms = ui_.markerScale8;
    g2::vec2i pts[8];
    int n = 0;
    if (mk.kind != 0) {
      n = upgradeIconPolygon(mk.kind, mk.x, mk.y - 10 * ms / 8, pts, ms);
    } else {
      int yb = mk.y - 3 * ms / 8, yt = yb - 7 * ms / 8;
      int hw = 5 * ms / 8;
      if (hw < 2) hw = 2;
      pts[0] = {mk.x - hw, yt};
      pts[1] = {mk.x + hw, yt};
      pts[2] = {mk.x, yb};
      n = 3;
    }
    if (n < 3) continue;
    if (n == 3) {
      fillTriangle2D(g, oy, pts[0].x, pts[0].y, pts[1].x, pts[1].y, pts[2].x,
                     pts[2].y, mk.color, additive);
      continue;
    }
    // Fan from the center (every icon is star-shaped around it)
    int cx = 0, cy = 0;
    for (int k = 0; k < n; k++) cx += pts[k].x, cy += pts[k].y;
    cx /= n, cy /= n;
    for (int k = 0; k < n; k++) {
      const g2::vec2i &p = pts[k], &q = pts[(k + 1) % n];
      fillTriangle2D(g, oy, cx, cy, p.x, p.y, q.x, q.y, mk.color, additive);
    }
  }
}

// The white outline of a carrier's marker, as 2D lines over everything
void Renderer::queueMarkerOutlines() {
  for (int i = 0; i < markerCount_; i++) {
    const Marker2D &mk = markers_[i];
    if (mk.kind != 0 || !mk.outline) continue;
    const int ms = ui_.markerScale8;
    const int yb = mk.y - 3 * ms / 8, yt = yb - 7 * ms / 8;
    const int hw = 5 * ms / 8 + 1;
    const g2::Color c = g2::makeColor(mk.outline, mk.outline, mk.outline);
    const int32_t x0 = (mk.x - hw) << 4, x1 = (mk.x + hw) << 4;
    const int32_t y0 = (yt - 1) << 4, y1 = (yb + 1) << 4, xm = mk.x << 4;
    addLineScreen(x0, y0, x1, y0, c, 255, 255, L2D_OVER);
    addLineScreen(x1, y0, xm, y1, c, 255, 255, L2D_OVER);
    addLineScreen(xm, y1, x0, y0, c, 255, 255, L2D_OVER);
  }
}

void Renderer::putPointQ(const sim::Vec3 &p, g2::Color c,
                         const g3::Material &m) {
  if (addPoint2D(p, c)) return;
  const g3::FixedVertex v[1] = {fixedVertex(p, c)};
  static const uint16_t idx[1] = {0};
  g3::VertexBuffer vb = {1, nullptr, nullptr, v};
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
  // A sphere switch moves the origin to another world (the player flew
  // straight on, see Game::switchSphere()): the effects stay put then
  const bool newSphere = g.sphereSeed() != sphereSeedSeen_;
  if (originValid_ && !newSphere) {
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
  float wantOrbit = 0;
  float wantAhead = bodyR * 0.5f + 1.0f;  // look target ahead of the player
  float wantDown = 0.12f;                 // ... and below (x camera height)
  sim::GameState st = g.state();
  inFlight_ = st == sim::GameState::LAUNCH || st == sim::GameState::ARRIVE;
  altExcess_ = (p.r - sim::SPHERE_RADIUS - sim::ALTITUDE) * (1.0f / FU);
  if (altExcess_ < 0) altExcess_ = 0;
  wireFadeOffset_ = (int32_t)(altExcess_ * (FU * 16));
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
  } else if (inFlight_) {
    // The flight between spheres. The camera circles the player from
    // behind to the front during the launch (the sphere left behind comes
    // into view beyond the player) and from the front back to behind
    // during the arrival (the next sphere ahead), low over the flight path;
    // the last part of the descent brings it back up to the play angle.
    // The sphere LOD keeps the player's own nominal distance.
    auto smooth01 = [](float u) {
      if (u <= 0) return 0.0f;
      if (u >= 1) return 1.0f;
      return u * u * (3 - 2 * u);
    };
    float orbit, low;  // 0..PI, 0 = behind; 0..1 flight elevation
    if (st == sim::GameState::LAUNCH) {
      const float t0 = 0.3f * sim::LAUNCH_TICKS;  // lift off first
      float u = smooth01((g.stateTimer() - t0) / (sim::LAUNCH_TICKS - t0));
      orbit = PI * u;
      low = u;
    } else {
      // On round the other side: beside the player exactly at the switch
      // tick, then behind it, so that the launch and the arrival together
      // circle the player once
      const float t0 = 2.0f * sim::ARRIVE_SWITCH_TICKS;
      float u = smooth01(g.stateTimer() / t0);
      orbit = PI * (1 + u);
      // Once behind, rise a little to see the body from above during the
      // dive, then up to the play angle for the landing
      const float t1 = sim::ARRIVE_TICKS - 1.5f * sim::TICK_RATE;
      low = 1 - 0.55f * smooth01((g.stateTimer() - t0) / (t1 - t0)) -
            0.45f * smooth01((g.stateTimer() - t1) / (sim::ARRIVE_TICKS - t1));
    }
    // A little farther than in play. Before the sphere switch rescales
    // the player, farther still: the distance at which the body looks as
    // big on screen as the rescaled body will at its own flight distance
    // (the constant 8 FU makes a small body smaller on screen than a large
    // one), so that the switch does not change the size of the body on
    // screen. The pull-back happens during the orbit of the launch.
    float flightDist = wantDist * 1.25f;
    if (st == sim::GameState::LAUNCH || g.switchPending()) {
      const uint32_t sizeAfter = g.playerSizeAfterSwitch();
      if (sizeAfter < p.size) {
        float bodyRAfter = sim::fragmentHalfSize(sim::log2Floor(sizeAfter)) /
                           (float)FU * (2.0f + 0.2f * p.fragmentCount);
        float distAfter = 3.5f * bodyRAfter + 8.0f;
        if (distAfter < 11.0f) distAfter = 11.0f;
        flightDist = bodyR * (distAfter * 1.25f / bodyRAfter);
      }
    }
    wantDist += (flightDist - wantDist) * low;
    wantHeight = wantDist * (1.0f - 0.65f * low);
    wantAhead *= 1 - low;
    wantDown *= 1 - low;
    wantOrbit = orbit;
  } else if (st == sim::GameState::TITLE ||
             st == sim::GameState::WEAPON_SELECT) {
    // Cinematic orbit around the attract-mode player
    wantDist *= 2.2f;
    nominalDist = wantDist;
    wantHeight *= 1.6f;
    fwd = rotateAroundAxis(fwd, up, time_ * 0.25f);
  } else if (st == sim::GameState::DEAD || !p.alive) {
    // Follow the cruising ghost of the player, a little farther back
    wantDist *= 1.3f;
    nominalDist = wantDist;
    wantHeight *= 1.2f;
  }

  // Pitch along the flight path: the radial movement against the speed
  // along the surface (both units per tick). Zero on the surface.
  float wantPitch = 0;
  if (g.playerClimb() != 0) {
    wantPitch = std::atan2((float)g.playerClimb(), (float)p.speed);
  }

  if (!camValid_ || newSphere) {
    // A new sphere: the player has been rescaled, so the distances snap
    // to its new size (the sphere is off screen at that moment and the
    // player keeps its size on screen). The pitch snaps too: the frame
    // was turned so that the new pitch gives the old flight direction,
    // and easing from the old pitch would swing the body round
    sphereSeedSeen_ = g.sphereSeed();
    sphereCountValid_ = false;  // the level is searched afresh (see sphere.cpp)
    camDist_ = wantDist;
    camNominal_ = nominalDist;
    camHeight_ = wantHeight;
    camAhead_ = wantAhead;
    camDown_ = wantDown;
    if (!camValid_) {
      camFov_ = wantFov;
      camRoll_ = wantRoll;
    }
    camPitch_ = wantPitch;
    if (!camValid_ || (st == sim::GameState::ARRIVE &&
                       g.stateTimer() < sim::ARRIVE_SWITCH_TICKS)) {
      // ... and the first sphere of a game starts with the camera in front
      // of the player straight away (the menus do not use the orbit)
      camOrbit_ = wantOrbit;
    }
    camValid_ = true;
  } else {
    // The camera eases more slowly after death
    float rate = (st == sim::GameState::DEAD || !p.alive) ? 1.5f : 5.0f;
    float k = 1.0f - std::exp(-dt * rate);
    camDist_ += (wantDist - camDist_) * k;
    camNominal_ += (nominalDist - camNominal_) * k;
    camHeight_ += (wantHeight - camHeight_) * k;
    camFov_ += (wantFov - camFov_) * k;
    camRoll_ += (wantRoll - camRoll_) * k;
    camAhead_ += (wantAhead - camAhead_) * k;
    camDown_ += (wantDown - camDown_) * k;
    // The orbit follows its (already smoothed) script closely; the pitch
    // is what makes the player nose over when the sphere is switched
    // (the orbit is an angle: ease along the shorter way round, and keep
    // it wrapped so that coming full circle ends at zero)
    auto wrapAngle = [](float a) {
      while (a > PI) a -= 2 * PI;
      while (a <= -PI) a += 2 * PI;
      return a;
    };
    float kOrbit = 1.0f - std::exp(-dt * 8.0f);
    camOrbit_ =
        wrapAngle(camOrbit_ + wrapAngle(wantOrbit - camOrbit_) * kOrbit);
    float kPitch = 1.0f - std::exp(-dt * 4.0f);
    camPitch_ += (wantPitch - camPitch_) * kPitch;
    if (std::fabs(camOrbit_) < 1e-4f) camOrbit_ = 0;
    if (std::fabs(camPitch_) < 1e-4f) camPitch_ = 0;
  }

  // The frame pitched along the flight path (identical to the frame on the
  // surface), for the camera, the player's body and the dash dust. The
  // flight path is continuous through a sphere switch (the frame is turned
  // under it), so this basis is too.
  vec3f fwdP = fwd, upP = up;
  if (camPitch_ != 0) {
    const vec3f right = g3::normalize(g3::cross(fwd, up));
    fwdP = rotateAroundAxis(fwd, right, camPitch_);
    upP = rotateAroundAxis(up, right, camPitch_);
  }
  flightFwd_ = fwdP;
  flightUp_ = upP;
  playerPitchBrad_ = (uint16_t)(int32_t)(camPitch_ * (65536.0f / (2 * PI)));
  // ... and circled by the camera
  vec3f fwdO = camOrbit_ != 0 ? rotateAroundAxis(fwdP, upP, camOrbit_) : fwdP;

  vec3f eye = fwdO * (-camDist_) + upP * camHeight_;
  // Look at a point ahead of (and normally slightly below) the player so
  // that the sphere surface fills the lower part of the screen
  vec3f target = fwdP * camAhead_ - upP * (camHeight_ * camDown_);
  vec3f dir = g3::normalize(target - eye);
  vec3f upR = rotateAroundAxis(upP, dir, camRoll_);
  cam_.eye = eye;
  cam_.target = target;
  cam_.up = upR;
  cam_.fovY = camFov_;
  eyeQ_ = {(int32_t)(eye.x * FU), (int32_t)(eye.y * FU), (int32_t)(eye.z * FU)};

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

  // The integer camera of the backdrop (sphere.cpp). The view basis is the
  // rows of the lookAt matrix; forward is what the matrix negates.
  auto toQ = [](const vec3f &v) {
    return sim::Vec3{fToQ30(v.x), fToQ30(v.y), fToQ30(v.z)};
  };
  camQ_.right = toQ({view_.m[0], view_.m[4], view_.m[8]});
  camQ_.up = toQ({view_.m[1], view_.m[5], view_.m[9]});
  camQ_.fwd = toQ({-view_.m[2], -view_.m[6], -view_.m[10]});
  camQ_.unit = toQ(camUnit_);
  camQ_.player = toQ(g3::normalize(sphereCenter_ * -1.0f));
  constexpr float POS = (float)FU * 16.0f;  // FU -> 1/16 units
  camQ_.eye = {(int32_t)(rel.x * POS), (int32_t)(rel.y * POS),
               (int32_t)(rel.z * POS)};
  camQ_.focal16 = (int32_t)(focalPx_ * 16.0f);
  camQ_.cx16 = w_ * 8;  // (w / 2) * 16
  camQ_.cy16 = h_ * 8;
  camQ_.cosHorizon = fToQ30(cosHorizon_);
  for (int l = 0; l <= MAX_SPHERE_LEVEL; l++) {
    camQ_.cullCos[l] = cullCos_[l] <= -1.0f ? INT32_MIN : fToQ30(cullCos_[l]);
  }
}

// ---------------------------------------------------------------------------
// Scene

const g3::Material &Renderer::materialForEntity(const sim::Entity &c) const {
  if (c.isPlayer) return palette_[PAL_PLAYER];
  if (c.bounty) return palette_[PAL_ENEMY_BOUNTY];  // the ones to kill
  const sim::Entity &p = game_->player();
  return palette_[sim::effectiveSizeQ8(c) > sim::effectiveSizeQ8(p)
                      ? PAL_ENEMY_BIG
                      : PAL_ENEMY_SMALL];
}

g2::Color Renderer::colorForEntity(const sim::Entity &c) const {
  const g3::Material &m = materialForEntity(c);
  return g2::makeColorF(m.diffuse.r, m.diffuse.g, m.diffuse.b);
}

// px: projected body radius in pixels; full: draw every fragment (otherwise
// an outline of the body)
void Renderer::drawEntity(const sim::Entity &c, const sim::Vec3 &pos, float px,
                          bool full, bool blink) {
  using sim::Vec3;
  Vec3 up = c.frame.n;
  Vec3 fwd = c.frame.t;
  Vec3 right = sim::crossQ30(fwd, up);
  // In flight the player is pitched along its flight path (nose up when
  // climbing): a rotation around its right axis, which stays as it is
  if (c.isPlayer && playerPitchBrad_ != 0) {
    fwd = rotQ30(fwd, right, playerPitchBrad_);
    up = rotQ30(up, right, playerPitchBrad_);
  }
  const int32_t coreY = c.coreY, coreHalf = c.coreHalf;
  const int32_t focusY = c.layoutFocusY;
  const g3::Material &m = materialForEntity(c);

  // Bank into the turn: roll the body around the heading
  if (c.bank != 0) {
    right = rotQ30(right, fwd, (uint16_t)c.bank);
    up = rotQ30(up, fwd, (uint16_t)c.bank);
  }
  // Hit flash: the whole body turns white for a moment
  int idx = (int)(&c - game_->entities);
  bool flashing = idx >= 0 && idx < sim::MAX_ENTITIES && flash_[idx] > 0;
  // (the player flashes red, everyone else white)
  const g3::Material &bodyMat =
      flashing ? palette_[c.isPlayer ? PAL_FLASH_RED : PAL_CORE] : m;

  if (blink) {
    entitiesDrawn_++;
    return;
  }
  // A carrier of an upgrade is outlined in white (the bounty holders are
  // told apart by their body color instead)
  bool carrier = !c.isPlayer && c.upgrade != (uint8_t)sim::UpgradeKind::NONE;
  const g2::Color outline = g2::makeColor(255, 255, 255);
  if (full) {
    // Dihedral: fragments tilt outwards (around the heading) the farther
    // they are from the body's axis, so the body looks like it has volume
    constexpr uint16_t TILT_MAX_BRAD = sim::degToBrad(35);  // TILT_MAX
    int32_t bodyR = c.bodyRadius * 7 / 5;
    if (bodyR < FU / 10) bodyR = FU / 10;
    for (int i = 0; i < c.fragmentCount; i++) {
      const sim::Fragment &p = c.fragments[i];
      const int32_t s = sim::fragmentHalfSize(p.sizeLog2);
      const int32_t lx = p.x * 7 / 5, ly = p.y;  // widen like wings
      for (int side = -1; side <= 1; side += 2) {
        const int32_t x = lx * side;
        int32_t ax = x < 0 ? -x : x;
        if (ax > bodyR) ax = bodyR;
        // rotating +right around fwd by a positive angle moves it towards
        // -up: the outer edge of each wing droops (anhedral)
        const int32_t tilt =
            side * (int32_t)((int64_t)ax * TILT_MAX_BRAD / bodyR);
        const Vec3 rightT = rotQ30(right, fwd, (uint16_t)tilt);
        int32_t dx = x, dy = ly - focusY;
        if (dx == 0 && dy == 0) dy = -1;
        const Vec3 u = sim::normalizeQ30({dx, dy, 0});  // (dx, dy) unit, Q30
        const Vec3 dir = sim::scaleQ30(rightT, u.x) + sim::scaleQ30(fwd, u.y);
        const Vec3 perp = sim::scaleQ30(rightT, -u.y) + sim::scaleQ30(fwd, u.x);
        const Vec3 kc =
            pos + sim::scaleToLength(right, x) + sim::scaleToLength(fwd, ly);
        putKiteQ(kc, dir, perp, s, s * 5 / 2, bodyMat);
        if (carrier) {
          // A slightly brighter outline shows that this enemy carries an
          // upgrade (only visible up close)
          const Vec3 pts[4] = {kc + sim::scaleToLength(dir, s * 5 / 2),
                               kc + sim::scaleToLength(perp, s),
                               kc - sim::scaleToLength(dir, s),
                               kc - sim::scaleToLength(perp, s)};
          putLineLoopQ(pts, 4, outline, palette_[PAL_LINE], L2D_OVER);
        }
      }
    }
    // Core (white, pointing forward)
    putKiteQ(pos + sim::scaleToLength(fwd, coreY), fwd, right, coreHalf,
             coreHalf * 5 / 2, palette_[PAL_CORE]);
  } else {
    // Far away: the outline of a body-sized kite (lines stay visible even
    // edge-on), plus the core when it is more than a few pixels. A carrier
    // of an upgrade is drawn in white so it stands out from afar
    const int32_t s = c.bodyRadius / 2;
    const Vec3 center = pos + sim::scaleToLength(fwd, coreY - s);
    const Vec3 pts[4] = {center - sim::scaleToLength(fwd, s * 2),
                         center + sim::scaleToLength(right, s),
                         center + sim::scaleToLength(fwd, s),
                         center - sim::scaleToLength(right, s)};
    putLineLoopQ(pts, 4, carrier ? outline : colorForEntity(c),
                 palette_[PAL_LINE], L2D_UNDER);
    if (px >= 4.0f) {
      putKiteQ(pos + sim::scaleToLength(fwd, coreY), fwd, right, coreHalf,
               coreHalf * 5 / 2, palette_[PAL_CORE]);
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
    // Horizon test in integer, before anything is converted to float
    if (sim::dotQ30(fp.n, camQ_.unit) < camQ_.cosHorizon - HORIZON_Q01)
      continue;
    const sim::Vec3 posQ = sim::scaleToLength(fp.n, fp.r) - origin_;
    const sim::Vec3 rel = posQ - eyeQ_;
    if (dotUnits(rel, camQ_.fwd) < -2 * FU) continue;
    const int32_t sUnits = sim::fragmentHalfSize(fp.sizeLog2);
    if (!atLeastPx(sUnits, dist2Units(rel), camQ_.focal16, 16)) {  // < 1 px
      putPointQ(posQ, edible ? pointColor : whitePoint, palette_[PAL_LINE]);
      continue;
    }
    // Tangent frame spun by the fragment's own angle, tilted a little
    // towards the camera like the entities. All in Q30.
    using sim::Vec3;
    Vec3 up = fp.n;
    const Vec3 toCam = sim::normalizeQ30(eyeQ_ - posQ);
    int32_t elev = sim::dotQ30(toCam, up);
    if (elev < 0) elev = -elev;
    constexpr int32_t ELEV0 = (int32_t)(0.55 * (1 << 30));
    constexpr int32_t K_MAX = (int32_t)(0.8 * (1 << 30));
    constexpr int32_t K_PER = (int32_t)(0.8 / 0.55 * (1 << 30));  // 0.8 / 0.55
    if (elev < ELEV0) {
      int32_t k = sim::mulQ30(ELEV0 - elev, K_PER);
      if (k > K_MAX) k = K_MAX;
      up = sim::normalizeQ30(up + sim::scaleQ30(toCam, k));
    }
    const int32_t ux = up.x < 0 ? -up.x : up.x;
    const Vec3 helper = ux < (int32_t)(0.9 * (1 << 30))
                            ? Vec3{sim::Q30_ONE, 0, 0}
                            : Vec3{0, sim::Q30_ONE, 0};
    const Vec3 a = sim::normalizeQ30(sim::crossQ30(up, helper));
    const Vec3 b = sim::crossQ30(up, a);
    const Vec3 dir = sim::scaleQ30(a, sim::cosQ30(fp.spin)) +
                     sim::scaleQ30(b, sim::sinQ30(fp.spin));
    const Vec3 perp = sim::crossQ30(up, dir);
    putKiteQ(posQ, dir, perp, sUnits, sUnits * 5 / 2, m);
  }
}

void Renderer::drawBullets() {
  const sim::Game &g = *game_;
  for (int i = 0; i < sim::MAX_BULLETS; i++) {
    const sim::Bullet &b = g.bullets[i];
    if (!b.alive) continue;
    if (sim::dotQ30(b.frame.n, camQ_.unit) < camQ_.cosHorizon - HORIZON_Q01)
      continue;
    const sim::Vec3 posQ = sim::scaleToLength(b.frame.n, b.r) - origin_;
    if (dotUnits(posQ - eyeQ_, camQ_.fwd) < -2 * FU) continue;
    const sim::Vec3 fwd = b.frame.t;
    const sim::Vec3 right = sim::crossQ30(fwd, b.frame.n);
    const int32_t base = sim::fragmentHalfSize(sim::log2Floor(b.ownerSize));
    switch (b.kind) {
      case sim::Weapon::VULCAN: {
        const g3::Material &m =
            palette_[b.fromPlayer ? PAL_BULLET_PLAYER : PAL_BULLET_ENEMY];
        const int32_t s = base * 3 / 10 + FU * 8 / 100;  // 0.3 base + 0.08 FU
        putKiteQ(posQ, fwd, right, s, s * 11 / 5, m);
        break;
      }
      case sim::Weapon::LASER: {
        const g3::Material &m =
            palette_[b.fromPlayer ? PAL_LASER_PLAYER : PAL_BULLET_ENEMY];
        // Long beam trailing behind the bullet's head, never longer than
        // the distance flown so far (so it grows out of the muzzle instead
        // of appearing behind the shooter)
        const sim::WeaponSpec &ws = sim::WEAPON_SPECS[(int)b.kind];
        const int32_t flown = (int32_t)(ws.lifetime - b.life) * b.speed;
        int32_t len = base * 24 + 8 * FU;
        if (len > flown) len = flown;
        if (len < FU / 2) len = FU / 2;
        putQuadQ(posQ - sim::scaleToLength(fwd, len / 2), fwd, right, len / 2,
                 base * 12 / 100 + FU * 6 / 100, m);
        break;
      }
      case sim::Weapon::MISSILE: {
        const g3::Material &m =
            palette_[b.fromPlayer ? PAL_BULLET_PLAYER : PAL_BULLET_ENEMY];
        const int32_t s = base * 9 / 20 + FU / 10;
        putKiteQ(posQ, fwd, right, s, s * 5 / 2, m);
        break;
      }
    }
  }
}

// murmur3 finalizer: a good 32-bit mixer
static inline uint32_t mix32(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

void Renderer::drawStars() {
  constexpr int STARS = 120;
  constexpr int32_t DIST = 1500 * FU * 16;  // 1/16 units, inside the far plane
  if (!starsValid_) {
    // Fixed directions and colors: the float work happens once
    for (int i = 0; i < STARS; i++) {
      // Well mixed hashes (a plain multiply of consecutive indices leaves a
      // visible lattice in the sky)
      uint32_t h = mix32((uint32_t)i * 0x9E3779B9u + 0x1234567u);
      uint32_t h2 = mix32(h ^ 0xA5A5A5A5u);
      uint32_t h3 = mix32(h2 + 0x3C6EF372u);
      float z = ((h & 0xFFFF) / 32768.0f) - 1.0f;  // -1..1
      float phi = ((h2 & 0xFFFF) / 65536.0f) * 6.2831853f;
      h2 = h3;
      float r = std::sqrt(1.0f - z * z);
      starDir_[i] = {fToQ30(r * std::cos(phi)), fToQ30(r * std::sin(phi)),
                     fToQ30(z)};
      int v8 = 90 + (int)((h2 >> 8) % 120);
      starColor_[i] = g2::makeColor(v8, v8, v8 + 20 > 255 ? 255 : v8 + 20);
    }
    starsValid_ = true;
  }
  constexpr int32_t VIEW_MIN = (int32_t)(0.3f * (1 << 30));
  constexpr int32_t LIMB_MARGIN = (int32_t)(0.05f * (1 << 30));
  for (int i = 0; i < STARS; i++) {
    const sim::Vec3 &dir = starDir_[i];
    // Only stars in front of the camera and above the sphere's limb
    if (sim::dotQ30(dir, camQ_.fwd) < VIEW_MIN) continue;
    if (sim::dotQ30(dir, camQ_.unit) < -camQ_.cosHorizon + LIMB_MARGIN)
      continue;
    // Projected here, drawn as a pixel by drawBackdropBand()
    const sim::Vec3 pos = {camQ_.eye.x + sim::mulQ30(dir.x, DIST),
                           camQ_.eye.y + sim::mulQ30(dir.y, DIST),
                           camQ_.eye.z + sim::mulQ30(dir.z, DIST)};
    int32_t sx, sy;
    if (starCount_ < MAX_STARS && projectQ(pos, sx, sy)) {
      const int x = sx >> 4, y = sy >> 4;
      if (x >= 0 && x < w_ && y >= 0 && y < h_) {
        stars_[starCount_++] = {(int16_t)x, (int16_t)y, starColor_[i]};
        pointCount_++;
      }
    }
  }
}

// Presence auras: enemies close to the player but outside the screen are
// shown as a soft glow at the screen edge in their direction (a fan whose
// color fades from the enemy's color at the center to black at the rim,
// added onto the frame). Nearer enemies get bigger and brighter auras.
//
// With DEVOURSPHERE_SUPPRESS_ALPHA the glow is a solid triangle in the
// enemy's color instead, its tip on the screen edge pointing at the enemy,
// bigger the nearer the enemy. A fan is thirteen vertices, twelve
// Gouraud-shaded triangles and a few thousand additively blended pixels --
// about a millisecond each on a Cortex-M0+, and up to twelve of them in a
// crowd; the triangle is one flat, opaque primitive.
void Renderer::drawPresenceAuras() {
  const sim::Game &g = *game_;
  if (g.state() != sim::GameState::PLAYING) return;
  constexpr float RANGE = 100.0f;  // FU
  constexpr int SEGMENTS = 12;
  constexpr int MAX_AURAS = 12;
  constexpr float DEPTH = 2.0f;  // view-space distance of the fan
  vec3f camUp =
      std::fabs(g3::dot(viewDir_, cam_.up)) < 0.99f ? cam_.up : vec3f{0, 0, 1};
  vec3f right = g3::normalize(g3::cross(viewDir_, camUp));
  vec3f up = g3::cross(right, viewDir_);
  float tanY = std::tan(camFov_ * 0.5f);
  float tanX = tanY * (float)w_ / (float)h_;
  // Within RANGE of the player: an angle of 2 asin(RANGE / 2 / R) = 0.196
  // around the sphere; tested in Q30 (with a margin) before any float, as
  // most of the two hundred entities are far beyond it
  constexpr int32_t COS_RANGE = (int32_t)(0.98007 * (1 << 30));  // cos 0.2
  int drawn = 0;
  for (int i = 0; i < sim::MAX_ENTITIES && drawn < MAX_AURAS; i++) {
    const sim::Entity &c = g.entities[i];
    if (!c.alive || c.isPlayer) continue;
    if (sim::dotQ30(c.frame.n, camQ_.player) < COS_RANGE) continue;
    vec3f pos = toLocal(sim::scaleToLength(c.frame.n, c.r));
    float d = g3::length(pos);  // distance from the player
    if (d > RANGE || d < 0.1f) continue;
    float sx, sy;
    if (project(pos, sx, sy) && sx >= 0 && sx < w_ && sy >= 0 && sy < h_) {
      continue;  // visible: no aura needed
    }
    // Direction on the screen from the view-space position
    vec3f v = view_.transformPoint(pos);
    float dx = v.x, dy = -v.y;  // screen y points down
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) continue;
    dx /= len, dy /= len;
    // Intersect the ray from the screen center with the screen rectangle
    float hx = w_ * 0.5f, hy = h_ * 0.5f;
    float tx = dx != 0 ? hx / std::fabs(dx) : 1e9f;
    float ty = dy != 0 ? hy / std::fabs(dy) : 1e9f;
    float tEdge = tx < ty ? tx : ty;
    float near = 1.0f - d / RANGE;  // 0 far .. 1 close
    float radiusPx = (20.0f + 50.0f * near) * ui_.scale8 / 8.0f;
    float cx = hx + dx * tEdge;
    float cy = hy + dy * tEdge;
    // Screen -> view space at DEPTH
    auto toView = [&](float px, float py) {
      float vx = (px / w_ * 2.0f - 1.0f) * tanX * DEPTH;
      float vy = (1.0f - py / h_ * 2.0f) * tanY * DEPTH;
      return cam_.eye + viewDir_ * DEPTH + right * vx + up * vy;
    };
#if DEVOURSPHERE_SUPPRESS_ALPHA
    // A solid triangle: tip on the edge, base inwards, width across the
    // direction. 8..18 px long on the reference screen, 6..13 on 240x240
    // (it scales half as fast as the UI so it stays readable when small).
    const float triLen = (20.0f + 20.0f * near) * (ui_.scale8 + 8) / 16.0f;
    const float half = triLen * 0.55f;
    const float bx = cx - dx * triLen, by = cy - dy * triLen;  // base center
    const vec3f tri[3] = {toView(cx, cy),
                          toView(bx - dy * half, by + dx * half),
                          toView(bx + dy * half, by - dx * half)};
    static const uint16_t triIdx[3] = {0, 1, 2};
    putSolid(tri, 3, triIdx, 3, materialForEntity(c));
    drawn++;
    continue;
#endif
    vec3f center = toView(cx, cy);
    float radius = radiusPx / focalPx_ * DEPTH;
    g2::Color col = colorForEntity(c);
    float bright = 0.3f + 0.7f * near;
    g2::Color centerCol = g2::makeColor((int)(g2::colorR(col) * bright),
                                        (int)(g2::colorG(col) * bright),
                                        (int)(g2::colorB(col) * bright));
    g3::Vertex verts[SEGMENTS + 2];
    uint16_t idx[SEGMENTS + 2];
    verts[0].position = center;
    verts[0].normal = {0, 1, 0};
    verts[0].uv = {0, 0};
    verts[0].color = centerCol;
    idx[0] = 0;
    for (int k = 0; k <= SEGMENTS; k++) {
      float a = k * (2.0f * PI / SEGMENTS);
      verts[k + 1].position =
          center + (right * fastCos(a) + up * fastSin(a)) * radius;
      verts[k + 1].normal = {0, 1, 0};
      verts[k + 1].uv = {0, 0};
      verts[k + 1].color = g2::makeColor(0, 0, 0);
      idx[k + 1] = (uint16_t)(k + 1);
    }
    g3::VertexBuffer vb = {(uint16_t)(SEGMENTS + 2), verts};
    g3::Primitive prim = {g3::PrimitiveType::TRIANGLE_FAN, &vb,
                          (uint16_t)(SEGMENTS + 2), idx,
                          &palette_[PAL_LINE_ADD]};
    g3d_.putPrimitive(prim);
    drawn++;
  }
}

// Low health warning: at half health and below, the four screen edges glow
// red (a gradient from the edge inwards, added onto the frame), deeper and
// wider the closer the health gets to zero. Drawn last so it lies over
// everything in the scene; the HUD is drawn on top of it.
//
// With DEVOURSPHERE_SUPPRESS_ALPHA only the top and bottom edges glow, and
// half as wide: the four bands are four Gouraud, additively blended quads
// covering a third of the screen, which took a Cortex-M0+ from 28 to 15 fps
// the moment the health dropped.
void Renderer::drawHealthWarning() {
  const sim::Game &g = *game_;
  if (g.state() != sim::GameState::PLAYING &&
      g.state() != sim::GameState::LAUNCH &&
      g.state() != sim::GameState::ARRIVE) {
    return;
  }
  const sim::Entity &p = g.player();
  if (!p.alive || p.hpMax <= 0 || p.hp * 2 > p.hpMax) return;
  // 0 at half health .. 1 at zero
  float t = 1.0f - (float)p.hp * 2.0f / (float)p.hpMax;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  float bright = t * 0.5f;
  g2::Color edge = g2::makeColor((int)(255 * bright), (int)(24 * bright),
                                 (int)(16 * bright));
  g2::Color inner = g2::makeColor(0, 0, 0);
  float band = h_ * (DEVOURSPHERE_SUPPRESS_ALPHA ? 0.05f : 0.1f);  // px
  constexpr int BANDS = DEVOURSPHERE_SUPPRESS_ALPHA ? 2 : 4;

  constexpr float DEPTH = 2.0f;  // view-space distance of the quads
  ScreenPlane sp = screenPlane();
  auto toView = [&](float sx, float sy) {
    return screenToWorld(sp, sx, sy, DEPTH);
  };
  // Each band: screen quad (x0,y0)-(x1,y1) with the edge side colored
  struct Band {
    float x0, y0, x1, y1;
    bool edgeFirst;  // the edge lies on the (x0 / y0) side
  };
  const float W = (float)w_, H = (float)h_;
  const Band bands[4] = {
      {0, 0, W, band, true},       // top
      {0, H - band, W, H, false},  // bottom
      {0, 0, band, H, true},       // left
      {W - band, 0, W, H, false},  // right
  };
  for (int b = 0; b < BANDS; b++) {
    const Band &bd = bands[b];
    bool horizontal = b < 2;
    g3::Vertex verts[4];
    uint16_t idx[4] = {0, 1, 2, 3};
    const float xs[4] = {bd.x0, bd.x1, bd.x1, bd.x0};
    const float ys[4] = {bd.y0, bd.y0, bd.y1, bd.y1};
    for (int k = 0; k < 4; k++) {
      bool onFirst = horizontal ? (ys[k] == bd.y0) : (xs[k] == bd.x0);
      verts[k].position = toView(xs[k], ys[k]);
      verts[k].normal = {0, 1, 0};
      verts[k].uv = {0, 0};
      verts[k].color = (onFirst == bd.edgeFirst) ? edge : inner;
    }
    g3::VertexBuffer vb = {4, verts};
    g3::Primitive prim = {g3::PrimitiveType::TRIANGLE_FAN, &vb, 4, idx,
                          &palette_[PAL_LINE_ADD]};
    g3d_.putPrimitive(prim);
  }
}

// Enemies beyond the horizon get a marker on the horizon in their direction:
// opponents of a comparable size (1/4 .. 4x), and every carrier of an
// upgrade or of a bounty (`always`). The color (the body's, so a bounty
// holder's is yellow) fades with the distance along the surface, except a
// bounty holder's, which stays at full brightness; enemies
// farther than MARKER_MAX_ANGLE around the sphere are not shown at all,
// except the `always` ones and the bigger ones when the player is close to
// the top (rank <= MARKER_ALWAYS_RANK): those are the remaining targets,
// wherever they are. A carrier's white outline fades the same way. No
// markers at all during the flight (LAUNCH / ARRIVE): buildScene skips them.
void Renderer::addEnemyMarker(const sim::Entity &c, bool always) {
  const sim::Game &g = *game_;
  uint32_t ps = g.player().size;
  if (always) {
    if (markerCount_ >= MAX_MARKERS) return;
  } else {
    if (markerCount_ >= MAX_ENEMY_MARKERS || c.size * 4 < ps ||
        c.size > ps * 4) {
      return;
    }
  }
  vec3f up = q30ToF(c.frame.n);
  bool remaining =
      always || (g.playerRank() <= MARKER_ALWAYS_RANK && c.size > ps);
  float cosDist = g3::dot(up, camUnit_);
  vec3f d = up - camUnit_ * cosDist;
  float len = g3::length(d);  // the sine of the angle
  if (len < 1e-5f) return;
  // The angle from its sine and cosine through the simulation's atan2:
  // acos is a library call, a few thousand cycles for every enemy beyond
  // the horizon on a core without an FPU
  float ang = sim::atan2Brad((int32_t)(len * (1 << 30)),
                             (int32_t)(cosDist * (1 << 30))) *
              BRAD_TO_RAD;
  if (ang > MARKER_MAX_ANGLE && !remaining) return;
  float fade =
      1.0f - (ang - horizonAngle_) / (MARKER_MAX_ANGLE - horizonAngle_);
  if (fade < 0) fade = 0;
  if (fade > 1) fade = 1;
  d = d * (1.0f / len);
  vec3f hp = camUnit_ * fastCos(horizonAngle_) + d * fastSin(horizonAngle_);
  vec3f world = sphereCenter_ + hp * SPHERE_R;
  float sx, sy;
  if (!project(world, sx, sy)) return;
  int edge = ui(4, 2);
  if (sx < edge || sx >= w_ - edge || sy < edge || sy >= h_ - edge) return;
  Marker2D &mk = markers_[markerCount_++];
  mk.x = (int16_t)sx;
  mk.y = (int16_t)sy;
  mk.kind = 0;
  g2::Color col = colorForEntity(c);
  // A bounty holder's marker never fades: it is the target, wherever it is
  // (at the far side the fade would leave it at 25 %, lost on the black)
  float k = c.bounty ? 1.0f
                     : MARKER_MIN_BRIGHTNESS + (1.0f - MARKER_MIN_BRIGHTNESS) * fade;
  mk.color =
      g2::makeColor((int)(g2::colorR(col) * k), (int)(g2::colorG(col) * k),
                    (int)(g2::colorB(col) * k));
  bool carrier = c.upgrade != (uint8_t)sim::UpgradeKind::NONE;
  mk.outline = carrier ? (uint8_t)(255 * k) : 0;
}

Renderer::ScreenPlane Renderer::screenPlane() const {
  ScreenPlane sp;
  vec3f camUp =
      std::fabs(g3::dot(viewDir_, cam_.up)) < 0.99f ? cam_.up : vec3f{0, 0, 1};
  sp.right = g3::normalize(g3::cross(viewDir_, camUp));
  sp.up = g3::cross(sp.right, viewDir_);
  sp.tanY = std::tan(camFov_ * 0.5f);
  sp.tanX = sp.tanY * (float)w_ / (float)h_;
  return sp;
}

vec3f Renderer::screenToWorld(const ScreenPlane &sp, float sx, float sy,
                              float depth) const {
  float vx = (sx / w_ * 2.0f - 1.0f) * sp.tanX * depth;
  float vy = (1.0f - sy / h_ * 2.0f) * sp.tanY * depth;
  return cam_.eye + viewDir_ * depth + sp.right * vx + sp.up * vy;
}

// Horizon markers: collected here (Marker2D, screen coordinates) and
// drawn by renderBand() as 2D fills over the 3D layers, so they never
// hide the enemies flying near the horizon and cost no 3D primitive.
// Enemies: a downward triangle just above the horizon point, with a white
// outline (fading with the distance) when they carry an upgrade;
// upgrades: their icon (the HUD shape).

void Renderer::buildScene() {
  const sim::Game &g = *game_;
  g3d_.beginScene();
  g3d_.lookAt(cam_.eye, cam_.target, cam_.up);
  g3d_.disableParallelLight();
  g3d_.disableEnvironmentLight();
  g3d_.setDepthBias(0.0f);

  // The backdrop. The stars sit 1500 FU away on a sphere around the camera
  // and nothing in the scene is behind them; everything that stands on the
  // surface is in front of the sphere wireframe. Neither needs depth, and
  // neither goes through the 3D pipeline any more: both are projected here
  // and drawn into each band ahead of the 3D layers (see WireSeg). Within
  // the wireframe, which of two crossing edges wins costs at most one
  // RGB565 step: it is one blue ramp fading with distance.
  drawStars();
  buildSphere();
  frameProfile_.stamp(FP_SPHERE);
  // Layers. A scene is a sequence of them and each is drawn in front of the
  // ones opened before it, so the depth sort only has to resolve what is
  // inside one. Opening them back to front is this function's job.
  //
  // The world: everything standing on the surface, depth sorted.
  g3d_.beginLayer();
  // Upgrades first: their horizon markers must never be crowded out by
  // the enemies' markers; then the enemies carrying one or a bounty, shown
  // whatever their size and distance; the other enemies take what is left.
  // Nothing is marked during the flight between spheres
  const bool flying = g.state() == sim::GameState::LAUNCH ||
                      g.state() == sim::GameState::ARRIVE;
  drawFloatingUpgrades(!flying);
  for (int i = 0; i < sim::MAX_ENTITIES && !flying; i++) {
    const sim::Entity &c = g.entities[i];
    if (!c.alive || c.isPlayer) continue;
    if (c.upgrade == (uint8_t)sim::UpgradeKind::NONE && !c.bounty) continue;
    if (sim::dotQ30(c.frame.n, camQ_.unit) < camQ_.cosHorizon - HORIZON_Q02) {
      addEnemyMarker(c, true);
    }
  }

  // Visible entities sorted by distance (vis_ is a member: see renderer.hpp)
  int n = 0;
  for (int i = 0; i < sim::MAX_ENTITIES; i++) {
    const sim::Entity &c = g.entities[i];
    if (!c.alive) continue;
    if (sim::dotQ30(c.frame.n, camQ_.unit) < camQ_.cosHorizon - HORIZON_Q02) {
      // Beyond the horizon: a marker (carriers and bounty holders were
      // collected above)
      if (!flying && !c.isPlayer && !c.bounty &&
          c.upgrade == (uint8_t)sim::UpgradeKind::NONE) {
        addEnemyMarker(c, false);
      }
      continue;
    }
    const sim::Vec3 posQ = sim::scaleToLength(c.frame.n, c.r) - origin_;
    const sim::Vec3 rel = posQ - eyeQ_;
    if (dotUnits(rel, camQ_.fwd) < -c.bodyRadius) continue;
    const int64_t d2 = dist2Units(rel);
    if (!atLeastPx(c.bodyRadius, d2, camQ_.focal16, 13)) {  // under 0.8 px
      putPointQ(posQ, colorForEntity(c), palette_[PAL_LINE]);
      continue;
    }
    // The body's radius on the screen, for the detail and gauge thresholds
    const int32_t d = (int32_t)sim::isqrt64((uint64_t)d2);
    const float px =
        (float)((int64_t)c.bodyRadius * camQ_.focal16 / d) * (1.0f / 16);
    // insertion sort by distance
    int k = n++;
    while (k > 0 && vis_[k - 1].d > d) {
      vis_[k] = vis_[k - 1];
      k--;
    }
    vis_[k] = {d, px, (int16_t)i};
  }

  // Triangle budget: what is left after the wireframe, minus a reserve for
  // fragments, bullets and effects.
  //
  // The triangle buffer is a byte budget, not a slot count: ShapoGFX stores
  // each primitive in the record layout it needs. Entity bodies are the
  // cheapest kind -- flat (putKite / putQuad give every vertex the same
  // color) and depth sorted -- so their record is the header plus the depth
  // plane, and the buffer also spends 4 bytes on an entry for each. The
  // 64-bit figure is used on a host so the budget is never optimistic there.
  constexpr int TRI_BYTES = sizeof(void *) > 4 ? 76 : 64;
  g3::Stats st = g3d_.getStats();
  // The reserve is what the fragments, bullets and effects drawn after the
  // entities may need: 380 records on a large arena, but never more than a
  // third of what is free -- on a 32 KB arena (21 KB of buffer, 336 records)
  // a fixed 380 left every entity an outline.
  const int slots = (int)((st.triBytesTotal - st.triBytes) / TRI_BYTES);
  int reserve = 380;
  if (reserve > slots / 3) reserve = slots / 3;
  int triBudget = slots - reserve;
  if (detailTris_ > 0 && triBudget > detailTris_) triBudget = detailTris_;
  for (int k = 0; k < n; k++) {
    const sim::Entity &c = g.entities[vis_[k].idx];
    int fullTris = (1 + 2 * c.fragmentCount) * 2;
    bool full = vis_[k].px >= 6.0f && triBudget >= fullTris;
    triBudget -= full ? fullTris : 6;
    const sim::Vec3 posQ = sim::scaleToLength(c.frame.n, c.r) - origin_;
    bool blink =
        c.invincible > 0 && ((g.tickCount() / (sim::TICK_RATE / 8)) & 1);
    drawEntity(c, posQ, vis_[k].px, full, blink);
    // Health gauge over enemies
    if (!c.isPlayer && vis_[k].px >= 2.5f && gaugeCount_ < MAX_GAUGES) {
      const vec3f pos = {posQ.x * (1.0f / FU), posQ.y * (1.0f / FU),
                         posQ.z * (1.0f / FU)};
      float bodyR = c.bodyRadius / (float)FU;
      vec3f up = q30ToF(c.frame.n);
      float sx, sy;
      if (project(pos + up * (bodyR * 0.3f + 0.5f), sx, sy)) {
        int w = (int)(vis_[k].px * 1.6f);
        int wMin = ui(14, 6), wMax = ui(48, 12);
        w = w < wMin ? wMin : (w > wMax ? wMax : w);
        if (sx > -w && sx < w_ + w && sy > -8 && sy < h_ + 8) {
          Gauge2D &gg = gauges_[gaugeCount_++];
          gg.x = (int16_t)(sx - w / 2);
          gg.y = (int16_t)(sy - ui(6, 2));
          gg.w = (int16_t)w;
          // Rounded up: an enemy with a sliver of health left still shows
          // a sliver of gauge rather than an empty one that keeps flying
          int ratio = c.hpMax > 0
                          ? (int)(((int64_t)c.hp * 255 + c.hpMax - 1) / c.hpMax)
                          : 0;
          gg.ratio = (uint8_t)(ratio < 0 ? 0 : (ratio > 255 ? 255 : ratio));
        }
      }
    }
  }

  frameProfile_.stamp(FP_ENTITIES);
  drawFloatingFragments();
  frameProfile_.stamp(FP_FRAGMENTS);
  drawBullets();
  frameProfile_.stamp(FP_BULLETS);
  drawEffects();
  frameProfile_.stamp(FP_EFFECTS_DRAW);
  // Screen-space overlays: fans and quads on planes 1.9 to 2.0 units in
  // front of the camera, all additive, already in back-to-front order and
  // nearer than anything in the world. Drawing them after drawEffects()
  // rather than around it is what the depth sort was doing anyway.
  g3d_.beginLayer(g3::LayerFlags::NO_DEPTH);
  drawPresenceAuras();
  drawHealthWarning();
  g3d_.endScene();
  queueMarkerOutlines();
  frameProfile_.stamp(FP_OVERLAYS);
}

// ---------------------------------------------------------------------------
// Frame

// The shown score chases the real one: a fraction SCORE_ROLL_PER_SEC of the
// gap per second (at least SCORE_ROLL_MIN_PER_SEC points per second, never
// past the target); a drop (new game) is shown at once
void Renderer::updateScoreDisplay(float dt) {
  double actual = (double)game_->score();
  if (actual <= scoreShown_) {
    scoreShown_ = actual;
    return;
  }
  if (dt < 0) dt = 0;
  if (dt > 0.5f) dt = 0.5f;
  double gap = actual - scoreShown_;
  double step = gap * (double)(SCORE_ROLL_PER_SEC * dt);
  double minStep = (double)(SCORE_ROLL_MIN_PER_SEC * dt);
  if (step < minStep) step = minStep;
  if (step > gap) step = gap;
  scoreShown_ += step;
}

void Renderer::beginFrame(const sim::Game &game, float dt) {
  frameProfile_.begin();
  game_ = &game;
  // Paused: the camera, the debris, the dust and the score roll-up stop too
  if (game.paused()) dt = 0;
  // Everything the HUD reads, taken here so that renderBand() never touches
  // the simulation (see HudState)
  const sim::Entity &p = game.player();
  hud_.tickCount = game.tickCount();
  hud_.score = game.score();
  hud_.highScore = game.highScore();
  hud_.events = game.events();
  hud_.playerHp = p.hp;
  hud_.playerHpMax = p.hpMax;
  hud_.stateTimer = game.stateTimer();
  hud_.sphereLevel = game.sphereLevel();
  hud_.spheresCleared = game.spheresCleared();
  hud_.playerRank = game.playerRank();
  hud_.aliveEntities = game.aliveEntities();
  hud_.bountyCount = game.bountyCount();
  hud_.playerAlive = p.alive;
  hud_.selectedWeapon = game.selectedWeapon();
  hud_.playerWeapon = (int)p.weapon;
  hud_.cores = game.cores();
  for (int k = 0; k < sim::UPGRADE_KINDS; k++)
    hud_.upgradeLevel[k] = game.upgradeLevel((sim::UpgradeKind)(k + 1));
  hud_.dodgeReadyQ8 = game.dodgeReadyQ8();
  hud_.timeLeftTicks = game.sphereTimeLeft();
  hud_.timeUp = game.timeUp();
  hud_.sphereScore = game.sphereScore();
  hud_.clearBonus = game.clearBonus();
  hud_.clearTicks = game.clearTicks();
  hud_.highScoreSphere = game.highScoreSphere();
  hud_.state = game.state();
  hud_.debugMode = game.debugMode();
  hud_.paused = game.paused();
  hud_.muted = game.muted();
  time_ += dt;
  updateScoreDisplay(dt);
  lineCount_ = 0;
  pointCount_ = 0;
  wireCount_ = 0;
  starCount_ = 0;
  lineCount2D_ = 0;
  pointCount2D_ = 0;
  gaugeCount_ = 0;
  markerCount_ = 0;
  entitiesDrawn_ = 0;
  kites_ = 0;
  updateCamera(dt);
  frameProfile_.stamp(FP_CAMERA);
  collectEffects();
  updateEffects(dt);
  frameProfile_.stamp(FP_EFFECTS);
  buildScene();
  g3d_.beginRender();
  frameProfile_.stamp(FP_SORT);
}

void Renderer::renderBand(const g2::Surface &dst, int y, int h, int dstY) {
  frameProfile_.begin();
  g2::Graphics2D g(dst);
  g.setClipRect(0, dstY, w_, h);
  g.clear(g2::makeColor(0, 0, 4));
  drawBackdropBand(dst, y, h, dstY);
  frameProfile_.stamp(FP_BAND_2D);
  g3d_.render(0, (int16_t)y, (int16_t)w_, (int16_t)h, dst, 0, (int16_t)dstY);
  frameProfile_.stamp(FP_BAND_3D);
  int oy = dstY - y;
  drawMarkers2D(g, oy);
  drawLines2D(dst, y, h, dstY, L2D_OVER);
  const int gaugeH = ui(3, 2), gaugeB = ui(1, 1);
  for (int i = 0; i < gaugeCount_; i++) {
    const Gauge2D &gg = gauges_[i];
    if (gg.y + gaugeH <= y || gg.y >= y + h) continue;
    g.fillRect(gg.x - gaugeB, gg.y + oy - gaugeB, gg.w + 2 * gaugeB,
               gaugeH + 2 * gaugeB, g2::makeColor(0, 0, 0, 170));
    int fill = (gg.w * gg.ratio + 254) / 255;  // rounded up
    g2::Color c = gg.ratio > 128 ? g2::makeColor(90, 230, 140)
                                 : (gg.ratio > 50 ? g2::makeColor(240, 200, 60)
                                                  : g2::makeColor(240, 70, 60));
    if (fill > 0) g.fillRect(gg.x, gg.y + oy, fill, gaugeH, c);
  }
  drawHud(g, oy);
  frameProfile_.stamp(FP_BAND_2D);
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
