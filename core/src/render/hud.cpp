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
          {cx, cy - sc(7, s)}, {cx + sc(2, s), cy - sc(2, s)},
          {cx + sc(7, s), cy}, {cx + sc(2, s), cy + sc(2, s)},
          {cx, cy + sc(7, s)}, {cx - sc(2, s), cy + sc(2, s)},
          {cx - sc(7, s), cy}, {cx - sc(2, s), cy - sc(2, s)}};
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
  return g2::makeColor(g2::colorR(c) / 3, g2::colorG(c) / 3, g2::colorB(c) / 3);
}

// Upgrades (bottom left): icon + level pips; spare cores (bottom right).
// The pips keep a readable minimum size, so on a small screen they take a
// larger share of the width than the reference layout gives them.
void Renderer::drawUpgradeStatus(g2::Graphics2D &g, int oy) {
  const HudState &hud = hud_;
  const int is = ui_.iconScale8;
  const int half = 7 * is / 8;
  const int pipW = ui(4, 2), pipH = ui(7, 3), pipPitch = ui(6, 3);
  const int y = oy + h_ - ui(14, half + 2);
  const int gap = ui(3, 1);  // icon to pips
  int pitch = half + gap + pipPitch * sim::UPGRADE_MAX_LEVEL + ui(10, 3);
  if (pitch < ui(44)) pitch = ui(44);
  int x = ui(14, half + 2);
  for (int k = 1; k <= sim::UPGRADE_KINDS; k++) {
    int level = hud.upgradeLevel[k - 1];
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
    if (i >= hud.cores) c = dimmed(c);
    drawUpgradeIcon(g, (int)sim::UpgradeKind::EXTRA_CORE, cx, y, c, is);
    cx -= corePitch;
  }
}

// "HIGH SCORE 12345  SPHERE 5" and its short form
static void formatHighScore(char *buf, size_t bufLen, char *alt, size_t altLen,
                            const HudState &hud) {
  if (hud.highScoreSphere > 0) {
    std::snprintf(buf, bufLen, "HIGH SCORE %u  SPHERE %d",
                  (unsigned)hud.highScore, hud.highScoreSphere);
    std::snprintf(alt, altLen, "HI %u S%d", (unsigned)hud.highScore,
                  hud.highScoreSphere);
  } else {
    std::snprintf(buf, bufLen, "HIGH SCORE %u", (unsigned)hud.highScore);
    std::snprintf(alt, altLen, "HI %u", (unsigned)hud.highScore);
  }
}

void Renderer::drawHud(g2::Graphics2D &g, int oy) {
  const HudState &hud = hud_;
  char buf[64], alt[64];
  uint32_t t = hud.tickCount;
  bool blinkOn = (t / (sim::TICK_RATE / 2)) & 1;
  const int margin = ui_.margin;

  switch (hud.state) {
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
      if (hud.highScore > 0) {
        formatHighScore(buf, sizeof(buf), alt, sizeof(alt), hud);
        drawCenteredFit(g, oy + uiY(236), buf, alt, HUD_DIM);
      }
      if (hud.muted) {
        drawCenteredFit(g, oy + uiY(252), "SOUND OFF", nullptr,
                        g2::makeColor(255, 120, 120));
      }
      int lineH = g.lineAdvance() + ui(6, 2);
      drawCenteredFit(g, oy + h_ - margin - 2 * lineH, hints_.move,
                      hints_.moveAlt, HUD_DIM);
      drawCenteredFit(g, oy + h_ - margin - lineH, hints_.dash, hints_.dashAlt,
                      HUD_DIM);
      // The core's version, in the bottom right corner beside the last hint
      // line when there is room for both
      {
        const char *dash = textFits(g, hints_.dash) ? hints_.dash : hints_.dashAlt;
        int vw = g.measureText(sim::VERSION_STRING);
        if ((g.measureText(dash) + vw) / 2 + 2 * margin + vw < w_ / 2 + w_ / 2 - margin &&
            g.measureText(dash) / 2 + vw + 2 * margin < w_ / 2) {
          g.setTextColor(HUD_DIM);
          g.drawString(w_ - margin - vw, oy + h_ - margin - lineH,
                       sim::VERSION_STRING);
        }
      }
      break;
    }
    case sim::GameState::WEAPON_SELECT: {
      setHudFont(g, HudFont::LARGE);
      drawCenteredFit(g, oy + uiY(50), "SELECT WEAPON", "WEAPON", HUD_TEXT);
      int sel = hud.selectedWeapon;
      int colW = w_ / sim::WEAPON_COUNT;
      // The names sit side by side while the columns are wide enough for the
      // longest of them; on a narrow screen they stack in the middle instead
      setHudFont(g, HudFont::MEDIUM);
      int nameH = g.textHeight();
      int widest = 0;
      for (int i = 0; i < sim::WEAPON_COUNT; i++) {
        int tw = g.measureText(WEAPON_NAMES[i]);
        if (tw > widest) widest = tw;
      }
      if (widest + ui(20, 6) > colW) {  // try the small font first
        setHudFont(g, HudFont::SMALL);
        nameH = g.textHeight();
        widest = 0;
        for (int i = 0; i < sim::WEAPON_COUNT; i++) {
          int tw = g.measureText(WEAPON_NAMES[i]);
          if (tw > widest) widest = tw;
        }
      }
      const bool stacked = widest + ui(20, 6) > colW;
      // The box wraps the line box of the name with an even padding, so it
      // fits the text at every font size instead of following its own scale
      const int padX = ui(10, 3), padY = ui(6, 2);
      const int boxH = nameH + 2 * padY;
      const int rowH = boxH + ui(4, 2);  // stacked: one row per weapon
      const int top = (h_ - rowH * sim::WEAPON_COUNT) / 2;
      for (int i = 0; i < sim::WEAPON_COUNT; i++) {
        int cx = stacked ? w_ / 2 : colW * i + colW / 2;
        int textY = stacked ? oy + top + rowH * i + padY : oy + uiY(136);
        bool on = i == sel;
        g2::Color c = on ? g2::makeColor(255, 230, 120) : HUD_DIM;
        int tw = g.measureText(WEAPON_NAMES[i]);
        if (on) {
          g.drawRect(cx - tw / 2 - padX, textY - padY, tw + 2 * padX, boxH, c);
        }
        g.setTextColor(c);
        g.drawString(cx - tw / 2, textY, WEAPON_NAMES[i]);
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
      int hintY = stacked ? oy + h_ - margin - g.textHeight() : oy + uiY(220);
      // Either axis chooses, so the hint names the one that matches the layout
      drawCenteredFit(g, hintY,
                      stacked ? "UP / DOWN: choose    A: confirm"
                              : "LEFT / RIGHT: choose    A: confirm",
                      "A: confirm", HUD_DIM);
      break;
    }
    case sim::GameState::PLAYING:
    case sim::GameState::LAUNCH:
    case sim::GameState::ARRIVE:
    case sim::GameState::DEAD: {
      // Health gauge (top left)
      const int gx = margin, gy = oy + margin;
      const int gw = ui_.gaugeW, gh = ui_.gaugeH, gb = ui(1, 1);
      g.drawRect(gx - gb, gy - gb, gw + 2 * gb, gh + 2 * gb, HUD_DIM, gb);
      int fill =
          hud.playerHpMax > 0  // rounded up: never empty while alive
              ? (int)(((int64_t)hud.playerHp * gw + hud.playerHpMax - 1) /
                      hud.playerHpMax)
              : 0;
      if (fill < 0) fill = 0;
      g2::Color hpColor = fill > gw / 2
                              ? g2::makeColor(90, 230, 140)
                              : (fill > gw / 5 ? g2::makeColor(240, 200, 60)
                                               : g2::makeColor(240, 70, 60));
      if (fill > 0) g.fillRect(gx, gy, fill, gh, hpColor);
      // Dodge readiness: a thin bar under the gauge that refills over the
      // cooldown (dim while charging, bright when ready)
      {
        const int by = gy + gh + gb + ui(2, 1), bh = ui(2, 1);
        int ready = hud.dodgeReadyQ8;
        if (ready < 0) ready = 0;
        if (ready > 256) ready = 256;
        int bw = (int)(((int64_t)gw * ready) >> 8);
        g2::Color dodgeColor = ready >= 256 ? g2::makeColor(120, 200, 255)
                                           : g2::makeColor(50, 80, 110);
        if (bw > 0) g.fillRect(gx, by, bw, bh, dodgeColor);
      }

      // Score (top center): the shown value rolls up towards the real one
      setHudFont(g, HudFont::MEDIUM);
      std::snprintf(buf, sizeof(buf), "%u", (unsigned)scoreShown());
      {
        int tw = g.measureText(buf);
        int sh = ui_.fontMult;
        g.setTextColor(HUD_SHADOW);
        g.drawString((w_ - tw) / 2 + sh, gy + sh, buf);
        g.setTextColor(HUD_TEXT);
        g.drawString((w_ - tw) / 2, gy, buf);
      }
      if (hud.debugMode) {
        // Cheats were used: say so over the score, large and translucent,
        // so that a screenshot cannot pass for a real one
        setHudFont(g, HudFont::LARGE);
        drawCenteredFit(g, gy - ui(2, 1), "DEBUG MODE", "DEBUG",
                        g2::makeColor(255, 70, 70, 150));
      }

      // Rank, under the gauge (same size as the score)
      std::snprintf(buf, sizeof(buf), "RANK %d / %d", hud.playerRank,
                    hud.aliveEntities);
      if (!textFits(g, buf)) {
        std::snprintf(buf, sizeof(buf), "%d/%d", hud.playerRank,
                      hud.aliveEntities);
      }
      {
        int ry = gy + gh + ui(12, 4);
        int sh = ui_.fontMult;
        g.setTextColor(HUD_SHADOW);
        g.drawString(gx + sh, ry + sh, buf);
        g.setTextColor(hud.playerRank == 1 ? g2::makeColor(255, 230, 120)
                                           : HUD_TEXT);
        g.drawString(gx, ry, buf);
      }

      drawUpgradeStatus(g, oy);

      // Time left, sphere and weapon (top right)
      setHudFont(g, HudFont::SMALL);
      int ry = gy;
      if (hud.state == sim::GameState::PLAYING) {
        // Rounded up: 0:00 only when the time is really up. White, yellow
        // in the last minute, red / white at 0.5 s in the last 30 s
        int secs = (hud.timeLeftTicks + sim::TICK_RATE - 1) / sim::TICK_RATE;
        std::snprintf(buf, sizeof(buf), "%d:%02d", secs / 60, secs % 60);
        g2::Color tc = HUD_TEXT;
        if (hud.timeLeftTicks < sim::TIME_ALARM_TICKS) {
          tc = blinkOn ? g2::makeColor(255, 70, 60) : HUD_TEXT;
        } else if (hud.timeLeftTicks < sim::TIME_WARN_TICKS) {
          tc = g2::makeColor(255, 220, 80);
        }
        int tw = g.measureText(buf);
        g.setTextColor(tc);
        g.drawString(w_ - margin - tw, ry, buf);
        ry += g.lineAdvance() + ui(2, 1);
      }
      std::snprintf(buf, sizeof(buf), "SPHERE %d", hud.sphereLevel);
      int tw = g.measureText(buf);
      if (tw > w_ / 3) {
        std::snprintf(buf, sizeof(buf), "S%d", hud.sphereLevel);
        tw = g.measureText(buf);
      }
      g.setTextColor(HUD_TEXT);
      g.drawString(w_ - margin - tw, ry, buf);
      if (!ui_.tiny) {
        tw = g.measureText(WEAPON_NAMES[hud.playerWeapon]);
        g.setTextColor(HUD_DIM);
        g.drawString(w_ - margin - tw, ry + g.lineAdvance() + ui(2, 1),
                     WEAPON_NAMES[hud.playerWeapon]);
      }

      // Hit flash
      if (hud.events & sim::Event::PLAYER_HIT) {
        g.drawRect(0, oy, w_, h_, g2::makeColor(255, 60, 60, 160), ui(3, 1));
      }

      // The flight banners give way to PAUSED
      if (hud.state == sim::GameState::LAUNCH && !hud.paused) {
        // Gone before the camera comes round to the front of the player
        if (hud.stateTimer < sim::LAUNCH_TICKS / 2) {
          setHudFont(g, HudFont::LARGE);
          drawCenteredFit(g, oy + uiY(100), "SPHERE DEVOURED", "DEVOURED",
                          g2::makeColor(255, 230, 120));
          setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
          drawCenteredFit(g, oy + uiY(135), "leaving for a larger world...",
                          "next sphere...", HUD_TEXT);
        }
        // The sphere's tally, for the whole flight: what it earned, the
        // time it took and the bonus for the time left, and the total
        setHudFont(g, HudFont::SMALL);
        int secs = hud.clearTicks / sim::TICK_RATE;
        if (ui_.compact) {
          std::snprintf(buf, sizeof(buf), "+%u  %d:%02d  +%u",
                        (unsigned)hud.sphereScore, secs / 60, secs % 60,
                        (unsigned)hud.clearBonus);
          std::snprintf(alt, sizeof(alt), "%d:%02d +%u", secs / 60, secs % 60,
                        (unsigned)hud.clearBonus);
          drawCenteredFit(g, oy + uiY(190), buf, alt, HUD_TEXT);
        } else {
          int lh = g.lineAdvance() + ui(4, 2);
          int y = oy + uiY(180);
          std::snprintf(buf, sizeof(buf), "SPHERE SCORE %u",
                        (unsigned)hud.sphereScore);
          drawCenteredFit(g, y, buf, nullptr, HUD_TEXT);
          std::snprintf(buf, sizeof(buf), "TIME %d:%02d   BONUS +%u", secs / 60,
                        secs % 60, (unsigned)hud.clearBonus);
          drawCenteredFit(g, y + lh, buf, nullptr, HUD_TEXT);
          std::snprintf(buf, sizeof(buf), "TOTAL %u", (unsigned)hud.score);
          drawCenteredFit(g, y + 2 * lh, buf, nullptr,
                          g2::makeColor(255, 230, 120));
        }
      } else if (hud.state == sim::GameState::PLAYING && hud.timeUp) {
        // The wreck of the timed-out player, before the sphere starts over
        setHudFont(g, HudFont::LARGE);
        drawCenteredFit(g, oy + uiY(100), "TIME UP", nullptr,
                        g2::makeColor(255, 90, 90));
      } else if (hud.state == sim::GameState::ARRIVE && !hud.paused) {
        // From the moment the camera is behind the player (the next sphere
        // in view beyond it) until shortly before the landing
        if (hud.stateTimer >= 2 * sim::ARRIVE_SWITCH_TICKS &&
            hud.stateTimer < sim::ARRIVE_TICKS - sim::TICK_RATE / 2) {
          setHudFont(g, HudFont::LARGE);
          std::snprintf(buf, sizeof(buf), "SPHERE %d", hud.sphereLevel);
          drawCenteredFit(g, oy + uiY(100), buf, nullptr, HUD_TEXT);
        }
      } else if (hud.state == sim::GameState::DEAD) {
        setHudFont(g, HudFont::LARGE);
        drawCenteredFit(g, oy + uiY(100), "YOU WERE DEVOURED", "DEVOURED",
                        g2::makeColor(255, 90, 90));
        setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
        std::snprintf(buf, sizeof(buf), "SCORE %u", (unsigned)hud.score);
        std::snprintf(alt, sizeof(alt), "%u", (unsigned)hud.score);
        drawCenteredFit(g, oy + uiY(135), buf, alt, HUD_TEXT);
        formatHighScore(buf, sizeof(buf), alt, sizeof(alt), hud);
        drawCenteredFit(g, oy + uiY(152), buf, alt,
                        hud.score >= hud.highScore && hud.score > 0
                            ? g2::makeColor(255, 230, 120)
                            : HUD_DIM);
        setHudFont(g, HudFont::SMALL);
        std::snprintf(buf, sizeof(buf), "spheres devoured: %d",
                      hud.spheresCleared);
        std::snprintf(alt, sizeof(alt), "spheres: %d", hud.spheresCleared);
        drawCenteredFit(g, oy + uiY(172), buf, alt, HUD_DIM);
        if (hud.stateTimer > 2 * sim::TICK_RATE && blinkOn) {
          setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
          drawCenteredFit(g, oy + uiY(200), "PRESS A", nullptr, HUD_TEXT);
        }
      } else if (hud.stateTimer < 3 * sim::TICK_RATE && hud.sphereLevel == 1 &&
                 hud.spheresCleared == 0) {
        setHudFont(g, ui_.compact ? HudFont::SMALL : HudFont::MEDIUM);
        drawCenteredFit(g, oy + uiY(240),
                        "devour the fragments, become the largest",
                        "devour the fragments", HUD_DIM);
      }
      if (hud.paused) {
        // Over whatever the state shows: the scene stands still behind it
        setHudFont(g, HudFont::LARGE);
        drawCenteredFit(g, oy + uiY(100), "PAUSED", nullptr, HUD_TEXT);
        setHudFont(g, HudFont::SMALL);
        if (hud.muted) {
          drawCenteredFit(g, oy + uiY(135), "SOUND OFF", nullptr,
                          g2::makeColor(255, 120, 120));
        }
        int lineH = g.lineAdvance() + ui(6, 2);
        drawCenteredFit(g, oy + h_ - margin - lineH, hints_.pause,
                        hints_.pauseAlt, HUD_DIM);
      }
      break;
    }
  }
}

}  // namespace devoursphere::render
