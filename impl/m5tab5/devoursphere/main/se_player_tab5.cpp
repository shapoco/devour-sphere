// Sound effects on the Tab5 (the audio:: interface of
// impl/xiamocon/devoursphere/include/se_player.hpp, shared with the
// handhelds so that high_score_store.hpp works the same here).
//
// The handhelds have one voice and a priority table: an RP2 plays a sound by
// pointing a DMA channel at the pack, and an ESP32S3 by feeding one cursor
// into an I2S ring, so a second sound would have to take the first one's
// place. This board has neither constraint -- M5Unified's speaker mixes
// eight channels out of an ES8388 -- so every sound a tick asks for is
// played, which is what the browser does and what the game was tuned
// against. There is no priority table here for the same reason: nothing has
// to lose.
//
// The pack is docs/play/se.bin, the browser's own (signed 16-bit PCM at
// 24 kHz), linked into flash by the build (main/CMakeLists.txt). It is
// memory mapped, so playRaw() can read it in place.

#include "se_player.hpp"

#include <M5Unified.h>

#include "devoursphere/sim/game.hpp"
#include "ds_platform.hpp"

// docs/play/se.bin, embedded by main/CMakeLists.txt
extern const uint8_t g_sePack[] asm("_binary_se_bin_start");
extern const uint8_t g_sePackEnd[] asm("_binary_se_bin_end");

namespace audio {
namespace {

namespace sim = devoursphere::sim;

struct Entry {
  uint32_t first;  // first sample of this sound in the pack
  uint32_t count;
};

const Entry *g_toc = nullptr;
const int16_t *g_samples = nullptr;
uint32_t g_rate = 0, g_count = 0;
bool g_muted = false;

}  // namespace

void init(const Config &) {
  // The Config fields describe an RP2's PWM pin and pacing; nothing here
  // needs them. M5.begin() has already brought the speaker up.
  const uint8_t *p = g_sePack;
  const size_t bytes = (size_t)(g_sePackEnd - g_sePack);
  if (bytes < 16 || p[0] != 'D' || p[1] != 'S' || p[2] != 'S' || p[3] != 'E') {
    ds::trace("sound pack not recognised", (uint32_t)bytes);
    return;
  }
  auto u32 = [&](int off) {
    return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) |
           ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
  };
  g_rate = u32(4);
  g_count = u32(8);
  if (g_count == 0 || g_count > sim::SOUND_KINDS) {
    ds::trace("sound pack has an odd count", g_count);
    g_count = 0;
    return;
  }
  g_toc = reinterpret_cast<const Entry *>(p + 16);
  g_samples = reinterpret_cast<const int16_t *>(p + 16 + g_count * sizeof(Entry));
  ds::trace("sound pack rate", g_rate);
  ds::trace("sound pack sounds", g_count);
}

void request(uint32_t bits) {
  if (g_muted || !g_toc || !bits) return;
  for (uint32_t k = 0; k < g_count; k++) {
    if (!(bits & (1u << k))) continue;
    const Entry &e = g_toc[k];
    // channel -1: the speaker takes the highest free one and returns false
    // when all eight are busy, which is the right thing to do -- dropping
    // the ninth simultaneous sound is inaudible next to the eight playing.
    M5.Speaker.playRaw(g_samples + e.first, e.count, g_rate, false, 1, -1,
                       false);
  }
}

void setMuted(bool muted) {
  if (muted == g_muted) return;
  g_muted = muted;
  if (muted) M5.Speaker.stop();
}

bool playing() { return M5.Speaker.isPlaying(); }

void stop() { M5.Speaker.stop(); }

}  // namespace audio
