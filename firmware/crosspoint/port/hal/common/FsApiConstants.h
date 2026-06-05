/* WODLE-PORT: SdFat oflag constants → DFS POSIX fcntl. */
#pragma once

#include <fcntl.h>

typedef int oflag_t;

/* SdFat extras that fcntl.h doesn't define */
#ifndef O_AT_END
#define O_AT_END 0x4000000 /* open at EOF; handled in HalStorage::open */
#endif
