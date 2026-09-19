// When the high score reaches the flash. Shared by the Xiamocon and the
// PicoSystem front ends; the flash itself is behind ds_platform.hpp
// (readHighScoreRecord / writeHighScoreRecord) and the bytes are
// core/include/devoursphere/sim/high_score_record.hpp.
//
// A write stalls both cores for up to a few hundred milliseconds (the sector
// erase) and, on the RP2 chips, takes the flash away from the sound DMA. So
// it happens once per run: on the game over screen, once the death sound has
// ended -- or, if the player leaves that screen (or the power goes) with a
// sound still playing, right then, cutting it. The title demo never scores
// (Game::keepHighScore), so a pending record can only come from a run.
#pragma once

#include <cstdint>

#include "devoursphere/sim/game.hpp"
#include "devoursphere/sim/high_score_record.hpp"
#include "ds_platform.hpp"
#include "se_player.hpp"

namespace ds {

class HighScoreStore {
 public:
  // At start-up, before the Game is shared with the other core
  void load(devoursphere::sim::Game &g) {
    uint8_t rec[devoursphere::sim::HIGH_SCORE_RECORD_BYTES];
    uint32_t score = 0;
    int sphere = 0;
    if (readHighScoreRecord(rec) &&
        devoursphere::sim::decodeHighScoreRecord(rec, &score, &sphere)) {
      g.setHighScore(score, sphere);
      stored_ = score;
    }
  }

  // Call whenever the Game is this core's (between batches), after
  // Game::keepHighScore(). True when it wrote.
  bool poll(devoursphere::sim::Game &g) {
    using devoursphere::sim::GameState;
    if (!pending(g)) return false;
    switch (g.state()) {
      case GameState::DEAD:
        if (audio::playing()) return false;  // let the death sound end
        break;
      case GameState::TITLE:
      case GameState::WEAPON_SELECT:
        break;  // left the game over screen before the sound ended
      default:
        return false;  // the run goes on
    }
    return write(g);
  }

  // Power off: write whatever is pending, cutting a sound
  bool flush(devoursphere::sim::Game &g) { return pending(g) && write(g); }

 private:
  // The score in the flash (0: none) -- or the one given up on after a
  // failed write: one stall per run, not one per frame
  uint32_t stored_ = 0;

  bool pending(const devoursphere::sim::Game &g) const {
    return g.highScore() > stored_;
  }

  bool write(devoursphere::sim::Game &g) {
    uint8_t rec[devoursphere::sim::HIGH_SCORE_RECORD_BYTES];
    devoursphere::sim::encodeHighScoreRecord(rec, g.highScore(),
                                             g.highScoreSphere());
    // The RP2 DMA reads the pack from the flash the write takes away
    audio::stop();
    const bool ok = writeHighScoreRecord(rec);
    stored_ = g.highScore();
    return ok;
  }
};

}  // namespace ds
