#include "core/ProgramLoader.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "apple2/CPU.h"
#include "apple2/Memory.h"

constexpr uint16_t IO_REGION_END = 0xCFFF;
constexpr uint32_t PRG_HEADER_SIZE = 128;
constexpr uint32_t APL_HEADER_SIZE = 4;
constexpr uint8_t MEM_FILL_VALUE = 0xFF;

namespace {
constexpr uint32_t PRG_MAGIC = 0x214C470A;

enum class ProgramType { None, Prg, Apl };

struct ProgramHeader {
  ProgramType type;
  uint16_t load_addr;
  uint32_t length;
  uint32_t offset;
};

static auto DetectProgram(const char* path, ProgramHeader* out_header)
    -> ProgramLoadResult_e {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) {
    return PROGRAM_LOAD_FILE_ERROR;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return PROGRAM_LOAD_FILE_ERROR;
  }
  long ftell_res = ftell(f);
  if (ftell_res < 0) {
    fclose(f);
    return PROGRAM_LOAD_FILE_ERROR;
  }
  uint32_t file_size = static_cast<uint32_t>(ftell_res);
  fseek(f, 0, SEEK_SET);

  uint8_t buffer[PRG_HEADER_SIZE];
  size_t read = fread(buffer, 1, sizeof(buffer), f);
  fclose(f);

  if (read < 8) {
    return PROGRAM_LOAD_NOT_A_PROGRAM;
  }

  uint32_t magic = 0;
  memcpy(&magic, buffer, 4);
  if (magic == PRG_MAGIC) {
    uint16_t word_len = 0;
    out_header->type = ProgramType::Prg;
    memcpy(&out_header->load_addr, buffer + 5, 2);
    memcpy(&word_len, buffer + 7, 2);
    out_header->length = static_cast<uint32_t>(word_len) << 1;
    out_header->offset = PRG_HEADER_SIZE;
    return PROGRAM_LOAD_OK;
  }

  // APL Check (heuristic)
  uint16_t apl_len = 0;
  memcpy(&apl_len, buffer + 2, 2);
  bool size_match =
      ((static_cast<uint32_t>(apl_len) + APL_HEADER_SIZE) == file_size) ||
      ((static_cast<uint32_t>(apl_len) + APL_HEADER_SIZE +
        ((256 - ((apl_len + APL_HEADER_SIZE) & 255)) & 255)) == file_size);

  if (size_match) {
    out_header->type = ProgramType::Apl;
    memcpy(&out_header->load_addr, buffer, 2);
    out_header->length = apl_len;
    out_header->offset = APL_HEADER_SIZE;
    return PROGRAM_LOAD_OK;
  }

  return PROGRAM_LOAD_NOT_A_PROGRAM;
}
}  // namespace

auto ProgramLoader_TryLoad(const char* path) -> ProgramLoadResult_e {
  ProgramHeader header = {ProgramType::None, 0, 0, 0};
  ProgramLoadResult_e res = DetectProgram(path, &header);

  if (res != PROGRAM_LOAD_OK) {
    return res;
  }

  // Range check: reject I/O region $C000-$CFFF
  if (header.load_addr >= IO_REGION_START &&
      header.load_addr <= IO_REGION_END) {
    return PROGRAM_LOAD_INVALID;
  }
  // Reject if program ends in or past I/O region
  if (static_cast<uint64_t>(header.load_addr) + header.length >
      IO_REGION_START) {
    return PROGRAM_LOAD_INVALID;
  }

  FILE* f = fopen(path, "rb");
  if (f == nullptr) {
    return PROGRAM_LOAD_FILE_ERROR;
  }

  if (fseek(f, static_cast<long>(header.offset), SEEK_SET) != 0) {
    fclose(f);
    return PROGRAM_LOAD_FILE_ERROR;
  }
  if (fread(mem + header.load_addr, 1, header.length, f) != header.length) {
    fclose(f);
    return PROGRAM_LOAD_FILE_ERROR;
  }
  fclose(f);

  memset(memdirty, MEM_FILL_VALUE, NUM_PAGES_48K);
  CpuGetRegisters()->pc = header.load_addr;

  return PROGRAM_LOAD_OK;
}
