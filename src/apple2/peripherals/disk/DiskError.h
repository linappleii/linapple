#pragma once

typedef enum {
  DISK_ERR_NONE               = 0,
  DISK_ERR_FILE_NOT_FOUND     = 1,
  DISK_ERR_UNSUPPORTED_FORMAT = 2,
  DISK_ERR_CORRUPT            = 3,
  DISK_ERR_WRITE_PROTECTED    = 4,
  DISK_ERR_OUT_OF_MEMORY      = 5,
  DISK_ERR_IO                 = 6
} DiskError_e;
