MEMORY {
    extram (ARWX) : ORIGIN = 0x81000000, LENGTH = 64M
}
SECTIONS {
    /DISCARD/ : {
        *(.got) *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment)
    }
}
