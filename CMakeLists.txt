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
    set(FRONTEND_HAS_FTP OFF)
else()
    set(DEFAULT_ENABLE_DEBUGGER ON)
    set(FRONTEND_HAS_FTP ON)
endif()
option(ENABLE_DEBUGGER "Enable integrated debugger" ${DEFAULT_ENABLE_DEBUGGER})
option(ENABLE_FTP "Enable FTP support" ON)

option(PROFILING "Enable profiling" OFF)
option(REGISTRY_WRITEABLE "Enable registry writing" ON)
option(BUILD_SHARED_PERIPHERALS "Build peripherals as shared libraries instead of built-in" OFF)
if(BUILD_SHARED_PERIPHERALS)
    add_compile_definitions(BUILD_SHARED_PERIPHERALS)
endif()

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

if(ENABLE_ROM_MOUSE)
    add_compile_definitions(ENABLE_ROM_MOUSE=1)
else()
    add_compile_definitions(ENABLE_ROM_MOUSE=0)
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
if(ENABLE_FTP)
    find_package(CURL REQUIRED)
    add_compile_definitions(ENABLE_FTP=1)
    set(FTP_LIBRARIES ${CURL_LIBRARIES})
    set(FTP_INCLUDE_DIRS ${CURL_INCLUDE_DIRS})
else()
    add_compile_definitions(ENABLE_FTP=0)
    set(FTP_LIBRARIES "")
    set(FTP_INCLUDE_DIRS "")
endif()
find_package(PkgConfig REQUIRED)

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
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/DISK2-P6.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/SSC.rom"
            "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/MouseInterface.rom"
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
    ${FTP_INCLUDE_DIRS}
)

if(ENABLE_DEBUGGER)
    list(APPEND LINAPPLE_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/Debugger")
endif()

if(FRONTEND STREQUAL "tui")
    list(APPEND LINAPPLE_INCLUDES ${PULSE_SIMPLE_INCLUDE_DIRS} ${ALSA_INCLUDE_DIRS})
endif()

# --- Source Lists ---
set(SOURCES_CORE
    src/frontends/common/AudioMixer.cpp
    src/frontends/common/FramePacer.cpp
    src/core/LinAppleCore.cpp
    src/core/Log.cpp
    src/core/ProgramLoader.cpp
    src/core/Registry.cpp
    src/core/Asset.cpp
    src/core/BasicLiveSync.cpp
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
    src/core/services/ftp/FtpParser.cpp
)

set(SOURCES_A2_MANDATORY
    src/apple2/chips/6821.cpp
    src/apple2/CPU.cpp
    src/apple2/Memory.cpp
    src/apple2/Snapshot.cpp
    src/apple2/Video.cpp
    src/apple2/peripherals/Peripheral.cpp
    src/apple2/peripherals/Peripheral_Internal.cpp
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
    src/frontends/common/sdl/AudioDevice.cpp
    src/frontends/common/sdl/DiskUI.cpp
    src/frontends/common/sdl/FtpDialog.cpp
    src/frontends/common/sdl/DiskSelect.cpp
    src/frontends/common/sdl/Harddisk.cpp
    src/frontends/common/sdl/MainSession.cpp
    src/frontends/common/sdl/Keyboard.cpp
    src/frontends/common/sdl/JoystickFrontend.cpp
    src/core/services/ftp/FtpClient.cpp
)

set(SOURCES_FE_SDL3
    ${SOURCES_FE_COMMON_SDL}
    src/frontends/sdl3/DiskChoose.cpp
    src/frontends/sdl3/Frame.cpp
    src/frontends/sdl3/Main.cpp
    src/frontends/sdl3/SDL_Asset.cpp
    src/frontends/sdl3/SDL_Input.cpp
    src/frontends/sdl3/SDL_Path.cpp
    src/frontends/sdl3/SDL_Video.cpp
)

set(SOURCES_FE_SDL2
    ${SOURCES_FE_COMMON_SDL}
    src/frontends/sdl2/DiskChoose.cpp
    src/frontends/sdl2/Frame.cpp
    src/frontends/sdl2/Main.cpp
    src/frontends/sdl2/SDL_Asset.cpp
    src/frontends/sdl2/SDL_Input.cpp
    src/frontends/sdl2/SDL_Path.cpp
    src/frontends/sdl2/SDL_Video.cpp
)

set(SOURCES_FE_SDL1
    ${SOURCES_FE_COMMON_SDL}
    src/frontends/sdl1/DiskChoose.cpp
    src/frontends/sdl1/Frame.cpp
    src/frontends/sdl1/Main.cpp
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

# A media library is code two or more cards share, such as a parser of
# untrusted input. Declaring one only records it; the target is created by
# finalize_peripheral_libraries once every card has said which libraries it
# wants, so a library no enabled card names is never built and its external
# dependencies never linked. The lists live in the cache so the functions
# below can read them without threading PARENT_SCOPE through each call.
set(PERIPHERAL_LIBRARIES "" CACHE INTERNAL "Media libraries declared" FORCE)
set(PERIPHERAL_LIBRARIES_WANTED "" CACHE INTERNAL "Media libraries an enabled peripheral needs" FORCE)
function(add_peripheral_library name)
    cmake_parse_arguments(LIB "" "" "SOURCES;EXTERNAL" ${ARGN})
    if(NOT LIB_SOURCES)
        message(FATAL_ERROR "add_peripheral_library(${name}) needs SOURCES")
    endif()
    set(PERIPHERAL_LIBRARY_${name}_SOURCES "${LIB_SOURCES}" CACHE INTERNAL "Sources of media library ${name}" FORCE)
    set(PERIPHERAL_LIBRARY_${name}_EXTERNAL "${LIB_EXTERNAL}" CACHE INTERNAL "External dependencies of media library ${name}" FORCE)
    set(PERIPHERAL_LIBRARIES "${PERIPHERAL_LIBRARIES};${name}" CACHE INTERNAL "Media libraries declared" FORCE)
endfunction()

# Function to add a peripheral with optional compilation and shared lib support
function(add_peripheral name desc sources extra_libs libs force_builtin)
    string(TOUPPER ${name} upper_name)
    string(REPLACE "-" "_" upper_name ${upper_name})
    set(opt_name "ENABLE_PERIPHERAL_${upper_name}")

    # A card owns its directory and may compile shared silicon or media, and
    # nothing else: a source reached from another card's directory is a
    # dependency between cards, which the include rule forbids as well.
    set(source_dir ${name})
    if(name STREQUAL "super_serial")
        set(source_dir "super_serial_card")
    endif()
    foreach(source IN LISTS sources)
        if(IS_ABSOLUTE "${source}")
            file(RELATIVE_PATH source "${CMAKE_CURRENT_SOURCE_DIR}" "${source}")
        endif()
        if(NOT source MATCHES "^src/apple2/(peripherals/${source_dir}|chips|media)/")
            message(FATAL_ERROR "Peripheral ${name} lists ${source}, which is outside src/apple2/peripherals/${source_dir}/, src/apple2/chips/ and src/apple2/media/")
        endif()
    endforeach()

    option(${opt_name} "Enable ${desc}" ON)

    if(${opt_name})
        foreach(lib IN LISTS libs)
            if(NOT lib IN_LIST PERIPHERAL_LIBRARIES)
                message(FATAL_ERROR "Peripheral ${name} wants media library ${lib}, which add_peripheral_library never declared")
            endif()
            set(PERIPHERAL_LIBRARIES_WANTED "${PERIPHERAL_LIBRARIES_WANTED};${lib}" CACHE INTERNAL "Media libraries an enabled peripheral needs" FORCE)
        endforeach()
        if(BUILD_SHARED_PERIPHERALS AND NOT force_builtin)
            add_library(peripheral-${name} SHARED ${sources})
            target_compile_definitions(peripheral-${name} PRIVATE BUILD_SHARED_PERIPHERAL)
            target_include_directories(peripheral-${name} PRIVATE ${LINAPPLE_INCLUDES})
            if(extra_libs)
                target_link_libraries(peripheral-${name} PRIVATE ${extra_libs})
            endif()
            # The media-<lib> targets are created by finalize_peripheral_libraries,
            # after every card is declared; a link by name resolves at generate time.
            foreach(lib IN LISTS libs)
                target_link_libraries(peripheral-${name} PRIVATE media-${lib})
            endforeach()
            # Plugins hide all symbols except the PERIPHERAL_REGISTER descriptor.
            set_target_properties(peripheral-${name} PROPERTIES
                PREFIX "" OUTPUT_NAME "${name}"
                C_VISIBILITY_PRESET hidden
                CXX_VISIBILITY_PRESET hidden
                VISIBILITY_INLINES_HIDDEN ON)
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

# Creates media-<lib> for every library an enabled card asked for and lists
# the targets in MEDIA_TARGETS for the executable and every test or fuzzer
# that carries card code. Hidden visibility keeps the library's symbols out of
# the executable's dynamic table (it has ENABLE_EXPORTS for the plugins) and
# out of any plugin's, so two copies can never interpose on each other.
function(add_media_target target lib)
    add_library(${target} STATIC ${PERIPHERAL_LIBRARY_${lib}_SOURCES})
    target_include_directories(${target} PUBLIC ${LINAPPLE_INCLUDES})
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON)
    # An external is looked up here and nowhere else, so a build with no card
    # that wants it never sees the package or links it.
    foreach(external IN LISTS PERIPHERAL_LIBRARY_${lib}_EXTERNAL)
        if(external STREQUAL "ZLIB")
            if(NOT TARGET ZLIB::ZLIB)
                find_package(ZLIB REQUIRED)
            endif()
            target_link_libraries(${target} PUBLIC ZLIB::ZLIB)
        elseif(external STREQUAL "LIBZIP")
            if(NOT TARGET PkgConfig::LIBZIP)
                pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
            endif()
            target_link_libraries(${target} PUBLIC PkgConfig::LIBZIP)
        else()
            message(FATAL_ERROR "Media library ${lib} names an unknown external dependency ${external}")
        endif()
    endforeach()
endfunction()

function(finalize_peripheral_libraries)
    set(media_targets "")
    set(media_fuzz_targets "")
    set(wanted "${PERIPHERAL_LIBRARIES_WANTED}")
    list(REMOVE_DUPLICATES wanted)
    foreach(lib IN LISTS wanted)
        if(NOT lib)
            continue()
        endif()
        add_media_target(media-${lib} ${lib})
        list(APPEND media_targets media-${lib})
        # A fuzzer's compile options never reach a library it links, so the
        # parser the fuzzer exists to exercise would run uninstrumented: no
        # coverage feedback and no sanitizer inside it. The fuzzers link a
        # twin built with the instrumentation instead.
        if(ENABLE_FUZZING)
            add_media_target(media-${lib}-fuzz ${lib})
            target_compile_options(media-${lib}-fuzz PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
            list(APPEND media_fuzz_targets media-${lib}-fuzz)
        endif()
    endforeach()
    set(MEDIA_TARGETS "${media_targets}" PARENT_SCOPE)
    set(MEDIA_FUZZ_TARGETS "${media_fuzz_targets}" PARENT_SCOPE)
endfunction()

# Define all peripherals
add_peripheral(speaker "built-in speaker" "src/apple2/peripherals/speaker/Speaker.cpp" "" "" FALSE)
add_peripheral(keyboard "keyboard" "src/apple2/peripherals/keyboard/Keyboard.cpp;src/apple2/peripherals/keyboard/Keyboard_Maps.c" "" "" TRUE)
add_peripheral_library(image-container
    SOURCES src/apple2/media/image_container/ImageContainer.cpp
    EXTERNAL ZLIB LIBZIP)
# A disk format driver registers itself from its own translation unit, so
# adding a format is one new file here and nothing else to edit. These sources
# compile straight into the executable via SOURCES_A2_OPTIONAL and never into a
# static library, which is what stops -Wl,--gc-sections discarding an object
# whose only reference is its own static initialiser.
file(GLOB DISK_FORMAT_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/peripherals/disk/formats/*Driver.cpp")
list(LENGTH DISK_FORMAT_SOURCES DISK_FORMAT_DRIVER_COUNT)

set(DISK_BACKEND_SOURCES
    src/apple2/peripherals/disk/DiskEncoding.cpp
    src/apple2/peripherals/disk/formats/SectorDiskImage.cpp
    src/apple2/peripherals/disk/formats/NibbleDiskImage.cpp)

add_peripheral(disk "Disk II" "src/apple2/peripherals/disk/Disk.cpp;src/apple2/peripherals/disk/DiskLoader.cpp;${DISK_BACKEND_SOURCES};${DISK_FORMAT_SOURCES}" "" "image-container" TRUE)
add_peripheral(harddisk "Harddisk (SmartPort)" "src/apple2/peripherals/harddisk/Harddisk.cpp;src/apple2/peripherals/harddisk/HarddiskLoader.cpp;src/apple2/peripherals/harddisk/formats/RawHdDriver.cpp;src/apple2/peripherals/harddisk/formats/TwoImgDriver.cpp;src/apple2/peripherals/harddisk/formats/BlockDiskImage.cpp" "" "image-container" TRUE)
add_peripheral(mouse "Mouse Interface" "src/apple2/peripherals/mouse/Mouse.cpp" "" "" TRUE)
add_peripheral(mockingboard "Mockingboard" "src/apple2/peripherals/mockingboard/Mockingboard.cpp;src/apple2/chips/AY8910.cpp;src/apple2/chips/6522.cpp" "" "" FALSE)
add_peripheral(super_serial "Super Serial Card" "src/apple2/peripherals/super_serial_card/SuperSerial.cpp" "" "" TRUE)
add_peripheral(joystick "Joystick" "src/apple2/peripherals/joystick/Joystick.cpp" "" "" TRUE)
add_peripheral(printer "Parallel Printer" "src/apple2/peripherals/printer/Printer.cpp" "" "" FALSE)
add_peripheral(clock "Clock Card" "src/apple2/peripherals/clock/ClockCard.cpp" "" "" FALSE)
finalize_peripheral_libraries()

# The chips have no target of their own: the core list and the Mockingboard
# compile them straight into whichever executable or plugin needs them, so
# the visibility the media targets get as a property is set on the sources.
# Without it the executable's export table and a shared mockingboard both
# carry the chip symbols, and two copies would interpose on each other.
file(GLOB CHIP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/apple2/chips/*.cpp")
set_source_files_properties(${CHIP_SOURCES} PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")

# --- Executable Targets ---
set(COMMON_LIBS ${CMAKE_DL_LIBS} Threads::Threads)

function(add_linapple_executable TARGET_NAME FE_NAME FE_SOURCES DBG_SOURCES EXTRA_LIBS EXTRA_INCLUDES EXTRA_DEFS)
    add_executable(${TARGET_NAME} ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${DBG_SOURCES} ${FE_SOURCES} ${SOURCES_A2_OPTIONAL})
    add_dependencies(${TARGET_NAME} generate_xpms generate_embedded_roms)
    if(PERIPHERAL_MODULES)
        add_dependencies(${TARGET_NAME} ${PERIPHERAL_MODULES})
    endif()
    set_target_properties(${TARGET_NAME} PROPERTIES ENABLE_EXPORTS TRUE)
    target_include_directories(${TARGET_NAME} PRIVATE ${LINAPPLE_INCLUDES} ${EXTRA_INCLUDES})
    target_link_libraries(${TARGET_NAME} PRIVATE ${COMMON_LIBS} ${EXTRA_LIBS} ${MEDIA_TARGETS})
    if(FE_NAME STREQUAL "sdl1")
        list(APPEND EXTRA_DEFS LINAPPLE_SDL_VERSION=1)
    elseif(FE_NAME STREQUAL "sdl2")
        list(APPEND EXTRA_DEFS LINAPPLE_SDL_VERSION=2)
    elseif(FE_NAME STREQUAL "sdl3")
        list(APPEND EXTRA_DEFS LINAPPLE_SDL_VERSION=3)
    endif()
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
    add_linapple_executable(linapple-sdl3 "sdl3" "${SOURCES_FE_SDL3}" "${SOURCES_DBG_FULL}" "SDL3::SDL3;SDL3_image::SDL3_image;${FTP_LIBRARIES}" "" "")
    add_linapple_executable(linapple-sdl2 "sdl2" "${SOURCES_FE_SDL2}" "${SOURCES_DBG_FULL}" "SDL2::SDL2;${FTP_LIBRARIES}" "" "")
    add_linapple_executable(linapple-sdl1 "sdl1" "${SOURCES_FE_SDL1}" "${SOURCES_DBG_FULL}" "${SDL_LIBRARY};${SDL_IMAGE_LIBRARIES};${FTP_LIBRARIES}" "${SDL_INCLUDE_DIR}" "")
    add_linapple_executable(linapple-headless "headless" "${SOURCES_FE_HEADLESS}" "${SOURCES_DBG_STUBS}" "" "" "-DHEADLESS")
    add_linapple_executable(linapple-tui "tui" "${SOURCES_FE_TUI}" "${SOURCES_DBG_FULL}" "${PULSE_SIMPLE_LIBRARIES};${ALSA_LIBRARIES}" "" "${TUI_EXTRA_DEFS}")
elseif(FRONTEND_LOWER STREQUAL "sdl3")
    add_linapple_executable(linapple "sdl3" "${SOURCES_FE_SDL3}" "${SOURCES_DBG}" "SDL3::SDL3;SDL3_image::SDL3_image;${FTP_LIBRARIES}" "" "")
elseif(FRONTEND_LOWER STREQUAL "sdl2")
    add_linapple_executable(linapple "sdl2" "${SOURCES_FE_SDL2}" "${SOURCES_DBG}" "SDL2::SDL2;${FTP_LIBRARIES}" "" "")
elseif(FRONTEND_LOWER STREQUAL "sdl1")
    add_linapple_executable(linapple "sdl1" "${SOURCES_FE_SDL1}" "${SOURCES_DBG}" "${SDL_LIBRARY};${SDL_IMAGE_LIBRARIES};${FTP_LIBRARIES}" "${SDL_INCLUDE_DIR}" "")
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

    # A per-test ScopedTestConfig_t only protects the suites that construct
    # one, and most suites own their doctest main, so the universal redirect
    # lives where every ctest run passes. With this no suite can read the
    # developer's config or write linapple.log under $HOME.
    macro(linapple_test_home test_name dir_name)
        set(LINAPPLE_TEST_HOME "${CMAKE_BINARY_DIR}/test-home/${dir_name}")
        file(MAKE_DIRECTORY "${LINAPPLE_TEST_HOME}")
        set(LINAPPLE_TEST_ENV
            "XDG_CONFIG_HOME=${LINAPPLE_TEST_HOME}"
            "XDG_DATA_HOME=${LINAPPLE_TEST_HOME}"
            "XDG_CONFIG_DIRS=${LINAPPLE_TEST_HOME}"
            "HOME=${LINAPPLE_TEST_HOME}")
        if(ENABLE_ASAN OR ENABLE_UBSAN)
            list(APPEND LINAPPLE_TEST_ENV "ASAN_OPTIONS=detect_leaks=1:symbolize=1" "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
        endif()
        set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "${LINAPPLE_TEST_ENV}")
    endmacro()

    # Helper for adding tests
    macro(add_linapple_test name sources)
        add_executable(test-${name} ${sources} tests/test_fixtures_core.cpp ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} src/frontends/headless/Stubs.cpp src/frontends/headless/Headless_Path.cpp)
        add_dependencies(test-${name} generate_xpms generate_embedded_roms)
        if(PERIPHERAL_MODULES)
            add_dependencies(test-${name} ${PERIPHERAL_MODULES})
        endif()
        set_target_properties(test-${name} PROPERTIES ENABLE_EXPORTS TRUE)
        target_include_directories(test-${name} PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR} tests)
        target_compile_definitions(test-${name} PRIVATE ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
        target_link_libraries(test-${name} PRIVATE ${COMMON_LIBS} ${MEDIA_TARGETS})
        add_test(NAME test-${name} COMMAND test-${name})
        linapple_test_home(test-${name} ${name})
    endmacro()

    # Refactored tests
    add_executable(test-app-config tests/test_app_config.cpp src/core/Registry.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp)
    target_include_directories(test-app-config PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
    add_test(NAME AppConfig COMMAND test-app-config)

    add_executable(test-app-args tests/test_app_args.cpp src/frontends/common/AppArgs.cpp src/core/Registry.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp)
    target_include_directories(test-app-args PRIVATE src src/core tests ${DOCTEST_INCLUDE_DIR})
    add_test(NAME AppArgs COMMAND test-app-args)

    add_executable(test-fixtures tests/test_fixtures.cpp)
    target_include_directories(test-fixtures PRIVATE tests ${DOCTEST_INCLUDE_DIR})
    target_compile_definitions(test-fixtures PRIVATE
        TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
    add_test(NAME test-fixtures COMMAND test-fixtures)
    if(ENABLE_ASAN OR ENABLE_UBSAN)
        set_tests_properties(test-fixtures PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:symbolize=1;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
    endif()

    if(ENABLE_ASAN OR ENABLE_UBSAN OR DEFINED ENV{CI})
        set(PERIPHERAL_TEST_TIMEOUT 10)
    else()
        set(PERIPHERAL_TEST_TIMEOUT 1)
    endif()

    add_linapple_test(app-env tests/test_app_env.cpp)
    add_linapple_test(app-controller tests/test_app_controller.cpp)
    add_linapple_test(integration tests/test_main.cpp)
    add_linapple_test(cpu-context tests/test_cpu_context.cpp)
    add_linapple_test(memory-context tests/test_memory_context.cpp)

    add_linapple_test(superserial-peripheral "tests/test_main.cpp;tests/test_superserial_peripheral.cpp")

    add_linapple_test(abi tests/test_abi.cpp)
    # Verifies ABI headers compile cleanly as pure C99.
    set_source_files_properties(tests/test_peripheral_abi_c.c PROPERTIES COMPILE_OPTIONS "-std=c99;-pedantic")
    add_linapple_test(peripheral-abi "tests/test_main.cpp;tests/test_peripheral_abi.cpp;tests/test_peripheral_abi_c.c")

    add_linapple_test(keyboard-peripheral "tests/test_main.cpp;tests/test_keyboard_peripheral.cpp")
    if(ENABLE_PERIPHERAL_CLOCK)
        set_source_files_properties(tests/test_clock_abi_c.c PROPERTIES COMPILE_OPTIONS "-std=c99;-pedantic")
        add_linapple_test(clock-peripheral "tests/test_main.cpp;tests/test_clock_peripheral.cpp;tests/test_clock_abi_c.c")
        set_tests_properties(test-clock-peripheral PROPERTIES TIMEOUT ${PERIPHERAL_TEST_TIMEOUT})
        # The fixture is the one frame with its pin engaged the loader is
        # proven against; regenerating it from today's card would drop the
        # pin, so its bytes are pinned.
        add_test(NAME test-clock-frame-v1-pinned COMMAND sh -c
            "echo 'b04c4dcef167bb1e9094e7fc452e0bac61e6669c  tests/fixtures/clock-frame-v1.bin' | sha1sum -c --quiet"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(test-clock-frame-v1-pinned PROPERTIES TIMEOUT 5)
        add_linapple_test(clock-snapshot tests/test_clock_snapshot.cpp)
        set_tests_properties(test-clock-snapshot PROPERTIES TIMEOUT ${PERIPHERAL_TEST_TIMEOUT})
    endif()
    if(BUILD_SHARED_PERIPHERALS)
        # Verify plugin imports no external symbols from the main executable.
        foreach(module IN LISTS PERIPHERAL_MODULES)
            string(REPLACE "peripheral-" "" plugin_name "${module}")
            add_test(NAME test-plugin-exports-${plugin_name} COMMAND sh -c
            [=[
plugin="$1"
exported=$(nm -D --defined-only "$plugin" | awk '{print $NF}')
if [ "$exported" != "linapple_peripheral_descriptor" ]; then
  echo "$plugin exports these where linapple_peripheral_descriptor alone was expected:"
  echo "$exported"
  exit 1
fi
imported=$(nm -D --undefined-only "$plugin" | awk '{print $NF}' \
  | grep -vE '@(GLIBC|GLIBCXX|CXXABI|GCC)_' \
  | grep -vE '^(__gmon_start__|_ITM_[A-Za-z]+|__cxa_finalize)$')
if [ -n "$imported" ]; then
  echo "$plugin imports these from outside the toolchain:"
  echo "$imported"
  exit 1
fi
]=]
                sh $<TARGET_FILE:${module}>)
            set_tests_properties(test-plugin-exports-${plugin_name} PROPERTIES TIMEOUT 5)
        endforeach()
    endif()
    if(ENABLE_PERIPHERAL_PRINTER)
        set_source_files_properties(tests/test_printer_abi_c.c PROPERTIES COMPILE_OPTIONS "-std=c99;-pedantic")
        add_linapple_test(printer-peripheral "tests/test_main.cpp;tests/HeadlessHarness.cpp;tests/test_printer_peripheral.cpp;tests/test_printer_abi_c.c")
        set_tests_properties(test-printer-peripheral PROPERTIES TIMEOUT ${PERIPHERAL_TEST_TIMEOUT})
        add_linapple_test(printer-frontend "tests/HeadlessHarness.cpp;tests/test_printer_frontend.cpp")
        set_tests_properties(test-printer-frontend PROPERTIES TIMEOUT ${PERIPHERAL_TEST_TIMEOUT})
        add_linapple_test(printer-snapshot tests/test_printer_snapshot.cpp)
        set_tests_properties(test-printer-snapshot PROPERTIES TIMEOUT ${PERIPHERAL_TEST_TIMEOUT})
        # The card and the suite each carry a transcription of this file and
        # are compared with each other; the file they were taken from is pinned
        # so the reference cannot drift.
        add_test(NAME test-printer-rom-pinned COMMAND sh -c
            "echo 'c75c751589cbcc1ad4ab4f63676af7abadd86f5b  res/roms/Parallel.rom' | sha1sum -c --quiet"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(test-printer-rom-pinned PROPERTIES TIMEOUT 5)
    endif()
    add_executable(test-via6522 tests/test_via6522.cpp src/apple2/chips/6522.cpp)
    target_include_directories(test-via6522 PRIVATE src ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-via6522 COMMAND test-via6522)

    add_executable(test-ay8910 tests/test_ay8910.cpp src/apple2/chips/AY8910.cpp)
    target_include_directories(test-ay8910 PRIVATE src ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-ay8910 COMMAND test-ay8910)

    add_linapple_test(mockingboard-peripheral "tests/test_main.cpp;tests/test_mockingboard_peripheral.cpp")
    add_linapple_test(mockingboard-core "tests/test_main.cpp;tests/test_mockingboard_core.cpp")
    add_linapple_test(mockingboard-e2e "tests/test_main.cpp;tests/test_mockingboard_e2e.cpp")
    set_tests_properties(test-mockingboard-e2e PROPERTIES LABELS "e2e")
    add_linapple_test(joystick-peripheral "tests/test_main.cpp;tests/test_joystick_peripheral.cpp")
    add_linapple_test(mouse-exhaustive "tests/test_main.cpp;tests/test_mouse_exhaustive.cpp")
    add_linapple_test(peripheral-slot0 "tests/test_main.cpp;tests/test_peripheral_slot0.cpp")
    add_linapple_test(slot0-dispatch "tests/test_main.cpp;tests/test_slot0_dispatch.cpp")
    # Every test that mounts a disk or harddisk card at run time is built only
    # with that card, so a build without one still has a green test suite and
    # proves nothing of the card's leaked into it.
    if(ENABLE_PERIPHERAL_DISK)
        # The smoke test proves the ABI headers are C99, not merely C the compiler
        # happens to accept.
        set_source_files_properties(tests/test_disk_abi_c.c PROPERTIES COMPILE_OPTIONS "-std=c99;-pedantic")
        add_linapple_test(disk-abi "tests/test_disk_abi.cpp;tests/test_disk_abi_c.c")

        add_executable(test-disk-gcr tests/test_disk_gcr.cpp src/apple2/peripherals/disk/DiskEncoding.cpp)
        target_include_directories(test-disk-gcr PRIVATE src ${DOCTEST_INCLUDE_DIR})
        add_test(NAME test-disk-gcr COMMAND test-disk-gcr)

        add_executable(test-disk-adapter tests/test_disk_adapter.cpp src/apple2/peripherals/disk/DiskEncoding.cpp)
        target_include_directories(test-disk-adapter PRIVATE src ${DOCTEST_INCLUDE_DIR})
        add_test(NAME test-disk-adapter COMMAND test-disk-adapter)

        add_executable(test-disk-drivers tests/test_disk_drivers.cpp src/core/Log.cpp src/frontends/headless/Headless_Path.cpp src/apple2/peripherals/disk/DiskLoader.cpp ${DISK_BACKEND_SOURCES} ${DISK_FORMAT_SOURCES})
        target_include_directories(test-disk-drivers PRIVATE src ${DOCTEST_INCLUDE_DIR})
        target_link_libraries(test-disk-drivers PRIVATE ${COMMON_LIBS} ${MEDIA_TARGETS})
        add_test(NAME test-disk-drivers COMMAND test-disk-drivers)

        add_executable(test-disk-registry tests/test_disk_registry.cpp src/frontends/headless/Headless_Path.cpp src/apple2/peripherals/disk/DiskLoader.cpp ${DISK_BACKEND_SOURCES} ${DISK_FORMAT_SOURCES})
        target_include_directories(test-disk-registry PRIVATE src ${DOCTEST_INCLUDE_DIR})
        target_compile_definitions(test-disk-registry PRIVATE DISK_FORMAT_DRIVER_COUNT=${DISK_FORMAT_DRIVER_COUNT})
        target_link_libraries(test-disk-registry PRIVATE ${COMMON_LIBS} ${MEDIA_TARGETS})
        add_test(NAME test-disk-registry COMMAND test-disk-registry)

        # The P6 sequencer PROM is a verbatim Apple part; a wrong or truncated
        # image would mis-clock every read instead of failing loudly.
        add_test(NAME test-disk-p6-rom COMMAND sh -c
    [=[
digest=$(sha1sum "$1" | cut -d " " -f 1)
size=$(wc -c < "$1")
if [ "$digest" != "bc39fbd5b9a8d2287ac5d0a42e639fc4d3c2f9d4" ]; then
  echo "DISK2-P6.rom sha1 is $digest"
  exit 1
fi
if [ "$size" -ne 256 ]; then
  echo "DISK2-P6.rom is $size bytes"
  exit 1
fi
]=]
            sh "${CMAKE_CURRENT_SOURCE_DIR}/res/roms/DISK2-P6.rom")

        # The disk fixtures are generated, so a hand edit to one, or a generator
        # that stopped being deterministic, would quietly move every golden built
        # on it. The committed copies and a fresh regeneration are both held to
        # the same pins. Raw images and stored archives are pinned byte for byte;
        # the gzip member's deflate bytes depend on which zlib perl links, so it
        # is pinned by its extracted payload instead.
        add_test(NAME test-disk-fixtures COMMAND sh -c
    [=[
set -e
scratch=$(mktemp -d "${TMPDIR:-/tmp}/linapple_fixtures_XXXXXX")
trap 'rm -rf "$scratch"' EXIT
perl "$1/build_fixtures.pl" "$scratch"
for dir in "$1" "$scratch"; do
  (cd "$dir" && sha1sum -c --quiet "$1/SHA1SUMS")
  while read -r digest name; do
    actual=$(gzip -dc "$dir/$name" | sha1sum | cut -d " " -f 1)
    if [ "$actual" != "$digest" ]; then
      echo "$dir/$name payload sha1 is $actual"
      exit 1
    fi
  done < "$1/PAYLOAD_SHA1SUMS"
done
]=]
            sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

        add_linapple_test(disk-smoke "tests/HeadlessHarness.cpp;tests/test_disk_smoke.cpp")
        add_linapple_test(disk-config-startup tests/test_disk_config_startup.cpp)
        add_linapple_test(disk-config-runtime-insert tests/test_disk_config_runtime_insert.cpp)
        add_linapple_test(disk-config-runtime-eject tests/test_disk_config_runtime_eject.cpp)
        add_linapple_test(headless-boot "tests/HeadlessHarness.cpp;tests/test_headless_boot.cpp")
    endif()

    add_linapple_test(program-loader tests/test_program_loader.cpp)
    add_linapple_test(system-roms "tests/test_main.cpp;tests/test_system_roms.cpp")
    add_linapple_test(basic-live-sync "tests/test_main.cpp;tests/test_basic_live_sync.cpp")

    add_linapple_test(file-browser tests/test_file_browser.cpp)

    add_executable(test-util-hash tests/test_util_hash.cpp src/frontends/common/Util_Hash.cpp)
    target_include_directories(test-util-hash PRIVATE src src/core src/frontends/common ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-util-hash COMMAND test-util-hash)

    add_executable(test-util-path tests/test_util_path.cpp)
    target_include_directories(test-util-path PRIVATE src src/core ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-util-path COMMAND test-util-path)

    add_executable(test-util-text tests/test_util_text.cpp)
    target_include_directories(test-util-text PRIVATE src src/core ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-util-text COMMAND test-util-text)

    add_linapple_test(video-stretch "tests/test_main.cpp;tests/test_video_stretch.cpp")

    add_linapple_test(ftp-parser "tests/test_main.cpp;tests/test_ftp_parser.cpp")

    if(ENABLE_FTP)
        add_linapple_test(ftp-client "tests/test_main.cpp;tests/test_ftp_client.cpp;src/core/services/ftp/FtpClient.cpp")
        target_link_libraries(test-ftp-client PRIVATE ${FTP_LIBRARIES})
    endif()

    if(ENABLE_PERIPHERAL_DISK)
        add_executable(test-disk-ui tests/test_disk_ui.cpp src/frontends/common/sdl/DiskUI.cpp)
        target_include_directories(test-disk-ui PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR})
        add_test(NAME test-disk-ui COMMAND test-disk-ui)
    endif()

    add_executable(test-audio-dumper tests/test_audio_dumper.cpp src/frontends/common/AudioDumper.cpp)
    target_include_directories(test-audio-dumper PRIVATE src src/core src/frontends/common ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test-audio-dumper PRIVATE Threads::Threads)
    add_test(NAME test-audio-dumper COMMAND test-audio-dumper)

    add_executable(test-tui-shape-detector tests/test_tui_shape_detector.cpp src/frontends/tui/TuiShapeDetector.cpp)
    target_include_directories(test-tui-shape-detector PRIVATE src src/core src/frontends/tui ${DOCTEST_INCLUDE_DIR})
    add_test(NAME test-tui-shape-detector COMMAND test-tui-shape-detector)

    add_linapple_test(tui-disk-select "tests/test_main.cpp;tests/test_tui_disk_select.cpp;src/frontends/tui/TuiDiskSelect.cpp;src/frontends/tui/TuiVideo.cpp;src/frontends/tui/TuiShapeDetector.cpp")

    set(DISK_TEST_CASES "")
    if(ENABLE_PERIPHERAL_DISK)
        add_linapple_test(disk-e2e tests/test_disk_e2e.cpp)

        foreach(T disk_motor;disk_prot;disk_woz;disk_woz1;disk_savestate;disk_errors;disk_stepper;disk_io;disk_compression;disk_modes;disk_lss;disk_bits;disk_sector;disk_nibble;disk_container)
            string(REPLACE "_" "-" TEST_NAME ${T})
            add_linapple_test(${TEST_NAME} tests/test_${T}.cpp)
        endforeach()
        list(APPEND DISK_TEST_CASES
            test-disk-abi
            test-disk-adapter
            test-disk-bits
            test-disk-compression
            test-disk-config-runtime-eject
            test-disk-config-runtime-insert
            test-disk-config-startup
            test-disk-container
            test-disk-drivers
            test-disk-e2e
            test-disk-errors
            test-disk-fixtures
            test-disk-gcr
            test-disk-io
            test-disk-lss
            test-disk-modes
            test-disk-motor
            test-disk-nibble
            test-disk-p6-rom
            test-disk-prot
            test-disk-registry
            test-disk-savestate
            test-disk-sector
            test-disk-smoke
            test-disk-stepper
            test-disk-ui
            test-disk-woz
            test-disk-woz1)
    endif()
    # The container library exists only when a card that opens images does.
    if(ENABLE_PERIPHERAL_DISK OR ENABLE_PERIPHERAL_HARDDISK)
        add_linapple_test(image-container tests/test_image_container.cpp)
        list(APPEND DISK_TEST_CASES test-image-container)
    endif()
    if(DISK_TEST_CASES)
        set_tests_properties(${DISK_TEST_CASES} PROPERTIES TIMEOUT ${PERIPHERAL_TEST_TIMEOUT})
    endif()

    # The peripheral dependency rule in AGENTS.md is enforced by these two
    # greps, which print nothing on a conforming tree. The case is built into
    # every configuration, cards on or off, because a stray include is a
    # source fact rather than a run-time one.
    add_test(NAME test-peripheral-dependency-rule COMMAND sh -c
    [=[
violations=$(
  for d in src/apple2/peripherals/*/; do n=$(basename "$d")
    grep -rnE '#include\s*"apple2/peripherals/[a-z_]+/' "$d" \
      | grep -vE "\"apple2/peripherals/$n/"; done
  grep -rnE '#include\s*"(apple2/peripherals/[a-z_]+/|core/LinAppleCore|frontends/)' \
    src/apple2/chips src/apple2/media
)
if [ -n "$violations" ]; then
  echo "$violations"
  exit 1
fi
]=]
            sh
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(test-peripheral-dependency-rule PROPERTIES TIMEOUT 5)

    if(FRONTEND_LOWER STREQUAL "all")
        set(CPU_TEST_TARGET linapple-sdl3)
    else()
        set(CPU_TEST_TARGET linapple)
    endif()
    add_test(NAME test-cpu COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_cpu_tests.sh")
    linapple_test_home(test-cpu cpu)
    set_property(TEST test-cpu APPEND PROPERTY ENVIRONMENT "EMULATOR=$<TARGET_FILE:${CPU_TEST_TARGET}>")

    add_linapple_test(speaker "tests/test_main.cpp;tests/test_speaker.cpp")
    add_linapple_test(speaker-core "tests/test_main.cpp;tests/test_speaker_core.cpp")
    add_linapple_test(audio-mixer "tests/test_main.cpp;tests/test_audio_mixer.cpp")
    add_linapple_test(frame-pacer "tests/test_main.cpp;tests/test_frame_pacer.cpp")
    add_linapple_test(speaker-e2e "tests/test_main.cpp;tests/test_speaker_e2e.cpp")
    set_tests_properties(test-speaker-e2e PROPERTIES LABELS "e2e")
    add_linapple_test(frontend-init-order tests/test_frontend_init_order.cpp)
    if(ENABLE_PERIPHERAL_DISK)
        add_linapple_test(e2e-applesoft "tests/HeadlessHarness.cpp;tests/test_e2e_applesoft.cpp")
        set_tests_properties(test-e2e-applesoft PROPERTIES LABELS "e2e")
    endif()
    # Its own directory, which no plugin search path names, so an ordinary
    # run never loads it.
    set(TEST_PLUGIN_DIR "${CMAKE_BINARY_DIR}/test-plugins")
    add_library(test-plugin-slot0 SHARED tests/fixture_plugin_slot0.cpp)
    target_compile_definitions(test-plugin-slot0 PRIVATE BUILD_SHARED_PERIPHERAL)
    target_include_directories(test-plugin-slot0 PRIVATE ${LINAPPLE_INCLUDES} tests)
    set_target_properties(test-plugin-slot0 PROPERTIES
        PREFIX "" OUTPUT_NAME "slot0_fixture"
        LIBRARY_OUTPUT_DIRECTORY "${TEST_PLUGIN_DIR}")

    add_linapple_test(peripheral-lifecycle tests/test_peripheral_lifecycle.cpp)
    add_dependencies(test-peripheral-lifecycle test-plugin-slot0)
    target_compile_definitions(test-peripheral-lifecycle PRIVATE TEST_PLUGIN_DIR="${TEST_PLUGIN_DIR}")
    if(ENABLE_PERIPHERAL_HARDDISK)
        add_linapple_test(harddisk-peripheral tests/test_harddisk_peripheral.cpp)
        add_linapple_test(harddisk-exhaustive tests/test_harddisk_exhaustive.cpp)
    endif()
    add_linapple_test(snapshot tests/test_snapshot.cpp)
    # Pinned fixture for regression testing.
    add_test(NAME test-snapshot-fixture-pinned COMMAND sh -c
        "echo '508d85b3144ab7a576cb7a01be705c21d59d68c2  tests/fixtures/minimal.aws' | sha1sum -c --quiet"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(test-snapshot-fixture-pinned PROPERTIES TIMEOUT 5)
    add_linapple_test(memory-exhaustive "tests/test_main.cpp;tests/test_memory_exhaustive.cpp")
    add_linapple_test(cpu-exhaustive "tests/test_main.cpp;tests/test_cpu_exhaustive.cpp")
    add_linapple_test(miniasm "tests/test_main.cpp;tests/test_miniasm.cpp")
    if(ENABLE_DEBUGGER)
        add_linapple_test(debugger "tests/test_main.cpp;tests/test_debugger.cpp")
    endif()
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
        target_compile_definitions(test-frontend-sdl1 PRIVATE LINAPPLE_SDL_VERSION=1 ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
        target_link_libraries(test-frontend-sdl1 PRIVATE ${SDL_LIBRARY} ${SDL_IMAGE_LIBRARIES} ${FTP_LIBRARIES} ${COMMON_LIBS} ${MEDIA_TARGETS})
        add_test(NAME test-frontend-sdl1 COMMAND test-frontend-sdl1)
        linapple_test_home(test-frontend-sdl1 frontend-sdl1)
    endif()

    if(FRONTEND_LOWER STREQUAL "sdl2" OR FRONTEND_LOWER STREQUAL "all")
        set(TEST_SOURCES_FE_SDL2 ${SOURCES_FE_SDL2})
        list(REMOVE_ITEM TEST_SOURCES_FE_SDL2 src/frontends/sdl2/Main.cpp)
        add_executable(test-frontend-sdl2 tests/test_main.cpp tests/test_frontend_sdl2.cpp ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} ${TEST_SOURCES_FE_SDL2})
        add_dependencies(test-frontend-sdl2 generate_xpms generate_embedded_roms)
        target_include_directories(test-frontend-sdl2 PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR} tests src)
        target_compile_definitions(test-frontend-sdl2 PRIVATE LINAPPLE_SDL_VERSION=2 ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
        target_link_libraries(test-frontend-sdl2 PRIVATE SDL2::SDL2 ${FTP_LIBRARIES} ${COMMON_LIBS} ${MEDIA_TARGETS})
        add_test(NAME test-frontend-sdl2 COMMAND test-frontend-sdl2)
        linapple_test_home(test-frontend-sdl2 frontend-sdl2)
    endif()

    if(FRONTEND_LOWER STREQUAL "sdl3" OR FRONTEND_LOWER STREQUAL "all")
        set(TEST_SOURCES_FE_SDL3 ${SOURCES_FE_SDL3})
        list(REMOVE_ITEM TEST_SOURCES_FE_SDL3 src/frontends/sdl3/Main.cpp)
        add_executable(test-frontend-sdl3 tests/test_main.cpp tests/test_frontend_sdl3.cpp ${SOURCES_CORE} ${SOURCES_A2_MANDATORY} ${SOURCES_DBG} ${SOURCES_A2_OPTIONAL} ${TEST_SOURCES_FE_SDL3})
        add_dependencies(test-frontend-sdl3 generate_xpms generate_embedded_roms)
        target_include_directories(test-frontend-sdl3 PRIVATE ${LINAPPLE_INCLUDES} ${DOCTEST_INCLUDE_DIR} tests src)
        target_compile_definitions(test-frontend-sdl3 PRIVATE LINAPPLE_SDL_VERSION=3 ${PERIPHERAL_DEFINITIONS} ENABLE_RIFF TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" SOURCE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
        target_link_libraries(test-frontend-sdl3 PRIVATE SDL3::SDL3 SDL3_image::SDL3_image ${FTP_LIBRARIES} ${COMMON_LIBS} ${MEDIA_TARGETS})
        add_test(NAME test-frontend-sdl3 COMMAND test-frontend-sdl3)
        linapple_test_home(test-frontend-sdl3 frontend-sdl3)
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
        target_link_libraries(${name} PRIVATE ${COMMON_LIBS} ${MEDIA_FUZZ_TARGETS})
    endmacro()

    if(ENABLE_PERIPHERAL_DISK)
        add_linapple_fuzzer(fuzz-disk-loader tests/fuzz/fuzz_disk_loader.cpp)
        add_linapple_fuzzer(fuzz-disk-controller tests/fuzz/fuzz_disk_controller.cpp)
        add_linapple_fuzzer(fuzz-disk-adapter tests/fuzz/fuzz_disk_adapter.cpp)
    endif()
    if(ENABLE_PERIPHERAL_DISK OR ENABLE_PERIPHERAL_HARDDISK)
        add_linapple_fuzzer(fuzz-image-container tests/fuzz/fuzz_image_container.cpp)
    endif()
    add_linapple_fuzzer(fuzz-snapshot tests/fuzz/fuzz_snapshot.cpp)
    add_linapple_fuzzer(fuzz-memory tests/fuzz/fuzz_memory.cpp)
    add_linapple_fuzzer(fuzz-speaker tests/fuzz/fuzz_speaker.cpp)
    add_linapple_fuzzer(fuzz-audio-mixer tests/fuzz/fuzz_audio_mixer.cpp)
    add_linapple_fuzzer(fuzz-mockingboard tests/fuzz/fuzz_mockingboard.cpp)
    add_linapple_fuzzer(fuzz-ay8910 tests/fuzz/fuzz_ay8910.cpp)
    add_linapple_fuzzer(fuzz-via6522 tests/fuzz/fuzz_via6522.cpp)

    if(BUILD_TESTING)
        # Every fuzz case is given ten seconds of search; libFuzzer needs a
        # second more to load its corpus and print its summary, so the wall
        # clock that calls one hung is three times the budget it was given.
        set(FUZZ_CASE_TIMEOUT 30)

        if(ENABLE_PERIPHERAL_DISK)
            # libFuzzer writes new inputs only to the first corpus directory, so
            # the fixtures serve as read-only seeds without a copy of each one.
            add_test(NAME fuzz-disk-loader
                     COMMAND fuzz-disk-loader
                             ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/disk-loader
                             ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                             -dict=${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/dictionaries/disk_formats.dict
                             -max_len=65536
                             -rss_limit_mb=512
                             -malloc_limit_mb=256
                             -max_total_time=10)
            set_tests_properties(fuzz-disk-loader PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})
        endif()

        if(ENABLE_PERIPHERAL_DISK OR ENABLE_PERIPHERAL_HARDDISK)
            # One selector byte picks the container the rest of the input is
            # written as, so a well-formed archive reaches its own extractor. The
            # harness passes a 64 KiB threshold, which with this length bounds one
            # extraction at 100 x 64 KiB of temporary file.
            add_test(NAME fuzz-image-container
                     COMMAND fuzz-image-container
                             ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/image-container
                             ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                             -max_len=65536
                             -rss_limit_mb=512
                             -malloc_limit_mb=256
                             -max_total_time=10)
            set_tests_properties(fuzz-image-container PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})
        endif()

        if(ENABLE_PERIPHERAL_DISK)
            # Six bytes a record and a sequence long enough to spin the medium
            # past a whole sector.
            add_test(NAME fuzz-disk-controller
                     COMMAND fuzz-disk-controller
                             ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/disk-controller
                             -max_len=1024
                             -rss_limit_mb=512
                             -malloc_limit_mb=256
                             -max_total_time=10)
            set_tests_properties(fuzz-disk-controller PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})

            # One control byte and then one nibble per byte, so this covers a
            # track three fifths the length of a real one.
            add_test(NAME fuzz-disk-adapter
                     COMMAND fuzz-disk-adapter
                             ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/disk-adapter
                             -max_len=4096
                             -rss_limit_mb=512
                             -malloc_limit_mb=256
                             -max_total_time=10)
            set_tests_properties(fuzz-disk-adapter PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})
        endif()

        add_test(NAME fuzz-snapshot
                 COMMAND fuzz-snapshot
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/snapshot
                         -max_len=131824
                         -rss_limit_mb=512
                         -malloc_limit_mb=256
                         -max_total_time=10)
        set_tests_properties(fuzz-snapshot PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})

        add_test(NAME fuzz-memory
                 COMMAND fuzz-memory
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/memory
                         -dict=${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/dictionaries/memory_text.dict
                         -max_len=65536
                         -rss_limit_mb=512
                         -malloc_limit_mb=256
                         -max_total_time=10)
        set_tests_properties(fuzz-memory PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})

        # One record is ten bytes of control plus an SsIoSpeaker_t; eight of
        # them is enough of a sequence to reach the interesting states.
        add_test(NAME fuzz-speaker
                 COMMAND fuzz-speaker
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/speaker
                         -max_len=400
                         -rss_limit_mb=512
                         -malloc_limit_mb=256
                         -max_total_time=10)
        set_tests_properties(fuzz-speaker PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})

        # No record structure to mirror: the harness reads a byte at a time and
        # turns each into one mixer call, so this is simply enough bytes for a
        # long enough sequence.
        add_test(NAME fuzz-audio-mixer
                 COMMAND fuzz-audio-mixer
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/audio-mixer
                         -max_len=512
                         -rss_limit_mb=512
                         -malloc_limit_mb=256
                         -max_total_time=10)
        set_tests_properties(fuzz-audio-mixer PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})

        # One record is eight bytes of control plus a MockingboardSaveState_t;
        # eight of them reach an armed timer, a loaded state and a rendered
        # slice in one input.
        add_test(NAME fuzz-mockingboard
                 COMMAND fuzz-mockingboard
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/mockingboard
                         -max_len=1920
                         -rss_limit_mb=512
                         -malloc_limit_mb=256
                         -max_total_time=10)
        set_tests_properties(fuzz-mockingboard PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})

        # Four bytes per record: a register write and a run of the generators.
        add_test(NAME fuzz-ay8910
                 COMMAND fuzz-ay8910
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/ay8910
                         -max_len=256
                         -rss_limit_mb=512
                         -malloc_limit_mb=256
                         -max_total_time=10)
        set_tests_properties(fuzz-ay8910 PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})

        # Five bytes per record: a register access and a run of the timers.
        add_test(NAME fuzz-via6522
                 COMMAND fuzz-via6522
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/via6522
                         -max_len=320
                         -rss_limit_mb=512
                         -malloc_limit_mb=256
                         -max_total_time=10)
        set_tests_properties(fuzz-via6522 PROPERTIES LABELS "fuzz" TIMEOUT ${FUZZ_CASE_TIMEOUT})
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
