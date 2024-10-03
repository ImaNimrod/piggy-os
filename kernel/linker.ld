OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(kernel_entry)

PHDRS
{
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ;
    rodata  PT_LOAD    FLAGS((1 << 2)) ;
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ;
}

SECTIONS
{
    . = 0xffffffff80000000;

    text_start_addr = .;

    .text : {
        *(.text .text.*)

        . += CONSTANT(MAXPAGESIZE);

        unmap_after_init_start_addr = .;
        *(.unmap_after_init .unmap_after_init.*)
        unmap_after_init_end_addr = .;
    } :text

    text_end_addr = .;

    . += CONSTANT(MAXPAGESIZE);

    rodata_start_addr = .;

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    rodata_end_addr = .;

    . += CONSTANT(MAXPAGESIZE);

    data_start_addr = .;

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    data_end_addr = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
        *(.comment)
        *(.note.gnu.build-id)
    }
}
