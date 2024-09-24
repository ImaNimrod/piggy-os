#ifndef _MATH_H
#define _MATH_H

#define NAN       __builtin_nanf("")
#define INFINITY  __builtin_inff()

#define HUGE_VALF   INFINITY
#define HUGE_VAL    ((double) INFINITY)
#define HUGE_VALL   ((long double) INFINITY)

double acos(double);
double asin(double);
double atan2(double, double);
double ceil(double);
double cos(double);
double cosh(double);
double exp(double);
double fabs(double);
double floor(double);
double fmod(double, double);
double frexp(double, int*);
double ldexp(double, int);
double log(double);
double log10(double);
double log2(double);
double pow(double, double);
double sin(double);
double sinh(double);
double sqrt(double);
double tan(double);
double tanh(double);

#endif /* _MATH_H */
