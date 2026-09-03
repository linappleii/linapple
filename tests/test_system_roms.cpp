// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "EmbeddedRoms.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AppEnvironment.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

TEST_SUITE("System ROMs Architecture & Subsystems") {
  TEST_CASE("ROM Byte Sizes and Memory Layout") {
#if ENABLE_ROM_APPLE2
    CHECK(g_rom_apple2_size == 12288);
    CHECK(g_rom_apple2_video_size == 2048);
#endif
#if ENABLE_ROM_APPLE2PLUS
    CHECK(g_rom_apple2_plus_size == 12288);
#endif
#if ENABLE_ROM_APPLE2_JPLUS
    CHECK(g_rom_apple2_jplus_size == 12288);
    CHECK(g_rom_apple2_jplus_video_size == 2048);
#endif
#if ENABLE_ROM_APPLE2E
    CHECK(g_rom_apple2e_size == 16384);
#endif
#if ENABLE_ROM_APPLE2ENHANCED
    CHECK(g_rom_apple2e_enhanced_size == 16384);
    CHECK(g_rom_apple2e_enhanced_video_size == 4096);
#endif
#if ENABLE_ROM_CLONE_BASE64A
    CHECK(g_rom_clone_base64a_size == 49152);
    CHECK(g_rom_clone_base64a_german_video_size == 4096);
#endif
#if ENABLE_ROM_CLONE_PRAVETS
    CHECK(g_rom_clone_pravets82_size == 12288);
    CHECK(g_rom_clone_pravets8c_size == 16384);
    CHECK(g_rom_clone_pravets8m_size == 12288);
#endif
#if ENABLE_ROM_CLONE_TK3000E
    CHECK(g_rom_clone_tk3000e_size == 16384);
#endif
#if ENABLE_ROM_DISK2
    CHECK(g_rom_disk2_size == 256);
    CHECK(g_rom_disk2_13sector_size == 256);
    CHECK(g_rom_disk2[0] == 0xA2);
    CHECK(g_rom_disk2[1] == 0x20);
#endif
#if ENABLE_ROM_SSC
    CHECK(g_rom_ssc_size == 2048);
    CHECK(g_rom_ssc[0x00] == 0x20);
    CHECK(g_rom_ssc[0x05] == 0x48);
#endif
#if ENABLE_ROM_MOUSE
    CHECK(g_rom_mouse_interface_size == 2048);
    CHECK(g_rom_mouse_interface[0] == 0x2C);
    CHECK(g_rom_mouse_interface[5] == 0x38);
    CHECK(g_rom_mouse_interface[7] == 0x18);
#endif
#if ENABLE_ROM_MOCKINGBOARD
    CHECK(g_rom_mockingboard_d_size == 2048);
    CHECK(g_rom_mockingboard_d[0] == 0x28);  // '('
    CHECK(g_rom_mockingboard_d[1] == 0x43);  // 'C'
    CHECK(g_rom_mockingboard_d[2] == 0x29);  // ')'
#endif
#if ENABLE_ROM_PRINTER
    CHECK(g_rom_parallel_size == 256);
    CHECK(g_rom_parallel[0] == 0x18);
    CHECK(g_rom_parallel[1] == 0xB0);
#endif
#if ENABLE_ROM_CLOCK
    CHECK(g_rom_thunderclock_plus_size == 2048);
    CHECK(g_rom_thunderclock_plus[0] == 0x08);
    CHECK(g_rom_thunderclock_plus[2] == 0x28);
    CHECK(g_rom_thunderclock_plus[4] == 0x58);
    CHECK(g_rom_thunderclock_plus[6] == 0x70);

    CHECK(g_rom_tkclock_size == 2304);
    CHECK(g_rom_tkclock[0] == 0x08);
    CHECK(g_rom_tkclock[2] == 0x28);
    CHECK(g_rom_tkclock[4] == 0x58);
    CHECK(g_rom_tkclock[6] == 0x70);
#endif
  }

  TEST_CASE("Hardware Reset, IRQ, and NMI Vectors") {
#if ENABLE_ROM_APPLE2
    // Apple ][ Integer BASIC ROM ($D000-$FFFF)
    uint16_t a2_nmi = static_cast<uint16_t>(g_rom_apple2[0x2FFA]) |
                      (static_cast<uint16_t>(g_rom_apple2[0x2FFB]) << 8);
    uint16_t a2_reset = static_cast<uint16_t>(g_rom_apple2[0x2FFC]) |
                        (static_cast<uint16_t>(g_rom_apple2[0x2FFD]) << 8);
    uint16_t a2_irq = static_cast<uint16_t>(g_rom_apple2[0x2FFE]) |
                      (static_cast<uint16_t>(g_rom_apple2[0x2FFF]) << 8);
    CHECK(a2_nmi == 0x03FB);
    CHECK(a2_reset == 0xFF59);  // Old Monitor cold entry
    CHECK(a2_irq == 0xFA86);
#endif

#if ENABLE_ROM_APPLE2PLUS
    // Apple ][+ Autostart Monitor ($D000-$FFFF)
    uint16_t a2p_nmi = static_cast<uint16_t>(g_rom_apple2_plus[0x2FFA]) |
                       (static_cast<uint16_t>(g_rom_apple2_plus[0x2FFB]) << 8);
    uint16_t a2p_reset =
        static_cast<uint16_t>(g_rom_apple2_plus[0x2FFC]) |
        (static_cast<uint16_t>(g_rom_apple2_plus[0x2FFD]) << 8);
    uint16_t a2p_irq = static_cast<uint16_t>(g_rom_apple2_plus[0x2FFE]) |
                       (static_cast<uint16_t>(g_rom_apple2_plus[0x2FFF]) << 8);
    CHECK(a2p_nmi == 0x03FB);
    CHECK(a2p_reset == 0xFA62);  // Autostart Monitor reset
    CHECK(a2p_irq == 0xFA40);
#endif

#if ENABLE_ROM_APPLE2_JPLUS
    // Apple ][ J-Plus Katakana Monitor ($D000-$FFFF)
    uint16_t a2j_nmi = static_cast<uint16_t>(g_rom_apple2_jplus[0x2FFA]) |
                       (static_cast<uint16_t>(g_rom_apple2_jplus[0x2FFB]) << 8);
    uint16_t a2j_reset =
        static_cast<uint16_t>(g_rom_apple2_jplus[0x2FFC]) |
        (static_cast<uint16_t>(g_rom_apple2_jplus[0x2FFD]) << 8);
    uint16_t a2j_irq = static_cast<uint16_t>(g_rom_apple2_jplus[0x2FFE]) |
                       (static_cast<uint16_t>(g_rom_apple2_jplus[0x2FFF]) << 8);
    CHECK(a2j_nmi == 0x03FB);
    CHECK(a2j_reset == 0xFA62);  // Autostart Monitor reset
    CHECK(a2j_irq == 0xFA40);
#endif

#if ENABLE_ROM_APPLE2E
    // Apple //e Unenhanced ROM ($C000-$FFFF)
    uint16_t a2e_nmi = static_cast<uint16_t>(g_rom_apple2e[0x3FFA]) |
                       (static_cast<uint16_t>(g_rom_apple2e[0x3FFB]) << 8);
    uint16_t a2e_reset = static_cast<uint16_t>(g_rom_apple2e[0x3FFC]) |
                         (static_cast<uint16_t>(g_rom_apple2e[0x3FFD]) << 8);
    uint16_t a2e_irq = static_cast<uint16_t>(g_rom_apple2e[0x3FFE]) |
                       (static_cast<uint16_t>(g_rom_apple2e[0x3FFF]) << 8);
    CHECK(a2e_nmi == 0x03FB);
    CHECK(a2e_reset == 0xFA62);
    CHECK(a2e_irq == 0xFA40);
#endif

#if ENABLE_ROM_APPLE2ENHANCED
    // Apple //e Enhanced ROM ($C000-$FFFF)
    uint16_t a2ee_nmi =
        static_cast<uint16_t>(g_rom_apple2e_enhanced[0x3FFA]) |
        (static_cast<uint16_t>(g_rom_apple2e_enhanced[0x3FFB]) << 8);
    uint16_t a2ee_reset =
        static_cast<uint16_t>(g_rom_apple2e_enhanced[0x3FFC]) |
        (static_cast<uint16_t>(g_rom_apple2e_enhanced[0x3FFD]) << 8);
    uint16_t a2ee_irq =
        static_cast<uint16_t>(g_rom_apple2e_enhanced[0x3FFE]) |
        (static_cast<uint16_t>(g_rom_apple2e_enhanced[0x3FFF]) << 8);
    CHECK(a2ee_nmi == 0x03FB);
    CHECK(a2ee_reset == 0xFA62);
    CHECK(a2ee_irq == 0xC3FA);
#endif

#if ENABLE_ROM_CLONE_PRAVETS
    uint16_t p82_reset =
        static_cast<uint16_t>(g_rom_clone_pravets82[0x2FFC]) |
        (static_cast<uint16_t>(g_rom_clone_pravets82[0x2FFD]) << 8);
    CHECK(p82_reset == 0xFA62);

    uint16_t p8c_reset =
        static_cast<uint16_t>(g_rom_clone_pravets8c[0x3FFC]) |
        (static_cast<uint16_t>(g_rom_clone_pravets8c[0x3FFD]) << 8);
    CHECK(p8c_reset == 0xFA62);
#endif

#if ENABLE_ROM_CLONE_TK3000E
    uint16_t tk_reset =
        static_cast<uint16_t>(g_rom_clone_tk3000e[0x3FFC]) |
        (static_cast<uint16_t>(g_rom_clone_tk3000e[0x3FFD]) << 8);
    CHECK(tk_reset == 0xFA62);
#endif
  }

  TEST_CASE("Model Identification Bytes in ROM") {
#if ENABLE_ROM_APPLE2
    // Apple ][ model byte at $FBB3 ($D000 + 0x2BB3)
    CHECK(g_rom_apple2[0x2BB3] == 0x38);
#endif

#if ENABLE_ROM_APPLE2PLUS
    // Apple ][+ model byte at $FBB3
    CHECK(g_rom_apple2_plus[0x2BB3] == 0xEA);
#endif

#if ENABLE_ROM_APPLE2_JPLUS
    // Apple ][ J-Plus Japanese model byte at $FBB3
    CHECK(g_rom_apple2_jplus[0x2BB3] == 0xC9);
    CHECK(g_rom_apple2_jplus[0x2BC0] == 0xEA);
#endif

#if ENABLE_ROM_APPLE2E
    // Apple //e Unenhanced model byte at $FBB3 ($C000 + 0x3BB3)
    CHECK(g_rom_apple2e[0x3BB3] == 0x06);
    CHECK(g_rom_apple2e[0x3BC0] == 0xEA);
#endif

#if ENABLE_ROM_APPLE2ENHANCED
    // Apple //e Enhanced model bytes: $FBB3 = $06, $FBC0 = $E0
    CHECK(g_rom_apple2e_enhanced[0x3BB3] == 0x06);
    CHECK(g_rom_apple2e_enhanced[0x3BC0] == 0xE0);
#endif
  }

  TEST_CASE("Firmware Entry Points and Subsystems") {
#if ENABLE_ROM_APPLE2
    // Apple ][ Integer BASIC entry at $E000: JSR $F000
    CHECK(g_rom_apple2[0x1000] == 0x20);  // JSR
    CHECK(g_rom_apple2[0x1001] == 0x00);
    CHECK(g_rom_apple2[0x1002] == 0xF0);  // $F000

    // Apple ][ Mini-Assembler entry at $F666 -> JMP $F592
    CHECK(g_rom_apple2[0x2666] == 0x4C);  // JMP
    CHECK(g_rom_apple2[0x2667] == 0x92);
    CHECK(g_rom_apple2[0x2668] == 0xF5);  // $F592

    // Mini-Assembler entry routine at $F592: JSR $FF3A; LDA #$A1; STA $33; JSR
    // $FD67
    CHECK(g_rom_apple2[0x2592] == 0x20);  // JSR
    CHECK(g_rom_apple2[0x2593] == 0x3A);
    CHECK(g_rom_apple2[0x2594] == 0xFF);  // $FF3A (BELL)
    CHECK(g_rom_apple2[0x2595] == 0xA9);  // LDA #
    CHECK(g_rom_apple2[0x2596] == 0xA1);  // '!'
    CHECK(g_rom_apple2[0x2597] == 0x85);  // STA
    CHECK(g_rom_apple2[0x2598] == 0x33);  // PROMPT ($33)
#endif

#if ENABLE_ROM_APPLE2PLUS
    // Apple ][+ Applesoft BASIC cold entry at $E000: JMP $F128
    CHECK(g_rom_apple2_plus[0x1000] == 0x4C);  // JMP
    CHECK(g_rom_apple2_plus[0x1001] == 0x28);
    CHECK(g_rom_apple2_plus[0x1002] == 0xF1);  // $F128
#endif

#if ENABLE_ROM_APPLE2_JPLUS
    // Apple ][ J-Plus Applesoft BASIC cold entry at $E000: JMP $F128
    CHECK(g_rom_apple2_jplus[0x1000] == 0x4C);  // JMP
    CHECK(g_rom_apple2_jplus[0x1001] == 0x28);
    CHECK(g_rom_apple2_jplus[0x1002] == 0xF1);  // $F128
#endif

#if ENABLE_ROM_APPLE2E
    // Apple //e Unenhanced Applesoft BASIC cold entry at $E000 ($C000 +
    // 0x2000): JMP $F128
    CHECK(g_rom_apple2e[0x2000] == 0x4C);  // JMP
    CHECK(g_rom_apple2e[0x2001] == 0x28);
    CHECK(g_rom_apple2e[0x2002] == 0xF1);  // $F128
#endif

#if ENABLE_ROM_APPLE2ENHANCED
    // Apple //e Enhanced Applesoft BASIC cold entry at $E000 ($C000 + 0x2000):
    // JMP $F128
    CHECK(g_rom_apple2e_enhanced[0x2000] == 0x4C);  // JMP
    CHECK(g_rom_apple2e_enhanced[0x2001] == 0x28);
    CHECK(g_rom_apple2e_enhanced[0x2002] == 0xF1);  // $F128
#endif
  }

  TEST_CASE("End-to-End System Initialization per Model") {
#if ENABLE_ROM_APPLE2
    {
      AppConfig_t config = {};
      app_config_default(&config);
      config.apple2_type = A2TYPE_APPLE2;
      config.apple2_type_explicit = true;
      app_env_resolve_paths(&config);

      REQUIRE(app_controller_initialize(&config) == 0);
      CHECK(cpu_get_registers()->pc == 0xFF59);
      app_controller_shutdown();
    }
#endif

#if ENABLE_ROM_APPLE2PLUS
    {
      AppConfig_t config = {};
      app_config_default(&config);
      config.apple2_type = A2TYPE_APPLE2PLUS;
      config.apple2_type_explicit = true;
      app_env_resolve_paths(&config);

      REQUIRE(app_controller_initialize(&config) == 0);
      CHECK(cpu_get_registers()->pc == 0xFA62);
      app_controller_shutdown();
    }
#endif

#if ENABLE_ROM_APPLE2_JPLUS
    {
      AppConfig_t config = {};
      app_config_default(&config);
      config.apple2_type = A2TYPE_APPLE2JPLUS;
      config.apple2_type_explicit = true;
      app_env_resolve_paths(&config);

      REQUIRE(app_controller_initialize(&config) == 0);
      CHECK(cpu_get_registers()->pc == 0xFA62);
      app_controller_shutdown();
    }
#endif

#if ENABLE_ROM_APPLE2E
    {
      AppConfig_t config = {};
      app_config_default(&config);
      config.apple2_type = A2TYPE_APPLE2E;
      config.apple2_type_explicit = true;
      app_env_resolve_paths(&config);

      REQUIRE(app_controller_initialize(&config) == 0);
      CHECK(cpu_get_registers()->pc == 0xFA62);
      app_controller_shutdown();
    }
#endif

#if ENABLE_ROM_APPLE2ENHANCED
    {
      AppConfig_t config = {};
      app_config_default(&config);
      config.apple2_type = A2TYPE_APPLE2EENHANCED;
      config.apple2_type_explicit = true;
      app_env_resolve_paths(&config);

      REQUIRE(app_controller_initialize(&config) == 0);
      CHECK(cpu_get_registers()->pc == 0xFA62);
      app_controller_shutdown();
    }
#endif

#if ENABLE_ROM_CLONE_PRAVETS
    {
      AppConfig_t config = {};
      app_config_default(&config);
      config.apple2_type = A2TYPE_CLONE_PRAVETS82;
      config.apple2_type_explicit = true;
      app_env_resolve_paths(&config);

      REQUIRE(app_controller_initialize(&config) == 0);
      CHECK(cpu_get_registers()->pc == 0xFA62);
      app_controller_shutdown();
    }
    {
      AppConfig_t config = {};
      app_config_default(&config);
      config.apple2_type = A2TYPE_CLONE_PRAVETS8C;
      config.apple2_type_explicit = true;
      app_env_resolve_paths(&config);

      REQUIRE(app_controller_initialize(&config) == 0);
      CHECK(cpu_get_registers()->pc == 0xFA62);
      app_controller_shutdown();
    }
#endif

#if ENABLE_ROM_CLONE_TK3000E
    {
      AppConfig_t config = {};
      app_config_default(&config);
      config.apple2_type = A2TYPE_CLONE_TK3000E;
      config.apple2_type_explicit = true;
      app_env_resolve_paths(&config);

      REQUIRE(app_controller_initialize(&config) == 0);
      CHECK(cpu_get_registers()->pc == 0xFA62);
      app_controller_shutdown();
    }
#endif
  }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
