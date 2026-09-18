// The sound pack (core/tools/pack_se.py --pwm) as a blob in flash. The
// build generates the file and passes its path as SE_PACK_PATH; the
// .rodata section keeps it in flash (the linker script copies only
// .time_critical and the RAM-code library into RAM).
        .section .rodata.se_pack, "a"
        .balign 4
        .global g_sePack
        .global g_sePackEnd
g_sePack:
        .incbin SE_PACK_PATH
g_sePackEnd:
        .balign 4
