#pragma once

#include <sys/stat.h>

bool file_copy(int src_fd, const char* src_path, int dest_fd, const char* dest_path);
bool get_prompt(void);
bool is_same_file(const struct stat* st1, const struct stat* st2);

