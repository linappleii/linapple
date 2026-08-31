// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/IieDriver.h"
#include "apple2/peripherals/disk/formats/Nb2Driver.h"
#include "apple2/peripherals/disk/formats/NibDriver.h"
#include "apple2/peripherals/disk/formats/PoDriver.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"

extern "C" const char* __asan_default_options() {
  return "detect_leaks=0";
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) {
    return 0;
  }

  // 1. Fuzz MacBinary container detection
  disk_container_detect_macbinary(data, size, static_cast<uint32_t>(size));

  // 2. Fuzz individual format driver probes
  static const DiskFormatDriver_t* drivers[] = {
      &g_woz2_driver, &g_nib_driver, &g_nb2_driver,
      &g_iie_driver,  &g_po_driver,  &g_do_driver,
  };

  for (const auto* driver : drivers) {
    if (driver && driver->probe) {
      driver->probe(data, size, static_cast<uint32_t>(size), "fuzz.dsk");
    }
  }

  // 3. Fuzz full disk loading via temp file
  char tmp_template[] = "/tmp/linapple_fuzz_disk_XXXXXX";
  int fd = mkstemp(tmp_template);
  if (fd >= 0) {
    ssize_t written = write(fd, data, size);
    (void)written;
    close(fd);

    disk_loader_init();
    DiskFormatDriver_t* out_driver = nullptr;
    void* out_instance = nullptr;
    bool is_ro = false;

    disk_loader_open(tmp_template, false, 0, &is_ro, &out_driver,
                     &out_instance);

    if (out_driver && out_instance && out_driver->close) {
      out_driver->close(out_instance);
    }

    unlink(tmp_template);
  }

  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
