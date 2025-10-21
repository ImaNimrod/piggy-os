#ifndef _KERNEL_CONFIG_H
#define _KERNEL_CONFIG_H

#define RELEASE "0.0.1"

#ifdef GIT_COMMIT
#define VERSION "git-" GIT_COMMIT " " __DATE__ " " __TIME__
#else
#define VERSION __DATE__ " " __TIME__
#endif

#endif /* _KERNEL_CONFIG_H */
