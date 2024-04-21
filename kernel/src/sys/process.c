#include <mem/heap.h>
#include <sys/process.h>

static pid_t next_pid = 0;

struct process* process_create(struct pagemap* pagemap) {
    struct process* new_process = kmalloc(sizeof(struct process));

    new_process->pid = next_pid++;
    new_process->pagemap = pagemap;

    new_process->threads = (typeof(new_process->threads)) {0};

    return new_process;
}
