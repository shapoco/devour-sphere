/* 1.6.8 (PARA_BACK): the Paradise does not live while a program runs.
 * The Paradise keeps no time of its own while a program runs, but its seconds counter 0x45000304 does.
 * Back in the Paradise, its firmware compares "now" (base plus counter) with the time it lived up to
 * (the six date fields at +0x1C of the settings block 0xEFE000/0xEFF000, year .. second, u16 each) and
 * lives the whole gap at once: hunger, mood, care misses, even death. Same as TamaParadise v83.
 * So: when a program starts, it notes the counter and the Paradise's time in the clock mailbox of paraclock.h
 * ("PCLK" at 0x20000FC0, kept over the restart; the menu fills it at its start, a program started
 * straight from the Paradise fills it itself). When it goes back, the date fields move forward by the time
 * spent outside (never past now), both copies, new CRC, and the mailbox is cleared.
 * (A first try with its own words at 0x20000FE0 made the Paradise start the program again: those words
 * are not free.)
 * Header only; needs flash_read and flash_update4k, a 4 KB buffer from the caller. */
#ifndef PARABACK_H
#define PARABACK_H
#include <stdint.h>
#ifndef PB_ATTR
#define PB_ATTR
#endif
#ifndef PB_MARK_ATTR
#define PB_MARK_ATTR   /* para_leave_mark runs first thing at the start, before the flash window is set up: never in the flash */
#endif
#ifndef PB_MB
#define PB_MB ((volatile uint32_t *)0x20000FC0u)   /* same as PARA_MB: magic, time, counter, ~time */
#endif
#define PB_MAGIC 0x4B4C4350u                      /* "PCLK" */
#ifndef PB_CNT
#define PB_CNT (*(volatile uint32_t *)0x45000304u)
#endif

/* t: the Paradise's local time (seconds since 1970) when the program started, cnt: the counter then */
__attribute__((noinline, unused)) PB_MARK_ATTR static void para_leave_mark(uint32_t cnt, uint32_t t)
{
  volatile uint32_t *m = PB_MB;
  if (m[0] == PB_MAGIC && m[3] == ~m[1] && m[1] && cnt - m[2] <= 400u * 86400u) return;   /* noted by the menu */
  if (!t) { m[0] = 0; return; }
  m[1] = t; m[2] = cnt; m[3] = ~t; m[0] = PB_MAGIC;
}

static inline uint32_t pb_days(int32_t y, int32_t mo, int32_t d)   /* days since 1970-01-01 */
{
  y -= mo <= 2; int32_t era = (y >= 0 ? y : y - 399) / 400; int32_t yoe = y - era * 400;
  int32_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)(era * 146097 + doe - 719468);
}
__attribute__((minsize, noinline)) PB_ATTR static uint32_t pb_crc(const uint8_t *b)
{
  uint32_t c = 0;
  for (uint32_t i = 4; i < 0x1000u; i++) { c ^= b[i]; for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xA001u : c >> 1; }
  return c;
}
static inline uint32_t pb_u16(const uint8_t *b, uint32_t o) { return b[o] | (uint32_t)b[o + 1] << 8; }
static inline void pb_w16(uint8_t *b, uint32_t o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }

/* Before going back. Returns 1 when the fields were moved. */
__attribute__((noinline, unused, minsize)) PB_ATTR static int para_back_fix(uint8_t *b)
{
  volatile uint32_t *m = PB_MB;
  if (m[0] != PB_MAGIC || m[3] != ~m[1] || !m[1]) return 0;
  uint32_t gap = PB_CNT - m[2], now = m[1] + gap;
#ifdef PARA_MB_KEEP
  /* test29: the time stays in the mailbox, moved to now. A start device goes back to the menu, not to the
   * Paradise: the SRAM then holds no Paradise clock, and the next program started without a time. */
  m[1] = now; m[2] = PB_CNT; m[3] = ~now;
#else
  m[0] = 0;
#endif
  if (gap < 60u || gap > 400u * 86400u) return 0;
  flash_read(0xEFE000u, b, 0x1000u);
  uint32_t c = pb_crc(b);
  if (pb_u16(b, 0) != c || pb_u16(b, 2) != (~c & 0xFFFFu)) return 0;
  uint32_t y = pb_u16(b, 0x1C), mo = pb_u16(b, 0x1E), d = pb_u16(b, 0x20);
  uint32_t hh = pb_u16(b, 0x22), mi = pb_u16(b, 0x24), se = pb_u16(b, 0x26);
  if (y < 2000u || y > 2100u || mo < 1u || mo > 12u || d < 1u || d > 31u || hh > 23u || mi > 59u || se > 59u) return 0;
  uint32_t f = pb_days((int32_t)y, (int32_t)mo, (int32_t)d) * 86400u + hh * 3600u + mi * 60u + se;
  if (f >= now) return 0;
  uint32_t nf = f + gap; if (nf > now) nf = now;
  /* back to a date (civil from days) */
  int32_t z = (int32_t)(nf / 86400u) + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t yy = (int32_t)yoe + era * 400; uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  uint32_t dd = doy - (153 * mp + 2) / 5 + 1, mm = mp < 10 ? mp + 3 : mp - 9; yy += mm <= 2;
  uint32_t s = nf % 86400u;
  pb_w16(b, 0x1C, (uint32_t)yy); pb_w16(b, 0x1E, mm); pb_w16(b, 0x20, dd);
  pb_w16(b, 0x22, s / 3600u); pb_w16(b, 0x24, s / 60u % 60u); pb_w16(b, 0x26, s % 60u);
  c = pb_crc(b);
  pb_w16(b, 0, c); pb_w16(b, 2, ~c & 0xFFFFu);
  flash_update4k(0xEFE000u, b);
  flash_update4k(0xEFF000u, b);
  return 1;
}
#endif
