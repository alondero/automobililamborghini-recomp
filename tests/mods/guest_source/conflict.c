/* An ordinary replacement must be rejected as a base source-patch conflict. */
#ifndef REPLACEMENT
#error REPLACEMENT must name a migrated original symbol
#endif
__attribute__((retain, section(".recomp_patch")))
void REPLACEMENT(void) {}
