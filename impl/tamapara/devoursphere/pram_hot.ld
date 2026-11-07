/* What else runs from PRAM (zero wait states) besides the .pram_text
 * sections: whole objects of the hot path. The PRAM is 64 KB; build.sh
 * prints what is used. Tune with tools/sim.py --prof. */
*gfx3d_gfx3d.o(.text* .rodata*)
*sim_fixed.o(.text* .rodata*)
*render_sphere.o(.text* .rodata*)
