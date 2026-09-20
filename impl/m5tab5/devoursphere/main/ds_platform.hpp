#ifndef DS_PLATFORM_HPP
#define DS_PLATFORM_HPP

// The pieces of this front end that are neither the frame loop nor the
// display: the entropy source, the high score in NVS and the stack watch.
// Same names as the Xiamocon and PicoSystem front ends', because
// profiler.cpp and high_score_store.hpp are shared with them and read these.

#include <cstddef>
#include <cstdint>

namespace devoursphere::sim {
class Game;
}

namespace ds {

// Where the simulation state lives. 84 KB, and this board has the internal
// SRAM for it -- unlike the ESP32S3, which has to keep it in PSRAM. Ticking
// out of PSRAM would cost for no reason here.
devoursphere::sim::Game *allocGame();

// Bytes of internal RAM still free, for the bring-up trace and the overlay.
uint32_t freeInternalRam();
uint32_t freeSpiRam();

// Bring-up progress on the USB-Serial/JTAG console (`./monitor.sh`). The one
// window into a board that is showing nothing, so it stays in.
void trace(const char *what, uint32_t value);

// What each core does while it waits. Neither spins: FreeRTOS has its own
// work to do on both (the touch and the PPA callbacks on core0, nothing
// pinned to core1 but the idle task still has to run), and a core that never
// yields starves it.
void frameIdle();

// The high score record (sim::HIGH_SCORE_RECORD_BYTES bytes; when it is read
// and written is high_score_store.hpp, shared with the handhelds). NVS, so
// unlike the raw-flash writes on the RP2 chips this neither stalls the other
// core for a sector erase nor takes the flash away from the sound -- the
// once-per-run policy is kept all the same, because there is no reason to
// write more often than that.
bool readHighScoreRecord(uint8_t *out);
bool writeHighScoreRecord(const uint8_t *in);

// A seed that differs from boot to boot (esp_random(), seeded by hardware).
uint32_t randomSeed();

// Stack high water marks, in bytes used. Both cores run FreeRTOS tasks,
// which record their own marks; the init has to be called from inside the
// task it names, because the readout is asked for from the other one.
// FreeRTOS does not expose a task's size, so these are the sizes the tasks
// are created with.
constexpr uint32_t STACK_BYTES_CORE0 = 16 * 1024;
constexpr uint32_t STACK_BYTES_CORE1 = 8 * 1024;
void stackWatchInitCore0();
void stackWatchInitCore1();
uint32_t stackUsedCore0();
uint32_t stackUsedCore1();

}  // namespace ds

#endif
