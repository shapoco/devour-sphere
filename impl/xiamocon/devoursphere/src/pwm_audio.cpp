// Sound effects on an RP2040 / RP2350 (see pwm_audio.hpp).
//
// The pack (core/tools/pack_se.py --pwm, linked into .rodata by the build's
// se_data.S) holds every sound as 16-bit PWM levels at one sample rate.
// Playing one is a DMA transfer from the pack in flash into the slice's
// compare register, paced either by the slice's own wrap request (one level
// per PWM period) or by a DMA pacing timer. The CPU does nothing while a
// sound plays. The read address is the uncached, non-allocating alias of the
// flash (XIP_NOCACHE_NOALLOC_BASE): a stream through the cached alias would
// turn the 16 KB XIP cache over every 0.4 s, and both cores already fight
// over it for code.
//
// One voice. A request while a sound plays takes the voice only if its
// priority is not lower, or once the playing sound has held it for HOLD_US
// (a two-second explosion must not mute every shot until it ends). Equal
// priority retriggers. A sound taken over mid-way starts past its leading
// ramp (if the pack has ramps), since the level is already around the middle.

#include "pwm_audio.hpp"

#if defined(ESP32)

namespace audio {
void init(const Config &) {}
void request(uint32_t) {}
void setMuted(bool) {}
bool playing() { return false; }
}  // namespace audio

#else

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/regs/addressmap.h>
#include <pico/stdlib.h>

#include "devoursphere/sim/game.hpp"

extern "C" const uint8_t g_sePack[];
extern "C" const uint8_t g_sePackEnd[];

namespace audio {
namespace {

namespace sim = devoursphere::sim;

// Priority per sim::SoundKind (higher wins; the order of the enum)
constexpr uint8_t PRIORITY[sim::SOUND_KINDS] = {
    0, 0, 0,  // SHOT_VULCAN, SHOT_LASER, SHOT_MISSILE
    0, 1,     // HIT_ENEMY, HIT_PLAYER
    2, 3, 5,  // ENEMY_KILLED_SMALL, ENEMY_KILLED_BIG, PLAYER_KILLED
    1, 3,     // GET_FRAGMENT, GET_UPGRADE
    0, 2,     // MENU_SELECT, MENU_START
    4, 4,     // LAUNCH, ARRIVE
};

// How long a playing sound keeps lower priorities out. After this, any
// request takes the voice.
constexpr uint32_t HOLD_US = 400 * 1000;

struct Header {
  char magic[4];  // "DSPW"
  uint32_t rate;
  uint32_t count;
  uint32_t total;
  uint32_t wrap;
  uint32_t ramp;
};
struct Entry {
  uint32_t first;
  uint32_t count;
};

const Entry *g_toc = nullptr;
const uint16_t *g_levels = nullptr;  // in the uncached flash alias
uint32_t g_count = 0;
uint32_t g_ramp = 0;
uint16_t g_idle = 0;
uint g_slice = 0;
uint g_channel = 0;
int g_dma = -1;
volatile uint16_t *g_cc = nullptr;  // the compare register half of our channel
uint8_t g_curPriority = 0;
uint32_t g_curStartUs = 0;
bool g_ready = false;
bool g_muted = false;

void stop() {
  dma_channel_abort(g_dma);
  *g_cc = g_idle;
}

void start(uint32_t kind, bool skipRamp) {
  const Entry &e = g_toc[kind];
  uint32_t skip = skipRamp ? g_ramp : 0;
  if (skip >= e.count) skip = 0;
  dma_channel_abort(g_dma);
  dma_channel_set_read_addr(g_dma, g_levels + e.first + skip, false);
  dma_channel_set_trans_count(g_dma, e.count - skip, true);
}

}  // namespace

void init(const Config &cfg) {
  const Header *h = reinterpret_cast<const Header *>(g_sePack);
  if (g_sePackEnd - g_sePack < (ptrdiff_t)sizeof(Header)) return;
  if (h->magic[0] != 'D' || h->magic[1] != 'S' || h->magic[2] != 'P' ||
      h->magic[3] != 'W') {
    return;
  }
  g_count = h->count;
  g_ramp = h->ramp;
  g_toc = reinterpret_cast<const Entry *>(g_sePack + sizeof(Header));
  const uint8_t *levels =
      g_sePack + sizeof(Header) + g_count * sizeof(Entry);
  // The same bytes through the uncached, non-allocating window
  g_levels = reinterpret_cast<const uint16_t *>(
      (uintptr_t)levels - XIP_BASE + XIP_NOCACHE_NOALLOC_BASE);
  g_idle = cfg.idleAtMiddle ? (uint16_t)((h->wrap + 1) / 2) : 0;

  // The PWM: the pack's wrap, at the system clock. With PWM_WRAP pacing one
  // period is one sample (the pack was built for this clock); with a DMA
  // timer the period is just the carrier.
  const uint pin = (uint)cfg.pin;
  g_slice = pwm_gpio_to_slice_num(pin);
  g_channel = pwm_gpio_to_channel(pin);
  gpio_set_function(pin, GPIO_FUNC_PWM);
  pwm_config c = pwm_get_default_config();
  pwm_config_set_clkdiv_int(&c, 1);
  pwm_config_set_wrap(&c, (uint16_t)h->wrap);
  pwm_init(g_slice, &c, false);
  pwm_set_chan_level(g_slice, g_channel, g_idle);
  pwm_set_enabled(g_slice, true);
  // The compare register holds A in the low half and B in the high half
  g_cc = reinterpret_cast<volatile uint16_t *>(&pwm_hw->slice[g_slice].cc) +
         g_channel;

  // The pacing request
  uint dreq;
  if (cfg.pacing == Pacing::PWM_WRAP) {
    dreq = pwm_get_dreq(g_slice);
  } else {
    // sysclk x 1 / n: 250 MHz / 22050 Hz = 11338, well within 16 bits
    int timer = dma_claim_unused_timer(true);
    uint32_t n = clock_get_hz(clk_sys) / h->rate;
    if (n > 65535) n = 65535;
    dma_timer_set_fraction((uint)timer, 1, (uint16_t)n);
    dreq = dma_get_timer_dreq(timer);
  }

  // The DMA: 16-bit levels, flash to the one register, one per request
  g_dma = dma_claim_unused_channel(true);
  dma_channel_config dc = dma_channel_get_default_config(g_dma);
  channel_config_set_transfer_data_size(&dc, DMA_SIZE_16);
  channel_config_set_read_increment(&dc, true);
  channel_config_set_write_increment(&dc, false);
  channel_config_set_dreq(&dc, dreq);
  dma_channel_configure(g_dma, &dc, g_cc, g_levels, 0, false);
  g_ready = true;
}

bool playing() { return g_ready && dma_channel_is_busy(g_dma); }

void setMuted(bool muted) {
  if (!g_ready || muted == g_muted) return;
  g_muted = muted;
  if (muted && dma_channel_is_busy(g_dma)) stop();
}

void request(uint32_t bits) {
  if (!g_ready || g_muted || bits == 0) return;
  // The highest priority among the requests (the first of equals)
  int best = -1;
  for (uint32_t k = 0; k < g_count && k < sim::SOUND_KINDS; k++) {
    if (!(bits & (1u << k))) continue;
    if (best < 0 || PRIORITY[k] > PRIORITY[best]) best = (int)k;
  }
  if (best < 0) return;
  const uint32_t now = time_us_32();
  const bool busy = dma_channel_is_busy(g_dma);
  if (busy && PRIORITY[best] < g_curPriority &&
      now - g_curStartUs < HOLD_US) {
    return;
  }
  start((uint32_t)best, busy);
  g_curPriority = PRIORITY[best];
  g_curStartUs = now;
}

}  // namespace audio

#endif  // ESP32
