/* The Paradise's own clock, read from its SRAM when an emulator starts (before the emulator uses
 * the SRAM). The Paradise keeps the time as a base (days, seconds of the day) plus its seconds
 * counter 0x45000304; next to it the six fields it shows (year, month, day, hour, minute, second,
 * u16 each). Where this lies differs between the editions, so it is searched: a place counts only
 * if base plus counter give the time of day the fields show (chance 1 in 86400). The fields may
 * lag the counter by a few seconds; that is added. Same method as TamaParadise (uhrstellen).
 * Only for the emulators' start; header only, shared by TamaEmu, T7Emu and T18Emu. */
#ifndef PARACLOCK_H
#define PARACLOCK_H
#include <stdint.h>
#ifndef PARA_ATTR
#define PARA_ATTR   /* TamaEmu: start-up code only (its translator takes the space afterwards) */
#endif

/* days since 1970-01-01 */
static inline int32_t para_days(int32_t y, int32_t m, int32_t d)
{
  y -= m <= 2; int32_t era = (y >= 0 ? y : y - 399) / 400; int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
/* Search [lo, hi) (byte offsets in the SRAM at 0x18000000) for the clock, skip [skip_lo, skip_hi).
 * Returns local time as seconds since 1970 (0 = not found). cnt: the seconds counter now. Must not
 * use anything but registers and the stack: it runs before the emulator sets up its memory. */
__attribute__((noinline, unused)) PARA_ATTR static uint32_t para_clock(uint32_t cnt, uint32_t lo, uint32_t hi, uint32_t skip_lo, uint32_t skip_hi)
{
  const volatile uint16_t *h = (const volatile uint16_t *)0x18000000u;
  uint32_t best = 0, best_lag = 600;
  if (lo < 0x10) lo = 0x10;
  for (uint32_t o = lo; o + 12 <= hi; o += 2) {
    if (o + 12 > skip_lo && o - 0x10 < skip_hi) continue;
    uint32_t y = h[o / 2];
    if (y < 2000u || y > 2100u) continue;
    uint32_t mo = h[o / 2 + 1], d = h[o / 2 + 2], hh = h[o / 2 + 3], mi = h[o / 2 + 4], se = h[o / 2 + 5];
    if (mo < 1u || mo > 12u || d < 1u || d > 31u || hh > 23u || mi > 59u || se > 59u) continue;
    uint32_t days = h[(o - 0x10) / 2] | (uint32_t)h[(o - 0x10) / 2 + 1] << 16;
    uint32_t sek = h[(o - 0x0C) / 2] | (uint32_t)h[(o - 0x0C) / 2 + 1] << 16;
    if (sek >= 86400u || days >= 40000u) continue;
    uint32_t tod = (sek + cnt % 86400u) % 86400u;               /* days * 86400 drops out */
    uint32_t shown = hh * 3600u + mi * 60u + se;
    uint32_t lag = (tod + 86400u - shown) % 86400u;            /* fields behind the clock by this much */
    if (lag < best_lag) {
      best_lag = lag;
      best = (uint32_t)para_days((int32_t)y, (int32_t)mo, (int32_t)d) * 86400u + shown + lag;
    }
  }
  return best;
}
/* 1.6.8: the Paramix menu runs before a program and uses the SRAM itself, so it looks the clock up at
 * its own start and leaves it in the mailbox (0x20000FC0: "PCLK", time, counter, ~time). A program
 * started from the menu takes it from there when its own search finds nothing. One day at most. */
#define PARA_MB ((volatile uint32_t *)0x20000FC0u)
#define PARA_MB_MAGIC 0x4B4C4350u
__attribute__((noinline, unused)) PARA_ATTR static uint32_t para_mailbox(uint32_t cnt)
{
  volatile uint32_t *m = PARA_MB;
#ifdef PARA_MB_KEEP
  if (m[0] != PARA_MB_MAGIC || m[3] != ~m[1] || !m[1] || cnt - m[2] > 400u * 86400u) return 0;   /* test29: kept, see paraback.h */
#else
  if (m[0] != PARA_MB_MAGIC || m[3] != ~m[1] || !m[1] || cnt - m[2] > 86400u) return 0;
#endif
  return m[1] + (cnt - m[2]);
}
/* 1.7.2 test17: the mailbox written in the last two minutes (the menu right before starting the program, or its clock in a
 * start device without the Paradise): it comes first, before the search in the SRAM, which may find old leftovers there */
__attribute__((noinline, unused)) PARA_ATTR static uint32_t para_mailbox_fresh(uint32_t cnt)
{
  volatile uint32_t *m = PARA_MB;
  if (m[0] != PARA_MB_MAGIC || m[3] != ~m[1] || !m[1] || cnt - m[2] > 120u) return 0;
  return m[1] + (cnt - m[2]);
}
#ifdef PARA_FLASH
/* 1.6.9 test37 (PARA_FLASH): the Paradise keeps the base of its clock in its settings page 0xEFE000 (copy at 0xEFF000):
 * days since 2025-01-01 at +0x0C and seconds at +0x10; now = base + the seconds counter 0x45000304, which runs on
 * while programs run. Found in the TamaParadise runs: the same base as in the SRAM clock (block in the SRAM at
 * 0x18000B9C is this page). Used when neither the SRAM nor the mailbox give the time (the Paradise restarted and the
 * mailbox was gone: reported, the clock of V3Emu and Tama-Go was not set now and then). The page must have a good
 * CRC and its date fields (+0x1C, the time the Paradise lived up to) must lie within a year before that time. */
__attribute__((noinline, unused)) PARA_ATTR static uint32_t para_flash(uint32_t cnt)
{
  for (uint32_t pg = 0x60EFE000u; pg <= 0x60EFF000u; pg += 0x1000u) {
    const volatile uint8_t *b = (const volatile uint8_t *)pg;
    uint32_t c = 0;
    for (uint32_t i = 4; i < 0x1000u; i++) { c ^= b[i]; for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xA001u : c >> 1; }
    if ((b[0] | (uint32_t)b[1] << 8) != c || (b[2] | (uint32_t)b[3] << 8) != (~c & 0xFFFFu)) continue;
    uint32_t days = b[0x0C] | (uint32_t)b[0x0D] << 8 | (uint32_t)b[0x0E] << 16 | (uint32_t)b[0x0F] << 24;
    uint32_t sek = b[0x10] | (uint32_t)b[0x11] << 8 | (uint32_t)b[0x12] << 16 | (uint32_t)b[0x13] << 24;
    if (days > 40000u || sek >= 86400u) continue;
    uint32_t y = b[0x1C] | (uint32_t)b[0x1D] << 8, mo = b[0x1E] | (uint32_t)b[0x1F] << 8, d = b[0x20] | (uint32_t)b[0x21] << 8;
    uint32_t hh = b[0x22] | (uint32_t)b[0x23] << 8, mi = b[0x24] | (uint32_t)b[0x25] << 8, se = b[0x26] | (uint32_t)b[0x27] << 8;
    if (y < 2000u || y > 2100u || mo < 1u || mo > 12u || d < 1u || d > 31u || hh > 23u || mi > 59u || se > 59u) continue;
    uint32_t t = (20089u + days) * 86400u + sek + cnt;   /* 20089: days from 1970 to 2025-01-01 */
    uint32_t f = (uint32_t)para_days((int32_t)y, (int32_t)mo, (int32_t)d) * 86400u + hh * 3600u + mi * 60u + se;
    if (f > t + 600u || t - f > 366u * 86400u) continue;
    return t;
  }
  return 0;
}
#endif
#ifdef PARA_CLEAN_CLK
/* 1.7.2 test17 (PARA_CLEAN_CLK): start device without the Paradise: nobody but the menu keeps the time, in its clock
 * sector 0x0C0000 ("PMXK", time, counter then, idle, check = xor of the four). A program that starts first (autostart,
 * the menu did not run) found no time before. Readable only after hw_init. */
__attribute__((noinline, unused)) PARA_ATTR static uint32_t para_cleanclk(uint32_t cnt)
{
  const volatile uint32_t *p = (const volatile uint32_t *)0x600C0000u;
  uint32_t m = p[0], t = p[1], c = p[2], i = p[3], k = p[4];
  if (m != 0x4B584D50u || k != (m ^ t ^ c ^ i) || !t || cnt < c) return 0;
  return t + (cnt - c);
}
#endif
__attribute__((noinline, unused)) static void para_mailbox_put(uint32_t t, uint32_t cnt)
{
  volatile uint32_t *m = PARA_MB;
  if (!t) return;
  m[1] = t; m[2] = cnt; m[3] = ~t; m[0] = PARA_MB_MAGIC;
}
#endif
