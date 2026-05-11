/*
 * DiskCommands.h - LinApple Disk Peripheral Command Interface
 */

#ifndef DISKCOMMANDS_H
#define DISKCOMMANDS_H

#include <stdint.h>
#include <stdbool.h>
#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct DiskFormatDriver_t;

#ifdef __cplusplus
}
#endif

extern bool enhancedisk;

#ifdef __cplusplus
extern "C" {
#endif

enum { DISK_DEFAULT_SLOT = 6 };

typedef enum {
  DISK_DRIVE_0 = 0,
  DISK_DRIVE_1 = 1,
  DISK_DRIVE_COUNT = 2
} DiskDrive_e;

// Legacy constants for compatibility
#define DRIVE_1 DISK_DRIVE_0
#define DRIVE_2 DISK_DRIVE_1

#define MAX_DISK_IMAGE_NAME 15
#define MAX_DISK_FULL_NAME 255

// Shared constants for Disk II emulation
#define TRACKS 40
#define NIBBLES_PER_TRACK 0x1A00
#define SECTORS_PER_TRACK_16 16
#define NUM_INTERLEAVE_MODES 3

// GCR encoding constants
#define GCR_ENCODE_TABLE_SIZE 64
#define GCR_DECODE_TABLE_SIZE 128
#define GCR_SECTOR_DATA_SIZE 342
#define GCR_SECTOR_WITH_CHECKSUM_SIZE 343

// GCR work buffer offsets
#define GCR_WORK_BUFFER_OFFSET 0x1000
#define GCR_CHECKSUM_BUFFER_OFFSET 0x1400

// GAP sizes
#define GCR_GAP1_SIZE 16
#define GCR_GAP2_SIZE 10
#define GCR_GAP3_SIZE 16

#define DRIVES DISK_DRIVE_COUNT

// Disk internal structures
typedef struct {
  char fullname[MAX_DISK_FULL_NAME + 1];
  char imagename[MAX_DISK_IMAGE_NAME + 1];
  int track;
  int phase;
  uint32_t byte;
  bool user_write_protected;
  bool os_readonly;
  bool trackimagedata;
  bool trackimagedirty;
  uint32_t spinning;
  uint32_t writelight;
  int nibbles;
  uint8_t* trackimage;
  struct DiskFormatDriver_t* driver;
  void* driver_instance;
  DiskError_e last_error;
} Disk_t;

typedef enum {
  DISK_CMD_INSERT      = 0x0001,  /* payload: DiskInsertCmd_t */
  DISK_CMD_EJECT       = 0x0002,  /* payload: DiskEjectCmd_t */
  DISK_CMD_SWAP_DRIVES = 0x0003,  /* no payload */
  DISK_CMD_SET_PROTECT = 0x0004,  /* payload: DiskSetProtectCmd_t */
  DISK_CMD_GET_STATUS  = 0x0005,  /* synchronous only; payload: DiskStatus_t* */
  DISK_CMD_BOOT        = 0x0006   /* no payload */
} DiskCmd_e;

#define DISK_INSERT_PATH_MAX 504

#pragma pack(push, 1)
typedef struct {
  uint8_t drive;
  char    path[DISK_INSERT_PATH_MAX];
  uint8_t write_protected;
  uint8_t create_if_necessary;
  uint8_t padding[5]; // Pad to 512
} DiskInsertCmd_t;

typedef struct {
  uint8_t drive;
} DiskEjectCmd_t;

typedef struct {
  uint8_t drive;
  uint8_t write_protected;
} DiskSetProtectCmd_t;

#define DISK_STATUS_NAME_MAX 32
#define DISK_STATUS_PATH_MAX 256

typedef struct {
  int32_t     drive0_last_error;
  uint8_t     drive0_loaded;
  uint8_t     drive0_spinning;
  uint8_t     drive0_writing;
  uint8_t     drive0_write_protected;
  char        drive0_name[DISK_STATUS_NAME_MAX];
  char        drive0_full_path[DISK_STATUS_PATH_MAX];

  int32_t     drive1_last_error;
  uint8_t     drive1_loaded;
  uint8_t     drive1_spinning;
  uint8_t     drive1_writing;
  uint8_t     drive1_write_protected;
  char        drive1_name[DISK_STATUS_NAME_MAX];
  char        drive1_full_path[DISK_STATUS_PATH_MAX];
} DiskStatus_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif // DISKCOMMANDS_H
