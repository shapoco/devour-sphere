// HUD and menu screens (2D text over the scene).

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

void Renderer::drawCenteredText(g2::Graphics2D &g, int y, const char *text,
                                g2::Color color) {
  int tw = g.measureText(text);
  int x = (w_ - tw) / 2;
  g.setTextColor(HUD_SHADOW);
  g.drawString(x + 1, y + 1, text);
  g.setTextColor(color);
  g.drawString(x, y, text);
}

// Icons of the upgrade kinds, centered at (cx, cy), about 14 px tall
void drawUpgradeIcon(g2::Graphics2D &g, int kind, int cx, int cy, g2::Color c) {
  switch ((sim::UpgradeKind)kind) {
    case sim::UpgradeKind::SHIELD: {  // tall diamond
      const g2::vec2i pts[4] = {
          {cx, cy - 7}, {cx + 4, cy}, {cx, cy + 7}, {cx - 4, cy}};
      g.fillPolygon(pts, 4, c);
      break;
    }
    case sim::UpgradeKind::OVERDRIVE:  // triangle
      g.fillTriangle(cx, cy - 6, cx + 6, cy + 5, cx - 6, cy + 5, c);
      break;
    case sim::UpgradeKind::THRUSTER: {  // arrowhead (chevron)
      const g2::vec2i pts[5] = {{cx, cy - 7},
                                {cx + 6, cy + 6},
                                {cx, cy + 2},
                                {cx - 6, cy + 6},
                                {cx, cy - 7}};
      g.fillPolygon(pts, 5, c);
      break;
    }
    case sim::UpgradeKind::EXTRA_CORE: {  // four-pointed star
      const g2::vec2i pts[8] = {
          {cx, cy - 7}, {cx + 2, cy - 2}, {cx + 7, cy}, {cx + 2, cy + 2},
          {cx, cy + 7}, {cx - 2, cy + 2}, {cx - 7, cy}, {cx - 2, cy - 2}};
      g.fillPolygon(pts, 8, c);
      break;
    }
    default: break;
  }
}

void Renderer::drawUpgradeStatus(g2::Graphics2D &g, int oy) {
  const sim::Game &game = *game_;
  int y = oy + h_ - 14;
  int x = 14;
  for (int k = 1; k <= sim::UPGRADE_KINDS; k++) {
    int level = game.upgradeLevel((sim::UpgradeKind)k);
    g2::Color c = upgradeColor(k);
    g2::Color dim =
        g2::makeColor(g2::colorR(c) / 3, g2::colorG(c) / 3, g2::colorB(c) / 3);
    drawUpgradeIcon(g, k, x, y, level > 0 ? c : dim);
    for (int i = 0; i < sim::UPGRADE_MAX_LEVEL; i++) {
      int px = x + 11 + i * 6;
      if (i < level) {
        g.fillRect(px, y - 3, 4, 7, c);
      } else {
        g.drawRect(px, y - 3, 4, 7, dim);
      }
    }
    x += 44;
  }
  // Spare cores
  int cx = w_ - 14;
  for (int i = 0; i < sim::CORES_MAX; i++) {
    g2::Color c = upgradeColor((int)sim::UpgradeKind::EXTRA_CORE);
    if (i >= game.cores())
      c = g2::makeColor(g2::colorR(c) / 3, g2::colorG(c) / 3,
                        g2::colorB(c) / 3);
    drawUpgradeIcon(g, (int)sim::UpgradeKind::EXTRA_CORE, cx, y, c);
    cx -= 18;
  }
}

void Renderer::drawHud(g2::Graphics2D &g, int oy) {
  const sim::Game &game = *game_;
  const sim::Entity &p = game.player();
  char buf[64];
  uint32_t t = game.tickCount();
  bool blinkOn = (t / (sim::TICK_RATE / 2)) & 1;

  switch (game.state()) {
    case sim::GameState::TITLE: {
      g.setFont(&ShapoSansP_s21c16a01w03, 2);
      drawCenteredText(g, oy + 70, "DEVOUR SPHERE",
                       g2::makeColor(120, 255, 235));
      g.setFont(&ShapoSansP_s12c09a01w02);
      drawCenteredText(g, oy + 130, "grow by devouring, become the largest",
                       HUD_DIM);
      if (blinkOn) drawCenteredText(g, oy + 200, "PRESS A TO START", HUD_TEXT);
      if (game.highScore() > 0) {
        g.setFont(&ShapoSansP_s08c07);
        std::snprintf(buf, sizeof(buf), "HIGH SCORE %u", game.highScore());
        drawCenteredText(g, oy + 236, buf, HUD_DIM);
      }
      g.setFont(&ShapoSansP_s08c07);
      drawCenteredText(g, oy + 280, "MOVE: ARROWS / WASD    A: SPACE / IJKL",
                       HUD_DIM);
      drawCenteredText(g, oy + 296, "UP: DASH (costs health)   DOWN: BRAKE",
                       HUD_DIM);
      break;
    }
    case sim::GameState::WEAPON_SELECT: {
      g.setFont(&ShapoSansP_s21c16a01w03);
      drawCenteredText(g, oy + 50, "SELECT WEAPON", HUD_TEXT);
      int sel = game.selectedWeapon();
      int colW = w_ / sim::WEAPON_COUNT;
      for (int i = 0; i < sim::WEAPON_COUNT; i++) {
        int cx = colW * i + colW / 2;
        bool on = i == sel;
        g.setFont(&ShapoSansP_s12c09a01w02);
        int tw = g.measureText(WEAPON_NAMES[i]);
        g2::Color c = on ? g2::makeColor(255, 230, 120) : HUD_DIM;
        if (on) {
          g.drawRect(cx - tw / 2 - 10, oy + 130, tw + 20, 24, c);
        }
        g.setTextColor(c);
        g.drawString(cx - tw / 2, oy + 136, WEAPON_NAMES[i]);
        g.setFont(&ShapoSansP_s08c07);
        tw = g.measureText(WEAPON_DESCS[i]);
        g.setTextColor(on ? HUD_TEXT : HUD_DIM);
        g.drawString(cx - tw / 2, oy + 162, WEAPON_DESCS[i]);
      }
      g.setFont(&ShapoSansP_s08c07);
      drawCenteredText(g, oy + 220, "LEFT / RIGHT: choose    A: confirm",
                       HUD_DIM);
      break;
    }
    case sim::GameState::PLAYING:
    case sim::GameState::LAUNCH:
    case sim::GameState::DEAD: {
      // Health gauge
      const int gx = 8, gy = oy + 8, gw = 120, gh = 8;
      g.drawRect(gx - 1, gy - 1, gw + 2, gh + 2, HUD_DIM);
      int fill = p.hpMax > 0 ? (int)((int64_t)p.hp * gw / p.hpMax) : 0;
      if (fill < 0) fill = 0;
      g2::Color hpColor = fill > gw / 2
                              ? g2::makeColor(90, 230, 140)
                              : (fill > gw / 5 ? g2::makeColor(240, 200, 60)
                                               : g2::makeColor(240, 70, 60));
      if (fill > 0) g.fillRect(gx, gy, fill, gh, hpColor);

      // Charge gauge (Lance / Ram) under the health gauge
      {
        const int cy = gy + gh + 4, ch = 4;
        bool hasCharge = game.upgradeLevel(sim::UpgradeKind::OVERDRIVE) >=
                             sim::UPGRADE_MAX_LEVEL ||
                         game.upgradeLevel(sim::UpgradeKind::THRUSTER) >=
                             sim::UPGRADE_MAX_LEVEL;
        sim::ChargeState cs = game.chargeState();
        if (hasCharge || cs != sim::ChargeState::IDLE) {
          g.drawRect(gx - 1, cy - 1, gw + 2, ch + 2, HUD_DIM);
          int cf = gw * game.chargeGauge() / 256;
          g2::Color cc = g2::makeColor(110, 200, 255);
          switch (cs) {
            case sim::ChargeState::READY_LANCE:
            case sim::ChargeState::READY_RAM:
            case sim::ChargeState::RAM_WINDOW:
              cc = blinkOn ? g2::makeColor(255, 255, 255)
                           : g2::makeColor(170, 220, 255);
              break;
            case sim::ChargeState::LANCE:
            case sim::ChargeState::RAM:
              cc = g2::makeColor(255, 240, 120);
              break;
            case sim::ChargeState::COOLDOWN:
              cc = g2::makeColor(255, 140, 60);
              break;
            default: break;
          }
          if (cf > 0) g.fillRect(gx, cy, cf, ch, cc);
        }
      }

      // Score (top center)
      g.setFont(&ShapoSansP_s12c09a01w02);
      std::snprintf(buf, sizeof(buf), "%u", game.score());
      {
        int tw = g.measureText(buf);
        g.setTextColor(HUD_SHADOW);
        g.drawString((w_ - tw) / 2 + 1, oy + 9, buf);
        g.setTextColor(HUD_TEXT);
        g.drawString((w_ - tw) / 2, oy + 8, buf);
      }

      // Rank and size
      std::snprintf(buf, sizeof(buf), "RANK %d / %d", game.playerRank(),
                    game.aliveEntities());
      g.setTextColor(HUD_SHADOW);
      g.drawString(gx + 1, gy + gh + 13, buf);
      g.setTextColor(game.playerRank() == 1 ? g2::makeColor(255, 230, 120)
                                            : HUD_TEXT);
      g.drawString(gx, gy + gh + 12, buf);

      // Upgrades (bottom left): icon + level pips; spare cores (bottom right)
      drawUpgradeStatus(g, oy);
      g.setFont(&ShapoSansP_s08c07);

      // Sphere and weapon (top right)
      std::snprintf(buf, sizeof(buf), "SPHERE %d", game.sphereLevel());
      int tw = g.measureText(buf);
      g.setTextColor(HUD_TEXT);
      g.drawString(w_ - 8 - tw, oy + 8, buf);
      tw = g.measureText(WEAPON_NAMES[(int)p.weapon]);
      g.setTextColor(HUD_DIM);
      g.drawString(w_ - 8 - tw, oy + 20, WEAPON_NAMES[(int)p.weapon]);

      // Hit flash
      if (game.events() & sim::Event::PLAYER_HIT) {
        g.drawRect(0, oy, w_, h_, g2::makeColor(255, 60, 60, 160), 3);
      }

      if (game.state() == sim::GameState::LAUNCH) {
        g.setFont(&ShapoSansP_s21c16a01w03);
        drawCenteredText(g, oy + 100, "SPHERE DEVOURED",
                         g2::makeColor(255, 230, 120));
        g.setFont(&ShapoSansP_s12c09a01w02);
        drawCenteredText(g, oy + 135, "leaving for a larger world...",
                         HUD_TEXT);
      } else if (game.state() == sim::GameState::DEAD) {
        g.setFont(&ShapoSansP_s21c16a01w03);
        drawCenteredText(g, oy + 100, "YOU WERE DEVOURED",
                         g2::makeColor(255, 90, 90));
        g.setFont(&ShapoSansP_s12c09a01w02);
        std::snprintf(buf, sizeof(buf), "SCORE %u", game.score());
        drawCenteredText(g, oy + 135, buf, HUD_TEXT);
        std::snprintf(buf, sizeof(buf), "HIGH SCORE %u", game.highScore());
        drawCenteredText(g, oy + 152, buf,
                         game.score() >= game.highScore() && game.score() > 0
                             ? g2::makeColor(255, 230, 120)
                             : HUD_DIM);
        g.setFont(&ShapoSansP_s08c07);
        std::snprintf(buf, sizeof(buf), "spheres devoured: %d",
                      game.spheresCleared());
        drawCenteredText(g, oy + 172, buf, HUD_DIM);
        if (game.stateTimer() > 2 * sim::TICK_RATE && blinkOn) {
          drawCenteredText(g, oy + 200, "PRESS A", HUD_TEXT);
        }
      } else if (game.stateTimer() < 3 * sim::TICK_RATE &&
                 game.sphereLevel() == 1 && game.spheresCleared() == 0) {
        g.setFont(&ShapoSansP_s12c09a01w02);
        drawCenteredText(g, oy + 240,
                         "devour the fragments, become the largest", HUD_DIM);
      }
      break;
    }
  }
}

}  // namespace devoursphere::render
