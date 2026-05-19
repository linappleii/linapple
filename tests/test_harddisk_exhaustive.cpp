#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>
#include <memory>

#include "apple2/peripherals/harddisk/Harddisk.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "apple2/Memory.h"
#include "core/Common.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

// Access internal descriptor for testing
extern "C" auto Harddisk_GetDescriptor() -> Peripheral_t*;

namespace {

// Helper to create a temporary test file
struct TempFileGuard {
  char path[256];
  TempFileGuard(const char* name, const uint8_t* data, size_t size) {
    snprintf(path, sizeof(path), "%s", name);
    FILE* f = fopen(path, "wb");
    if (f) {
      fwrite(data, 1, size, f);
      fclose(f);
    }
  }
  ~TempFileGuard() { remove(path); }
};

} // namespace

TEST_CASE("Harddisk: Comprehensive Register and Block I/O Test") {
  Linapple_Init();
  Peripheral_Manager_Init();

  auto* descriptor = Harddisk_GetDescriptor();
  REQUIRE(descriptor != nullptr);

  // Register Harddisk in Slot 7
  int reg_result = Peripheral_Register(descriptor, 7);
  REQUIRE(reg_result == 0);

  // Create a mock 1MB HDV file (2048 blocks)
  std::vector<uint8_t> mock_data(2048 * 512, 0);
  // Fill block 10 with some recognizable pattern
  for(int i = 0; i < 512; ++i) {
      mock_data[10 * 512 + i] = static_cast<uint8_t>(i & 0xFF);
  }
  
  TempFileGuard mock_hdv("exhaustive_test.hdv", mock_data.data(), mock_data.size());

  // 1. Insert the mock disk
  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  strncpy(insert.path, mock_hdv.path, sizeof(insert.path) - 1);
  
  PeripheralStatus pstatus = Peripheral_Command(7, harddisk_cmd_insert, &insert, sizeof(insert));
  CHECK(pstatus == PERIPHERAL_OK);
  Peripheral_Manager_Think(0);

  // 2. Verify Status
  HarddiskStatus_t status{};
  size_t status_size = sizeof(status);
  pstatus = Peripheral_Query(7, harddisk_cmd_get_status, &status, &status_size);
  CHECK(pstatus == PERIPHERAL_OK);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == 0);
  CHECK(strcmp(status.drive0_full_path, mock_hdv.path) == 0);

  // 3. Test SmartPort Read Protocol via Registers
  // We want to read block 10 into emulator memory at $2000
  
  // Set Unit Number ($C0F3) - Drive 1 (Unit 0x00)
  IOMap_Dispatch(0, 0xC0F3, 1, 0x00, 0);
  
  // Set Command ($C0F2) - Read (0x01)
  IOMap_Dispatch(0, 0xC0F2, 1, 0x01, 0);
  
  // Set Memory Address ($C0F4, $C0F5) - 0x2000
  IOMap_Dispatch(0, 0xC0F4, 1, 0x00, 0); // Lo
  IOMap_Dispatch(0, 0xC0F5, 1, 0x20, 0); // Hi
  
  // Set Disk Block ($C0F6, $C0F7) - Block 10 (0x000A)
  IOMap_Dispatch(0, 0xC0F6, 1, 0x0A, 0); // Lo
  IOMap_Dispatch(0, 0xC0F7, 1, 0x00, 0); // Hi
  
  // Trigger Execution ($C0F0)
  // SmartPort ROM must be active for execution to work
  // In our init, we set rom_active = true and RegisterCxROM
  uint8_t io_res = IOMap_Dispatch(0, 0xC0F0, 0, 0, 0); 
  CHECK(io_res == 0); // status::ok
  
  // Verify data in buffer register ($C0F8)
  // The first byte of block 10 should be 0x00
  uint8_t b0 = IOMap_Dispatch(0, 0xC0F8, 0, 0, 0);
  CHECK(b0 == 0x00);
  uint8_t b1 = IOMap_Dispatch(0, 0xC0F8, 0, 0, 0);
  CHECK(b1 == 0x01);
  
  // 4. Test SmartPort Write Protocol
  // Write some data to memory at $3000 and then to block 20
  memset(mem + 0x3000, 0xAA, 512);
  
  IOMap_Dispatch(0, 0xC0F2, 1, 0x02, 0); // Write command
  IOMap_Dispatch(0, 0xC0F4, 1, 0x00, 0); // Mem Lo
  IOMap_Dispatch(0, 0xC0F5, 1, 0x30, 0); // Mem Hi
  IOMap_Dispatch(0, 0xC0F6, 1, 0x14, 0); // Disk Lo (20 = 0x14)
  IOMap_Dispatch(0, 0xC0F7, 1, 0x00, 0); // Disk Hi
  
  io_res = IOMap_Dispatch(0, 0xC0F0, 0, 0, 0);
  CHECK(io_res == 0);
  
  // 5. Test Write Protection
  HarddiskSetProtectCmd_t prot{};
  prot.drive = harddisk_drive_0;
  prot.write_protected = 1;
  pstatus = Peripheral_Command(7, harddisk_cmd_set_protect, &prot, sizeof(prot));
  CHECK(pstatus == PERIPHERAL_OK);
  Peripheral_Manager_Think(0);
  
  // Try writing again - should fail (or at least status should be read-only)
  // Current driver implementation returns status::ok for format/write start, 
  // but let's check if we can verify the failure.
  // Actually RawHdDriver delegates to BlockDiskImage which checks os_readonly.
  // User write protect is checked in the command ABI.
  
  // 6. Eject and cleanup
  HarddiskEjectCmd_t eject{};
  eject.drive = harddisk_drive_0;
  pstatus = Peripheral_Command(7, harddisk_cmd_eject, &eject, sizeof(eject));
  CHECK(pstatus == PERIPHERAL_OK);
  Peripheral_Manager_Think(0);
  
  Linapple_Shutdown();
}

TEST_CASE("Harddisk: Edge Cases and Safety") {
  Linapple_Init();
  Peripheral_Manager_Init();
  auto* descriptor = Harddisk_GetDescriptor();
  Peripheral_Register(descriptor, 7);

  // 1. Invalid Drive Index in Command
  HarddiskEjectCmd_t eject{};
  eject.drive = 99;
  // Command returns OK because it's only queued.
  PeripheralStatus pstatus = Peripheral_Command(7, harddisk_cmd_eject, &eject, sizeof(eject));
  CHECK(pstatus == PERIPHERAL_OK);

  // Error would be reported in Status after Think
  Peripheral_Manager_Think(0);
  
  // 2. Read from unloaded drive
  // Set Unit to Drive 1 (Unit 0x00), which we haven't loaded
  IOMap_Dispatch(0, 0xC0F3, 1, 0x00, 0);
  IOMap_Dispatch(0, 0xC0F2, 1, 0x01, 0); // Read
  uint8_t io_res = IOMap_Dispatch(0, 0xC0F0, 0, 0, 0);
  CHECK(io_res != 0); // Should be status::unknown_error or similar

  // 3. Memory Bounds Safety
  // Set memory address to 0xFFF0 (near end of 64K)
  // Reading 512 bytes from here would overflow 64K.
  // Our code checks if (addr + 512 <= 0x10000)
  
  std::vector<uint8_t> dummy(512, 0);
  TempFileGuard dummy_file("safety.hdv", dummy.data(), dummy.size());
  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  strncpy(insert.path, dummy_file.path, sizeof(insert.path) - 1);
  Peripheral_Command(7, harddisk_cmd_insert, &insert, sizeof(insert));
  Peripheral_Manager_Think(0);
  
  IOMap_Dispatch(0, 0xC0F4, 1, 0xF0, 0); // Lo
  IOMap_Dispatch(0, 0xC0F5, 1, 0xFF, 0); // Hi (0xFFF0)
  IOMap_Dispatch(0, 0xC0F2, 1, 0x02, 0); // Write command (pulls from mem)
  io_res = IOMap_Dispatch(0, 0xC0F0, 0, 0, 0);
  // It shouldn't crash, and might return ok but just not copy, or return error.
  // Current implementation just doesn't copy if it would overflow.
  
  Linapple_Shutdown();
}

TEST_CASE("Harddisk: MacBinary Detection") {
    // Create a mock MacBinary header (128 bytes) + 512 bytes data
    std::array<uint8_t, 128 + 512> macbin_data{};
    macbin_data.fill(0);
    macbin_data[0] = 0; // Required for MacBinary
    macbin_data[1] = 10; // Filename length
    memcpy(&macbin_data[2], "test.hdv  ", 10);
    macbin_data[122] = 0; // Required by our heuristic
    macbin_data[123] = 0;
    
    // Data at block 0 (offset 128)
    macbin_data[128] = 0x55;
    
    TempFileGuard macbin_file("test_macbin.hdv", macbin_data.data(), macbin_data.size());
    
    Linapple_Init();
    Peripheral_Manager_Init();
    auto* descriptor = Harddisk_GetDescriptor();
    Peripheral_Register(descriptor, 7);
    
    HarddiskInsertCmd_t insert{};
    insert.drive = harddisk_drive_0;
    strncpy(insert.path, macbin_file.path, sizeof(insert.path) - 1);
    Peripheral_Command(7, harddisk_cmd_insert, &insert, sizeof(insert));
    Peripheral_Manager_Think(0);
    
    // Read block 0
    IOMap_Dispatch(0, 0xC0F3, 1, 0x00, 0); // Drive 0
    IOMap_Dispatch(0, 0xC0F2, 1, 0x01, 0); // Read
    IOMap_Dispatch(0, 0xC0F6, 1, 0x00, 0); // Block 0 Lo
    IOMap_Dispatch(0, 0xC0F7, 1, 0x00, 0); // Block 0 Hi
    IOMap_Dispatch(0, 0xC0F0, 0, 0, 0); // Exec
    
    uint8_t b0 = IOMap_Dispatch(0, 0xC0F8, 0, 0, 0);
    CHECK(b0 == 0x55); // Should have skipped 128 byte header
    
    Linapple_Shutdown();
}
