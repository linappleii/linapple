cmake_minimum_required(VERSION 3.12)
project(linapple VERSION 3.0.0)

if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

# --- Options ---
if(NOT FRONTEND)
    set(FRONTEND "sdl3" CACHE STRING "Frontend to build (sdl3, sdl2, sdl1, tui, headless, or all)")
endif()

string(TOLOWER "${FRONTEND}" FRONTEND_LOWER)
if(NOT FRONTEND_LOWER STREQUAL "sdl3" AND NOT FRONTEND_LOWER STREQUAL "sdl2" AND NOT FRONTEND_LOWER STREQUAL "sdl1" AND NOT FRONTEND_LOWER STREQUAL "tui" AND NOT FRONTEND_LOWER STREQUAL "headless" AND NOT FRONTEND_LOWER STREQUAL "all")
    message(FATAL_ERROR "Invalid FRONTEND: ${FRONTEND}. Must be 'sdl3', 'sdl2', 'sdl1', 'tui', 'headless', or 'all'.")
endif()

if(FRONTEND_LOWER STREQUAL "tui" OR FRONTEND_LOWER STREQUAL "headless")
    set(DEFAULT_ENABLE_DEBUGGER OFF)
else()
    set(DEFAULT_ENABLE_DEBUGGER ON)
endif()
option(ENABLE_DEBUGGER "Enable integrated debugger" ${DEFAULT_ENABLE_DEBUGGER})

option(PROFILING "Enable profiling" OFF)
option(REGISTRY_WRITEABLE "Enable registry writing" ON)
option(BUILD_SHARED_PERIPHERALS "Build peripherals as shared libraries instead of built-in" OFF)

option(ENABLE_ASAN "Enable AddressSanitizer (ASan)" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
option(ENABLE_FUZZING "Enable libFuzzer targets" OFF)

# --- ROM Options ---
option(ENABLE_ROM_APPLE2 "Include Apple ][ ROM" ON)
option(ENABLE_ROM_APPLE2PLUS "Include Apple ][+ ROM" ON)
option(ENABLE_ROM_APPLE2_JPLUS "Include Apple ][ J-Plus (Japanese) ROM" ON)
option(ENABLE_ROM_APPLE2E "Include Apple //e Unenhanced ROM" ON)
option(ENABLE_ROM_APPLE2ENHANCED "Include Apple //e Enhanced ROM" ON)

if(ENABLE_ROM_APPLE2)
    add_compile_definitions(ENABLE_ROM_APPLE2=1)
else()
    add_compile_definitions(ENABLE_ROM_APPLE2=0)
endif()

if(ENABLE_ROM_APPLE2PLUS)
    add_compile_definitions(ENABLE_ROM_APPLE2PLUS=1)
else()
    add_compile_definitions(ENABLE_ROM_APPLE2PLUS=0)
endif()

if(ENABLE_ROM_APPLE2_JPLUS)
    add_compile_definitions(ENABLE_ROM_APPLE2_JPLUS=1)
else()
    add_compile_definitions(ENABLE_ROM_APPLE2_JPLUS=0)
endif()

if(ENABLE_ROM_APPLE2E)
    add_compile_definitions(ENABLE_ROM_APPLE2E=1)
else()
    add_compile_definitions(ENABLE_ROM_APPLE2E=0)
endif()

if(ENABLE_ROM_APPLE2ENHANCED)
    add_compile_definitions(ENABLE_ROM_APPLE2ENHANCED=1)
else()
    add_compile_definitions(ENABLE_ROM_APPLE2ENHANCED=0)
endif()

# --- Third-Party Clone ROM Options ---
option(ENABLE_ROM_CLONE_BASE64A "Include Copam Base 64A Clone ROM" OFF)
option(ENABLE_ROM_CLONE_PRAVETS "Include Pravets 82/8C/8M Clone ROMs" OFF)
option(ENABLE_ROM_CLONE_TK3000E "Include Microdigital TK-3000 //e Clone ROM" OFF)

if(ENABLE_ROM_CLONE_BASE64A)
    add_compile_definitions(ENABLE_ROM_CLONE_BASE64A=1)
else()
    add_compile_definitions(ENABLE_ROM_CLONE_BASE64A=0)
endif()

if(ENABLE_ROM_CLONE_PRAVETS)
    add_compile_definitions(ENABLE_ROM_CLONE_PRAVETS=1)
else()
    add_compile_definitions(ENABLE_ROM_CLONE_PRAVETS=0)
endif()

if(ENABLE_ROM_CLONE_TK3000E)
    add_compile_definitions(ENABLE_ROM_CLONE_TK3000E=1)
else()
    add_compile_definitions(ENABLE_ROM_CLONE_TK3000E=0)
endif()

# --- Peripheral ROM Options ---
option(ENABLE_ROM_DISK2 "Include Disk II Interface Card ROM" ON)
option(ENABLE_ROM_SSC "Include Super Serial Card (SSC) ROM" ON)

if(ENABLE_ROM_DISK2)
    add_compile_definitions(ENABLE_ROM_DISK2=1)
else()
    add_compile_definitions(ENABLE_ROM_DISK2=0)
endif()

if(ENABLE_ROM_SSC)
    add_compile_definitions(ENABLE_ROM_SSC=1)
else()
    add_compile_definitions(ENABLE_ROM_SSC=0)
endif()

option(ENABLE_ROM_MOUSE "Include Mouse Interface Card ROM" ON)
option(ENABLE_ROM_MOCKINGBOARD "Include Mockingboard-D ROM" ON)

if(ENABLE_ROM_MOUSE)
    add_compile_definitions(ENABLE_ROM_MOUSE=1)
else()
    add_compile_definitions(ENABLE_ROM_MOUSE=0)
endif()

if(ENABLE_ROM_MOCKINGBOARD)
    add_compile_definitions(ENABLE_ROM_MOCKINGBOARD=1)
else()
    add_compile_definitions(ENABLE_ROM_MOCKINGBOARD=0)
endif()

option(ENABLE_ROM_PRINTER "Include Parallel Printer Interface Card ROM" ON)
option(ENABLE_ROM_CLOCK "Include Real-Time Clock Card ROMs" ON)

if(ENABLE_ROM_PRINTER)
    add_compile_definitions(ENABLE_ROM_PRINTER=1)
else()
    add_compile_definitions(ENABLE_ROM_PRINTER=0)
endif()

if(ENABLE_ROM_CLOCK)
    add_compile_definitions(ENABLE_ROM_CLOCK=1)
else()
    add_compile_definitions(ENABLE_ROM_CLOCK=0)
endif()

# --- Compiler Flags ---
add_compile_options(-O3 -Wall -Wextra)

set(SANITIZER_FLAGS "")
if(ENABLE_ASAN)
    list(APPEND SANITIZER_FLAGS "-fsanitize=address" "-fno-omit-frame-pointer")
endif()
if(ENABLE_UBSAN)
    list(APPEND SANITIZER_FLAGS "-fsanitize=undefined")
endif()

if(SANITIZER_FLAGS)
    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(${SANITIZER_FLAGS})
endif()

if(PROFILING)
    add_compile_options(-pg)
    add_link_options(-pg)
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy static analysis enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "ENABLE_CLANG_TIDY is ON, but clang-tidy executable was not found")
    endif()
endif()

# Check for LTO support
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT error)
if(lto_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

# --- Dependencies ---
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED libzip)

if(FRONTEND_LOWER STREQUAL "all")
    find_package(SDL3 REQUIRED)
    find_package(SDL3_image REQUIRED)
    find_package(SDL2 REQUIRED)
    find_package(SDL REQUIRED)
    find_package(SDL_image REQUIRED)
    pkg_check_modules(PULSE_SIMPLE libpulse-simple)
    pkg_check_modules(ALSA alsa)
elseif(FRONTEND_LOWER STREQUAL "sdl3")
    find_package(SDL3 REQUIRED)
    find_package(SDL3_image REQUIRED)
elseif(FRONTEND_LOWER STREQUAL "sdl2")
    find_package(SDL2 REQUIRED)
elseif(FRONTEND_LOWER STREQUAL "sdl1")
    find_package(SDL REQUIRED)
    find_package(SDL_image REQUIRED)
elseif(FRONTEND_LOWER STREQUAL "tui")
    pkg_check_modules(PULSE_SIMPLE libpulse-simple)
    pkg_check_modules(ALSA alsa)
endif()

# --- Tools ---
find_program(MAGICK_EXECUTABLE NAMES magick convert)
if(NOT MAGICK_EXECUTABLE)
    message(FATAL_ERROR "ImageMagick (magick or convert) is required for asset conversion")
endif()

# --- Preprocessor Definitions ---
add_definitions(-DVERSIONSTRING="${PROJECT_VERSION}")
if(FRONTEND_LOWER STREQUAL "headless")
    add_definitions(-DHEADLESS)
endif()
if(FRONTEND_LOWER STREQUAL "tui")
    add_definitions(-DTUI)
    if(PULSE_SIMPLE_FOUND)
        add_definitions(-DHAVE_PULSE_SIMPLE)
    endif()
    if(ALSA_FOUND)
        add_definitions(-DHAVE_ALSA)
    endif()
endif()
if(REGISTRY_WRITEABLE)
    add_definitions(-DREGISTRY_WRITEABLE=1)
endif()
if(ENABLE_DEBUGGER)
    add_definitions(-DENABLE_DEBUGGER=1)
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

# --- Embedded ROMs Generation ---
set(GEN_ROMS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/roms")
set(EMBEDDED_ROMS_H "${GEN_ROMS_DIR}/EmbeddedRoms.h")
set(EMBEDDED_ROMS_CPP "${GEN_ROMS_DIR}/EmbeddedRoms.cpp")
file(MAKE_DIRECTORY "${GEN_ROMS_DIR}")

# Generate at configure time so headers exist for IDE and clang-tidy
execute_process(
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_roms.sh" "${GEN_ROMS_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/res/roms"
)

add_custom_command(
    OUTPUT ${EMBEDDED_ROMS_H} ${EMBEDDED_ROMS_CPP}
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_roms.sh" "${GEN_ROMS_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/res/roms"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_roms.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Apple2.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Apple2_Video.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Apple2_Plus.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Apple2_JPlus.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Apple2_JPlus_Video.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Apple2e.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Apple2e_Enhanced.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Apple2e_Enhanced_Video.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Base64A.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Base64A_German_Video.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/PRAVETS82.ROM"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/PRAVETS8C.ROM"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/PRAVETS8M.ROM"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/TK3000e.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/DISK2.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/DISK2-13sector.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/SSC.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/MouseInterface.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Mockingboard-D.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/Parallel.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/ThunderClockPlus.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/TKClock.rom"
    COMMENT "Generating EmbeddedRoms source from res/roms"
    VERBATIM
)

add_custom_target(generate_embedded_roms DEPENDS ${EMBEDDED_ROMS_H} ${EMBEDDED_ROMS_CPP})

# --- Include Paths ---
set(LINAPPLE_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/chips"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/peripherals"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/peripherals/disk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/peripherals/harddisk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/peripherals/mouse"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontends/common"
    "${GEN_RES_DIR}"
    "${GEN_ROMS_DIR}"
    "${RES_DIR}"
    ${CURL_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

if(ENABLE_DEBUGGER)
    list(APPEND LINAPPLE_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/Debugger")
endif()

if(FRONTEND STREQUAL "tui")
    list(APPEND LINAPPLE_INCLUDES ${PULSE_SIMPLE_INCLUDE_DIRS} ${ALSA_INCLUDE_DIRS})
endif()

# --- Source Lists ---
set(SOURCES_CORE
    src/core/AudioMixer.cpp
    src/core/LinAppleCore.cpp
    src/core/Log.cpp
    src/core/ProgramLoader.cpp
    src/core/Registry.cpp
    src/core/Asset.cpp
    src/core/BasicLiveSync.cpp
    src/core/Peripheral.cpp
    src/core/Peripheral_Internal.cpp
    src/core/Resource.cpp
    "${EMBEDDED_ROMS_CPP}"
    src/frontends/common/AppArgs.cpp
    src/frontends/common/AppEnvironment.cpp
    src/frontends/common/AppController.cpp
    src/frontends/common/FileBrowser.cpp
    src/frontends/common/Util_Hash.cpp
    src/frontends/common/KeyboardTranslator.cpp
    src/frontends/common/AudioDumper.cpp
    src/frontends/common/SaveStateManager.cpp
    src/frontends/common/VideoSurface.cpp
    src/frontends/common/VideoStretch.cpp
    src/frontends/common/SuperSerialFrontend.cpp
    src/frontends/common/PrinterFrontend.cpp
)

set(SOURCES_A2_MANDATORY
    src/apple2/chips/6821.cpp
    src/apple2/CPU.cpp
    src/apple2/Memory.cpp
    src/apple2/Snapshot.cpp
    src/apple2/Video.cpp
)

set(SOURCES_DBG_FULL
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
set(SOURCES_DBG_STUBS
    src/Debugger/Debugger_Stubs.cpp
)

if(ENABLE_DEBUGGER)
    set(SOURCES_DBG ${SOURCES_DBG_FULL})
else()
    set(SOURCES_DBG ${SOURCES_DBG_STUBS})
endif()

set(SOURCES_FE_COMMON_SDL
    src/frontends/common/sdl/DiskUI.cpp
    src/frontends/common/sdl/DiskFTP.cpp
    src/frontends/common/sdl/DiskSelect.cpp
    src/frontends/common/sdl/Harddisk.cpp
)

set(SOURCES_FE_SDL3
    ${SOURCES_FE_COMMON_SDL}
    src/frontends/sdl3/DiskChoose.cpp
    src/frontends/sdl3/Frame.cpp
    src/frontends/sdl3/JoystickFrontend.cpp
    src/frontends/sdl3/Keyboard.cpp
    src/frontends/sdl3/Main.cpp
    src/frontends/sdl3/MainSession.cpp
    src/frontends/sdl3/SDL_Asset.cpp
    src/frontends/sdl3/SDL_Input.cpp
    src/frontends/sdl3/SDL_Path.cpp
    src/frontends/sdl3/SDL_Video.cpp
)

set(SOURCES_FE_SDL2
    ${SOURCES_FE_COMMON_SDL}
    src/frontends/sdl2/DiskChoose.cpp
    src/frontends/sdl2/Frame.cpp
    src/frontends/sdl2/JoystickFrontend.cpp
    src/frontends/sdl2/Keyboard.cpp
    src/frontends/sdl2/Main.cpp
    src/frontends/sdl2/MainSession.cpp
    src/frontends/sdl2/SDL_Asset.cpp
    src/frontends/sdl2/SDL_Input.cpp
    src/frontends/sdl2/SDL_Path.cpp
    src/frontends/sdl2/SDL_Video.cpp
)

set(SOURCES_FE_SDL1
    ${SOURCES_FE_COMMON_SDL}
    src/frontends/sdl1/DiskChoose.cpp
    src/frontends/sdl1/Frame.cpp
    src/frontends/sdl1/JoystickFrontend.cpp
    src/frontends/sdl1/Keyboard.cpp
    src/frontends/sdl1/Main.cpp
    src/frontends/sdl1/MainSession.cpp
    src/frontends/sdl1/SDL_Asset.cpp
    src/frontends/sdl1/SDL_Input.cpp
    src/frontends/sdl1/SDL_Path.cpp
    src/frontends/sdl1/SDL_Video.cpp
)

set(SOURCES_FE_HEADLESS
    src/frontends/headless/Main.cpp
    src/frontends/headless/Stubs.cpp
    src/frontends/headless/Headless_Path.cpp
)

set(SOURCES_FE_TUI
    src/frontends/tui/Main.cpp
    src/frontends/tui/Stubs.cpp
    src/frontends/tui/Tui_Path.cpp
    src/frontends/tui/TuiTerminal.cpp
    src/frontends/tui/TuiVideo.cpp
    src/frontends/tui/TuiShapeDetector.cpp
    src/frontends/tui/TuiInput.cpp
    src/frontends/tui/TuiAudio.cpp
    src/frontends/tui/TuiDiskSelect.cpp
)

# --- Peripheral Refinement ---
set(PERIPHERAL_DEFINITIONS "")
set(PERIPHERAL_MODULES "")
set(SOURCES_A2_OPTIONAL "")

# Function to add a peripheral with optional compilation and shared lib support
function(add_peripheral name desc sources extra_libs force_builtin)
    string(TOUPPER ${name} upper_name)
    string(REPLACE "-" "_" upper_name ${upper_name})
    set(opt_name "ENABLE_PERIPHERAL_${upper_name}")
    
    option(${opt_name} "Enable ${desc}" ON)
    
    if(${opt_name})
        if(BUILD_SHARED_PERIPHERALS AND NOT force_builtin)
            add_library(peripheral-${name} SHARED ${sources})
            target_compile_definitions(peripheral-${name} PRIVATE BUILD_SHARED_PERIPHERAL)
            target_include_directories(peripheral-${name} PRIVATE ${LINAPPLE_INCLUDES})
            if(extra_libs)
                target_link_libraries(peripheral-${name} PRIVATE ${extra_libs})
            endif()
            set_target_properties(peripheral-${name} PROPERTIES PREFIX "" OUTPUT_NAME "${name}")
            list(APPEND PERIPHERAL_MODULES peripheral-${name})
            set(PERIPHERAL_MODULES "${PERIPHERAL_MODULES}" PARENT_SCOPE)
        else()
            list(APPEND SOURCES_A2_OPTIONAL ${sources})
            list(APPEND PERIPHERAL_DEFINITIONS -DENABLE_PERIPHERAL_${upper_name})
            set(SOURCES_A2_OPTIONAL "${SOURCES_A2_OPTIONAL}" PARENT_SCOPE)
            set(PERIPHERAL_DEFINITIONS "${PERIPHERAL_DEFINITIONS}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Define all peripherals
add_peripheral(speaker "built-in speaker" "src/apple2/peripherals/speaker/Speaker.cpp" "" TRUE)
add_peripheral(keyboard "keyboard" "src/apple2/peripherals/keyboard/Keyboard.cpp;src/apple2/peripherals/keyboard/Keyboard_Maps.c" "" TRUE)
add_peripheral(disk "Disk II" "src/apple2/peripherals/disk/Disk.cpp;src/apple2/peripherals/disk/DiskEncoding.cpp;src/apple2/peripherals/disk/DiskLoader.cpp;src/apple2/peripherals/disk/ftpparse.cpp;src/apple2/peripherals/disk/formats/DoDriver.cpp;src/apple2/peripherals/disk/formats/IieDriver.cpp;src/apple2/peripherals/disk/formats/Nb2Driver.cpp;src/apple2/peripherals/disk/formats/NibDriver.cpp;src/apple2/peripherals/disk/formats/PoDriver.cpp;src/apple2/peripherals/disk/formats/Woz2Driver.cpp;src/apple2/peripherals/disk/formats/SectorDiskImage.cpp;src/apple2/peripherals/disk/formats/BitstreamDiskImage.cpp;src/apple2/peripherals/disk/formats/DiskContainer.cpp" "${ZLIB_LIBRARIES};${LIBZIP_LIBRARIES};${CURL_LIBRARIES}" TRUE)
add_peripheral(harddisk "Harddisk (SmartPort)" "src/apple2/peripherals/harddisk/Harddisk.cpp;src/apple2/peripherals/harddisk/HarddiskLoader.cpp;src/apple2/peripherals/harddisk/formats/RawHdDriver.cpp;src/apple2/peripherals/harddisk/formats/TwoImgDriver.cpp;src/apple2/peripherals/harddisk/formats/BlockDiskImage.cpp;src/apple2/peripherals/disk/formats/DiskContainer.cpp" "${ZLIB_LIBRARIES};${LIBZIP_LIBRARIES}" TRUE)
add_peripheral(mouse "Mouse Interface" "src/apple2/peripherals/mouse/Mouse.cpp" "" TRUE)
add_peripheral(mockingboard "Mockingboard" "src/apple2/peripherals/mockingboard/Mockingboard.cpp;src/apple2/chips/AY8910.cpp" "" TRUE)
add_peripheral(super_serial "Super Serial Card" "src/apple2/peripherals/super_serial_card/SuperSerial.cpp" "" TRUE)
add_peripheral(joystick "Joystick" "src/apple2/peripherals/joystick/Joystick.cpp" "" TRUE)
add_peripheral(printer "Parallel Printer" "src/apple2/peripherals/printer/Printer.cpp" "" FALSE)
add_peripheral(clock "No-Slot Clock" "src/apple2/peripherals/clock/Clock.cpp" "" FALSE)

# --- Executable Targets ---
set(COMMON_LIBS ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)

function(add_linapple_executable TARGET_NAME FE_NAME FE_SOURCES DBG_SOURCES EXTRA_LIBS EXTRA_INCLUDES EXTRA_DEFS)
    add_executable(${TARGET_NAME} ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${DBG_SOURCES} ${FE_SOURCES} ${SOURCES_A2_OPTIONAL})
    add_dependencies(${TARGET_NAME} generate_xpms generate_embedded_roms)
    set_target_properties(${TARGET_NAME} PROPERTIES ENABLE_EXPORTS TRUE)
    target_include_directories(${TARGET_NAME} PRIVATE ${LINAPPLE_INCLUDES} ${EXTRA_INCLUDES})
    target_link_libraries(${TARGET_NAME} PRIVATE ${COMMON_LIBS} ${EXTRA_LIBS})
    target_compile_definitions(${TARGET_NAME} PRIVATE
        ASSET_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/linapple"
        SYSCONF_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}/linapple"
        SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res"
        LINAPPLE_FRONTEND_NAME="${FE_NAME}"
        ${PERIPHERAL_DEFINITIONS}
        ${EXTRA_DEFS}
    )
    target_compile_definitions(${TARGET_NAME} PUBLIC ENABLE_RIFF)
    install(TARGETS ${TARGET_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

set(TUI_EXTRA_DEFS -DTUI)
if(PULSE_SIMPLE_FOUND)
    list(APPEND TUI_EXTRA_DEFS -DHAVE_PULSE_SIMPLE)
endif()
if(ALSA_FOUND)
    list(APPEND TUI_EXTRA_DEFS -DHAVE_ALSA)
endif()

if(FRONTEND_LOWER STREQUAL "all")
    add_linapple_executable(linapple-sdl3 "sdl3" "${SOURCES_FE_SDL3}" "${SOURCES_DBG_FULL}" "SDL3::SDL3;SDL3_image::SDL3_image" "" "")
    add_linapple_executable(linapple-sdl2 "sdl2" "${SOURCES_FE_SDL2}" "${SOURCES_DBG_FULL}" "SDL2::SDL2" "" "")
    add_linapple_executable(linapple-sdl1 "sdl1" "${SOURCES_FE_SDL1}" "${SOURCES_DBG_FULL}" "${SDL_LIBRARY};${SDL_IMAGE_LIBRARIES}" "${SDL_INCLUDE_DIR}" "")
    add_linapple_executable(linapple-headless "headless" "${SOURCES_FE_HEADLESS}" "${SOURCES_DBG_STUBS}" "" "" "-DHEADLESS")
    add_linapple_executable(linapple-tui "tui" "${SOURCES_FE_TUI}" "${SOURCES_DBG_FULL}" "${PULSE_SIMPLE_LIBRARIES};${ALSA_LIBRARIES}" "" "${TUI_EXTRA_DEFS}")
elseif(FRONTEND_LOWER STREQUAL "sdl3")
    add_linapple_executable(linapple "sdl3" "${SOURCES_FE_SDL3}" "${SOURCES_DBG}" "SDL3::SDL3;SDL3_image::SDL3_image" "" "")
elseif(FRONTEND_LOWER STREQUAL "sdl2")
    add_linapple_executable(linapple "sdl2" "${SOURCES_FE_SDL2}" "${SOURCES_DBG}" "SDL2::SDL2" "" "")
elseif(FRONTEND_LOWER STREQUAL "sdl1")
    add_linapple_executable(linapple "sdl1" "${SOURCES_FE_SDL1}" "${SOURCES_DBG}" "${SDL_LIBRARY};${SDL_IMAGE_LIBRARIES}" "${SDL_INCLUDE_DIR}" "")
elseif(FRONTEND_LOWER STREQUAL "headless")
    add_linapple_executable(linapple "headless" "${SOURCES_FE_HEADLESS}" "${SOURCES_DBG}" "" "" "-DHEADLESS")
elseif(FRONTEND_LOWER STREQUAL "tui")
    add_linapple_executable(linapple "tui" "${SOURCES_FE_TUI}" "${SOURCES_DBG}" "${PULSE_SIMPLE_LIBRARIES};${ALSA_LIBRARIES}" "" "${TUI_EXTRA_DEFS}")
endif()

# --- Installation ---
include(GNUInstallDirs)
if(PERIPHERAL_MODULES)
    install(TARGETS ${PERIPHERAL_MODULES} DESTINATION ${CMAKE_INSTALL_LIBDIR}/linapple/plugins)
endif()
install(FILES "${RES_DIR}/Master.dsk" DESTINATION ${CMAKE_INSTALL_DATADIR}/linapple)
install(FILES "${RES_DIR}/icon.bmp" DESTINATION ${CMAKE_INSTALL_DATADIR}/linapple)
install(FILES "${RES_DIR}/linapple.conf" DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/linapple)

# --- Tests ---
include(CTest)
if(BUILD_TESTING)
    find_path(DOCTEST_INCLUDE_DIR doctest.h PATH_SUFFIXES doctest)
    if(NOT DOCTEST_INCLUDE_DIR)
        include(FetchContent)
        set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
        FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
        FetchContent_MakeAvailable(doctest)
        set(DOCTEST_INCLUDE_DIR ${doctest_SOURCE_DIR}/doctest)
    endif()

    # Helper for adding tests
    macro(add_linapple_test name sources)
        add_executable(test-${name} ${sources} ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
        add_dependencies(test-${name} generate_xpms generate_embedded_roms)
        target_include_directories(test-${name} PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR} tests)
        target_compile_definitions(test-${name} PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
        target_link_libraries(test-${name} PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
        add_test(NAME test-${name} COMMAND test-${name})
        if(ENABLE_ASAN OR ENABLE_UBSAN)
            set_tests_properties(test-${name} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:symbolize=1;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
        endif()
    endmacro()

    # Refactored tests
    add_executable(test-app-config tests/test_app_config.cpp src/core/Registry.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp)
    target_include_directories(test-app-config PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
    add_test(NAME AppConfig COMMAND test-app-config)

    add_executable(test-app-args tests/test_app_args.cpp src/frontends/common/AppArgs.cpp src/core/Registry.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp)
    target_include_directories(test-app-args PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
    add_test(NAME AppArgs COMMAND test-app-args)

    add_linapple_test(app-env tests/test_app_env.cpp)
    add_linapple_test(app-controller tests/test_app_controller.cpp)
    add_linapple_test(integration tests/test_main.cpp)
    add_linapple_test(cpu-context tests/test_cpu_context.cpp)
    add_linapple_test(memory-context tests/test_memory_context.cpp)
    
    # Custom test-superserial-peripheral to avoid symbol conflict
    add_executable(test-superserial-peripheral tests/test_superserial_peripheral.cpp src/apple2/peripherals/super_serial_card/SuperSerial.cpp ${EMBEDDED_ROMS_CPP} src/core/Registry.cpp src/core/Log.cpp src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
    target_include_directories(test-superserial-peripheral PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR})
    target_compile_definitions(test-superserial-peripheral PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF)
    target_link_libraries(test-superserial-peripheral PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
    add_test(NAME test-superserial-peripheral COMMAND test-superserial-peripheral)

    add_linapple_test(abi tests/test_abi.cpp)
    add_linapple_test(peripheral-abi "tests/test_main.cpp;tests/test_peripheral_abi.cpp;tests/test_peripheral_abi_c.c")
    
    add_executable(test-keyboard-maps tests/test_keyboard_maps_standalone.cpp src/apple2/peripherals/keyboard/Keyboard_Maps.c)
    target_include_directories(test-keyboard-maps PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
    add_test(NAME KeyboardMaps COMMAND test-keyboard-maps)

    add_linapple_test(keyboard-peripheral "tests/test_main.cpp;tests/test_keyboard_peripheral.cpp")
    add_linapple_test(clock-peripheral "tests/test_main.cpp;tests/test_clock_peripheral.cpp")
    add_linapple_test(printer-peripheral "tests/test_main.cpp;tests/test_printer_peripheral.cpp")
    add_linapple_test(mouse-peripheral "tests/test_main.cpp;tests/test_mouse_peripheral.cpp")
    add_linapple_test(mockingboard-peripheral "tests/test_main.cpp;tests/test_mockingboard_peripheral.cpp")
    add_linapple_test(joystick-peripheral "tests/test_main.cpp;tests/test_joystick_peripheral.cpp")
    add_linapple_test(mouse-exhaustive "tests/test_main.cpp;tests/test_mouse_exhaustive.cpp")
    add_linapple_test(peripheral-slot0 "tests/test_main.cpp;tests/test_peripheral_slot0.cpp")
    add_linapple_test(disk-abi "tests/test_disk_abi.cpp;tests/test_disk_abi_c.c")
    
    add_executable(test-disk-gcr tests/test_disk_gcr.cpp src/apple2/peripherals/disk/DiskEncoding.cpp)
    target_include_directories(test-disk-gcr PRIVATE src ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-disk-gcr COMMAND test-disk-gcr)

    add_executable(test-disk-drivers tests/test_disk_drivers.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp src/apple2/peripherals/disk/formats/DoDriver.cpp src/apple2/peripherals/disk/formats/IieDriver.cpp src/apple2/peripherals/disk/formats/Nb2Driver.cpp src/apple2/peripherals/disk/formats/NibDriver.cpp src/apple2/peripherals/disk/formats/PoDriver.cpp src/apple2/peripherals/disk/formats/Woz2Driver.cpp src/apple2/peripherals/disk/DiskEncoding.cpp src/apple2/peripherals/disk/formats/SectorDiskImage.cpp src/apple2/peripherals/disk/formats/BitstreamDiskImage.cpp src/apple2/peripherals/disk/formats/DiskContainer.cpp)
    target_include_directories(test-disk-drivers PRIVATE src ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test-disk-drivers PRIVATE ${COMMON_LIBS})
    add_test(NAME test-disk-drivers COMMAND test-disk-drivers)

    add_linapple_test(program-loader tests/test_program_loader.cpp)
    add_linapple_test(system-roms "tests/test_main.cpp;tests/test_system_roms.cpp")
    add_linapple_test(basic-live-sync "tests/test_main.cpp;tests/test_basic_live_sync.cpp")
    add_linapple_test(disk-smoke tests/test_disk_smoke.cpp)
    add_linapple_test(disk-config-startup tests/test_disk_config_startup.cpp)
    add_linapple_test(disk-config-runtime-insert tests/test_disk_config_runtime_insert.cpp)
    add_linapple_test(disk-config-runtime-eject tests/test_disk_config_runtime_eject.cpp)
    add_linapple_test(headless-boot tests/test_headless_boot.cpp)

    add_linapple_test(file-browser tests/test_file_browser.cpp)

    add_executable(test-util-hash tests/test_util_hash.cpp src/frontends/common/Util_Hash.cpp)
    target_include_directories(test-util-hash PRIVATE src src/core src/frontends/common ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-util-hash COMMAND test-util-hash)

    add_linapple_test(video-stretch "tests/test_main.cpp;tests/test_video_stretch.cpp")

    add_linapple_test(ftpparse "tests/test_ftpparse.cpp;src/apple2/peripherals/disk/ftpparse.cpp")

    add_executable(test-disk-ui tests/test_disk_ui.cpp src/frontends/common/sdl/DiskUI.cpp)
    target_include_directories(test-disk-ui PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-disk-ui COMMAND test-disk-ui)

    add_executable(test-audio-dumper tests/test_audio_dumper.cpp src/frontends/common/AudioDumper.cpp)
    target_include_directories(test-audio-dumper PRIVATE src src/core src/frontends/common ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test-audio-dumper PRIVATE Threads::Threads)
    add_test(NAME test-audio-dumper COMMAND test-audio-dumper)

    add_executable(test-tui-shape-detector tests/test_tui_shape_detector.cpp src/frontends/tui/TuiShapeDetector.cpp)
    target_include_directories(test-tui-shape-detector PRIVATE src src/core src/frontends/tui ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-tui-shape-detector COMMAND test-tui-shape-detector)

    add_linapple_test(tui-disk-select "tests/test_main.cpp;tests/test_tui_disk_select.cpp;src/frontends/tui/TuiDiskSelect.cpp;src/frontends/tui/TuiVideo.cpp;src/frontends/tui/TuiShapeDetector.cpp")

    foreach(T disk_motor;disk_prot;disk_woz;disk_savestate;disk_errors;disk_stepper;disk_io;disk_compression)
        string(REPLACE "_" "-" TEST_NAME ${T})
        add_linapple_test(${TEST_NAME} tests/test_${T}.cpp)
    endforeach()

    if(FRONTEND_LOWER STREQUAL "all")
        set(CPU_TEST_TARGET linapple-sdl3)
    else()
        set(CPU_TEST_TARGET linapple)
    endif()
    add_test(NAME test-cpu COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_cpu_tests.sh")
    set_tests_properties(test-cpu PROPERTIES ENVIRONMENT "EMULATOR=$<TARGET_FILE:${CPU_TEST_TARGET}>")

    add_linapple_test(speaker "tests/test_main.cpp;tests/test_speaker.cpp")
    add_linapple_test(peripheral-lifecycle tests/test_peripheral_lifecycle.cpp)
    add_linapple_test(harddisk-peripheral tests/test_harddisk_peripheral.cpp)
    add_linapple_test(harddisk-exhaustive tests/test_harddisk_exhaustive.cpp)
    add_linapple_test(snapshot tests/test_snapshot.cpp)
    add_linapple_test(memory-exhaustive "tests/test_main.cpp;tests/test_memory_exhaustive.cpp")
    add_linapple_test(cpu-exhaustive "tests/test_main.cpp;tests/test_cpu_exhaustive.cpp")

    find_package(Z3 CONFIG QUIET)
    if(NOT Z3_FOUND)
        find_path(Z3_INCLUDE_DIR z3++.h)
        find_library(Z3_LIBRARY NAMES z3)
        if(Z3_INCLUDE_DIR AND Z3_LIBRARY)
            set(Z3_FOUND TRUE)
        endif()
    endif()

    if(Z3_FOUND)
        message(STATUS "Found Z3: SMT formal verification tests enabled")
        add_linapple_test(formal-disk "tests/test_main.cpp;tests/test_formal_disk.cpp")
        if(TARGET z3::libz3)
            target_link_libraries(test-formal-disk PRIVATE z3::libz3)
        elseif(TARGET z3)
            target_link_libraries(test-formal-disk PRIVATE z3)
        else()
            target_include_directories(test-formal-disk PRIVATE ${Z3_INCLUDE_DIR})
            target_link_libraries(test-formal-disk PRIVATE ${Z3_LIBRARY})
        endif()
    endif()

    if(FRONTEND_LOWER STREQUAL "sdl1" OR FRONTEND_LOWER STREQUAL "all")
        set(TEST_SOURCES_FE_SDL1 ${SOURCES_FE_SDL1})
        list(REMOVE_ITEM TEST_SOURCES_FE_SDL1 src/frontends/sdl1/Main.cpp)
        add_executable(test-frontend-sdl1 tests/test_main.cpp tests/test_frontend_sdl1.cpp ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} ${TEST_SOURCES_FE_SDL1})
        add_dependencies(test-frontend-sdl1 generate_xpms generate_embedded_roms)
        target_include_directories(test-frontend-sdl1 PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR} ${SDL_INCLUDE_DIR} tests src)
        target_compile_definitions(test-frontend-sdl1 PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
        target_link_libraries(test-frontend-sdl1 PRIVATE ${SDL_LIBRARY} ${SDL_IMAGE_LIBRARIES} ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
        add_test(NAME test-frontend-sdl1 COMMAND test-frontend-sdl1)
    endif()

    if(FRONTEND_LOWER STREQUAL "sdl2" OR FRONTEND_LOWER STREQUAL "all")
        set(TEST_SOURCES_FE_SDL2 ${SOURCES_FE_SDL2})
        list(REMOVE_ITEM TEST_SOURCES_FE_SDL2 src/frontends/sdl2/Main.cpp)
        add_executable(test-frontend-sdl2 tests/test_main.cpp tests/test_frontend_sdl2.cpp ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} ${TEST_SOURCES_FE_SDL2})
        add_dependencies(test-frontend-sdl2 generate_xpms generate_embedded_roms)
        target_include_directories(test-frontend-sdl2 PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR} tests src)
        target_compile_definitions(test-frontend-sdl2 PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
        target_link_libraries(test-frontend-sdl2 PRIVATE SDL2::SDL2 ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
        add_test(NAME test-frontend-sdl2 COMMAND test-frontend-sdl2)
    endif()

    if(FRONTEND_LOWER STREQUAL "sdl3" OR FRONTEND_LOWER STREQUAL "all")
        set(TEST_SOURCES_FE_SDL3 ${SOURCES_FE_SDL3})
        list(REMOVE_ITEM TEST_SOURCES_FE_SDL3 src/frontends/sdl3/Main.cpp)
        add_executable(test-frontend-sdl3 tests/test_main.cpp tests/test_frontend_sdl3.cpp ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} ${TEST_SOURCES_FE_SDL3})
        add_dependencies(test-frontend-sdl3 generate_xpms generate_embedded_roms)
        target_include_directories(test-frontend-sdl3 PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR} tests src)
        target_compile_definitions(test-frontend-sdl3 PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
        target_link_libraries(test-frontend-sdl3 PRIVATE SDL3::SDL3 SDL3_image::SDL3_image ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
        add_test(NAME test-frontend-sdl3 COMMAND test-frontend-sdl3)
    endif()
endif()

if(ENABLE_FUZZING)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "libFuzzer requires Clang. Fuzz targets may fail to build with other compilers.")
    endif()
    set(FUZZ_FLAGS "-fsanitize=fuzzer,address,undefined")

    macro(add_linapple_fuzzer name source)
        add_executable(${name} ${source} ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
        add_dependencies(${name} generate_xpms)
        target_include_directories(${name} PRIVATE ${LINAPPLE_INCLUDES})
        target_compile_definitions(${name} PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF)
        target_compile_options(${name} PRIVATE ${FUZZ_FLAGS})
        target_link_options(${name} PRIVATE ${FUZZ_FLAGS})
        target_link_libraries(${name} PRIVATE ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBZIP_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
    endmacro()

    add_linapple_fuzzer(fuzz-disk-loader tests/fuzz/fuzz_disk_loader.cpp)
    add_linapple_fuzzer(fuzz-snapshot tests/fuzz/fuzz_snapshot.cpp)
    add_linapple_fuzzer(fuzz-memory tests/fuzz/fuzz_memory.cpp)

    if(BUILD_TESTING)
        add_test(NAME fuzz-disk-loader
                 COMMAND fuzz-disk-loader
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/disk-loader
                         -dict=${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/dictionaries/disk_formats.dict
                         -max_total_time=10)
        set_tests_properties(fuzz-disk-loader PROPERTIES LABELS "fuzz")

        add_test(NAME fuzz-snapshot
                 COMMAND fuzz-snapshot
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/snapshot
                         -max_len=131824
                         -max_total_time=10)
        set_tests_properties(fuzz-snapshot PROPERTIES LABELS "fuzz")

        add_test(NAME fuzz-memory
                 COMMAND fuzz-memory
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/memory
                         -dict=${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/dictionaries/memory_text.dict
                         -max_total_time=10)
        set_tests_properties(fuzz-memory PROPERTIES LABELS "fuzz")
    endif()
endif()

if(TARGET linapple)
    add_custom_target(linapple-core DEPENDS linapple)
else()
    add_custom_target(linapple-core DEPENDS linapple-sdl3)
endif()
if(PERIPHERAL_MODULES)
    add_custom_target(linapple-peripherals-standard DEPENDS ${PERIPHERAL_MODULES})
endif()

# --- Packaging (CPack) ---
set(CPACK_PACKAGE_NAME "linapple")
set(CPACK_PACKAGE_VENDOR "LinApple Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LinApple Apple II / II+ / //e Emulator")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "David Baucum <david@baucum.me>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING.txt")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "David Baucum <david@baucum.me>")
set(CPACK_DEBIAN_PACKAGE_SECTION "emulators")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0-only")
set(CPACK_RPM_PACKAGE_GROUP "Emulators")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
include(CPack)
