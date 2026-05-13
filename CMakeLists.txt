cmake_minimum_required(VERSION 3.12)
project(linapple VERSION 3.0.0)

if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

# --- Options ---
if(NOT FRONTEND)
    set(FRONTEND "sdl3" CACHE STRING "Frontend to build (sdl3 or headless)")
endif()

if(NOT FRONTEND STREQUAL "sdl3" AND NOT FRONTEND STREQUAL "headless")
    message(FATAL_ERROR "Invalid FRONTEND: ${FRONTEND}. Must be 'sdl3' or 'headless'.")
endif()

option(PROFILING "Enable profiling" OFF)
option(REGISTRY_WRITEABLE "Enable registry writing" OFF)
option(BUILD_SHARED_PERIPHERALS "Build peripherals as shared libraries instead of built-in" OFF)

# --- Compiler Flags ---
add_compile_options(-O3 -Wall -Wextra)

# Check for LTO support
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT error)
if(lto_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# --- Dependencies ---
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

if(FRONTEND STREQUAL "sdl3")
    find_package(SDL3 REQUIRED)
    find_package(SDL3_image REQUIRED)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED libzip)

# --- Tools ---
find_program(MAGICK_EXECUTABLE NAMES magick convert)
if(NOT MAGICK_EXECUTABLE)
    message(FATAL_ERROR "ImageMagick (magick or convert) is required for asset conversion")
endif()

# --- Preprocessor Definitions ---
add_definitions(-DVERSIONSTRING="${PROJECT_VERSION}")
if(FRONTEND STREQUAL "headless")
    add_definitions(-DHEADLESS)
endif()
if(REGISTRY_WRITEABLE)
    add_definitions(-DREGISTRY_WRITEABLE=1)
endif()

# --- Asset Conversion (PNG to XPM) ---
set(RES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res")
set(GEN_RES_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY ${GEN_RES_DIR})

set(PNG_ASSETS
    charset40_british.png
    charset40_french.png
    charset40_german.png
    charset40_IIplus.png
    charset40.png
    splash.png
)

set(XPM_HEADERS "")
foreach(PNG_FILE ${PNG_ASSETS})
    get_filename_component(BASE_NAME ${PNG_FILE} NAME_WE)
    set(XPM_FILE "${GEN_RES_DIR}/${BASE_NAME}.xpm")
    add_custom_command(
        OUTPUT ${XPM_FILE}
        COMMAND ${MAGICK_EXECUTABLE} "${RES_DIR}/${PNG_FILE}" -layers flatten "${XPM_FILE}"
        COMMAND ${CMAKE_COMMAND} -DXPM_FILE=${XPM_FILE} -DBASE_NAME=${BASE_NAME} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ConvertXPM.cmake"
        DEPENDS "${RES_DIR}/${PNG_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ConvertXPM.cmake"
        COMMENT "Converting ${PNG_FILE} to XPM"
        VERBATIM
    )
    list(APPEND XPM_HEADERS ${XPM_FILE})
endforeach()

add_custom_target(generate_xpms DEPENDS ${XPM_HEADERS})

# --- Include Paths ---
set(LINAPPLE_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/chips"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/peripherals"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/peripherals/disk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/peripherals/harddisk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Debugger"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontends/common"
    "${GEN_RES_DIR}"
    "${RES_DIR}"
    ${CURL_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

# --- Source Lists ---
set(SOURCES_CORE
    src/core/Common.cpp
    src/core/LinAppleCore.cpp
    src/core/Log.cpp
    src/core/ProgramLoader.cpp
    src/core/Registry.cpp
    src/core/asset.cpp
    src/core/Peripheral.cpp
    src/core/Peripheral_Internal.cpp
    src/frontends/common/AppArgs.cpp
    src/frontends/common/AppEnvironment.cpp
    src/frontends/common/AppController.cpp
    src/frontends/common/FileBrowser.cpp
    src/frontends/common/Util_Hash.cpp
)

set(SOURCES_A2_MANDATORY
    src/apple2/chips/6821.cpp
    src/apple2/CPU.cpp
    src/apple2/Memory.cpp
    src/apple2/SaveState.cpp
    src/apple2/SoundCore.cpp
    src/apple2/Timer.cpp
    src/apple2/Video.cpp
    src/apple2/stretch.cpp
    src/apple2/Riff.cpp
)

set(SOURCES_DBG "")
if(NOT FRONTEND STREQUAL "headless")
    set(SOURCES_DBG
        src/Debugger/Debug.cpp
        src/Debugger/Debugger_Assembler.cpp
        src/Debugger/Debugger_Bookmarks.cpp
        src/Debugger/Debugger_Breakpoints.cpp
        src/Debugger/Debugger_Cmd_Benchmark.cpp
        src/Debugger/Debugger_Cmd_CPU.cpp
        src/Debugger/Debugger_Cmd_Config.cpp
        src/Debugger/Debugger_Cmd_Disk.cpp
        src/Debugger/Debugger_Cmd_Flags.cpp
        src/Debugger/Debugger_Cmd_Output.cpp
        src/Debugger/Debugger_Cmd_Video.cpp
        src/Debugger/Debugger_Cmd_Window.cpp
        src/Debugger/Debugger_Cmd_ZeroPage.cpp
        src/Debugger/Debugger_Color.cpp
        src/Debugger/Debugger_Commands.cpp
        src/Debugger/Debugger_Console.cpp
        src/Debugger/Debugger_DisassemblerData.cpp
        src/Debugger/Debugger_Display.cpp
        src/Debugger/Debugger_Help.cpp
        src/Debugger/Debugger_Memory.cpp
        src/Debugger/Debugger_Parser.cpp
        src/Debugger/Debugger_Range.cpp
        src/Debugger/Debugger_Symbols.cpp
        src/Debugger/Debugger_View_Code.cpp
        src/Debugger/Debugger_View_Console.cpp
        src/Debugger/Debugger_View_Memory.cpp
        src/Debugger/Util_MemoryTextFile.cpp
    )
endif()

if(FRONTEND STREQUAL "sdl3")
    set(SOURCES_FE
        src/frontends/sdl3/DiskChoose.cpp
        src/frontends/sdl3/DiskFTP.cpp
        src/frontends/sdl3/DiskSelect.cpp
        src/frontends/sdl3/DiskUI.cpp
        src/frontends/sdl3/Frame.cpp
        src/frontends/sdl3/Harddisk.cpp
        src/frontends/sdl3/JoystickFrontend.cpp
        src/frontends/sdl3/Keyboard.cpp
        src/frontends/sdl3/Main.cpp
        src/frontends/sdl3/MainSession.cpp
        src/frontends/sdl3/PrinterFrontend.cpp
        src/frontends/sdl3/SDL_Asset.cpp
        src/frontends/sdl3/SDL_Input.cpp
        src/frontends/sdl3/SDL_Path.cpp
        src/frontends/sdl3/SDL_Video.cpp
        src/frontends/sdl3/SerialCommsFrontend.cpp
    )
elseif(FRONTEND STREQUAL "headless")
    set(SOURCES_FE
        src/frontends/headless/Main.cpp
        src/frontends/headless/Stubs.cpp
        src/frontends/headless/Headless_Path.cpp
    )
endif()

# --- Peripheral Refinement ---
set(PERIPHERAL_DEFINITIONS "")
set(PERIPHERAL_MODULES "")
set(SOURCES_A2_OPTIONAL "")

# Macro to add a peripheral with optional compilation and shared lib support
macro(add_peripheral name desc sources extra_libs force_builtin)
    string(TOUPPER ${name} upper_name)
    string(REPLACE "-" "_" upper_name ${upper_name})
    set(opt_name "ENABLE_PERIPHERAL_${upper_name}")
    
    option(${opt_name} "Enable ${desc}" ON)
    
    if(${opt_name})
        if(BUILD_SHARED_PERIPHERALS AND NOT force_builtin)
            add_library(peripheral-${name} SHARED ${sources})
            target_compile_definitions(peripheral-${name} PRIVATE BUILD_SHARED_PERIPHERAL)
            target_include_directories(peripheral-${name} PRIVATE ${LINAPPLE_INCLUDES})
            target_link_libraries(peripheral-${name} PRIVATE ${extra_libs})
            set_target_properties(peripheral-${name} PROPERTIES PREFIX "" OUTPUT_NAME "${name}")
            list(APPEND PERIPHERAL_MODULES peripheral-${name})
        else()
            list(APPEND SOURCES_A2_OPTIONAL ${sources})
            list(APPEND PERIPHERAL_DEFINITIONS -DENABLE_PERIPHERAL_${upper_name})
        endif()
    endif()
endmacro()

# Define all peripherals
add_peripheral(speaker "built-in speaker" "src/apple2/peripherals/Speaker.cpp" "" TRUE)
add_peripheral(keyboard "keyboard" "src/apple2/peripherals/Keyboard.cpp;src/apple2/peripherals/Keyboard_Maps.c" "" TRUE)
add_peripheral(disk "Disk II" "src/apple2/peripherals/disk/Disk.cpp;src/apple2/peripherals/disk/DiskGCR.cpp;src/apple2/peripherals/disk/DiskLoader.cpp;src/apple2/peripherals/disk/ftpparse.cpp;src/apple2/peripherals/disk/formats/DoDriver.cpp;src/apple2/peripherals/disk/formats/IieDriver.cpp;src/apple2/peripherals/disk/formats/Nb2Driver.cpp;src/apple2/peripherals/disk/formats/NibDriver.cpp;src/apple2/peripherals/disk/formats/PoDriver.cpp;src/apple2/peripherals/disk/formats/Woz2Driver.cpp;src/apple2/peripherals/disk/formats/SectorDiskImage.cpp" "${ZLIB_LIBRARIES};${LIBZIP_LIBRARIES}" TRUE)
add_peripheral(harddisk "Harddisk (SmartPort)" "src/apple2/peripherals/harddisk/Harddisk.cpp;src/apple2/peripherals/harddisk/HarddiskLoader.cpp;src/apple2/peripherals/harddisk/formats/RawHdDriver.cpp" "${ZLIB_LIBRARIES};${LIBZIP_LIBRARIES}" TRUE)
add_peripheral(mouse "Mouse Interface" "src/apple2/peripherals/MouseInterface.cpp" "" TRUE)
add_peripheral(mockingboard "Mockingboard" "src/apple2/peripherals/Mockingboard.cpp;src/apple2/chips/AY8910.cpp" "" TRUE)
add_peripheral(ssc "Super Serial Card" "src/apple2/peripherals/SerialComms.cpp" "" TRUE)
add_peripheral(joystick "Joystick" "src/apple2/peripherals/Joystick.cpp" "" TRUE)
add_peripheral(printer "Parallel Printer" "src/apple2/peripherals/ParallelPrinter.cpp" "" FALSE)
add_peripheral(clock "No-Slot Clock" "src/apple2/peripherals/Clock.cpp" "" FALSE)

# --- Main Target ---
add_executable(linapple ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_FE} ${SOURCES_A2_OPTIONAL})
add_dependencies(linapple generate_xpms)
set_target_properties(linapple PROPERTIES ENABLE_EXPORTS TRUE)

target_include_directories(linapple PRIVATE ${LINAPPLE_INCLUDES})

if(FRONTEND STREQUAL "sdl3")
    target_link_libraries(linapple PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
endif()

target_link_libraries(linapple PRIVATE
    ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads
)

target_compile_definitions(linapple PRIVATE
    ASSET_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/linapple"
    SYS_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}/linapple"
    ${PERIPHERAL_DEFINITIONS}
)
target_compile_definitions(linapple PUBLIC ENABLE_RIFF)

# --- Installation ---
include(GNUInstallDirs)
install(TARGETS linapple DESTINATION ${CMAKE_INSTALL_BINDIR})
if(PERIPHERAL_MODULES)
    install(TARGETS ${PERIPHERAL_MODULES} DESTINATION ${CMAKE_INSTALL_LIBDIR}/linapple/plugins)
endif()
install(FILES "${RES_DIR}/Master.dsk" DESTINATION ${CMAKE_INSTALL_DATADIR}/linapple)
install(FILES "${RES_DIR}/linapple.conf" DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/linapple)

# --- Tests ---
include(CTest)
if(BUILD_TESTING)
    find_path(DOCTEST_INCLUDE_DIR doctest.h PATH_SUFFIXES doctest)
    if(NOT DOCTEST_INCLUDE_DIR)
        include(FetchContent)
        FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
        FetchContent_MakeAvailable(doctest)
        set(DOCTEST_INCLUDE_DIR ${doctest_SOURCE_DIR}/doctest)
    endif()

    # Helper for adding tests
    macro(add_linapple_test name sources)
        add_executable(test-${name} ${sources} ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
        target_include_directories(test-${name} PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR})
        target_compile_definitions(test-${name} PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF)
        target_link_libraries(test-${name} PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
        add_test(NAME test-${name} COMMAND test-${name})
    endmacro()

    # Refactored tests
    add_executable(test-app-config tests/test_app_config.cpp src/core/Common.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp src/core/Peripheral.cpp)
    target_include_directories(test-app-config PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
    add_test(NAME AppConfig COMMAND test-app-config)

    add_executable(test-app-args tests/test_app_args.cpp src/frontends/common/AppArgs.cpp src/core/Common.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp src/core/Peripheral.cpp)
    target_include_directories(test-app-args PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
    add_test(NAME AppArgs COMMAND test-app-args)

    add_linapple_test(app-env tests/test_app_env.cpp)
    add_linapple_test(app-controller tests/test_app_controller.cpp)
    add_linapple_test(integration tests/test_main.cpp)
    
    # Custom test-ssc to avoid symbol conflict
    add_executable(test-ssc tests/test_ssc.cpp src/apple2/peripherals/SerialComms.cpp src/core/Common.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp src/core/Peripheral.cpp)
    target_include_directories(test-ssc PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR})
    target_compile_definitions(test-ssc PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF)
    target_link_libraries(test-ssc PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
    add_test(NAME test-ssc COMMAND test-ssc)

    add_linapple_test(abi tests/test_abi.cpp)
    add_linapple_test(peripheral-abi "tests/test_main.cpp;tests/test_peripheral_abi.cpp;tests/test_peripheral_abi_c.c")
    
    add_executable(test-keyboard-maps tests/test_keyboard_maps_standalone.cpp src/apple2/peripherals/Keyboard_Maps.c src/core/Peripheral.cpp)
    target_include_directories(test-keyboard-maps PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
    add_test(NAME KeyboardMaps COMMAND test-keyboard-maps)

    add_linapple_test(keyboard-peripheral "tests/test_main.cpp;tests/test_keyboard_peripheral.cpp")
    add_linapple_test(clock-peripheral "tests/test_main.cpp;tests/test_clock_peripheral.cpp")
    add_linapple_test(printer-peripheral "tests/test_main.cpp;tests/test_printer_peripheral.cpp")
    add_linapple_test(mouse-peripheral "tests/test_main.cpp;tests/test_mouse_peripheral.cpp")
    add_linapple_test(mockingboard-peripheral "tests/test_main.cpp;tests/test_mockingboard_peripheral.cpp")
    add_linapple_test(joystick-peripheral "tests/test_main.cpp;tests/test_joystick_peripheral.cpp")
    add_linapple_test(peripheral-slot0 "tests/test_main.cpp;tests/test_peripheral_slot0.cpp")
    add_linapple_test(disk-abi "tests/test_disk_abi.cpp;tests/test_disk_abi_c.c")
    
    add_executable(test-disk-gcr tests/test_disk_gcr.cpp src/apple2/peripherals/disk/DiskGCR.cpp src/core/Peripheral.cpp)
    target_include_directories(test-disk-gcr PRIVATE src ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-disk-gcr COMMAND test-disk-gcr)

    add_executable(test-disk-drivers tests/test_disk_drivers.cpp src/apple2/peripherals/disk/formats/DoDriver.cpp src/apple2/peripherals/disk/formats/IieDriver.cpp src/apple2/peripherals/disk/formats/Nb2Driver.cpp src/apple2/peripherals/disk/formats/NibDriver.cpp src/apple2/peripherals/disk/formats/PoDriver.cpp src/apple2/peripherals/disk/formats/Woz2Driver.cpp src/apple2/peripherals/disk/DiskGCR.cpp src/apple2/peripherals/disk/formats/SectorDiskImage.cpp src/core/Peripheral.cpp)
    target_include_directories(test-disk-drivers PRIVATE src ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-disk-drivers COMMAND test-disk-drivers)

    add_linapple_test(program-loader tests/test_program_loader.cpp)
    add_linapple_test(disk-smoke tests/test_disk_smoke.cpp)
    add_linapple_test(disk-config-startup tests/test_disk_config_startup.cpp)
    add_linapple_test(disk-config-runtime-insert tests/test_disk_config_runtime_insert.cpp)
    add_linapple_test(disk-config-runtime-eject tests/test_disk_config_runtime_eject.cpp)
    add_linapple_test(headless-boot tests/test_headless_boot.cpp)

    add_executable(test-file-browser tests/test_file_browser.cpp src/frontends/common/FileBrowser.cpp src/core/Peripheral.cpp)
    target_include_directories(test-file-browser PRIVATE src src/core src/frontends/common ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-file-browser COMMAND test-file-browser)

    add_executable(test-util-hash tests/test_util_hash.cpp src/frontends/common/Util_Hash.cpp src/core/Peripheral.cpp)
    target_include_directories(test-util-hash PRIVATE src src/core src/frontends/common ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-util-hash COMMAND test-util-hash)

    add_executable(test-disk-ui tests/test_disk_ui.cpp src/frontends/sdl3/DiskUI.cpp src/core/Peripheral.cpp)
    target_include_directories(test-disk-ui PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-disk-ui COMMAND test-disk-ui)

    foreach(T disk_motor;disk_prot;disk_woz;disk_savestate;disk_errors)
        string(REPLACE "_" "-" TEST_NAME ${T})
        add_linapple_test(${TEST_NAME} tests/test_${T}.cpp)
    endforeach()

    add_test(NAME test-cpu COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_cpu_tests.sh")
    set_tests_properties(test-cpu PROPERTIES ENVIRONMENT "EMULATOR=$<TARGET_FILE:linapple>")

    add_linapple_test(speaker tests/test_main.cpp;tests/test_speaker.cpp)
    add_linapple_test(peripheral-lifecycle tests/test_peripheral_lifecycle.cpp)
    add_linapple_test(harddisk-peripheral tests/test_harddisk_peripheral.cpp)
endif()

add_custom_target(linapple-core DEPENDS linapple)
if(PERIPHERAL_MODULES)
    add_custom_target(linapple-peripherals-standard DEPENDS ${PERIPHERAL_MODULES})
endif()
