#ifndef _KERNEL_SYS_SIGNAL_H 
#define _KERNEL_SYS_SIGNAL_H 

#include <cpu/isr.h>

typedef void (*sig_handler_t)(int);

#define SIG_ERR ((sig_handler_t)(void*)(-1))
#define SIG_DFL ((sig_handler_t)(void*)(0))
#define SIG_IGN ((sig_handler_t)(void*)(1))

#define SIGABRT     1
#define SIGALRM     2
#define SIGBUS      3
#define SIGCHLD     4
#define SIGCONT     5
#define SIGFPE      6
#define SIGHUP      7
#define SIGILL      8
#define SIGINT      9
#define SIGKILL     10
#define SIGPIPE     11
#define SIGQUIT     12
#define SIGSEGV     13
#define SIGSTOP     14
#define SIGTERM     15
#define SIGTSTP     16
#define SIGTTIN     17
#define SIGTTOU     18
#define SIGUSR1     19
#define SIGUSR2     20
#define SIGWINCH    21
#define SIGSYS      22
#define SIGTRAP     23
#define SIGURG      24
#define SIGVTALRM   25
#define SIGXCPU     26
#define SIGXFSZ     27

#define SIGCANCEL   28
#define SIGIO       29
#define SIGPOLL     30
#define SIGPROF     31
#define SIGPWR      32

#define SIGRTMIN    33
#define SIGRTMAX    64

#define NSIG SIGRTMAX + 1
#define UNBLOCKABLE_SIGNALS ((1ul << (SIGKILL - 1)) | (1ul << (SIGSTOP - 1)))

#define SA_NODEFER      (1 << 0)
#define SA_ONSTACK      (1 << 1)
#define SA_RESETHAND    (1 << 2)
#define SA_RESTART      (1 << 3)
#define SA_SIGINFO      (1 << 4)

#define SS_DISABLE (1 << 0)
#define SS_ONSTACK (1 << 1)

#define MINSIGSTKSZ 2048

struct process;
struct process_group;
struct thread;

void signal_handle_pending(struct registers* r);
bool signal_on_altstack(struct thread* thread, uintptr_t sp);
[[noreturn]] void signal_restore_signal_frame(struct registers* r);
int signal_send_process(struct process* process, int signal);
int signal_send_process_group(struct process_group* group, int signal);
int signal_send_thread(struct thread* thread, int signal);

#endif /* _KERNEL_SYS_SIGNAL_H */
