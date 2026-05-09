#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "apple2/peripherals/disk/DiskGCR.h"

TEST_CASE("DiskGCR: [GCR-01] GCR_WORKBUF_SIZE is 8KB") {
  CHECK(GCR_WORKBUF_SIZE == 0x2000);
}
