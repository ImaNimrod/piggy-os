#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define DEFAULT_INDENT  7
#define MARGIN_SPACE    5

struct roff_context {
    size_t cur_x;
    size_t indent;

    bool padded;

    bool tp_tag_pending;
    bool tp_in_body;
    size_t tp_tag_indent;
    size_t tp_body_indent;

    char* title;
    char* section;
    char* date;

    char* header;
    char* footer;
};

static struct winsize winsz;

static char* next_arg(char* c, char** out);

static bool is_tab_or_space(char c) {
    return (c == ' ' || c == '\t');
}

static inline void newline(struct roff_context* ctx, bool vertical_padding) {
    if (ctx->cur_x != 0) {
        ctx->cur_x = 0;
        printf("\n%s", vertical_padding ? "\n" : "");
        ctx->padded = vertical_padding;
        return;
    } else if (vertical_padding && !ctx->padded) {
        putchar('\n');
        ctx->padded = true;
        return;
    }

    ctx->padded = vertical_padding;
    return;
}

static inline void reset_paragraph(struct roff_context* ctx) {
    ctx->tp_in_body = false;
    ctx->tp_tag_pending = false;
    ctx->indent = DEFAULT_INDENT;
}

static inline void spaces(size_t n) {
    for (size_t i = 0; i < n; i++) {
        putchar(' ');
    }
}

static void ensure_indent(struct roff_context* ctx) {
    if (ctx->cur_x == 0) {
        spaces(ctx->indent);
        ctx->cur_x = ctx->indent;
    }
}

static char* interpret_section(char* section) {
    if (section == NULL) {
        return "Unknown";
    }

    if (strlen(section) == 1 && isdigit(*section)) {
        switch (atoi(section)) {
            case 1:
                return "General Commands Manual";
            case 2:
                return "System Calls Manual";
            case 3:
                return "Library Functions Manual";
            case 4:
                return "Special Files";
            case 5:
                return "File Formats";
            case 6:
                return "Games";
            case 7:
                return "Miscellaneous";
            case 8:
                return "System Administration Manual";
            case 9:
                return "Kernel Programming Manual";
            case 0:
                return "Section Zero";
        }
    }

    return section;
}

static char* next_arg(char* c, char** out) {
    char* value = NULL;
    *out = NULL;

    while (*c && is_tab_or_space(*c)) {
        c++;
    }

    if (*c) {
        int quoted = 0;
        if (*c == '"') {
            c++;
            quoted = 1;
        }

        value = c;

        if (quoted) {
            while (*c && *c != '"') {
                c++;
            }
        } else {
            while (*c && !is_tab_or_space(*c)) {
                c++;
            }
        }

        if (*c) {
            *c++ = '\0';
        }

        while (*c && is_tab_or_space(*c)) {
            c++;
        }
    }

    if (value != NULL) {
        *out = strdup(value);
    }

    return c;
}

// TODO: actually handle ROFF escape sequences
static void print_word(struct roff_context* ctx, const char* word) {
    size_t len = strlen(word);
    size_t printed = 0;

    if (ctx->cur_x && ctx->cur_x + 1 + len > ((size_t)winsz.ws_col - MARGIN_SPACE)) {
        newline(ctx, false);
    }

    ensure_indent(ctx);

    if (ctx->cur_x > ctx->indent) {
        putchar(' ');
        ctx->cur_x++;
        printed++;
    }

    for (size_t i = 0; i < len; i++) {
        if (word[i] == '\\') {
            char c = word[i + 1];
            if (c == '\0') {
                break;
            }

            if (c == 'f') {
                i += 1;
                continue;
            }

            i += 1;
            putchar(c);
            printed++;
            continue;
        }

        putchar(word[i]);
        printed++;
    }

    ctx->cur_x += printed;
}

static void print_words(struct roff_context* ctx, char* line) {
    char* tok = strtok(line, " \t");
    while (tok != NULL) {
        print_word(ctx, tok);
        tok = strtok(NULL, " \t");
    }
}

static void print_title_and_section(const char* title, const char* section) {
    if (title == NULL || section == NULL) {
        printf("??");
        return;
    }

    printf("%s(%s)", title, section);
}

static void print_header(struct roff_context* ctx) {
    char* title = ctx->title;
    char* section = ctx->section;

    size_t title_len = title ? strlen(title) : 0;
    size_t section_len = section ? strlen(section) : 0;
    char* section_name = ctx->header ? ctx->header : interpret_section(section);
    size_t section_name_len = strlen(section_name);

    size_t avail = winsz.ws_col - 1;

    size_t space_used = (title_len + section_len + 2) * 2 + section_name_len;
    if (avail < space_used) {
        print_title_and_section(title, section);
        putchar('\n');
        return;
    }

    size_t space_left = (avail - space_used) / 2;
    size_t space_right = avail - space_used - space_left;

    print_title_and_section(title, section);
    spaces(space_left);
    printf("%s", section_name);
    spaces(space_right);
    print_title_and_section(title, section);
    putchar('\n');
}

static void print_footer(struct roff_context* ctx) {
    size_t title_len = ctx->title ? strlen(ctx->title) : 0;
    size_t section_len = ctx->section ? strlen(ctx->section) : 0;
    size_t date_len = ctx->date ? strlen(ctx->date) : 0;
    size_t footer_len = ctx->footer ? strlen(ctx->footer) : 0;

    size_t avail = winsz.ws_col - 1;
    size_t space_used = title_len + section_len + 2 + date_len + footer_len;

    size_t space_left = (avail - date_len) / 2 - footer_len;
    size_t space_right = avail - space_used - space_left;

    printf("%s", ctx->footer ? ctx->footer : "");
    spaces(space_left);
    printf("%s", ctx->date ? ctx->date : "");
    spaces(space_right);
    print_title_and_section(ctx->title, ctx->section);
    putchar('\n');
}

static int parse_roff(const char* filename, FILE* fp) {
    struct roff_context ctx = {0};

    char* line = NULL;
    size_t n = 0;

    int ret = EXIT_SUCCESS;

    ssize_t len = 0;
    while ((len = getline(&line, &n, fp)) >= 0) {
        if (len && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        if (line[0] == '\'') {
            line[0] = '.';
        }

        if (strstr(line, ".\\\"") == line) {
            continue;
        }

        if (line[0] == '.') {
            if (strncmp(line, ".TP", 3) != 0) {
                reset_paragraph(&ctx);
            }

            if (strstr(line, ".TH ") == line) {
                char* c = line + 4;

                c = next_arg(c, &ctx.title);
                c = next_arg(c, &ctx.section);
                c = next_arg(c, &ctx.date);
                c = next_arg(c, &ctx.footer);
                c = next_arg(c, &ctx.header);

                print_header(&ctx);
                continue;
            } else if (strstr(line, ".SH ") == line) {
                newline(&ctx, true);

                char* c = line + 3;
                char* arg = NULL;

                int first = 1;

                for (;;) {
                    c = next_arg(c, &arg);
                    if (arg == NULL) {
                        break;
                    }

                    if (!first) {
                        putchar(' ');
                        ctx.cur_x++;
                    }
                    first = 0;

                    printf("%s", arg);
                    ctx.cur_x += strlen(arg);

                    free(arg);
                }

                putchar('\n');
                ctx.cur_x = 0;
                continue;
            } else if (strstr(line, ".SS ") == line) {
                newline(&ctx, true);

                char* c = line + 4;
                char* arg = NULL;
                c = next_arg(c, &arg);

                if (arg != NULL) {
                    printf("   %s\n", arg);
                    free(arg);
                }

                ctx.indent = 3;
                ctx.cur_x = 0;
                continue;
            } else if (strstr(line, ".P") == line || strstr(line, ".PP") == line || strstr(line, ".LP") == line) {
                newline(&ctx, true);
                continue;
            } else if (strstr(line, ".TP") == line) {
                newline(&ctx, true);

                ctx.tp_tag_pending = true;
                ctx.tp_in_body = false;

                ctx.tp_tag_indent = ctx.indent;
                ctx.tp_body_indent = ctx.indent + DEFAULT_INDENT;

                ctx.cur_x = 0;
                continue;
            } else if (strstr(line, ".B ") == line || strstr(line, ".I ") == line) {
                // TODO: add actual font formatting
                char* c = line + 2;
                char* arg = NULL;

                for (;;) {
                    c = next_arg(c, &arg);
                    if (arg == NULL) {
                        break;
                    }

                    print_word(&ctx, arg);
                    free(arg);
                }

                continue;
            } else if (strstr(line, ".BR ") == line || strstr(line, ".IR ") == line || strstr(line, ".RB") == line || strstr(line, "BI") == line) {
                char* c = line + 4;
                char* arg = NULL;

                while ((c = next_arg(c, &arg))) {
                    if (arg == NULL) {
                        break;
                    }

                    print_word(&ctx, arg);
                    free(arg);
                }

                continue;
            } else if (strstr(line, ".br") == line) {
                newline(&ctx, 0);
                continue;
            } else {
                warnx("%s: unknown macro on line: '%s'", filename, line);
                ret = EXIT_FAILURE;
                goto end;
            }
        }

        if (ctx.tp_tag_pending) {
            ctx.indent = ctx.tp_tag_indent;
            ctx.cur_x = 0;

            print_words(&ctx, line);
            newline(&ctx, false);

            ctx.tp_tag_pending = false;
            ctx.tp_in_body = true;

            ctx.indent = ctx.tp_body_indent;
            ctx.cur_x = 0;
            continue;
        }

        if (ctx.tp_in_body) {
            print_words(&ctx, line);
            newline(&ctx, false);
            continue;
        }

        print_words(&ctx, line);
        newline(&ctx, false);
    }

    newline(&ctx, true);
    print_footer(&ctx);

end:
    free(line);

    if (ctx.title != NULL) {
        free(ctx.title);
    }
    if (ctx.section != NULL) {
        free(ctx.section);
    }
    if (ctx.date != NULL) {
        free(ctx.date);
    }
    if (ctx.header != NULL) {
        free(ctx.header);
    }
    if (ctx.footer != NULL) {
        free(ctx.footer);
    }

    return ret;
}

static void usage(void) {
    fprintf(stderr, "usage: roff [FILE]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc > 1) {
        warnx("extra operands provided");
        usage();
    }

    if (isatty(STDERR_FILENO)) {
        if (ioctl(STDERR_FILENO, TIOCGWINSZ, &winsz) < 0) {
            err(EXIT_FAILURE, "ioctl");
        }
    }

    char* filename;
    FILE* fp;

    if (argv[0] == NULL || strcmp(argv[0], "-") == 0) {
        filename = "stdin";
        fp = stdin;
    } else {
        filename = argv[0];

        fp = fopen(filename, "r");
        if (fp == NULL) {
            err(EXIT_FAILURE, filename);
        }
    }

    int ret = parse_roff(filename, fp);

    if (fp != stdin) {
        fclose(fp);
    }

    return ret;
}
