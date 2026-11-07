SECTIONS {
    /* Must match librecomp::patch_rdram_start. End before race-field storage. */
    . = 0x80801000;
    .recomp_patch : AT(0) { *(.recomp_patch) }
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "Guest patches must not retain unsnapshotted state")
    ASSERT(. < 0x80900000, "Guest patches overlap expanded race fields")
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.note*) *(.eh_frame*) *(.got) }
}
