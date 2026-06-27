#ifndef _KERNEL_DEV_CHAR_FB_H
#define _KERNEL_DEV_CHAR_FB_H

#define FB_DEV_MAJOR 4

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOBLANK           0x4611

extern struct flanterm_context* fb_context;
extern bool flanterm_console_decckm;

void fb_dev_init(void);
void fb_dev_early_init(void);

#endif /* _KERNEL_DEV_CHAR_FB_H */
