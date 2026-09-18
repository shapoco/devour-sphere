// Sound effects: one voice with priorities (see se_player.hpp).
//
// The selection is common: a request while a sound plays takes the voice
// only if its priority is not lower, or once the playing sound has held it
// for HOLD_US (a two-second explosion must not mute every shot until it
// ends). Equal priority retriggers. The backends differ in how the samples
// get out:
//
// RP2040 / RP2350: the pack (core/tools/pack_se.py --pwm, linked into
// .rodata by the build's se_data.S) holds every sound as 16-bit PWM levels.
// Playing one is a DMA transfer from the pack in flash into the PWM slice's
// compare register, paced either by the slice's own wrap request (one level
// per PWM period) or by a DMA pacing timer. The CPU does nothing while a
// sound plays. The read address is the uncached, non-allocating alias of the
// flash (XIP_NOCACHE_NOALLOC_BASE): a stream through the cached alias would
// turn the 16 KB XIP cache over every 0.4 s, and both cores already fight
// over it for code. A sound taken over mid-way starts past its leading ramp,
// since the level is already around the middle.
//
// ESP32S3: the DMA cannot read the flash, so the browser's pack (signed
// 16-bit PCM, docs/play/se.bin, embedded by platformio.ini) is copied into
// the I2S driver's DMA ring by a small task pinned to core1, 128 samples at
// a time, zeros when nothing plays; the I2S peripheral does the PDM. The
// voice is a cursor word (sound and position) that request() replaces and
// the task advances with a compare-and-swap.

#include "se_player.hpp"

#include <cstring>

#include "devoursphere/sim/game.hpp"

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

// The pack (core/tools/pack_se.py): "DSSE" is signed 16-bit PCM, "DSPW" is
// PWM levels with the wrap and the ramp length in the header
struct Entry {
  uint32_t first;
  uint32_t count;
};
struct Pack {
  bool pwm = false;
  uint32_t rate = 0, count = 0, wrap = 0, ramp = 0;
  const Entry *toc = nullptr;
  const uint8_t *samples = nullptr;  // 16-bit, little endian
};

bool parsePack(const uint8_t *begin, const uint8_t *end, Pack &p) {
  if (end - begin < 16) return false;
  auto u32 = [&](int off) {
    return (uint32_t)begin[off] | ((uint32_t)begin[off + 1] << 8) |
           ((uint32_t)begin[off + 2] << 16) | ((uint32_t)begin[off + 3] << 24);
  };
  if (begin[0] != 'D' || begin[1] != 'S') return false;
  int header;
  if (begin[2] == 'S' && begin[3] == 'E') {
    p.pwm = false;
    header = 16;
  } else if (begin[2] == 'P' && begin[3] == 'W') {
    p.pwm = true;
    header = 24;
    p.wrap = u32(16);
    p.ramp = u32(20);
  } else {
    return false;
  }
  p.rate = u32(4);
  p.count = u32(8);
  p.toc = reinterpret_cast<const Entry *>(begin + header);
  p.samples = begin + header + p.count * sizeof(Entry);
  return p.count > 0;
}

Pack g_pack;
uint8_t g_curPriority = 0;
uint32_t g_curStartUs = 0;
bool g_ready = false;
bool g_muted = false;

// The backend: whether a sound plays, start one (retrigger: another sound
// was playing), stop, and a clock for the hold
bool backendBusy();
void backendStart(uint32_t kind, bool retrigger);
void backendStop();
uint32_t backendNowUs();

}  // namespace
}  // namespace audio

// ===========================================================================
#if defined(ESP32)

#include <driver/i2s_pdm.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// docs/play/se.bin, embedded by platformio.ini (board_build.embed_files) from
// the copy that embed_se.py puts at se/se.bin
extern const uint8_t g_sePack[] asm("_binary_se_se_bin_start");
extern const uint8_t g_sePackEnd[] asm("_binary_se_se_bin_end");

namespace audio {
namespace {

constexpr uint32_t CURSOR_NONE = 0xFFFFFFFFu;  // else kind << 24 | position
constexpr int CHUNK = 128;                     // samples per I2S write
constexpr int DMA_DESCS = 4;                   // ring: 4 x CHUNK = 23 ms

volatile uint32_t g_cursor = CURSOR_NONE;
i2s_chan_handle_t g_i2s = nullptr;
int16_t g_chunk[CHUNK];

void audioTask(void *) {
  for (;;) {
    uint32_t c = g_cursor;
    int n = 0;
    if (c != CURSOR_NONE) {
      uint32_t kind = c >> 24, pos = c & 0xFFFFFFu;
      const Entry &e = g_pack.toc[kind];
      n = (int)(e.count - pos);
      if (n > CHUNK) n = CHUNK;
      if (n < 0) n = 0;
      std::memcpy(g_chunk, g_pack.samples + (e.first + pos) * 2, n * 2);
      uint32_t next = pos + n >= e.count ? CURSOR_NONE : (kind << 24) | (pos + n);
      // A request may have replaced the cursor meanwhile: then this chunk
      // is the last of the old sound and the new one starts next round
      __atomic_compare_exchange_n(&g_cursor, &c, next, false,
                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    if (n < CHUNK) std::memset(g_chunk + n, 0, (CHUNK - n) * 2);
    size_t written = 0;
    i2s_channel_write(g_i2s, g_chunk, sizeof(g_chunk), &written, portMAX_DELAY);
  }
}

bool backendInit(const Config &cfg) {
  if (g_pack.pwm) return false;  // this chip wants the signed PCM pack
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan.dma_desc_num = DMA_DESCS;
  chan.dma_frame_num = CHUNK;
  chan.auto_clear = true;  // zeros, not a repeat, if the task ever falls behind
  if (i2s_new_channel(&chan, &g_i2s, nullptr) != ESP_OK) return false;
  i2s_pdm_tx_config_t tx = {
      .clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(g_pack.rate),
      .slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .clk = I2S_GPIO_UNUSED,
              .dout = (gpio_num_t)cfg.pin,
              .dout2 = I2S_GPIO_UNUSED,
              .invert_flags = {.clk_inv = false},
          },
  };
  if (i2s_channel_init_pdm_tx_mode(g_i2s, &tx) != ESP_OK) return false;
  if (i2s_channel_enable(g_i2s) != ESP_OK) return false;
  // Above the simulation task (priority 10, core1); it sleeps in the driver
  // between chunks
  return xTaskCreatePinnedToCore(audioTask, "se", 3072, nullptr, 11, nullptr,
                                 1) == pdPASS;
}

bool backendBusy() { return g_cursor != CURSOR_NONE; }
void backendStart(uint32_t kind, bool) {
  __atomic_store_n(&g_cursor, kind << 24, __ATOMIC_RELEASE);
}
void backendStop() { __atomic_store_n(&g_cursor, CURSOR_NONE, __ATOMIC_RELEASE); }
uint32_t backendNowUs() { return (uint32_t)esp_timer_get_time(); }

}  // namespace
}  // namespace audio

// ===========================================================================
#else  // RP2040 / RP2350

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/regs/addressmap.h>
#include <pico/stdlib.h>

extern "C" const uint8_t g_sePack[];
extern "C" const uint8_t g_sePackEnd[];

namespace audio {
namespace {

const uint16_t *g_levels = nullptr;  // the samples, in the uncached flash alias
uint16_t g_idle = 0;
uint g_slice = 0;
uint g_channel = 0;
int g_dma = -1;
volatile uint16_t *g_cc = nullptr;  // the compare register half of our channel

bool backendInit(const Config &cfg) {
  if (!g_pack.pwm) return false;  // this chip wants the PWM pack
  // The same bytes through the uncached, non-allocating window
  g_levels = reinterpret_cast<const uint16_t *>(
      (uintptr_t)g_pack.samples - XIP_BASE + XIP_NOCACHE_NOALLOC_BASE);
  g_idle = cfg.idleAtMiddle ? (uint16_t)((g_pack.wrap + 1) / 2) : 0;

  // The PWM: the pack's wrap, at the system clock. With PWM_WRAP pacing one
  // period is one sample (the pack was built for this clock); with a DMA
  // timer the period is just the carrier.
  const uint pin = (uint)cfg.pin;
  g_slice = pwm_gpio_to_slice_num(pin);
  g_channel = pwm_gpio_to_channel(pin);
  gpio_set_function(pin, GPIO_FUNC_PWM);
  pwm_config c = pwm_get_default_config();
  pwm_config_set_clkdiv_int(&c, 1);
  pwm_config_set_wrap(&c, (uint16_t)g_pack.wrap);
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
    uint32_t n = clock_get_hz(clk_sys) / g_pack.rate;
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
  return true;
}

bool backendBusy() { return dma_channel_is_busy(g_dma); }

void backendStart(uint32_t kind, bool retrigger) {
  const Entry &e = g_pack.toc[kind];
  // Taking over mid-sound: the level is around the middle already, so skip
  // the ramp up from the idle level (packs without ramps skip nothing)
  uint32_t skip = retrigger ? g_pack.ramp : 0;
  if (skip >= e.count) skip = 0;
  dma_channel_abort(g_dma);
  dma_channel_set_read_addr(g_dma, g_levels + e.first + skip, false);
  dma_channel_set_trans_count(g_dma, e.count - skip, true);
}

void backendStop() {
  dma_channel_abort(g_dma);
  *g_cc = g_idle;
}

uint32_t backendNowUs() { return time_us_32(); }

}  // namespace
}  // namespace audio

#endif

// ===========================================================================
namespace audio {

void init(const Config &cfg) {
  if (!parsePack(g_sePack, g_sePackEnd, g_pack)) return;
  g_ready = backendInit(cfg);
}

bool playing() { return g_ready && backendBusy(); }

void setMuted(bool muted) {
  if (!g_ready || muted == g_muted) return;
  g_muted = muted;
  if (muted && backendBusy()) backendStop();
}

void request(uint32_t bits) {
  if (!g_ready || g_muted || bits == 0) return;
  // The highest priority among the requests (the first of equals)
  int best = -1;
  for (uint32_t k = 0; k < g_pack.count && k < sim::SOUND_KINDS; k++) {
    if (!(bits & (1u << k))) continue;
    if (best < 0 || PRIORITY[k] > PRIORITY[best]) best = (int)k;
  }
  if (best < 0) return;
  const uint32_t now = backendNowUs();
  const bool busy = backendBusy();
  if (busy && PRIORITY[best] < g_curPriority &&
      now - g_curStartUs < HOLD_US) {
    return;
  }
  backendStart((uint32_t)best, busy);
  g_curPriority = PRIORITY[best];
  g_curStartUs = now;
}

}  // namespace audio
