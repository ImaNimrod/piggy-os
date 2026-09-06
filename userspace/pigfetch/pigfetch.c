#include <piggy/sysinfo.h>

#include <cpuid.h>
#include <err.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096

#define SIZEOF_ARRAY(xs) (sizeof((xs)) / sizeof((xs)[0]))

static const char* logo[] = {
    "                         @@@@@@@@@@@@@@                            ",
    "                   @@@@@@@@@@@@@@@@@@@@@@@@@@                      ",
    "               @@@@@@@@@                 @@@@@@@@                  ",
    "           @@@@@@@                            @@@@@@@              ",
    "         @@@@@                                    @@@@@            ",
    "       @@@@@                                        @@@@@          ",
    "      @@@@                                            @@@@         ",
    "     @@@            @@@@                @@@@            @@@        ",
    "    @@@@           @@@@@@@            @@@@@@@            @@@       ",
    "   @@@@           @@@@@@@@@          @@@@@@@@@           @@@@      ",
    "   @@@           @@@@@@@@@@          @@@@@@@@@@           @@@      ",
    "   @@@           @@@@@@@@@@          @@@@@@@@@@           @@@      ",
    "   @@@           @@@@@@@@@@          @@@@@@@@@@           @@@      ",
    "   @@@@           @@@@@@@@@          @@@@@@@@@           @@@@      ",
    "    @@@@           @@@@@@@            @@@@@@@            @@@       ",
    "     @@@@           @@@@                @@@@            @@@        ",
    "      @@@@                                            @@@@         ",
    "       @@@@@                                        @@@@@          ",
    "         @@@@@                                    @@@@@            ",
    "           @@@@@@@                            @@@@@@@              ",
    "               @@@@@@@@@                @@@@@@@@@@                 ",
    "                   @@@@@@@@@@@@@@@@@@@@@@@@@@@                     ",
    "                         @@@@@@@@@@@@@@                            ",
};

static void get_cpu_name(char* buf, size_t len) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    if (!__get_cpuid(0x80000002, &eax, &ebx, &ecx, &edx)) {
        goto error;
    }

    ((uint32_t*) buf)[0] = eax;
    ((uint32_t*) buf)[1] = ebx;
    ((uint32_t*) buf)[2] = ecx;
    ((uint32_t*) buf)[3] = edx;

    if (!__get_cpuid(0x80000003, &eax, &ebx, &ecx, &edx)) {
        goto error;
    }

    ((uint32_t*) buf)[4] = eax;
    ((uint32_t*) buf)[5] = ebx;
    ((uint32_t*) buf)[6] = ecx;
    ((uint32_t*) buf)[7] = edx;

    if (!__get_cpuid(0x80000004, &eax, &ebx, &ecx, &edx)) {
        goto error;
    }

    ((uint32_t*) buf)[8] = eax;
    ((uint32_t*) buf)[9] = ebx;
    ((uint32_t*) buf)[10] = ecx;
    ((uint32_t*) buf)[11] = edx;

    return;

error:
    snprintf(buf, len, "Unknown");
}

int main(void) {
    struct sysinfo info;
    if (sysinfo(&info) < 0) {
        err(EXIT_FAILURE, "sysinfo");
    }

    char line1[150], line2[32 + HOST_NAME_MAX], line3[150];
    snprintf(line1, sizeof(line1), "\033[1;34mOS\033[0m: %s %s", info.sysname, info.machine);
    snprintf(line2, sizeof(line2), "\033[1;34mHost\033[0m: %s", info.hostname);
    snprintf(line3, sizeof(line3), "\033[1;34mKernel\033[0m: %s (%s)", info.release, info.version);

    char cpu[48];
    get_cpu_name(cpu, sizeof(cpu));

    char line4[64];
    snprintf(line4, sizeof(line4), "\033[1;34mCPU\033[0m: %s", cpu);

    size_t total_mem_mib = ((info.total_mem_pages * PAGE_SIZE) >> 20);
    size_t used_mem_mib  = (((info.total_mem_pages - info.free_mem_pages) * PAGE_SIZE) >> 20);

    char line5[64];
    snprintf(line5, sizeof(line5), "\033[1;34mMemory\033[0m: %zuMiB / %zuMib",
            used_mem_mib, total_mem_mib);

    // Generate color palette, normal then bright
    char line6[150];

    char* ptr = line6;
    for (int i = 0; i < 8; i++) {
        ptr += snprintf(ptr, sizeof(line6) - (ptr - line6), "\033[%dm   \033[0m", 40 + i);
    }

    char line7[150];

    ptr = line7;
    for (int i = 0; i < 8; i++) {
        ptr += snprintf(ptr, sizeof(line7) - (ptr - line7), "\033[%dm   \033[0m", 100 + i);
    }

    const char* text[] = {
        "pigfetch",
        "--------",
        line1,
        line2,
        line3,
        line4,
        line5,
        "", "",
        line6,
        line7,
    };

    size_t logo_lines = SIZEOF_ARRAY(logo);
    size_t text_lines = SIZEOF_ARRAY(text);

    int top_padding = (logo_lines > text_lines) ? (logo_lines - text_lines) / 2 : 0;

    putchar('\n');

    size_t max_lines = (logo_lines > text_lines) ? logo_lines : text_lines + top_padding;
    for (size_t i = 0; i < max_lines; i++) {
        if (i < logo_lines) {
            printf("\033[1;35m%s\033[0m", logo[i]);
        } else {
            printf("          ");
        }

        printf("  ");

        size_t text_index = i - top_padding;
        if (text_index < text_lines) {
            printf("%s", text[text_index]);
        }

        putchar('\n');
    }

    putchar('\n');
    return EXIT_SUCCESS;
}
