#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

#include <cstdlib>
#include <cstring>

#include "apple2/peripherals/disk/DiskGCR.h"
#include "apple2/peripherals/disk/DiskCommands.h"

struct SectorDiskImage_t {
  FILE* file;
  uint32_t macbinary_offset;
  bool os_readonly;
  bool is_dos_order;
  bool is_enhanced;
  uint8_t work_buffer[GCR_WORKBUF_SIZE];
};

namespace {
constexpr int DOS_TRACK_SIZE = 4096;
constexpr int DISK_SIZE_140K = 143360;
} // namespace

SectorDiskImage_t* SectorDiskImage_Open(const char* path, uint32_t file_offset,
                                        bool is_dos_order, uint8_t enhanced_speed,
                                        bool* out_os_readonly) {
  auto* image = static_cast<SectorDiskImage_t*>(calloc(1, sizeof(SectorDiskImage_t)));
  if (!image) return nullptr;

  image->file = fopen(path, "r+b");
  if (image->file != nullptr) {
    image->os_readonly = false;
  } else {
    image->file = fopen(path, "rb");
    if (image->file != nullptr) {
      image->os_readonly = true;
    } else {
      free(image);
      return nullptr;
    }
  }

  if (out_os_readonly) *out_os_readonly = image->os_readonly;
  image->macbinary_offset = file_offset;
  image->is_dos_order = is_dos_order;
  image->is_enhanced = (enhanced_speed != 0);

  return image;
}

void SectorDiskImage_Close(SectorDiskImage_t* image) {
  if (image) {
    if (image->file) fclose(image->file);
    free(image);
  }
}

bool SectorDiskImage_IsWriteProtected(SectorDiskImage_t* image) {
  return image ? image->os_readonly : true;
}

void SectorDiskImage_ReadTrack(SectorDiskImage_t* image, int track,
                               uint8_t* trackImageBuffer, int* nibbles_out) {
  if (!image || track < 0 || track >= TRACKS) {
    if (nibbles_out) *nibbles_out = 0;
    return;
  }

  // Pre-fill with sync bytes
  memset(trackImageBuffer, 0xFF, NIBBLES_PER_TRACK);

  memset(image->work_buffer, 0, GCR_WORKBUF_SIZE);
  if (fseek(image->file, static_cast<long>(image->macbinary_offset + (track * DOS_TRACK_SIZE)), SEEK_SET) != 0) {
    if (nibbles_out) *nibbles_out = 0;
    return;
  }

  if (fread(image->work_buffer, 1, DOS_TRACK_SIZE, image->file) != DOS_TRACK_SIZE) {
    if (nibbles_out) *nibbles_out = 0;
    return;
  }

  uint32_t nibbles = GCR_NibblizeTrack(image->work_buffer, trackImageBuffer, image->is_dos_order, track);
  
  if (!image->is_enhanced) {
    GCR_SkewTrack(image->work_buffer, track, static_cast<int>(nibbles), trackImageBuffer);
  }

  if (nibbles_out) *nibbles_out = NIBBLES_PER_TRACK;
}

void SectorDiskImage_WriteTrack(SectorDiskImage_t* image, int track,
                                const uint8_t* trackImage, int nibbles) {
  if (!image || image->os_readonly || track < 0 || track >= TRACKS) return;

  memset(image->work_buffer, 0, GCR_WORKBUF_SIZE);
  GCR_DenibblizeTrack(image->work_buffer, const_cast<uint8_t*>(trackImage), image->is_dos_order, nibbles);
  
  if (fseek(image->file, static_cast<long>(image->macbinary_offset + (track * DOS_TRACK_SIZE)), SEEK_SET) == 0) {
    (void)fwrite(image->work_buffer, 1, DOS_TRACK_SIZE, image->file);
  }
}

DiskError_e SectorDiskImage_Create(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return DISK_ERR_IO;

  uint8_t zero[1024] = {0};
  for (int i = 0; i < DISK_SIZE_140K / 1024; ++i) {
    fwrite(zero, 1, sizeof(zero), f);
  }
  fclose(f);
  return DISK_ERR_NONE;
}

PeripheralStatus SectorDiskImage_Command(SectorDiskImage_t* image, uint32_t cmd_id,
                                        const void* data, size_t size) {
  if (!image) return PERIPHERAL_ERROR;

  if (cmd_id == DISK_DRIVER_CMD_SET_ENHANCED_SPEED) {
    if (size < 1) return PERIPHERAL_ERROR;
    image->is_enhanced = (*static_cast<const uint8_t*>(data) != 0);
    return PERIPHERAL_OK;
  }
  return PERIPHERAL_INCOMPATIBLE;
}
