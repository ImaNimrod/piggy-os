#include <sys/utsname.h>

#include <cpuid.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    struct utsname uts;
    if (uname(&uts) < 0) {
        err(EXIT_FAILURE, "uname");
    }

    char line1[200], line2[200], line3[200];
    snprintf(line1, sizeof(line1), "\033[1;34mOS\033[0m: %s %s", uts.sysname, uts.machine);
    snprintf(line2, sizeof(line2), "\033[1;34mHost\033[0m: %s", uts.nodename);
    snprintf(line3, sizeof(line3), "\033[1;34mKernel\033[0m: %s (%s)", uts.release, uts.version);

    char cpu[48];
    get_cpu_name(cpu, sizeof(cpu));

    char line4[200];
    snprintf(line4, sizeof(line4), "\033[1;34mCPU\033[0m: %s", cpu);

    const char* text[] = { "pigfetch", "--------", line1, line2, line3, line4 };

    size_t logo_lines = SIZEOF_ARRAY(logo);
    size_t text_lines = SIZEOF_ARRAY(text);

    int top_padding = (logo_lines > text_lines) ? (logo_lines - text_lines) / 2 : 0;

    putchar('\n');

    size_t max_lines = (logo_lines > text_lines) ? logo_lines : text_lines + top_padding;
    for (size_t i = 0; i < max_lines; i++) {
        if (i < logo_lines) {
            printf("\033[1;35m%s\033[0m", logo[i]);
        } else {
            printf("         ");
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
