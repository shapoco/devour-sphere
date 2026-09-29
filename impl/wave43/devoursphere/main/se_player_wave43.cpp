// Sound effects on the wave_43 (the audio:: interface of
// impl/xiamocon/devoursphere/include/se_player.hpp, shared with the
// handhelds so that high_score_store.hpp works the same here).
//
// The speaker hangs off an ES8311 codec: I2C for its registers (the bus the
// touch controller is on, board::i2cBus()), I2S for the samples, and a power
// amplifier switched by a GPIO. The wiring and the codec's configuration are
// Waveshare's, from their BSP for this board (esp32_p4_wifi6_touch_lcd_4_3
// 1.0.1, bsp_audio_init() and bsp_audio_codec_speaker_init()) and the
// 06_I2SCodec example that drives it, on the same esp_codec_dev library.
// Only the playback half is brought up: the ES7210 microphone codec and the
// I2S input are left alone.
//
// Like the Tab5 and unlike the handhelds, every sound a tick asks for is
// played -- up to SE_VOICES at once, mixed here -- which is what the browser
// does and what the game was tuned against. The mixer is a task on core1,
// beside the simulation: it sleeps in the I2S write until a DMA buffer frees
// up, mixes the next SE_CHUNK_FRAMES and goes back to sleep, a few
// microseconds of work every 6 ms. It never stops: with nothing playing it
// writes silence, so the codec and the amplifier see one continuous stream
// and there is no click when a sound starts.
//
// The pack is docs/play/se.bin, the browser's own (signed 16-bit PCM at
// 24 kHz), linked into flash by the build (main/CMakeLists.txt) and mixed
// straight out of it. The codec runs at the pack's rate, so there is no
// resampling.

#include "se_player.hpp"

#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board.hpp"
#include "devoursphere/sim/game.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"

// docs/play/se.bin, embedded by main/CMakeLists.txt
extern const uint8_t g_sePack[] asm("_binary_se_bin_start");
extern const uint8_t g_sePackEnd[] asm("_binary_se_bin_end");

namespace audio {
namespace {

namespace sim = devoursphere::sim;

// Waveshare's BSP (include/bsp/esp32_p4_wifi6_touch_lcd_4_3.h); their
// Arduino playback example agrees. The I2S's data-in line (GPIO11) is the
// microphone's and is not claimed.
constexpr gpio_num_t PIN_I2S_MCLK = GPIO_NUM_13;
constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_12;
constexpr gpio_num_t PIN_I2S_WS = GPIO_NUM_10;
constexpr gpio_num_t PIN_I2S_DOUT = GPIO_NUM_9;
constexpr int PIN_POWER_AMP = 53;    // high: amplifier on
constexpr int I2S_PORT = I2S_NUM_1;  // the BSP's CONFIG_BSP_I2S_NUM

struct Entry {
  uint32_t first;  // first sample of this sound in the pack
  uint32_t count;
};

struct Voice {
  const int16_t *p = nullptr;  // the next sample; null when free
  uint32_t left = 0;           // samples still to play
};

const Entry *g_toc = nullptr;
const int16_t *g_samples = nullptr;
uint32_t g_rate = 0, g_count = 0;

esp_codec_dev_handle_t g_codec = nullptr;

// The voices, shared between the simulation task (request, setMuted), the
// mixer and core0 (playing and stop, from the high score store). A spinlock
// rather than a mutex: every holder is in and out within a few microseconds.
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
Voice g_voices[ds::SE_VOICES];
bool g_muted = false;

bool bringUpCodec() {
  auto *bus = (i2c_master_bus_handle_t)ds::board::i2cBus();
  if (!bus) {
    ds::trace("no i2c bus for the codec", 0);
    return false;
  }

  // The I2S channel, output only. Short DMA buffers, so that a sound starts
  // within a couple of frames of the tick that asked for it (ds_config.hpp);
  // auto_clear_after_cb sends silence rather than the last buffer again if
  // the mixer is ever late.
  i2s_chan_config_t chan =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
  chan.dma_desc_num = ds::SE_DMA_BUFFERS;
  chan.dma_frame_num = ds::SE_CHUNK_FRAMES;
  chan.auto_clear_after_cb = true;
  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&chan, &tx, nullptr) != ESP_OK) {
    ds::trace("i2s channel failed", 0);
    return false;
  }
  // The rate and the slots set here are placeholders: esp_codec_dev_open()
  // below reconfigures both for the stream it is given.
  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(g_rate);
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO);
  std.gpio_cfg.mclk = PIN_I2S_MCLK;
  std.gpio_cfg.bclk = PIN_I2S_BCLK;
  std.gpio_cfg.ws = PIN_I2S_WS;
  std.gpio_cfg.dout = PIN_I2S_DOUT;
  std.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(tx, &std) != ESP_OK ||
      i2s_channel_enable(tx) != ESP_OK) {
    ds::trace("i2s setup failed", 0);
    return false;
  }

  audio_codec_i2s_cfg_t i2sCfg = {};
  i2sCfg.port = I2S_PORT;
  i2sCfg.tx_handle = tx;
  const audio_codec_data_if_t *dataIf = audio_codec_new_i2s_data(&i2sCfg);

  audio_codec_i2c_cfg_t i2cCfg = {};
  i2cCfg.port = 1;  // board.cpp's; unused when a bus handle is given
  i2cCfg.addr = ES8311_CODEC_DEFAULT_ADDR;
  i2cCfg.bus_handle = bus;
  const audio_codec_ctrl_if_t *ctrlIf = audio_codec_new_i2c_ctrl(&i2cCfg);
  const audio_codec_gpio_if_t *gpioIf = audio_codec_new_gpio();
  if (!dataIf || !ctrlIf || !gpioIf) {
    ds::trace("codec interfaces failed", 0);
    return false;
  }

  // As the BSP has it: the codec is the I2S slave on an external MCLK, and
  // it switches the amplifier itself when it is opened and closed.
  es8311_codec_cfg_t es = {};
  es.ctrl_if = ctrlIf;
  es.gpio_if = gpioIf;
  es.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
  es.pa_pin = PIN_POWER_AMP;
  es.pa_reverted = false;
  es.master_mode = false;
  es.use_mclk = true;
  es.hw_gain.pa_voltage = 5.0f;
  es.hw_gain.codec_dac_voltage = 3.3f;
  const audio_codec_if_t *codecIf = es8311_codec_new(&es);
  if (!codecIf) {
    ds::trace("ES8311 not found", 0);
    return false;
  }

  esp_codec_dev_cfg_t devCfg = {};
  devCfg.dev_type = ESP_CODEC_DEV_TYPE_OUT;
  devCfg.codec_if = codecIf;
  devCfg.data_if = dataIf;
  g_codec = esp_codec_dev_new(&devCfg);
  if (!g_codec) {
    ds::trace("codec device failed", 0);
    return false;
  }

  // One channel: the pack is mono, and a mono stream puts the I2S in mono
  // slot mode, which sends each sample to both slots. MCLK at 256 x fs.
  esp_codec_dev_sample_info_t fs = {};
  fs.bits_per_sample = 16;
  fs.channel = 1;
  fs.sample_rate = g_rate;
  fs.mclk_multiple = 256;
  const int err = esp_codec_dev_open(g_codec, &fs);
  if (err != ESP_CODEC_DEV_OK) {
    ds::trace("codec open failed, err", (uint32_t)err);
    g_codec = nullptr;
    return false;
  }
  esp_codec_dev_set_out_vol(g_codec, ds::SE_VOLUME);
  return true;
}

void mixerTask(void *) {
  static int16_t out[ds::SE_CHUNK_FRAMES];
  int32_t acc[ds::SE_CHUNK_FRAMES];
  for (;;) {
    for (int i = 0; i < ds::SE_CHUNK_FRAMES; i++) acc[i] = 0;
    portENTER_CRITICAL(&g_lock);
    for (Voice &v : g_voices) {
      if (!v.p) continue;
      const uint32_t n = v.left < (uint32_t)ds::SE_CHUNK_FRAMES
                             ? v.left
                             : (uint32_t)ds::SE_CHUNK_FRAMES;
      for (uint32_t i = 0; i < n; i++) acc[i] += v.p[i];
      v.p += n;
      v.left -= n;
      if (v.left == 0) v.p = nullptr;
    }
    portEXIT_CRITICAL(&g_lock);
    // Saturate rather than wrap: eight loud sounds at once clip their peaks
    // instead of tearing
    for (int i = 0; i < ds::SE_CHUNK_FRAMES; i++) {
      const int32_t s = acc[i];
      out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    // Blocks until the I2S has room, which is what paces the loop
    esp_codec_dev_write(g_codec, out, sizeof(out));
  }
}

}  // namespace

void init(const Config &) {
  // The Config fields describe an RP2's PWM pin and pacing; nothing here
  // needs them.
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
  ds::trace("sound pack rate", g_rate);
  ds::trace("sound pack sounds", g_count);

  if (!bringUpCodec()) {
    ds::trace("running without sound", 0);
    return;
  }
  // Only now: request() starts nothing until the table is set, so a codec
  // that failed leaves every call below a no-op.
  g_toc = reinterpret_cast<const Entry *>(p + 16);
  g_samples =
      reinterpret_cast<const int16_t *>(p + 16 + g_count * sizeof(Entry));

  // Core1, beside the simulation and one priority above it, so that a batch
  // of ticks can never keep the I2S waiting: the mixer's share of the core
  // is microseconds per chunk.
  if (xTaskCreatePinnedToCore(mixerTask, "ds_audio", 4096, nullptr, 6, nullptr,
                              1) != pdPASS) {
    ds::trace("audio task not created", 0);
    g_toc = nullptr;
  }
}

void request(uint32_t bits) {
  if (!g_toc || !bits) return;
  portENTER_CRITICAL(&g_lock);
  if (!g_muted) {
    for (uint32_t k = 0; k < g_count; k++) {
      if (!(bits & (1u << k))) continue;
      // The first free voice; with all of them busy the sound is dropped,
      // which is inaudible next to the ones already playing.
      for (Voice &v : g_voices) {
        if (v.p) continue;
        v.p = g_samples + g_toc[k].first;
        v.left = g_toc[k].count;
        break;
      }
    }
  }
  portEXIT_CRITICAL(&g_lock);
}

void setMuted(bool muted) {
  portENTER_CRITICAL(&g_lock);
  if (muted && !g_muted) {
    for (Voice &v : g_voices) v.p = nullptr;
  }
  g_muted = muted;
  portEXIT_CRITICAL(&g_lock);
}

bool playing() {
  bool any = false;
  portENTER_CRITICAL(&g_lock);
  for (const Voice &v : g_voices) any = any || v.p;
  portEXIT_CRITICAL(&g_lock);
  return any;
}

void stop() {
  portENTER_CRITICAL(&g_lock);
  for (Voice &v : g_voices) v.p = nullptr;
  portEXIT_CRITICAL(&g_lock);
}

}  // namespace audio
