#include <errno.h>
#include <string.h>

char* strerror(int errnum) {
    switch (errnum) {
        case 0:
            return "No error";
        case EAGAIN:
            return "Resource temporarily unavailable";
        case EBADF:
            return "Bad file descriptor";
        case ECHILD:
            return "No child processes";
        case EEXIST:
            return "File exists";
        case EFAULT:
            return "Bad address";
        case EIO:
            return "I/O error";
        case EINVAL:
            return "Invalid argument";
        case EISDIR:
            return "Is a directory";
        case EMFILE:
            return "Too many files open in system";
        case ENAMETOOLONG:
            return "Name too long";
        case ENOENT:
            return "No such file or directory";
        case ENOEXEC:
            return "Executable file format error";
        case ENOMEM:
            return "Not enough space";
        case ENOSYS:
            return "Syscall not supported";
        case ENOTDIR:
            return "Not a directory";
        case ENOTTY:
            return "Unsupported ioctl for device";
        case EPERM:
            return "Operation not permitted";
        case ESPIPE:
            return "Invalid seek";
        case EOVERFLOW:
            return "Value too large for supplied data type";
        case ERANGE:
            return "Result too large";
    }

    errno = EINVAL;
    return "Unknown error";
}
