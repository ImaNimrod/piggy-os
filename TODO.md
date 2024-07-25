Inital TODO list (5/2/24)

The goal is to get the kernel to load and run a static ELF binary from a limine module.
By this point, the process and thread functionality should be implemented such that following
syscalls are correctly implemented:
    - fork, exit, yield, getpid, gettid, thread_create, thread_sleep, thread_exit

All functionality regarding the VFS and filesystem drivers will be taken care of later.
The same can be said for a grand unified timer abstraction/API and a network stack.

===================================================================================================

Revised TODO list (6/26/24)

The new goal is to have a good chunk of the standard UNIX syscalls for process/thread
management and file system operations present. These should be implemented correctly and have
full input validation for security.

List of syscalls:
    - exit                  (DONE 6/30/24)
    - fork                  (DONE 6/30/24)
    - exec                  (DONE 6/30/24)
    - yield                 (DONE 6/26/24)
    - wait                  (DONE 7/03/24 not originally planned)
    - getpid                (DONE 6/26/24)
    - gettid                (DONE 6/26/24)
    - thread_create         (DONE 6/26/24)
    - thread_exit           (DONE 6/26/24)
    - sbrk                  (DONE 6/30/24 not originally planned)
    - open                  (DONE 6/28/24)
    - close                 (DONE 6/28/24)
    - read                  (DONE 6/26/24)
    - write                 (DONE 6/26/24)
    - ioctl                 (DONE 6/26/24)
    - seek                  (DONE 6/26/24)
    - truncate              (DONE 7/02/24 not originally planned)
    - stat                  (DONE 7/20/24 not originally planned + took a break)
    - chdir                 (DONE 6/26/24)
    - getcwd                (DONE 6/26/24)
    - utsname               (DONE 7/21/24 not originally planned)

Other things that need to happen:
    - syscall pointer argument validation (is the memory userspace, not NULL, etc.) (DONE 6/30/24)
    - create a basic devfs (in memory filesystem like tmpfs) (DONE 7/20/24)
    - create framebuffer device (/dev/fb0) that has working read, write, and ioctl (DONE 7/20/24)
    - create streams (/dev/null, /dev/zero) (DONE 7/20/24)
    - create a function for destroying vmm pagemaps (DONE 6/29/24)
    - implement errno system (errno is the absolute value of a negative syscall return value)
    - move vfs file metadata to a stat struct and implement stat syscall (DONE 7/20/24)
    - refactor file descriptor creation/deletion/management into its own file/api (DONE 6/28/24)
    - actual tty API using framebuffer for output and PS2 keyboard for input
    - write userspace programs that try to break things and fix the small things from there
