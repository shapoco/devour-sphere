// HUD and menu screens (2D text over the scene).
//
// Nothing here is in absolute pixels: the layout is written against the
// reference screen (UI_REF_W x UI_REF_H) and mapped onto the real frame
// buffer. Distances that should keep their apparent size (margins, the
// gauge, icons) go through ui(); vertical positions of the full-width
// overlays go through uiY(), so they follow the aspect ratio. Text that
// still does not fit is replaced by a shorter version or dropped.
//
// Vertical anchoring goes through hudY0() / hudY1(), and the bottom row --
// the upgrade icons, the spare cores, the version and the hint lines --
// through barX0() / barX1(), so that a platform that draws a virtual pad in
// the bottom corners can keep the HUD out of it (HudInsets). With no insets,
// which is every front end but the M5Tab5 one, those are the frame's own
// edges and nothing about the layout changes.

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
// The drop shadow under HUD text. Opaque where blending is suppressed: a
// translucent glyph reads the frame back pixel by pixel, which on a slow
// board costs more than the softer edge is worth.
static const g2::Color HUD_SHADOW =
    DEVOURSPHERE_SUPPRESS_ALPHA ? g2::makeColor(0, 0, 0)
                                : g2::makeColor(0, 0, 0, 180);
// The bounty holders' body color (PAL_ENEMY_BOUNTY), for the prompt to kill them
static const g2::Color BOUNTY_TEXT = g2::makeColor(255, 195, 60);

int textWidth(const g2::Graphics2D &g, const char *text, int scale) {
  return g.textMetrics(text).width * scale;
}

int textLineAdvance(const g2::Graphics2D &g, int scale) {
  return g.textMetrics("").lineAdvance * scale;
}

int textLineHeight(const g2::Graphics2D &g, int scale) {
  return g.textMetrics("").height * scale;
}

void drawText(g2::Graphics2D &g, int x, int y, const char *text, int scale) {
  if (scale == 1) {
    g.drawString(x, y, text);
    return;
  }
  g.setTransform({(float)scale, 0, 0, (float)scale, (float)x, (float)y});
  if (g.transformKind() == g2::TransformKind::IDENTITY) {
    // Built with SHAPOGFX2D_TRANSFORM=0: at least in the right place
    g.drawString(x, y, text);
    return;
  }
  g.drawString(0, 0, text);
  g.resetTransform();
}

void Renderer::setHudFont(g2::Graphics2D &g, const GFXfont *font,
                          int scale) const {
  g.setFont(font);
  textScale_ = scale;
}

// The three fonts are 8, 12 and 21 px tall and are magnified by an integer
// factor. On a screen below half the reference every role drops a step, so
// that a line of text never eats the whole width.
void Renderer::setHudFont(g2::Graphics2D &g, HudFont role) const {
  const int m = ui_.fontMult;
  switch (role) {
    case HudFont::SMALL: setHudFont(g, &ShapoSansP_s08c07, m); break;
    case HudFont::MEDIUM:
      if (ui_.tiny)
        setHudFont(g, &ShapoSansP_s08c07, m);
      else
        setHudFont(g, &ShapoSansP_s12c09a01w02, m);
      break;
    case HudFont::LARGE:
      if (ui_.tiny)
        setHudFont(g, &ShapoSansP_s08c07, m);
      else
        setHudFont(g, &ShapoSansP_s21c16a01w03, m);
      break;
    case HudFont::TITLE:
      if (ui_.tiny)
        setHudFont(g, &ShapoSansP_s12c09a01w02, m);
      else
        setHudFont(g, &ShapoSansP_s21c16a01w03, ui_.compact ? m : 2 * m);
      break;
  }
}

int Renderer::textFits(g2::Graphics2D &g, const char *text) const {
  return textFitsIn(g, text, w_);
}

int Renderer::textFitsIn(g2::Graphics2D &g, const char *text, int width) const {
  return textW(g, text) <= width - 2 * ui_.margin;
}

void Renderer::drawCenteredText(g2::Graphics2D &g, int y, const char *text,
                                g2::Color color) {
  int tw = textW(g, text);
  int x = (w_ - tw) / 2;
  int sh = ui_.fontMult;
  g.setTextColor(HUD_SHADOW);
  putText(g, x + sh, y + sh, text);
  g.setTextColor(color);
  putText(g, x, y, text);
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

// The bottom row's own version: centered between the pads rather than on the
// frame, and measured against that narrower span
const char *Renderer::drawBottomFit(g2::Graphics2D &g, int y, const char *text,
                                    const char *alt, g2::Color color) {
  const char *pick = nullptr;
  if (text && textFitsIn(g, text, barW())) {
    pick = text;
  } else if (alt && textFitsIn(g, alt, barW())) {
    pick = alt;
  }
  if (!pick) return nullptr;
  int tw = textW(g, pick);
  int x = barX0() + (barW() - tw) / 2;
  int sh = ui_.fontMult;
  g.setTextColor(HUD_SHADOW);
  putText(g, x + sh, y + sh, pick);
  g.setTextColor(color);
  putText(g, x, y, pick);
  return pick;
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
  const int y = oy + hudY1() - ui(14, half + 2);
  const int gap = ui(3, 1);  // icon to pips
  int pitch = half + gap + pipPitch * sim::UPGRADE_MAX_LEVEL + ui(10, 3);
  if (pitch < ui(44)) pitch = ui(44);
  int x = barX0() + ui(14, half + 2);
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
  int cx = barX1() - ui(14, half + 2);
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
      int lineH = lineAdv(g) + ui(6, 2);
      // The two hint lines, above the version line at the bottom
      drawBottomFit(g, oy + hudY1() - margin - 3 * lineH, hints_.move,
                    hints_.moveAlt, HUD_DIM);
      drawBottomFit(g, oy + hudY1() - margin - 2 * lineH, hints_.dash,
                    hints_.dashAlt, HUD_DIM);
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
      int nameH = lineBox(g);
      int widest = 0;
      for (int i = 0; i < sim::WEAPON_COUNT; i++) {
        int tw = textW(g, WEAPON_NAMES[i]);
        if (tw > widest) widest = tw;
      }
      if (widest + ui(20, 6) > colW) {  // try the small font first
        setHudFont(g, HudFont::SMALL);
        nameH = lineBox(g);
        widest = 0;
        for (int i = 0; i < sim::WEAPON_COUNT; i++) {
          int tw = textW(g, WEAPON_NAMES[i]);
          if (tw > widest) widest = tw;
        }
      }
      const bool stacked = widest + ui(20, 6) > colW;
      // The box wraps the line box of the name with an even padding, so it
      // fits the text at every font size instead of following its own scale
      const int padX = ui(10, 3), padY = ui(6, 2);
      const int boxH = nameH + 2 * padY;
      const int rowH = boxH + ui(4, 2);  // stacked: one row per weapon
      const int top = hudY0() + (hudH() - rowH * sim::WEAPON_COUNT) / 2;
      for (int i = 0; i < sim::WEAPON_COUNT; i++) {
        int cx = stacked ? w_ / 2 : colW * i + colW / 2;
        int textY = stacked ? oy + top + rowH * i + padY : oy + uiY(136);
        bool on = i == sel;
        g2::Color c = on ? g2::makeColor(255, 230, 120) : HUD_DIM;
        int tw = textW(g, WEAPON_NAMES[i]);
        if (on) {
          g.drawRect(cx - tw / 2 - padX, textY - padY, tw + 2 * padX, boxH, c);
        }
        g.setTextColor(c);
        putText(g, cx - tw / 2, textY, WEAPON_NAMES[i]);
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
        if (textW(g, WEAPON_DESCS[i]) > descRoom) descFit = false;
      }
      for (int i = 0; descFit && i < sim::WEAPON_COUNT; i++) {
        if (stacked && i != sel) continue;
        int tw = textW(g, WEAPON_DESCS[i]);
        int cx = stacked ? w_ / 2 : colW * i + colW / 2;
        g.setTextColor(i == sel ? HUD_TEXT : HUD_DIM);
        putText(g, cx - tw / 2, descY, WEAPON_DESCS[i]);
      }
      // Either axis chooses, so the hint names the one that matches the
      // layout. Stacked it lands on the bottom row, next to the pad.
      if (stacked) {
        drawBottomFit(g, oy + hudY1() - margin - lineBox(g),
                      "UP / DOWN: choose    A: confirm", "A: confirm", HUD_DIM);
      } else {
        drawCenteredFit(g, oy + uiY(220), "LEFT / RIGHT: choose    A: confirm",
                        "A: confirm", HUD_DIM);
      }
      break;
    }
    case sim::GameState::PLAYING:
    case sim::GameState::LAUNCH:
    case sim::GameState::ARRIVE:
    case sim::GameState::DEAD: {
      if (hudBandIdle(g, oy)) return;  // nothing of the HUD in this band
      // Health gauge (top left)
      const int gx = margin, gy = oy + hudY0() + margin;
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
        int tw = textW(g, buf);
        int sh = ui_.fontMult;
        g.setTextColor(HUD_SHADOW);
        putText(g, (w_ - tw) / 2 + sh, gy + sh, buf);
        g.setTextColor(HUD_TEXT);
        putText(g, (w_ - tw) / 2, gy, buf);
      }
      if (hud.debugMode) {
        // Cheats were used: say so over the score, large and translucent,
        // so that a screenshot cannot pass for a real one
        setHudFont(g, HudFont::LARGE);
        drawCenteredFit(g, gy - ui(2, 1), "DEBUG MODE", "DEBUG",
                        g2::makeColor(255, 70, 70, 150));
      }

      // Rank, under the gauge (same size as the score). On top with
      // bounties still out, the line says what is left to do instead, in
      // the bounty holders' yellow and blinking (1 s period); once they are
      // all dead (the grace before LAUNCH) the rank is back, blinking
      const bool onTop = hud.state == sim::GameState::PLAYING &&
                         hud.playerAlive && hud.playerRank == 1;
      const bool hunting = onTop && hud.bountyCount > 0;
      const bool cleared = onTop && hud.bountyCount == 0;
      g2::Color rankColor = HUD_TEXT;
      if (hunting) {
        if (hud.bountyCount == 1) {
          std::snprintf(buf, sizeof(buf), "KILL THE BOUNTY");
        } else {
          std::snprintf(buf, sizeof(buf), "KILL %d BOUNTIES", hud.bountyCount);
        }
        if (!textFits(g, buf)) {
          std::snprintf(buf, sizeof(buf), "BOUNTY x%d", hud.bountyCount);
        }
        rankColor = BOUNTY_TEXT;
      } else {
        std::snprintf(buf, sizeof(buf), "RANK %d / %d", hud.playerRank,
                      hud.aliveEntities);
        if (!textFits(g, buf)) {
          std::snprintf(buf, sizeof(buf), "%d/%d", hud.playerRank,
                        hud.aliveEntities);
        }
        if (cleared) rankColor = g2::makeColor(255, 230, 120);
      }
      if (!(hunting || cleared) || blinkOn) {
        int ry = gy + gh + ui(12, 4);
        int sh = ui_.fontMult;
        g.setTextColor(HUD_SHADOW);
        putText(g, gx + sh, ry + sh, buf);
        g.setTextColor(rankColor);
        putText(g, gx, ry, buf);
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
        int tw = textW(g, buf);
        g.setTextColor(tc);
        putText(g, w_ - margin - tw, ry, buf);
        ry += lineAdv(g) + ui(2, 1);
      }
      std::snprintf(buf, sizeof(buf), "SPHERE %d", hud.sphereLevel);
      int tw = textW(g, buf);
      if (tw > w_ / 3) {
        std::snprintf(buf, sizeof(buf), "S%d", hud.sphereLevel);
        tw = textW(g, buf);
      }
      g.setTextColor(HUD_TEXT);
      putText(g, w_ - margin - tw, ry, buf);
      if (!ui_.tiny) {
        tw = textW(g, WEAPON_NAMES[hud.playerWeapon]);
        g.setTextColor(HUD_DIM);
        putText(g, w_ - margin - tw, ry + lineAdv(g) + ui(2, 1),
                     WEAPON_NAMES[hud.playerWeapon]);
      }

      // Hit flash. The one thing here that frames the whole picture rather
      // than the HUD's safe area: it is an effect on the scene, and a border
      // inset from the edges would read as a box drawn over the game.
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
          int lh = lineAdv(g) + ui(4, 2);
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
        int lineH = lineAdv(g) + ui(6, 2);
        drawBottomFit(g, oy + hudY1() - margin - 2 * lineH, hints_.pause,
                      hints_.pauseAlt, HUD_DIM);
      }
      break;
    }
  }
  drawVersion(g, oy);
}

// The rows the HUD may touch while playing are the top strip (the gauge,
// the score, the rank line, the time / sphere / weapon lines) and the
// bottom strip (the upgrade status, the version line). Anything else --
// the hit flash border, the banners of the flight and the game over, the
// pause screen, the opening hint, the cheat notice -- is a state this
// answers false for, so the bounds below only have to cover the two
// strips, with a line of slack each.
bool Renderer::hudBandIdle(g2::Graphics2D &g, int oy) const {
  const HudState &hud = hud_;
  if (hud.state != sim::GameState::PLAYING || hud.paused || hud.timeUp ||
      hud.debugMode || (hud.events & sim::Event::PLAYER_HIT)) {
    return false;
  }
  if (hud.stateTimer < 3 * sim::TICK_RATE && hud.sphereLevel == 1 &&
      hud.spheresCleared == 0) {
    return false;  // the opening hint, near the bottom
  }
  const g2::Rect &clip = g.clipRect();
  const int y0 = clip.y - oy, y1 = clip.bottom() - oy;  // frame rows [y0, y1)
  setHudFont(g, HudFont::MEDIUM);
  const int advM = lineAdv(g);
  setHudFont(g, HudFont::SMALL);
  const int advS = lineAdv(g);
  const int gy = hudY0() + ui_.margin;
  int topEnd = gy + ui_.gaugeH + ui(12, 4) + 2 * advM;  // the rank line
  const int rightEnd = gy + 3 * (advS + ui(2, 1)) + advS;  // the right column
  if (rightEnd > topEnd) topEnd = rightEnd;
  const int half = 7 * ui_.iconScale8 / 8;
  int bottomStart = hudY1() - ui(14, half + 2) - half - ui(7, 3);  // the icons
  const int version = hudY1() - ui_.margin - (advS + ui(6, 2)) - advS;
  if (version < bottomStart) bottomStart = version;
  return y0 >= topEnd && y1 <= bottomStart;
}

// The extent of the upgrade status at the bottom: where the upgrade pips
// end on the left and where the spare cores begin on the right
void Renderer::upgradeStatusExtents(int &leftEnd, int &rightStart) const {
  const int is = ui_.iconScale8;
  const int half = 7 * is / 8;
  const int pipPitch = ui(6, 3);
  const int gap = ui(3, 1);
  int pitch = half + gap + pipPitch * sim::UPGRADE_MAX_LEVEL + ui(10, 3);
  if (pitch < ui(44)) pitch = ui(44);
  leftEnd = barX0() + ui(14, half + 2) + (sim::UPGRADE_KINDS - 1) * pitch +
            ui(11, half + 1) + pipPitch * sim::UPGRADE_MAX_LEVEL;
  int corePitch = half * 2 + ui(4, 2);
  if (corePitch < ui(18)) corePitch = ui(18);
  rightStart =
      barX1() - ui(14, half + 2) - half - (sim::CORES_MAX - 1) * corePitch;
}

// The core's version at the bottom center of every screen (so that it is
// in every screenshot), on the line below the hints; on the screens with
// the upgrade status it is drawn only when it fits between the pips and
// the cores
void Renderer::drawVersion(g2::Graphics2D &g, int oy) {
  const HudState &hud = hud_;
  setHudFont(g, HudFont::SMALL);
  const int vw = textW(g, sim::VERSION_STRING);
  const int lineH = lineAdv(g) + ui(6, 2);
  const int x = barX0() + (barW() - vw) / 2;
  // On a screen size where the upgrade status leaves no room for it (the
  // layout does not depend on the levels: 240x240), the title alone
  int leftEnd, rightStart;
  upgradeStatusExtents(leftEnd, rightStart);
  const bool fits = x >= leftEnd + ui_.margin && x + vw <= rightStart - ui_.margin;
  if (hud.state != sim::GameState::TITLE && !fits) return;
  g.setTextColor(HUD_DIM);
  putText(g, x, oy + hudY1() - ui_.margin - lineH, sim::VERSION_STRING);
}

// The banner (the monospace font, 6 x 10 px a character, magnified like the
// HUD) and the text table of the benchmark's results
static constexpr int TEXT_ADV_Y = 10;

template <typename F>
int Renderer::packHead(const TextTable &t, bool items, g2::Graphics2D &g,
                       int scale, F &&line) const {
  const int width = w_ - 2 * ui_.margin, sep = textWidth(g, "  ", scale);
  int lines = 0, x = 0;
  // The footer (the page and the keys) goes last, where there is room
  for (int i = items ? 0 : t.headCount; i <= t.headCount; i++) {
    const char *item = i < t.headCount ? t.head[i] : t.footer;
    if (!item || !item[0]) continue;
    const int w = textWidth(g, item, scale);
    if (lines == 0 || (x > 0 && x + sep + w > width)) {
      lines++;
      x = 0;
    } else if (x > 0) {
      x += sep;
    }
    line(x, lines - 1, item);
    x += w;
  }
  return lines;
}

// A table laid out in one font: the columns as wide as their widest cell,
// then as many copies side by side as the frame is wide, and the rows that
// fit under the head lines and above the footer
Renderer::TableLayout Renderer::layoutTable(const TextTable &t,
                                            const void *font, int scale) const {
  g2::Graphics2D g;
  const GFXfont *f = (const GFXfont *)font;
  g.setFont(f);
  TableLayout l;
  l.font = font;
  l.scale = scale;
  l.lineH = f->yAdvance * scale;
  l.gap = textWidth(g, " ", scale);
  l.panelGap = 3 * l.gap;
  const int cols = t.columns < 4 ? t.columns : 4;
  l.tableW = 0;
  for (int c = 0; c <= cols; c++) {
    int w = c > 0 ? textWidth(g, t.titles[c - 1], scale) : 0;
    for (int r = 0; r < t.rows; r++) {
      const int cw = textWidth(g, t.cells[r * (1 + t.columns) + c], scale);
      if (cw > w) w = cw;
    }
    if (c == 0) {
      l.labelW = w;
    } else {
      l.colW[c - 1] = w;
    }
    l.tableW += w + (c > 0 ? l.gap : 0);
  }
  const int width = w_ - 2 * ui_.margin;
  const int height = h_ - 2 * ui_.margin;
  l.panels = (width + l.panelGap) / (l.tableW + l.panelGap);
  if (l.panels < 1) l.panels = 1;
  // The first page has the head (with the footer) above the titles, the
  // others only the footer
  auto none = [](int, int, const char *) {};
  l.headLines = packHead(t, true, g, scale, none);
  l.footLines = packHead(t, false, g, scale, none);
  const int lines = height / l.lineH;
  l.rowsFirst = lines - l.headLines - 1;
  l.rowsRest = lines - l.footLines - 1;
  if (l.rowsFirst < 1) l.rowsFirst = 1;
  if (l.rowsRest < 1) l.rowsRest = 1;
  // No more panels than the rows fill
  while (l.panels > 1 && (l.panels - 1) * l.rowsFirst >= t.rows) {
    l.panels--;
  }
  const int first = l.panels * l.rowsFirst, rest = l.panels * l.rowsRest;
  l.pages = t.rows <= first ? 1 : 1 + (t.rows - first + rest - 1) / rest;
  // One page: the panels as even as they can be, not a full one and a stub
  if (l.pages == 1) l.rowsFirst = (t.rows + l.panels - 1) / l.panels;
  l.fits = l.tableW <= width;
  return l;
}

int Renderer::showTable(const TextTable *table, int page) {
  table_ = table;
  tablePage_ = page;
  if (!table) return 0;
  static const GFXfont *const FONTS[] = {
      &ShapoSansMono_s08c07, &ShapoSansP_s07c05a01, &ShapoSansP_s05};
  // Fewest pages among the layouts that fit the width, the biggest font
  // (the first tried) among those; if none fits, the narrowest
  bool first = true;
  for (int scale = ui_.fontMult; scale >= 1; scale--) {
    for (const GFXfont *f : FONTS) {
      TableLayout l = layoutTable(*table, f, scale);
      const bool better =
          first || (l.fits && !tl_.fits) ||
          (l.fits && tl_.fits && l.pages < tl_.pages) ||
          (!l.fits && !tl_.fits && l.tableW < tl_.tableW);
      if (better) tl_ = l;
      first = false;
    }
  }
  if (tablePage_ >= tl_.pages) tablePage_ = tl_.pages - 1;
  return tl_.pages;
}

void Renderer::drawTextScreen(g2::Graphics2D &g, int oy) const {
  const TextTable &t = *table_;
  const TableLayout &l = tl_;
  setHudFont(g, (const GFXfont *)l.font, l.scale);
  const g2::Color headColor = g2::makeColor(150, 255, 170);
  const g2::Color labelColor = g2::makeColor(120, 200, 140);
  const g2::Color valueColor = g2::makeColor(235, 240, 245);
  const g2::Color stripe = g2::makeColor(18, 26, 30);
  const int x0 = ui_.margin;
  g.setTextColor(headColor);
  const bool firstPage = tablePage_ == 0;
  packHead(t, firstPage, g, l.scale, [&](int x, int line, const char *item) {
    putText(g, x0 + x, ui_.margin + line * l.lineH + oy, item);
  });
  const int top =
      ui_.margin + (firstPage ? l.headLines : l.footLines) * l.lineH;
  const int rowsHere = firstPage ? l.rowsFirst : l.rowsRest;
  const int pageStart =
      firstPage ? 0
                : l.panels * (l.rowsFirst + (tablePage_ - 1) * l.rowsRest);
  const int cols = t.columns < 4 ? t.columns : 4;
  // The value columns are right aligned, each after the label and a gap
  auto colRight = [&](int c) {
    int x = l.labelW;
    for (int k = 0; k <= c; k++) x += l.gap + l.colW[k];
    return x;
  };
  for (int p = 0; p < l.panels; p++) {
    const int px = x0 + p * (l.tableW + l.panelGap);
    const int first = pageStart + p * rowsHere;
    if (first >= t.rows) break;
    g.setTextColor(headColor);
    for (int c = 0; c < cols; c++) {
      const char *s = t.titles[c];
      putText(g, px + colRight(c) - textW(g, s), top + oy, s);
    }
    for (int r = 0; r < rowsHere && first + r < t.rows; r++) {
      const int ry = top + (1 + r) * l.lineH;
      const char *const *row = t.cells + (first + r) * (1 + t.columns);
      // Every other row shaded, for the eye to follow a row across
      if (r % 2 == 0) {
        g.fillRect(px - l.gap / 2, ry - 1 + oy, l.tableW + l.gap, l.lineH,
                   stripe);
      }
      g.setTextColor(labelColor);
      putText(g, px, ry + oy, row[0]);
      g.setTextColor(valueColor);
      for (int c = 0; c < cols; c++) {
        const char *s = row[1 + c];
        putText(g, px + colRight(c) - textW(g, s), ry + oy, s);
      }
    }
  }
}

void Renderer::drawBanner(g2::Graphics2D &g, int oy) const {
  const int m = ui_.fontMult;
  setHudFont(g, &ShapoSansMono_s08c07, m);
  const int w = textW(g, banner_), hgt = TEXT_ADV_Y * m;
  const int x = (w_ - w) / 2, y = h_ - ui_.margin - hgt - hgt / 2;
  g.fillRect(x - 2 * m, y - m + oy, w + 4 * m, hgt + m,
             g2::makeColor(0, 0, 0, 190));
  g.setTextColor(g2::makeColor(150, 255, 170));
  putText(g, x, y + oy, banner_);
}

}  // namespace devoursphere::render
