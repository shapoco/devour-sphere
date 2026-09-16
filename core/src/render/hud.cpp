// HUD and menu screens (2D text over the scene).
//
// Nothing here is in absolute pixels: the layout is written against the
// reference screen (UI_REF_W x UI_REF_H) and mapped onto the real frame
// buffer. Distances that should keep their apparent size (margins, the
// gauge, icons) go through ui(); vertical positions of the full-width
// overlays go through uiY(), so they follow the aspect ratio. Text that
// still does not fit is replaced by a shorter version or dropped.

#include <cstdio>

#include "devoursphere/render/renderer.hpp"
#include "shapoco/gfx2d/fonts.hpp"

namespace devoursphere::render {

static const char *WEAPON_NAMES[sim::WEAPON_COUNT] = {"VULCAN", "LASER",
                                                      "MISSILE"};
static const char *WEAPON_DESCS[sim::WEAPON_COUNT] = {
    "rapid fire, short range", "fast and strong, narrow", "homing, weak"};

static const g2::Color HUD_TEXT = g2::makeColor(200, 230, 255);
static const g2::Color HUD_DIM = g2::makeColor(110, 140, 180);
static const g2::Color HUD_SHADOW = g2::makeColor(0, 0, 0, 180);

// The three fonts are 8, 12 and 21 px tall and are magnified by an integer
// factor. On a screen below half the reference every role drops a step, so
// that a line of text never eats the whole width.
void Renderer::setHudFont(g2::Graphics2D &g, HudFont role) const {
  const int m = ui_.fontMult;
  switch (role) {
    case HudFont::SMALL: g.setFont(&ShapoSansP_s08c07, m); break;
    case HudFont::MEDIUM:
      if (ui_.tiny)
        g.setFont(&ShapoSansP_s08c07, m);
      else
        g.setFont(&ShapoSansP_s12c09a01w02, m);
      break;
    case HudFont::LARGE:
      if (ui_.tiny)
        g.setFont(&ShapoSansP_s08c07, m);
      else
        g.setFont(&ShapoSansP_s21c16a01w03, m);
      break;
    case HudFont::TITLE:
      if (ui_.tiny)
        g.setFont(&ShapoSansP_s12c09a01w02, m);
      else
        g.setFont(&ShapoSansP_s21c16a01w03, ui_.compact ? m : 2 * m);
      break;
  }
}

int Renderer::textFits(g2::Graphics2D &g, const char *text) const {
  return g.measureText(text) <= w_ - 2 * ui_.margin;
}

void Renderer::drawCenteredText(g2::Graphics2D &g, int y, const char *text,
                                g2::Color color) {
  int tw = g.measureText(text);
  int x = (w_ - tw) / 2;
  int sh = ui_.fontMult;
  g.setTextColor(HUD_SHADOW);
  g.drawString(x + sh, y + sh, text);
  g.setTextColor(color);
  g.drawString(x, y, text);
}

const char *Renderer::drawCenteredFit(g2::Graphics2D &g, int y,
                                      const char *text, const char *alt,
                                      g2::Color color) {
  if (text && textFits(g, text)) {
    drawCenteredText(g, y, text, color);
    return text;
  }
  if (alt && textFits(g, alt)) {
    drawCenteredText(g, y, alt, color);
    return alt;
  }
  return nullptr;
}

// Icons of the upgrade kinds, centered at (cx, cy), 14 px tall at scale8 = 8
static inline int sc(int v, int s8) {
  int r = v * s8 / 8;
  if (v > 0 && r < 1) r = 1;
  if (v < 0 && r > -1) r = -1;
  return r;
}

int upgradeIconPolygon(int kind, int cx, int cy, g2::vec2i *pts, int scale8) {
  const int s = scale8;
  switch ((sim::UpgradeKind)kind) {
    case sim::UpgradeKind::SHIELD: {  // tall diamond
      const g2::vec2i p[4] = {{cx, cy - sc(7, s)},
                              {cx + sc(4, s), cy},
                              {cx, cy + sc(7, s)},
                              {cx - sc(4, s), cy}};
      for (int i = 0; i < 4; i++) pts[i] = p[i];
      return 4;
    }
    case sim::UpgradeKind::OVERDRIVE: {  // triangle
      const g2::vec2i p[3] = {{cx, cy - sc(6, s)},
                              {cx + sc(6, s), cy + sc(5, s)},
                              {cx - sc(6, s), cy + sc(5, s)}};
      for (int i = 0; i < 3; i++) pts[i] = p[i];
      return 3;
    }
    case sim::UpgradeKind::THRUSTER: {  // arrowhead (chevron)
      const g2::vec2i p[4] = {{cx, cy - sc(7, s)},
                              {cx + sc(6, s), cy + sc(6, s)},
                              {cx, cy + sc(2, s)},
                              {cx - sc(6, s), cy + sc(6, s)}};
      for (int i = 0; i < 4; i++) pts[i] = p[i];
      return 4;
    }
    case sim::UpgradeKind::EXTRA_CORE: {  // four-pointed star
      const g2::vec2i p[8] = {
          {cx, cy - sc(7, s)},           {cx + sc(2, s), cy - sc(2, s)},
          {cx + sc(7, s), cy},           {cx + sc(2, s), cy + sc(2, s)},
          {cx, cy + sc(7, s)},           {cx - sc(2, s), cy + sc(2, s)},
          {cx - sc(7, s), cy},           {cx - sc(2, s), cy - sc(2, s)}};
      for (int i = 0; i < 8; i++) pts[i] = p[i];
      return 8;
    }
    default: return 0;
  }
}

void drawUpgradeIcon(g2::Graphics2D &g, int kind, int cx, int cy, g2::Color c,
                     int scale8) {
  g2::vec2i pts[8];
  int n = upgradeIconPolygon(kind, cx, cy, pts, scale8);
  if (n > 0) g.fillPolygon(pts, n, c);
}

static g2::Color dimmed(g2::Color c) {
  return g2::makeColor(g2::colorR(c) / 3, g2::colorG(c) / 3,
                       g2::colorB(c) / 3);
}

// Upgrades (bottom left): icon + level pips; spare cores (bottom right).
// The pips keep a readable minimum size, so on a small screen they take a
// larger share of the width than the reference layout gives them.
void Renderer::drawUpgradeStatus(g2::Graphics2D &g, int oy) {
  const sim::Game &game = *game_;
  const int is = ui_.iconScale8;
  const int half = 7 * is / 8;
  const int pipW = ui(4, 2), pipH = ui(7, 3), pipPitch = ui(6, 3);
  const int y = oy + h_ - ui(14, half + 2);
  const int gap = ui(3, 1);  // icon to pips
  int pitch = half + gap + pipPitch * sim::UPGRADE_MAX_LEVEL + ui(10, 3);
  if (pitch < ui(44)) pitch = ui(44);
  int x = ui(14, half + 2);
  for (int k = 1; k <= sim::UPGRADE_KINDS; k++) {
    int level = game.upgradeLevel((sim::UpgradeKind)k);
    g2::Color c = upgradeColor(k);
    g2::Color dim = dimmed(c);
    drawUpgradeIcon(g, k, x, y, level > 0 ? c : dim, is);
    for (int i = 0; i < sim::UPGRADE_MAX_LEVEL; i++) {
      int px = x + ui(11, half + 1);
      px += i * pipPitch;
      if (i < level) {
        g.fillRect(px, y - pipH / 2, pipW, pipH, c);
      } else {
        g.drawRect(px, y - pipH / 2, pipW, pipH, dim);
      }
    }
    x += pitch;
  }
  // Spare cores
  int corePitch = half * 2 + ui(4, 2);
  if (corePitch < ui(18)) corePitch = ui(18);
  int cx = w_ - ui(14, half + 2);
  for (int i = 0; i < sim::CORES_MAX; i++) {
    g2::Color c = upgradeColor((int)sim::UpgradeKind::EXTRA_CORE);
    if (i >= game.cores()) c = dimmed(c);
    drawUpgradeIcon(g, (int)sim::UpgradeKind::EXTRA_CORE, cx, y, c, is);
    cx -= corePitch;
  }
}

void Renderer::drawHud(g2::Graphics2D &g, int oy) {
  const sim::Game &game = *game_;
  const sim::Entity &p = game.player();
  char buf[64], alt[64];
  uint32_t t = game.tickCount();
  bool blinkOn = (t / (sim::TICK_RATE / 2)) & 1;
  const int margin = ui_.margin;

  switch (game.state()) {
    case sim::GameState::TITLE: {
      setHudFont(g, HudFont::TITLE);
      drawCenteredFit(g, oy + uiY(70), "DEVOUR SPHERE", "DEVOUR",
                      g2::makeColor(120, 255, 235));
      setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
      drawCenteredFit(g, oy + uiY(130), "grow by devouring, become the largest",
                      "grow by devouring", HUD_DIM);
      if (blinkOn) {
        setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
        drawCenteredFit(g, oy + uiY(200), "PRESS A TO START", "PRESS A",
                        HUD_TEXT);
      }
      setHudFont(g, HudFont::SMALL);
      if (game.highScore() > 0) {
        std::snprintf(buf, sizeof(buf), "HIGH SCORE %u", game.highScore());
        std::snprintf(alt, sizeof(alt), "HI %u", game.highScore());
        drawCenteredFit(g, oy + uiY(236), buf, alt, HUD_DIM);
      }
      int lineH = 16 * ui_.fontMult;
      drawCenteredFit(g, oy + h_ - margin - 2 * lineH,
                      "MOVE: ARROWS / WASD    A: SPACE / IJKL",
                      "ARROWS: MOVE   SPACE: FIRE", HUD_DIM);
      drawCenteredFit(g, oy + h_ - margin - lineH,
                      "UP: DASH   DOWN: BRAKE", "UP / DOWN: DASH / BRAKE",
                      HUD_DIM);
      break;
    }
    case sim::GameState::WEAPON_SELECT: {
      setHudFont(g, HudFont::LARGE);
      drawCenteredFit(g, oy + uiY(50), "SELECT WEAPON", "WEAPON", HUD_TEXT);
      int sel = game.selectedWeapon();
      int colW = w_ / sim::WEAPON_COUNT;
      // The names sit side by side while the columns are wide enough for the
      // longest of them; on a narrow screen they stack in the middle instead
      setHudFont(g, HudFont::MEDIUM);
      int nameH = 12 * ui_.fontMult;
      int widest = 0;
      for (int i = 0; i < sim::WEAPON_COUNT; i++) {
        int tw = g.measureText(WEAPON_NAMES[i]);
        if (tw > widest) widest = tw;
      }
      if (widest + ui(20, 6) > colW) {  // try the small font first
        setHudFont(g, HudFont::SMALL);
        nameH = 8 * ui_.fontMult;
        widest = 0;
        for (int i = 0; i < sim::WEAPON_COUNT; i++) {
          int tw = g.measureText(WEAPON_NAMES[i]);
          if (tw > widest) widest = tw;
        }
      }
      const bool stacked = widest + ui(20, 6) > colW;
      const int rowH = nameH + ui(10, 4);
      const int top = stacked ? (h_ - rowH * sim::WEAPON_COUNT) / 2 : uiY(130);
      for (int i = 0; i < sim::WEAPON_COUNT; i++) {
        int cx = stacked ? w_ / 2 : colW * i + colW / 2;
        int by = stacked ? top + rowH * i : oy + uiY(130);
        bool on = i == sel;
        g2::Color c = on ? g2::makeColor(255, 230, 120) : HUD_DIM;
        int tw = g.measureText(WEAPON_NAMES[i]);
        if (on) {
          g.drawRect(cx - tw / 2 - ui(10, 3), stacked ? oy + by : by,
                     tw + ui(20, 6), stacked ? rowH : ui(24, 10), c);
        }
        g.setTextColor(c);
        g.drawString(cx - tw / 2,
                     stacked ? oy + by + ui(5, 2) : oy + uiY(136),
                     WEAPON_NAMES[i]);
      }
      // Descriptions: one per column (all of them or none, so that the
      // columns stay even), or only the selected one when stacked
      setHudFont(g, HudFont::SMALL);
      int descY = stacked ? oy + top + rowH * sim::WEAPON_COUNT + ui(8, 3)
                          : oy + uiY(162);
      int descRoom = stacked ? w_ - 2 * margin : colW;
      bool descFit = true;
      for (int i = 0; i < sim::WEAPON_COUNT; i++) {
        if (stacked && i != sel) continue;
        if (g.measureText(WEAPON_DESCS[i]) > descRoom) descFit = false;
      }
      for (int i = 0; descFit && i < sim::WEAPON_COUNT; i++) {
        if (stacked && i != sel) continue;
        int tw = g.measureText(WEAPON_DESCS[i]);
        int cx = stacked ? w_ / 2 : colW * i + colW / 2;
        g.setTextColor(i == sel ? HUD_TEXT : HUD_DIM);
        g.drawString(cx - tw / 2, descY, WEAPON_DESCS[i]);
      }
      int hintY = stacked ? oy + h_ - margin - 8 * ui_.fontMult : oy + uiY(220);
      drawCenteredFit(g, hintY, "LEFT / RIGHT: choose    A: confirm",
                      "A: confirm", HUD_DIM);
      break;
    }
    case sim::GameState::PLAYING:
    case sim::GameState::LAUNCH:
    case sim::GameState::DEAD: {
      // Health gauge (top left)
      const int gx = margin, gy = oy + margin;
      const int gw = ui_.gaugeW, gh = ui_.gaugeH, gb = ui(1, 1);
      g.drawRect(gx - gb, gy - gb, gw + 2 * gb, gh + 2 * gb, HUD_DIM, gb);
      int fill = p.hpMax > 0 ? (int)((int64_t)p.hp * gw / p.hpMax) : 0;
      if (fill < 0) fill = 0;
      g2::Color hpColor = fill > gw / 2
                              ? g2::makeColor(90, 230, 140)
                              : (fill > gw / 5 ? g2::makeColor(240, 200, 60)
                                               : g2::makeColor(240, 70, 60));
      if (fill > 0) g.fillRect(gx, gy, fill, gh, hpColor);

      // Score (top center): the shown value rolls up towards the real one
      setHudFont(g, HudFont::MEDIUM);
      std::snprintf(buf, sizeof(buf), "%u", scoreShown());
      {
        int tw = g.measureText(buf);
        int sh = ui_.fontMult;
        g.setTextColor(HUD_SHADOW);
        g.drawString((w_ - tw) / 2 + sh, gy + sh, buf);
        g.setTextColor(HUD_TEXT);
        g.drawString((w_ - tw) / 2, gy, buf);
      }

      // Rank, under the gauge (same size as the score)
      std::snprintf(buf, sizeof(buf), "RANK %d / %d", game.playerRank(),
                    game.aliveEntities());
      if (!textFits(g, buf)) {
        std::snprintf(buf, sizeof(buf), "%d/%d", game.playerRank(),
                      game.aliveEntities());
      }
      {
        int ry = gy + gh + ui(12, 4);
        int sh = ui_.fontMult;
        g.setTextColor(HUD_SHADOW);
        g.drawString(gx + sh, ry + sh, buf);
        g.setTextColor(game.playerRank() == 1 ? g2::makeColor(255, 230, 120)
                                              : HUD_TEXT);
        g.drawString(gx, ry, buf);
      }

      drawUpgradeStatus(g, oy);

      // Sphere and weapon (top right)
      setHudFont(g, HudFont::SMALL);
      std::snprintf(buf, sizeof(buf), "SPHERE %d", game.sphereLevel());
      int tw = g.measureText(buf);
      if (tw > w_ / 3) {
        std::snprintf(buf, sizeof(buf), "S%d", game.sphereLevel());
        tw = g.measureText(buf);
      }
      g.setTextColor(HUD_TEXT);
      g.drawString(w_ - margin - tw, gy, buf);
      if (!ui_.tiny) {
        tw = g.measureText(WEAPON_NAMES[(int)p.weapon]);
        g.setTextColor(HUD_DIM);
        g.drawString(w_ - margin - tw, gy + 12 * ui_.fontMult,
                     WEAPON_NAMES[(int)p.weapon]);
      }

      // Hit flash
      if (game.events() & sim::Event::PLAYER_HIT) {
        g.drawRect(0, oy, w_, h_, g2::makeColor(255, 60, 60, 160), ui(3, 1));
      }

      if (game.state() == sim::GameState::LAUNCH) {
        setHudFont(g, HudFont::LARGE);
        drawCenteredFit(g, oy + uiY(100), "SPHERE DEVOURED", "DEVOURED",
                        g2::makeColor(255, 230, 120));
        setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
        drawCenteredFit(g, oy + uiY(135), "leaving for a larger world...",
                        "next sphere...", HUD_TEXT);
      } else if (game.state() == sim::GameState::DEAD) {
        setHudFont(g, HudFont::LARGE);
        drawCenteredFit(g, oy + uiY(100), "YOU WERE DEVOURED", "DEVOURED",
                        g2::makeColor(255, 90, 90));
        setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
        std::snprintf(buf, sizeof(buf), "SCORE %u", game.score());
        std::snprintf(alt, sizeof(alt), "%u", game.score());
        drawCenteredFit(g, oy + uiY(135), buf, alt, HUD_TEXT);
        std::snprintf(buf, sizeof(buf), "HIGH SCORE %u", game.highScore());
        std::snprintf(alt, sizeof(alt), "HI %u", game.highScore());
        drawCenteredFit(g, oy + uiY(152), buf, alt,
                        game.score() >= game.highScore() && game.score() > 0
                            ? g2::makeColor(255, 230, 120)
                            : HUD_DIM);
        setHudFont(g, HudFont::SMALL);
        std::snprintf(buf, sizeof(buf), "spheres devoured: %d",
                      game.spheresCleared());
        std::snprintf(alt, sizeof(alt), "spheres: %d", game.spheresCleared());
        drawCenteredFit(g, oy + uiY(172), buf, alt, HUD_DIM);
        if (game.stateTimer() > 2 * sim::TICK_RATE && blinkOn) {
          setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
          drawCenteredFit(g, oy + uiY(200), "PRESS A", nullptr, HUD_TEXT);
        }
      } else if (game.stateTimer() < 3 * sim::TICK_RATE &&
                 game.sphereLevel() == 1 && game.spheresCleared() == 0) {
        setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
        drawCenteredFit(g, oy + uiY(240),
                        "devour the fragments, become the largest",
                        "devour the fragments", HUD_DIM);
      }
      break;
    }
  }
}

}  // namespace devoursphere::render
