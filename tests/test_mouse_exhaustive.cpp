// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include "doctest.h"

#include <cstring>
#include <vector>
#include <algorithm>

#include "apple2/peripherals/mouse/Mouse.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/Peripheral.h"

// --- Constants (Matching Mouse.cpp) ---
namespace regs {
enum {
  MOUSE_SET = 0x00,
  MOUSE_READ = 0x10,
  MOUSE_SERV = 0x20,
  MOUSE_CLEAR = 0x30,
  MOUSE_POS = 0x40,
  MOUSE_INIT = 0x50,
  MOUSE_CLAMP = 0x60,
  MOUSE_HOME = 0x70,
};

enum {
  STAT_PREV_BTN1 = 0x01,
  STAT_MOVE_INT = 0x02,
  STAT_BTN_INT = 0x04,
  STAT_VBL_INT = 0x08,
  STAT_CURR_BTN1 = 0x10,
  STAT_MOVEMENT = 0x20,
  STAT_PREV_BTN0 = 0x40,
  STAT_CURR_BTN0 = 0x80
};
}

// --- Mock Host ---

static bool g_irq_asserted = false;
static PeripheralIOHandler g_mouse_io = nullptr;
static void* g_mouse_instance = nullptr;

static void Mock_AssertIrq(int slot, bool assert) {
  (void)slot;
  g_irq_asserted = assert;
}

static void Mock_RegisterIO(int slot, PeripheralIOHandler r,
                            PeripheralIOHandler w, PeripheralIOHandler cr,
                            PeripheralIOHandler cw) {
  (void)slot;
  (void)w;
  (void)cr;
  (void)cw;
  g_mouse_io = r; 
}

static HostInterface_t mock_host = {
    .AssertIrq = Mock_AssertIrq,
    .RegisterIO = Mock_RegisterIO,
};

// --- Helpers ---

static uint8_t read_mouse(uint16_t addr) {
  if (!g_mouse_io || !g_mouse_instance) return 0;
  return g_mouse_io(g_mouse_instance, 0, addr, 0, 0, 0);
}

static void write_mouse(uint16_t addr, uint8_t val) {
  if (!g_mouse_io || !g_mouse_instance) return;
  g_mouse_io(g_mouse_instance, 0, addr, 1, val, 0);
}

static void send_mouse_byte(uint8_t val) {
  write_mouse(0xC0C0, val); // Port A
  write_mouse(0xC0C2, 0x20); // Bit 5 High
  write_mouse(0xC0C2, 0x00); // Bit 5 Low
}

static uint8_t recv_mouse_byte() {
  write_mouse(0xC0C2, 0x10); // Bit 4 High
  write_mouse(0xC0C2, 0x00); // Bit 4 Low
  return read_mouse(0xC0C0);
}

// Exactly match the firmware's 6-byte read sequence
static void drain_mouse_read(uint8_t* x_low, uint8_t* x_high, uint8_t* y_low, uint8_t* y_high, uint8_t* status) {
    uint8_t b1 = read_mouse(0xC0C0);
    uint8_t b2 = recv_mouse_byte();
    uint8_t b3 = recv_mouse_byte();
    uint8_t b4 = recv_mouse_byte();
    uint8_t b5 = recv_mouse_byte();
    if (x_low) *x_low = b1;
    if (x_high) *x_high = b2;
    if (y_low) *y_low = b3;
    if (y_high) *y_high = b4;
    if (status) *status = b5;
    (void)recv_mouse_byte();
}


static void init_mouse_card() {
  write_mouse(0xC0C1, 0x00); // Access DDRA
  write_mouse(0xC0C3, 0x00); // Access DDRB
  write_mouse(0xC0C0, 0xFF); // DDRA = all outputs
  write_mouse(0xC0C2, 0xFF); // DDRB = all outputs
  write_mouse(0xC0C1, 0x04); // Access ORA
  write_mouse(0xC0C3, 0x04); // Access ORB
}

TEST_CASE("Mouse Exhaustive Functional Tests") {
  auto* descriptor = Mouse_GetDescriptor();
  g_irq_asserted = false;

  SUBCASE("ABI Verification") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    CHECK(strcmp(descriptor->id, "linapple.mouse") == 0);
    
    uint8_t is_active = 0;
    size_t out_size = 1;
    PeripheralStatus status = descriptor->query(g_mouse_instance, mouse_query_is_active, &is_active, &out_size);
    CHECK(status == PERIPHERAL_OK);
    CHECK(is_active == 1);
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("I/O Register Access") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    write_mouse(0xC0C2, 0x55);
    CHECK(read_mouse(0xC0C2) == 0x55);

    write_mouse(0xC0C2, 0xAA);
    CHECK(read_mouse(0xC0C2) == 0xAA);
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("Firmware Commands - MOUSE_INIT") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    send_mouse_byte(regs::MOUSE_INIT);
    uint8_t r1 = read_mouse(0xC0C0); 
    CHECK(r1 == 0xFF);
    
    recv_mouse_byte(); // drain byte 2
    recv_mouse_byte(); // drain byte 3
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("Firmware Commands - MOUSE_SET & IRQ Logic") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    g_irq_asserted = false;
    MousePosPayload_t range_init = {0, 1023, 0, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &range_init, sizeof(range_init));

    // Mode 1: Mouse On, No IRQs
    send_mouse_byte(regs::MOUSE_SET | 1);
    
    MousePosPayload_t pos = {100, 1023, 200, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &pos, sizeof(pos));
    
    CHECK(g_irq_asserted == false);
    
    // Mode 0x0F: Mouse On, All IRQs enabled
    send_mouse_byte(regs::MOUSE_SET | 0x0F);
    
    pos.x = 110;
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &pos, sizeof(pos));
    CHECK(g_irq_asserted == true);
    
    send_mouse_byte(regs::MOUSE_SERV);
    read_mouse(0xC0C0); // status byte
    recv_mouse_byte();  // dummy byte
    CHECK(g_irq_asserted == false);
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("Firmware Commands - MOUSE_READ") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    MousePosPayload_t pos = {123, 1023, 456, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &pos, sizeof(pos));
    
    send_mouse_byte(regs::MOUSE_READ);
    
    uint8_t x_low, x_high, y_low, y_high, status;
    drain_mouse_read(&x_low, &x_high, &y_low, &y_high, &status);
    
    CHECK(x_low == 123);
    CHECK(x_high == 0);
    CHECK(y_low == (456 & 0xFF));
    CHECK(y_high == (456 >> 8));
    CHECK((status & regs::STAT_MOVEMENT) != 0);
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("Firmware Commands - MOUSE_POS") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    MousePosPayload_t range_init = {0, 1023, 0, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &range_init, sizeof(range_init));

    send_mouse_byte(regs::MOUSE_POS);
    send_mouse_byte(0xAA); 
    send_mouse_byte(0x01); 
    send_mouse_byte(0xBB); 
    send_mouse_byte(0x02); 
    
    send_mouse_byte(regs::MOUSE_READ);
    uint8_t xl, xh, yl, yh;
    drain_mouse_read(&xl, &xh, &yl, &yh, nullptr);
    CHECK(xl == 0xAA);
    CHECK(xh == 0x01);
    CHECK(yl == 0xBB);
    CHECK(yh == 0x02);
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("Firmware Commands - MOUSE_CLAMP") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    MousePosPayload_t range_init = {0, 1023, 0, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &range_init, sizeof(range_init));

    // Clamp X to 100-200
    send_mouse_byte(regs::MOUSE_CLAMP | 0); 
    send_mouse_byte(100 & 0xFF);
    send_mouse_byte(0); 
    send_mouse_byte(200 & 0xFF);
    send_mouse_byte(0); 
    
    // Try to set X to 50
    MousePosPayload_t pos = {50, 1023, 50, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &pos, sizeof(pos));
    
    send_mouse_byte(regs::MOUSE_READ);
    uint8_t xl, xh, yl, yh, st;
    drain_mouse_read(&xl, &xh, &yl, &yh, &st);
    CHECK(xl == 100);

    // Try to set X to 250
    pos.x = 250;
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &pos, sizeof(pos));
    send_mouse_byte(regs::MOUSE_READ);
    drain_mouse_read(&xl, &xh, &yl, &yh, &st);
    CHECK(xl == 200);
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("Firmware Commands - MOUSE_HOME") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    MousePosPayload_t range_init = {0, 1023, 0, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &range_init, sizeof(range_init));

    MousePosPayload_t pos = {500, 1023, 500, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &pos, sizeof(pos));
    
    send_mouse_byte(regs::MOUSE_HOME);
    
    send_mouse_byte(regs::MOUSE_READ);
    uint8_t xl, xh, yl, yh;
    drain_mouse_read(&xl, &xh, &yl, &yh, nullptr);
    CHECK(xl == 0);
    CHECK(xh == 0);
    CHECK(yl == 0);
    CHECK(yh == 0);
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("State Persistence") {
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    MousePosPayload_t pos = {789, 1023, 321, 1023};
    descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &pos, sizeof(pos));
    MouseButtonPayload_t btn = {0, true};
    descriptor->command(g_mouse_instance, mouse_cmd_set_button, &btn, sizeof(btn));
    send_mouse_byte(regs::MOUSE_SET | 0x0F);
    
    size_t state_size = 0;
    descriptor->save_state(g_mouse_instance, nullptr, &state_size);
    std::vector<uint8_t> buffer(state_size);
    descriptor->save_state(g_mouse_instance, buffer.data(), &state_size);
    
    descriptor->shutdown(g_mouse_instance);
    g_mouse_instance = descriptor->init(4, &mock_host);
    REQUIRE(g_mouse_instance != nullptr);
    init_mouse_card();
    
    descriptor->load_state(g_mouse_instance, buffer.data(), state_size);
    
    send_mouse_byte(regs::MOUSE_READ);
    uint8_t xl, xh, yl, yh, status;
    drain_mouse_read(&xl, &xh, &yl, &yh, &status);
    CHECK(xl == (789 & 0xFF));
    CHECK(xh == (789 >> 8));
    CHECK(yl == (321 & 0xFF));
    CHECK(yh == (321 >> 8));
    CHECK((status & regs::STAT_CURR_BTN0) != 0);
    descriptor->shutdown(g_mouse_instance);
  }

  SUBCASE("Clamping & Mapping") {
      g_mouse_instance = descriptor->init(4, &mock_host);
      REQUIRE(g_mouse_instance != nullptr);
      init_mouse_card();
      MousePosPayload_t pos = {50, 100, 25, 100};
      descriptor->command(g_mouse_instance, mouse_cmd_set_pos, &pos, sizeof(pos));
      
      send_mouse_byte(regs::MOUSE_READ);
      uint8_t xl, xh, yl, yh;
      drain_mouse_read(&xl, &xh, &yl, &yh, nullptr);
      uint16_t x = xl | (xh << 8);
      uint16_t y = yl | (yh << 8);
      
      CHECK(x == 511);
      CHECK(y == 255);
      descriptor->shutdown(g_mouse_instance);
  }

  g_mouse_instance = nullptr;
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
