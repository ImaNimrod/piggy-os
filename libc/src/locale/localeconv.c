#include <limits.h>
#include <locale.h>

static const struct lconv conv = {
    .currency_symbol = "",
    .decimal_point = ".",
    .frac_digits = CHAR_MAX,
    .grouping = "",
    .int_curr_symbol = "",
    .int_frac_digits = CHAR_MAX,
    .int_n_cs_precedes = CHAR_MAX,
    .int_n_sep_by_space = CHAR_MAX,
    .int_n_sign_posn = CHAR_MAX,
    .int_p_cs_precedes = CHAR_MAX,
    .int_p_sep_by_space = CHAR_MAX,
    .int_p_sign_posn = CHAR_MAX,
    .mon_decimal_point = "",
    .mon_grouping = "",
    .mon_thousands_sep = "",
    .negative_sign = "",
    .n_cs_precedes = CHAR_MAX,
    .n_sep_by_space = CHAR_MAX,
    .n_sign_posn = CHAR_MAX,
    .positive_sign = "",
    .p_cs_precedes = CHAR_MAX,
    .p_sep_by_space = CHAR_MAX,
    .p_sign_posn = CHAR_MAX,
    .thousands_sep = "",
};

struct lconv* localeconv(void) {
    return (struct lconv*) &conv;
}
