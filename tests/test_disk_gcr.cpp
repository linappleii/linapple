#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "apple2/peripherals/disk/DiskEncoding.h"

TEST_CASE("DiskGCR: [GCR-01] disk_encoding_work_buffer_size is 12KB") {
    CHECK( disk_encoding_work_buffer_size == 0x3000 );
}
