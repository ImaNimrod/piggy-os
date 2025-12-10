OUTPUT_FORMAT(elf64-x86-64)

ENTRY(kernel_entry)

PHDRS
{
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    . = 0xffffffff80000000;

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    text_start_addr = .;

    .text : {
        *(.text .text.*)
    } :text

    text_end_addr = .;

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    rodata_start_addr = .;

    .rodata : {
        *(.rodata .rodata.*)
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :rodata

    rodata_end_addr = .;

    . = ALIGN(CONSTANT(MAXPAGESIZE));

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
        *(.eh_frame*)
        *(.note .note.*)
    }
}
