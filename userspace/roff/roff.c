#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define DEFAULT_INDENTATION 7
#define MARGIN_SPACE        5

static struct winsize winsize;

struct roff_context {
    int current_x;
    int indent;
    int next_indent;
    int extra_indent;
    bool printing_table;
    bool squish_line;
    unsigned int previous_font;
    unsigned int current_font;

    char* topic_title;
    char* topic_section;
    char* topic_date;
    char* topic_footer;
    char* topic_header;

    bool padded;
    bool just_did_re;
    unsigned int active_font;
};

static inline bool is_tab_or_space(char c) {
    return (c == ' ' || c == '\t');
}

static inline void print_title_and_section(const char* title, const char* section) {
    if (!title || !section) {
        printf("??");
        return;
    }

    printf("\033[4m%s\033[24m(%s)", title, section);
}

static inline void spaces(size_t n) {
    for (size_t i = 0; i < n; i++) {
        putchar(' ');
    }
}

static char* interpret_section(char* section) {
    if (!section) {
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

static void format_title(struct roff_context* ctx) {
    char* title = ctx->topic_title;
    char* section = ctx->topic_section;

    size_t title_len = title ? strlen(title) : 0;
    size_t section_len = section ? strlen(section) : 0;
    char* section_name = ctx->topic_header ? ctx->topic_header : interpret_section(section);
    size_t section_name_len = strlen(section_name);

    size_t avail = winsize.ws_col;

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
    printf("\n\n");
    ctx->padded = true;
}

static void format_footer(struct roff_context * ctx) {
    size_t title_len = ctx->topic_title ? strlen(ctx->topic_title) : 0;
    size_t section_len = ctx->topic_section ? strlen(ctx->topic_section) : 0;
    size_t date_len = ctx->topic_date ? strlen(ctx->topic_date) : 0;
    size_t footer_len = ctx->topic_footer ? strlen(ctx->topic_footer) : 0;

    size_t avail = winsize.ws_col;
    size_t space_used = title_len + section_len + 2 + date_len + footer_len;

    size_t space_left = (avail - date_len) / 2 - footer_len;
    size_t space_right = avail - space_used - space_left;

    printf("%s", ctx->topic_footer ? ctx->topic_footer : "");
    spaces(space_left);
    printf("%s", ctx->topic_date ? ctx->topic_date : "");
    spaces(space_right);
    print_title_and_section(ctx->topic_title, ctx->topic_section);
    putchar('\n');
}

static int skip_escape(char* x, size_t* len) {
    switch (x[1]) {
        case 'f':
            if (x[2] == '(') {
                if (x[3] == 0) {
                    return 3;
                }
                if (x[4] == 0) {
                    return 4;
                }
                return 5;
            } else if (x[2] == 0) {
                return 2;
            }
            return 3;
        case '"':
            char* c = x;
            while (*c) {
                c++;
            }
            return c - x;
        case ',':
        case '/':
            return 2;
        case '?':
            return 2;
        case '(':
            (*len)++;
            if (!x[2]) {
                return 2;
            }
            if (!x[3]) {
                return 3;
            }
			return 4;
        default:
            (*len)++;
            return 2;
    }
}

#define PAIR(a, b) (((unsigned int) a << 8) | (unsigned int) b)

static void real_activate_font(struct roff_context* ctx, unsigned int desired_font) {
    if (ctx->active_font == desired_font) {
        return;
    }

    int want = (desired_font == 'B' ? 1 : 0) | (desired_font == 'I' ? 2 : 0) | (desired_font == PAIR('B','I') ? 3 : 0);
    int have = (ctx->active_font == 'B' ? 1 : 0) | (ctx->active_font == 'I' ? 2 : 0) | (ctx->active_font == PAIR('B','I') ? 3 : 0);
    int changed = want ^ have;
    if (changed) {
        printf("\033[%s%s%sm",
                (changed & 1) ? ((want & 1) ? "1" : "22") : "",
                (changed == 3) ? ";" : "",
                (changed & 2) ? ((want & 2) ? "4" : "24") : "");
    }

    ctx->active_font = desired_font;
}

static int flush_line(struct roff_context* ctx, bool for_vertical_padding) {
    if (ctx->current_x != 0) {
        ctx->current_x = 0;
        real_activate_font(ctx, 0);
        printf("\n%s", for_vertical_padding ? "\n" : "");
        ctx->padded = for_vertical_padding;
        return 1;
    } else if (for_vertical_padding && !ctx->padded) {
        real_activate_font(ctx, 0);
        putchar('\n');
        ctx->padded = true;
        return 1;
    }

    ctx->padded = for_vertical_padding;
    return 0;
}

static void switch_font(struct roff_context* ctx, unsigned int font) {
    if (font == 'P') {
        ctx->current_font = ctx->previous_font;
    } else {
        ctx->previous_font = ctx->current_font;
        ctx->current_font = font;
    }

    switch (ctx->current_font) {
        case '1':
            ctx->current_font = 'R';
            break;
        case '2':
            ctx->current_font = 'I';
            break;
        case '3':
            ctx->current_font = 'B';
            break;
        case '4':
            ctx->current_font = PAIR('B','I');
            break;
        case PAIR('C','B'):
            ctx->current_font = 'B';
            break;
        case PAIR('C','I'):
            ctx->current_font = 'I';
            break;
        case PAIR('C','R'):
            ctx->current_font = 'R';
            break;
        case PAIR('C','W'):
            ctx->current_font = 'R';
            break;
    }
}

static int do_escape(struct roff_context* ctx, char* x) {
    switch (x[1]) {
        case 'f':
            if (x[2] == 0) {
                return 2;
            }

			if (x[2] == '(') {
				if (x[3] == 0) {
                    return 3;
                }
				if (x[4] == 0) {
                    return 4;
                }

                switch_font(ctx, PAIR(x[3], x[4]));
            } else {
                switch_font(ctx, (unsigned int) x[2]);
            }

            real_activate_font(ctx, ctx->current_font);
            return x[2] == '(' ? 5 : 3;
        case '"':
            char* c = x;
            while (*c) {
                c++;
            }
            return c - x;
        case 'e':
            putchar('\\');
            return 2;
		case ',':
		case '/':
		case '&':
            return 2;
		case '(':
			if (!x[2]) {
                return 2;
            }
            if (!x[3]) {
                return 3;
            }

            switch (PAIR(x[2],x[3])) {
                case PAIR('h','a'):
                    putchar('^');
                    break;
                case PAIR('t','i'):
                    putchar('~');
                    break;
                case PAIR('a','q'):
                    putchar('\'');
                    break;
                case PAIR('d','q'):
                    putchar('"');
                    break;
                default:
                    putchar('?');
                    break;
            }
            return 4;
        default:
            putchar(x[1]);
			return 2;
	}
}

static void do_tab_or_space(struct roff_context * ctx, char c) {
    if (c == '\t') {
        putchar(' ');
        ctx->current_x++;

        while ((ctx->current_x - ctx->indent) % MARGIN_SPACE) {
            putchar(' ');
            ctx->current_x++;
        }
    } else {
        putchar(' ');
        ctx->current_x++;
    }
}

static size_t process_word(struct roff_context* ctx, char* c, bool delimited) {
    char* c_in = c;
    char* last_word = c;
	size_t last_len = 0;

    while (*c && (ctx->printing_table || !is_tab_or_space(*c))) {
        if (*c == '\\' && c[1]) {
            c += skip_escape(c, &last_len);
        } else {
            last_len++;
			c++;
		}
	}

    if (!last_len) {
        while (*c && is_tab_or_space(*c)) {
            c++;
        }
		return c - c_in;
	}

    if (last_len + ctx->current_x > (size_t) winsize.ws_col - MARGIN_SPACE) {
		flush_line(ctx, false);
		spaces(ctx->indent);
		ctx->current_x += ctx->indent;
	} else if (ctx->current_x == 0) {
		spaces(ctx->indent);
		ctx->current_x += ctx->indent;
	}

	ctx->padded = false;

	real_activate_font(ctx, ctx->current_font);

    char* x = last_word;
    while (*x && x != c) {
        if (*x == '\\' && x[1]) {
            x += do_escape(ctx, x);
		} else {
            putchar(*x);
            x++;
        }
	}
	ctx->current_x += last_len;

    if (!*c) {
        real_activate_font(ctx, 0);
    }

	if (ctx->printing_table || delimited) {
        bool something = false;
        while (*c && is_tab_or_space(*c) && (size_t) ctx->current_x < (size_t) winsize.ws_col - MARGIN_SPACE) {
            something = true;
			do_tab_or_space(ctx, *c);
			c++;
		}

        if (delimited && !something) {
            do_tab_or_space(ctx, ' ');
        }
	}

    while (*c && is_tab_or_space(*c)) {
        c++;
    }

	return c - c_in;
}

static char* collect_arg(char* c, char** out) {
	char* value = NULL;

    *out = NULL;

    while (*c && is_tab_or_space(*c)) {
        c++;
    }

    if (*c) {
		bool quoted = false;
        if (*c == '"') {
            c++;
            quoted = true;
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

    if (value) {
        *out = strdup(value);
        if (!*out) {
            err(EXIT_FAILURE, "strdup");
        }
    }

	return c;
}

static size_t process_arg(struct roff_context* ctx, char* c, int delimit_last) {
    char* c_in = c;

    char* arg = NULL;
    c = collect_arg(c, &arg);

    if (arg) {
        char* ca = arg;

        bool was_table = ctx->printing_table;
        ctx->printing_table = true;

        while (*ca) {
            ca += process_word(ctx, ca, 0);
        }

        ctx->printing_table = was_table;

        free(arg);
    }

    if (delimit_last) {
        putchar(' ');
        ctx->current_x++;
    }

	return c - c_in;
}

static bool parse_roff(const char* filename) {
    FILE* fp = !strcmp(filename,"-") ? stdin : fopen(filename, "r");
    if (!fp) {
        warn("%s", filename);
        return false;
    }

    struct roff_context ctx = {};
    ctx.previous_font = ctx.current_font = 'R';

    char* line = NULL;
    size_t avail = 0;
    ssize_t len;

	bool ret = true;

    while ((len = getline(&line, &avail, fp)) >= 0) {
        if (len && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }

        if (*line == '\'') {
            line[0] = '.';
        }

        if (strstr(line, ".\\\"") == line) {
            continue;
        }

        char* c = line;
        bool delimited = true;
        bool force_padded = false;

        bool just_did_re = ctx.just_did_re;
        ctx.just_did_re = false;

        if (line[0] == '.') {
            if (!strncmp(line, ".TH ", 4)) {
                char* c = line + 4;
                c = collect_arg(c, &ctx.topic_title);
                c = collect_arg(c, &ctx.topic_section);
                c = collect_arg(c, &ctx.topic_date);
                c = collect_arg(c, &ctx.topic_footer);
                c = collect_arg(c, &ctx.topic_header);

                format_title(&ctx);
                continue;
            } else if (!strncmp(line, ".SH ", 4)) {
                flush_line(&ctx, true);
                c = line + 4;
                switch_font(&ctx, 'B');
                ctx.indent = 0;
                ctx.extra_indent = 0;
				ctx.next_indent = DEFAULT_INDENTATION;
				ctx.squish_line = false;
				force_padded = true;
            } else if (!strncmp(line, ".SS ", 4)) {
                flush_line(&ctx, true);
                c = line + 4;
                switch_font(&ctx, 'B');
                ctx.indent = 3;
                ctx.extra_indent = 0;
                ctx.next_indent = DEFAULT_INDENTATION;
				ctx.squish_line = false;
				force_padded = true;
            } else if (!strncmp(line, ".P", 2) || !strncmp(line, ".PP", 3) || !strncmp(line, ".LP", 3)) {
                flush_line(&ctx, true);
                ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
                ctx.next_indent = 0;
                continue;
            } else if (!strncmp(line, ".TP", 3)) {
				flush_line(&ctx, true);

                int howmuch = DEFAULT_INDENTATION;
                char* arg;
                c = collect_arg(c + 3, &arg);
                if (arg) {
                    howmuch = atoi(arg);
                    free(arg);
                }

                ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
                ctx.next_indent = ctx.extra_indent + DEFAULT_INDENTATION + howmuch;
                ctx.squish_line = true;
                continue;
            } else if (!strncmp(line, ".B ", 3)) {
                switch_font(&ctx, 'B');
                c = line + 3;
            } else if (!strncmp(line, ".I ", 3)) {
                switch_font(&ctx, 'I');
                c = line + 3;
            } else if (!strncmp(line, ".IR ", 4)) {
                c = line + 4;
                while (*c) {
                    switch_font(&ctx, 'I');
                    c += process_arg(&ctx, c, 0);
                    switch_font(&ctx, 'R');
                    c += process_arg(&ctx, c, 0);
                }

                spaces(1);
				ctx.current_x++;
				goto processed_line;
            } else if (!strncmp(line, ".BR ", 4)) {
				c = line + 4;
				while (*c) {
					switch_font(&ctx, 'B');
					c += process_arg(&ctx, c, 0);
					switch_font(&ctx, 'R');
					c += process_arg(&ctx, c, 0);
				}

				spaces(1);
				ctx.current_x++;
				goto processed_line;
            } else if (!strncmp(line, ".RI ", 4)) {
				c = line + 4;
				while (*c) {
					switch_font(&ctx, 'R');
					c += process_arg(&ctx, c, 0);
					switch_font(&ctx, 'I');
					c += process_arg(&ctx, c, 0);
                }

                switch_font(&ctx, 'R');
                spaces(1);
                ctx.current_x++;
                goto processed_line;
            } else if (!strncmp(line, ".RB ", 4)) {
                c = line + 4;
                while (*c) {
                    switch_font(&ctx, 'R');
                    c += process_arg(&ctx, c, 0);
                    switch_font(&ctx, 'B');
                    c += process_arg(&ctx, c, 0);
                }

                switch_font(&ctx, 'R');
                spaces(1);
                ctx.current_x++;
                goto processed_line;
            } else if (!strncmp(line, ".IP ", 4)) {
                flush_line(&ctx, true);
                ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
                ctx.next_indent = ctx.extra_indent + DEFAULT_INDENTATION * 2;
                c = line + 4;
                ctx.squish_line = true;
                force_padded = true;
            } else if (!strncmp(line, ".IP", 3)) {
                flush_line(&ctx, true);
                ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
                ctx.next_indent = ctx.extra_indent + DEFAULT_INDENTATION;
                c = line + 3;
                ctx.squish_line = true;
            } else if (!strncmp(line, ".br", 3)) {
                flush_line(&ctx, false);
                continue;
            } else if (!strncmp(line, ".sp", 3)) {
                flush_line(&ctx, false);
                putchar('\n');
                ctx.padded = 1;
                continue;
            } else if (!strncmp(line, ".RS", 3)) {
                ctx.extra_indent += DEFAULT_INDENTATION;
                flush_line(&ctx, !ctx.padded && (ctx.indent == ctx.extra_indent + DEFAULT_INDENTATION || just_did_re));
                ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
                ctx.next_indent = 0;
                continue;
            } else if (!strncmp(line, ".RE", 3)) {
                ctx.extra_indent -= DEFAULT_INDENTATION;
                ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
                ctx.next_indent = 0;
                flush_line(&ctx, false);
                ctx.just_did_re = true;
                continue;
            } else if (!strncmp(line, ".nf", 3)) {
                flush_line(&ctx, false);
                ctx.printing_table = true;
                continue;
            } else if (!strncmp(line, ".fi", 3)) {
                flush_line(&ctx, false);
                ctx.printing_table = false;
                continue;
            } else if (!strncmp(line, ".TS", 3)) {
                flush_line(&ctx, false);
                ctx.printing_table = true;
                continue;
            } else if (!strncmp(line, ".TE", 3)) {
                flush_line(&ctx, false);
                putchar('\n');
                ctx.padded = true;
                ctx.printing_table = false;
                continue;
            } else {
                printf("%s: found an unrecognized macro on line: '%s'\n", filename, line);
                ret = false;
                goto cleanup;
            }
        }

        if (!*c && !ctx.printing_table) {
            flush_line(&ctx, true);
        }

        if (is_tab_or_space(*c)) {
            flush_line(&ctx, false);
            spaces(ctx.indent);
            ctx.current_x += ctx.indent;

            while (is_tab_or_space(*c)) {
                do_tab_or_space(&ctx, *c);
                c++;
            }
        }

        while (*c) {
            c += process_word(&ctx, c, delimited);
        }

processed_line:
        real_activate_font(&ctx, 0);

        if (ctx.printing_table) {
            putchar('\n');
            ctx.current_x = 0;
            ctx.padded = false;
            continue;
        }

        ctx.current_font = ctx.previous_font = 'R';

        if (ctx.next_indent) {
            ctx.indent = ctx.next_indent;
            ctx.next_indent = 0;

            if (ctx.squish_line && ctx.current_x < (ctx.indent + delimited)) {
                ctx.squish_line = false;
                while (ctx.current_x < ctx.indent) {
                    putchar(' ');
                    ctx.current_x++;
                }
            } else {
                flush_line(&ctx, false);
            }
        }

        if (ctx.current_x) {
            ctx.padded = false;
        }

        if (force_padded) {
            ctx.padded = true;
        }
    }

    flush_line(&ctx, true);
    format_footer(&ctx);

cleanup:
    if (ctx.topic_title) {
        free(ctx.topic_title);
    }
    if (ctx.topic_section) {
        free(ctx.topic_section);
    }
    if (ctx.topic_date) {
        free(ctx.topic_date);
    }

    if (fp != stdin) {
        fclose(fp);
    }

    free(line);
    return ret;
}

static void usage(void) {
    fprintf(stderr, "usage: roff [FILE]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (isatty(STDERR_FILENO)) {
        if (ioctl(STDERR_FILENO, TIOCGWINSZ, &winsize) < 0) {
            err(EXIT_FAILURE, "ioctl");
        }
    }

    int ret = EXIT_SUCCESS;

    if (argc < 1) {
        ret = parse_roff("-");
    } else {
        for (int i = 0; i < argc; i++) {
            if (!parse_roff(argv[i])) {
                ret = EXIT_FAILURE;
            }
        }
    }

    return ret;
}
