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

# Peripheral Options
option(PERIPHERAL_SPEAKER "Enable built-in speaker peripheral" ON)
option(PERIPHERAL_KEYBOARD "Enable built-in keyboard peripheral" ON)
option(PERIPHERAL_MOCKINGBOARD "Enable Mockingboard peripheral" ON)
option(PERIPHERAL_DISK "Enable Disk II peripheral" ON)
option(PERIPHERAL_SSC "Enable Super Serial Card peripheral" ON)
option(PERIPHERAL_PRINTER "Enable Parallel Printer peripheral" ON)
option(PERIPHERAL_HARDDISK "Enable Harddisk peripheral" ON)
option(PERIPHERAL_MOUSE "Enable Mouse peripheral" ON)
option(PERIPHERAL_CLOCK "Enable No-Slot Clock peripheral" ON)

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

# --- Peripheral Helper ---
set(PERIPHERAL_DEFINITIONS "")
set(PERIPHERAL_MODULES "")

function(add_peripheral_target name option_name sources extra_libs force_builtin)
    if(${option_name})
        if(BUILD_SHARED_PERIPHERALS AND NOT force_builtin)
            add_library(peripheral-${name} SHARED ${sources})
            target_compile_definitions(peripheral-${name} PRIVATE BUILD_SHARED_PERIPHERAL)
            target_include_directories(peripheral-${name} PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR})
            target_link_libraries(peripheral-${name} PRIVATE ${extra_libs})
            set_target_properties(peripheral-${name} PROPERTIES PREFIX "" OUTPUT_NAME "${name}")
            list(APPEND PERIPHERAL_MODULES peripheral-${name})
            set(PERIPHERAL_MODULES ${PERIPHERAL_MODULES} PARENT_SCOPE)
        else()
            list(APPEND SOURCES_A2 ${sources})
            set(SOURCES_A2 ${SOURCES_A2} PARENT_SCOPE)
            string(TOUPPER ${name} upper_name)
            string(REPLACE "-" "_" upper_name ${upper_name})
            list(APPEND PERIPHERAL_DEFINITIONS -DENABLE_PERIPHERAL_${upper_name})
            set(PERIPHERAL_DEFINITIONS ${PERIPHERAL_DEFINITIONS} PARENT_SCOPE)
        endif()
    endif()
endfunction()
# --- Source Discovery ---
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

# Mandatory Apple II hardware
set(SOURCES_A2
    src/apple2/6821.cpp
    src/apple2/CPU.cpp
    src/apple2/Disk.cpp
    src/apple2/DiskGCR.cpp
    src/apple2/DiskLoader.cpp
    src/apple2/HarddiskLoader.cpp
    src/apple2/formats/RawHdDriver.cpp

    src/apple2/formats/DoDriver.cpp
    src/apple2/formats/IieDriver.cpp
    src/apple2/formats/Nb2Driver.cpp
    src/apple2/formats/NibDriver.cpp
    src/apple2/formats/PoDriver.cpp
    src/apple2/formats/Woz2Driver.cpp
    src/apple2/Joystick.cpp
    src/apple2/Keyboard.cpp
    src/apple2/Keyboard_Maps.c
    src/apple2/Memory.cpp
    src/apple2/SaveState.cpp
    src/apple2/SoundCore.cpp
    src/apple2/Timer.cpp
    src/apple2/Video.cpp
    src/apple2/ftpparse.cpp
    src/apple2/stretch.cpp
    src/apple2/Riff.cpp
)

add_definitions(-DENABLE_RIFF)

# Optional peripherals
add_peripheral_target(speaker PERIPHERAL_SPEAKER "src/apple2/Speaker.cpp" "" TRUE)
add_peripheral_target(keyboard PERIPHERAL_KEYBOARD "src/apple2/Keyboard.cpp;src/apple2/Keyboard_Maps.c" "" TRUE)
add_peripheral_target(disk PERIPHERAL_DISK "src/apple2/Disk.cpp" "${ZLIB_LIBRARIES};${LIBZIP_LIBRARIES}" TRUE)
add_peripheral_target(harddisk PERIPHERAL_HARDDISK "src/apple2/Harddisk.cpp" "${ZLIB_LIBRARIES};${LIBZIP_LIBRARIES}" TRUE)
add_peripheral_target(mouse PERIPHERAL_MOUSE "src/apple2/MouseInterface.cpp" "" TRUE)
add_peripheral_target(mockingboard PERIPHERAL_MOCKINGBOARD "src/apple2/Mockingboard.cpp;src/apple2/AY8910.cpp" "" TRUE)
add_peripheral_target(ssc PERIPHERAL_SSC "src/apple2/SerialComms.cpp" "" TRUE)

add_peripheral_target(printer PERIPHERAL_PRINTER "src/apple2/ParallelPrinter.cpp" "" FALSE)
add_peripheral_target(clock PERIPHERAL_CLOCK "src/apple2/Clock.cpp" "" FALSE)

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

set(SOURCES ${SOURCES_CORE} ${SOURCES_A2} ${SOURCES_DBG} ${SOURCES_FE})

# --- Target ---
add_executable(linapple ${SOURCES})
add_dependencies(linapple generate_xpms)
set_target_properties(linapple PROPERTIES ENABLE_EXPORTS TRUE)

target_include_directories(linapple PRIVATE
    src
    src/core
    src/apple2
    src/Debugger
    src/frontends/${FRONTEND}
    ${GEN_RES_DIR}
    ${RES_DIR}
    ${CURL_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

if(FRONTEND STREQUAL "sdl3")
    target_link_libraries(linapple PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
    )
endif()

target_link_libraries(linapple PRIVATE
    ${CURL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBZIP_LIBRARIES}
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

# --- Installation ---
include(GNUInstallDirs)

# Default installation paths
set(BIN_DIR "${CMAKE_INSTALL_BINDIR}")
set(DATA_DIR "${CMAKE_INSTALL_DATADIR}/linapple")
set(CONF_DIR "${CMAKE_INSTALL_SYSCONFDIR}/linapple")
set(PLUGIN_DIR "${CMAKE_INSTALL_LIBDIR}/linapple/plugins")

target_compile_definitions(linapple PRIVATE
    ASSET_DIR="${CMAKE_INSTALL_PREFIX}/${DATA_DIR}"
    SYS_DIR="${CMAKE_INSTALL_PREFIX}/${CONF_DIR}"
    ${PERIPHERAL_DEFINITIONS}
)

install(TARGETS linapple DESTINATION ${BIN_DIR})
if(PERIPHERAL_MODULES)
    install(TARGETS ${PERIPHERAL_MODULES} DESTINATION ${PLUGIN_DIR})
endif()
install(FILES "${RES_DIR}/Master.dsk" DESTINATION ${DATA_DIR})
install(FILES "${RES_DIR}/linapple.conf" DESTINATION ${CONF_DIR})

# --- Tests ---
include(CTest)

if(BUILD_TESTING)
    # Doctest include path
    find_path(DOCTEST_INCLUDE_DIR doctest.h PATH_SUFFIXES doctest)
    if(NOT DOCTEST_INCLUDE_DIR)
        include(FetchContent)
        FetchContent_Declare(
          doctest
          GIT_REPOSITORY https://github.com/doctest/doctest.git
          GIT_TAG v2.4.11
        )
        FetchContent_MakeAvailable(doctest)
        set(DOCTEST_INCLUDE_DIR ${doctest_SOURCE_DIR}/doctest)
    endif()

add_executable(test-app-config tests/test_app_config.cpp src/core/Common.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp)
target_include_directories(test-app-config PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
add_test(NAME AppConfig COMMAND test-app-config)

add_executable(test-app-args tests/test_app_args.cpp src/frontends/common/AppArgs.cpp src/core/Common.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp)
target_include_directories(test-app-args PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
add_test(NAME AppArgs COMMAND test-app-args)

add_executable(test-app-env tests/test_app_env.cpp ${SOURCES_CORE} src/frontends/headless/Headless_Path.cpp)
target_include_directories(test-app-env PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR} ${GEN_RES_DIR} ${RES_DIR})
add_dependencies(test-app-env generate_xpms)
add_test(NAME AppEnv COMMAND test-app-env)

add_executable(test-app-controller tests/test_app_controller.cpp ${SOURCES_CORE} ${SOURCES_A2} ${SOURCES_DBG} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
add_dependencies(test-app-controller generate_xpms)
target_include_directories(test-app-controller PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-app-controller PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-app-controller PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME AppController COMMAND test-app-controller)


# Test Integration
add_executable(test-integration tests/test_main.cpp ${SOURCES_CORE} ${SOURCES_A2} ${SOURCES_DBG} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
add_dependencies(test-integration generate_xpms)
target_include_directories(test-integration PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
target_compile_definitions(test-integration PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-integration PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-integration COMMAND test-integration)

# Test SSC
add_executable(test-ssc tests/test_ssc.cpp src/apple2/SerialComms.cpp src/core/Common.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp)
target_include_directories(test-ssc PRIVATE src src/core src/apple2 ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
target_compile_definitions(test-ssc PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-ssc PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-ssc COMMAND test-ssc)


# Test ABI
add_executable(test-abi tests/test_abi.cpp ${SOURCES_CORE} ${SOURCES_A2} ${SOURCES_DBG} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
add_dependencies(test-abi generate_xpms)
target_include_directories(test-abi PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-abi PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-abi PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-abi COMMAND test-abi)

# Test Peripheral ABI (C and C++)
add_executable(test-peripheral-abi
    tests/test_main.cpp
    tests/test_peripheral_abi.cpp
    tests/test_peripheral_abi_c.c
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-peripheral-abi generate_xpms)
target_include_directories(test-peripheral-abi PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-peripheral-abi PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-peripheral-abi PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-peripheral-abi COMMAND test-peripheral-abi)

add_executable(test-keyboard-maps
    tests/test_keyboard_maps_standalone.cpp
    src/apple2/Keyboard_Maps.c
)
target_include_directories(test-keyboard-maps PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
add_test(NAME KeyboardMaps COMMAND test-keyboard-maps)

add_executable(test-keyboard-peripheral
    tests/test_main.cpp
    tests/test_keyboard_peripheral.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-keyboard-peripheral generate_xpms)
target_include_directories(test-keyboard-peripheral PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-keyboard-peripheral PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-keyboard-peripheral PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-keyboard-peripheral COMMAND test-keyboard-peripheral)

add_executable(test-clock-peripheral
    tests/test_main.cpp
    tests/test_clock_peripheral.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-clock-peripheral generate_xpms)
target_include_directories(test-clock-peripheral PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-clock-peripheral PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-clock-peripheral PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-clock-peripheral COMMAND test-clock-peripheral)

# Test Printer Peripheral
add_executable(test-printer-peripheral
    tests/test_main.cpp
    tests/test_printer_peripheral.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-printer-peripheral generate_xpms)
target_include_directories(test-printer-peripheral PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-printer-peripheral PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-printer-peripheral PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-printer-peripheral COMMAND test-printer-peripheral)

# Test Mouse Peripheral
add_executable(test-mouse-peripheral
    tests/test_main.cpp
    tests/test_mouse_peripheral.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-mouse-peripheral generate_xpms)
target_include_directories(test-mouse-peripheral PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-mouse-peripheral PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-mouse-peripheral PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-mouse-peripheral COMMAND test-mouse-peripheral)

# Test Mockingboard Peripheral
add_executable(test-mockingboard-peripheral
    tests/test_main.cpp
    tests/test_mockingboard_peripheral.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-mockingboard-peripheral generate_xpms)
target_include_directories(test-mockingboard-peripheral PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-mockingboard-peripheral PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-mockingboard-peripheral PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-mockingboard-peripheral COMMAND test-mockingboard-peripheral)

# Test Joystick Peripheral
add_executable(test-joystick-peripheral
    tests/test_main.cpp
    tests/test_joystick_peripheral.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-joystick-peripheral generate_xpms)
target_include_directories(test-joystick-peripheral PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-joystick-peripheral PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-joystick-peripheral PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-joystick-peripheral COMMAND test-joystick-peripheral)

# Test Peripheral Slot 0
add_executable(test-peripheral-slot0
    tests/test_main.cpp
    tests/test_peripheral_slot0.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-peripheral-slot0 generate_xpms)
target_include_directories(test-peripheral-slot0 PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-peripheral-slot0 PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-peripheral-slot0 PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-peripheral-slot0 COMMAND test-peripheral-slot0)

# Test Disk ABI
add_executable(test-disk-abi
    tests/test_disk_abi.cpp
    tests/test_disk_abi_c.c
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-disk-abi generate_xpms)
target_include_directories(test-disk-abi PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
target_compile_definitions(test-disk-abi PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-disk-abi PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-disk-abi COMMAND test-disk-abi)

# Test Disk GCR
add_executable(test-disk-gcr
    tests/test_disk_gcr.cpp
    src/apple2/DiskGCR.cpp
)
target_include_directories(test-disk-gcr PRIVATE src ${DOCTEST_INCLUDE_DIR})
add_test(NAME test-disk-gcr COMMAND test-disk-gcr)

# Test Disk Drivers
add_executable(test-disk-drivers
    tests/test_disk_drivers.cpp
    src/apple2/formats/DoDriver.cpp
    src/apple2/formats/IieDriver.cpp
    src/apple2/formats/Nb2Driver.cpp
    src/apple2/formats/NibDriver.cpp
    src/apple2/formats/PoDriver.cpp
    src/apple2/formats/Woz2Driver.cpp
    src/apple2/DiskGCR.cpp
)
target_include_directories(test-disk-drivers PRIVATE src ${DOCTEST_INCLUDE_DIR})
add_test(NAME test-disk-drivers COMMAND test-disk-drivers)

# Test Program Loader
add_executable(test-program-loader
    tests/test_program_loader.cpp
    src/core/ProgramLoader.cpp
    src/core/Common.cpp
    src/core/Log.cpp
    src/apple2/Memory.cpp
    src/apple2/CPU.cpp
    src/apple2/Video.cpp
    src/apple2/Keyboard.cpp
    src/apple2/stretch.cpp
    src/apple2/Harddisk.cpp
    src/apple2/Disk.cpp
    src/apple2/DiskGCR.cpp
    src/apple2/DiskLoader.cpp
    src/apple2/6821.cpp
    src/apple2/formats/DoDriver.cpp
    src/apple2/formats/IieDriver.cpp
    src/apple2/formats/Nb2Driver.cpp
    src/apple2/formats/NibDriver.cpp
    src/apple2/formats/PoDriver.cpp
    src/apple2/formats/Woz2Driver.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-program-loader generate_xpms)
target_include_directories(test-program-loader PRIVATE src src/core src/apple2 ${GEN_RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_link_libraries(test-program-loader PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-program-loader COMMAND test-program-loader)

# test-disk-smoke
add_executable(test-disk-smoke
    tests/test_disk_smoke.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-disk-smoke generate_xpms)
target_include_directories(test-disk-smoke PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
target_compile_definitions(test-disk-smoke PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-disk-smoke PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-disk-smoke COMMAND test-disk-smoke)

# Integration Tests
add_executable(test-disk-config-startup
    tests/test_disk_config_startup.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-disk-config-startup generate_xpms)
target_include_directories(test-disk-config-startup PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
target_compile_definitions(test-disk-config-startup PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-disk-config-startup PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-disk-config-startup COMMAND test-disk-config-startup)

# test-disk-config-runtime-insert
add_executable(test-disk-config-runtime-insert
    tests/test_disk_config_runtime_insert.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-disk-config-runtime-insert generate_xpms)
target_include_directories(test-disk-config-runtime-insert PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
target_compile_definitions(test-disk-config-runtime-insert PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-disk-config-runtime-insert PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-disk-config-runtime-insert COMMAND test-disk-config-runtime-insert)

# test-disk-config-runtime-eject
add_executable(test-disk-config-runtime-eject
    tests/test_disk_config_runtime_eject.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-disk-config-runtime-eject generate_xpms)
target_include_directories(test-disk-config-runtime-eject PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
target_compile_definitions(test-disk-config-runtime-eject PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-disk-config-runtime-eject PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-disk-config-runtime-eject COMMAND test-disk-config-runtime-eject)

# test-headless-boot
add_executable(test-headless-boot
    tests/test_headless_boot.cpp
    ${SOURCES_CORE}
    ${SOURCES_A2}
    ${SOURCES_DBG}
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)
add_dependencies(test-headless-boot generate_xpms)
target_include_directories(test-headless-boot PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
target_compile_definitions(test-headless-boot PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-headless-boot PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-headless-boot COMMAND test-headless-boot)

# test-file-browser
add_executable(test-file-browser
    tests/test_file_browser.cpp
    src/frontends/common/FileBrowser.cpp
)
target_include_directories(test-file-browser PRIVATE src src/core src/frontends/common ${DOCTEST_INCLUDE_DIR})
add_test(NAME test-file-browser COMMAND test-file-browser)

# test-util-hash
add_executable(test-util-hash
    tests/test_util_hash.cpp
    src/frontends/common/Util_Hash.cpp
)
target_include_directories(test-util-hash PRIVATE src src/core src/frontends/common ${DOCTEST_INCLUDE_DIR})
add_test(NAME test-util-hash COMMAND test-util-hash)

# test-disk-ui
add_executable(test-disk-ui
    tests/test_disk_ui.cpp
    src/frontends/sdl3/DiskUI.cpp
)
target_include_directories(test-disk-ui PRIVATE src src/core src/apple2 ${DOCTEST_INCLUDE_DIR})
add_test(NAME test-disk-ui COMMAND test-disk-ui)

foreach(T disk_motor;disk_prot;disk_woz;disk_savestate;disk_errors)
    string(REPLACE "_" "-" TEST_NAME ${T})
    add_executable(test-${TEST_NAME}
        tests/test_${T}.cpp
        ${SOURCES_CORE}
        ${SOURCES_A2}
        ${SOURCES_DBG}
        src/frontends/headless/Stubs.cpp
        src/frontends/headless/Headless_Path.cpp
    )
    add_dependencies(test-${TEST_NAME} generate_xpms)
    target_include_directories(test-${TEST_NAME} PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${DOCTEST_INCLUDE_DIR} ${RES_DIR})
    target_compile_definitions(test-${TEST_NAME} PRIVATE ${PERIPHERAL_DEFINITIONS})
    target_link_libraries(test-${TEST_NAME} PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
    add_test(NAME test-${TEST_NAME} COMMAND test-${TEST_NAME})
endforeach()

# CPU tests (using the built linapple binary)
add_test(NAME test-cpu COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_cpu_tests.sh")
set_tests_properties(test-cpu PROPERTIES ENVIRONMENT "EMULATOR=$<TARGET_FILE:linapple>")

# Test Speaker Subsystem
add_executable(test-speaker tests/test_main.cpp tests/test_speaker.cpp ${SOURCES_CORE} ${SOURCES_A2} ${SOURCES_DBG} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
add_dependencies(test-speaker generate_xpms)
target_include_directories(test-speaker PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-speaker PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-speaker PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-speaker COMMAND test-speaker)

# Test Peripheral Lifecycle
add_executable(test-peripheral-lifecycle tests/test_peripheral_lifecycle.cpp ${SOURCES_CORE} ${SOURCES_A2} ${SOURCES_DBG} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
add_dependencies(test-peripheral-lifecycle generate_xpms)
target_include_directories(test-peripheral-lifecycle PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-peripheral-lifecycle PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-peripheral-lifecycle PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-peripheral-lifecycle COMMAND test-peripheral-lifecycle)

# Test Harddisk Peripheral
add_executable(test-harddisk-peripheral tests/test_harddisk_peripheral.cpp ${SOURCES_CORE} ${SOURCES_A2} ${SOURCES_DBG} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
add_dependencies(test-harddisk-peripheral generate_xpms)
target_include_directories(test-harddisk-peripheral PRIVATE src src/core src/apple2 src/Debugger ${GEN_RES_DIR} ${RES_DIR} ${DOCTEST_INCLUDE_DIR})
target_compile_definitions(test-harddisk-peripheral PRIVATE ${PERIPHERAL_DEFINITIONS})
target_link_libraries(test-harddisk-peripheral PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
add_test(NAME test-harddisk-peripheral COMMAND test-harddisk-peripheral)

endif() # BUILD_TESTING

# --- Packaging Aliases (Issue #231) ---
add_custom_target(linapple-core DEPENDS linapple)

if(PERIPHERAL_MODULES)
    add_custom_target(linapple-peripherals-standard DEPENDS ${PERIPHERAL_MODULES})
endif()
