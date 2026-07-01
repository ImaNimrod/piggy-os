#pragma once

#include <sys/stat.h>

static inline bool is_same_file(const struct stat* st1, const struct stat* st2) {
    return st1->st_dev == st2->st_dev && st1->st_ino == st2->st_ino;
}

bool file_copy(int src_fd, const char* src_path, int dest_fd, const char* dest_path);
