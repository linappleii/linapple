/*
 * HarddiskCommands.h - LinApple Harddisk Peripheral Command Interface
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HARDDISK_DRIVE_0 = 0,
  HARDDISK_DRIVE_1 = 1,
  HARDDISK_DRIVE_COUNT = 2
} HarddiskDrive_e;

typedef enum {
  HARDDISK_CMD_INSERT = 0x0001,      /* payload: HarddiskInsertCmd_t */
  HARDDISK_CMD_EJECT = 0x0002,       /* payload: HarddiskEjectCmd_t */
  HARDDISK_CMD_SET_PROTECT = 0x0004, /* payload: HarddiskSetProtectCmd_t */
  HARDDISK_CMD_GET_STATUS =
      0x0005, /* synchronous only; payload: HarddiskStatus_t* */
  HARDDISK_CMD_RESET_STATUS = 0x0006 /* payload: none */
} HarddiskCmd_e;

#define HARDDISK_INSERT_PATH_MAX 504

#pragma pack(push, 1)
typedef struct {
  uint8_t drive;
  char path[HARDDISK_INSERT_PATH_MAX];
  uint8_t write_protected;
  uint8_t create_if_necessary;
  uint8_t padding[5];
} HarddiskInsertCmd_t;

typedef struct {
  uint8_t drive;
} HarddiskEjectCmd_t;

typedef struct {
  uint8_t drive;
  uint8_t write_protected;
} HarddiskSetProtectCmd_t;

#define HARDDISK_STATUS_NAME_MAX 32
#define HARDDISK_STATUS_PATH_MAX 256

typedef struct {
  int32_t drive0_last_error;
  uint8_t drive0_loaded;
  uint8_t drive0_write_protected;
  char drive0_name[HARDDISK_STATUS_NAME_MAX];
  char drive0_full_path[HARDDISK_STATUS_PATH_MAX];

  int32_t drive1_last_error;
  uint8_t drive1_loaded;
  uint8_t drive1_write_protected;
  char drive1_name[HARDDISK_STATUS_NAME_MAX];
  char drive1_full_path[HARDDISK_STATUS_PATH_MAX];

  uint8_t activity_status; // 0=Off, 1=Read, 2=Write, etc. (matches DISK_STATUS_*)
} HarddiskStatus_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif
