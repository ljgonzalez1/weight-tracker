cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Weight
#
# Intended use:
#
#     mkdir build && cd build
#     cmake ..
#     make            build the application
#     make deb        build a .deb package (Linux; installs nothing)
#     make dmg        build a .dmg disk image (macOS)
#     make dist       collect a redistributable folder (Windows)
#     make package    whichever of the three applies to this platform
#     sudo make install
#     sudo make uninstall
#     make clean
#     make test
#
# Where things go
# ---------------
#     build/                  everything disposable, including package staging
#     target/                 every deliverable
#     target/weight           the executable  (weight.exe on Windows)
#     target/Weight.app       the bundle      (macOS)
#     target/packages/        the .deb, the .dmg, the Windows archive
#
# There is deliberately no build-deb/ and no build-static/. One build directory
# is configured by the caller and every intermediate — including the package
# staging tree — lives inside it, so `rm -rf build` really does remove all of
# it, and running the packaging targets can never leave root-owned directories
# in the source tree.
# ---------------------------------------------------------------------------

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WEIGHT_VERSION_RAW)
string(STRIP "${WEIGHT_VERSION_RAW}" WEIGHT_VERSION)

project(weight
    VERSION ${WEIGHT_VERSION}
    DESCRIPTION "Body-mass logger with nonparametric trend estimation"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(WEIGHT_BUILD_TESTS "Build the test suite" ON)

# The artefact directory. Deliberately outside the build tree, so that wiping
# the build tree never destroys a deliverable and the deliverable never has to
# be hunted for inside a generator-specific subdirectory.
set(WEIGHT_TARGET_DIR "${CMAKE_SOURCE_DIR}/target" CACHE PATH "Where deliverables are placed")
set(WEIGHT_PACKAGE_DIR "${WEIGHT_TARGET_DIR}/packages"
    CACHE PATH "Where installable packages are placed")

# Package staging lives inside the build tree: it is scratch space, and putting
# scratch space next to the deliverables is how build-deb/ and build-static/
# came to exist in the first place.
set(WEIGHT_STAGING_DIR "${CMAKE_BINARY_DIR}/package-staging")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${WEIGHT_TARGET_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${WEIGHT_TARGET_DIR}")
endforeach()

# ---------------------------------------------------------------------------
# Recreating target/ at BUILD time, not only at configure time.
#
# This is the fix for:
#
#     [ 57%] Linking CXX executable .../target/weight
#     /usr/bin/ld: cannot open output file .../target/weight: No such file or directory
#
# CMake creates the runtime output directory while *generating*, and never
# again. target/ is also listed in ADDITIONAL_CLEAN_FILES, so `make clean`
# removes it — and from that moment every `make`, `make deb` and `make package`
# fails at the link step until somebody re-runs `cmake ..`, which is not a
# thing anyone should have to know.
#
# PRE_BUILD is treated as PRE_LINK by every generator except Visual Studio, so
# on all of them the directory is recreated immediately before the linker is
# invoked, in the same rule. If the directory was removed by hand the target's
# output file is missing too, so the target is considered out of date and the
# rule runs. There is no ordering in which the linker can run first.
# ---------------------------------------------------------------------------
function(weight_ensure_output_directory target directory)
    add_custom_command(TARGET ${target} PRE_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}"
        COMMENT "Making sure ${directory} exists"
        VERBATIM)
endfunction()

# macOS: build for both architectures unless the caller says otherwise, so one
# binary runs on Intel and Apple silicon. Requires a universal Qt; if yours is
# single-architecture, pass -DCMAKE_OSX_ARCHITECTURES=arm64 (or x86_64).
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
if(WEIGHT_BUILD_TESTS)
    find_package(Qt6 6.2 QUIET COMPONENTS Test)
endif()

set(WEIGHT_DISPLAY_NAME "Weight")
set(WEIGHT_EXECUTABLE_NAME "weight")
set(WEIGHT_ORGANISATION_DOMAIN "weight.invalid")
set(WEIGHT_DESCRIPTION "${PROJECT_DESCRIPTION}")
set(WEIGHT_COPYRIGHT "Weight contributors")
set(WEIGHT_BUNDLE_ID "invalid.weight.app")

include(WeightCompilerOptions)
include(WeightPreflight)

# Everything that can be checked before a file is compiled is checked here, so
# a missing package is named in one sentence instead of surfacing as a linker
# error at 87%.
weight_preflight("${WEIGHT_TARGET_DIR}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectVersion.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/core/ProjectVersion.hpp"
    @ONLY)

# ---------------------------------------------------------------------------
# Curve directories
#
# Adding a curve means: create the folder, add it here, add one entry to the
# array in settings/CurveCatalog.cpp. Nothing else changes.
# ---------------------------------------------------------------------------
set(WEIGHT_CURVE_DIRS
    adaptive_spline
    multiquadric_rbf
    local_loess)

set(WEIGHT_CURVE_SOURCES "")
foreach(curve IN LISTS WEIGHT_CURVE_DIRS)
    file(GLOB curve_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/math/${curve}/*.cpp")
    list(APPEND WEIGHT_CURVE_SOURCES ${curve_sources})
endforeach()

# ---------------------------------------------------------------------------
# Core library: numerics, storage, layout, settings.
# Links Gui but NOT Widgets, so no numerical or file-format code can reach a
# widget: the symbol simply is not there.
# ---------------------------------------------------------------------------
add_library(weight_core STATIC
    settings/Settings.cpp
    settings/Strings.cpp
    settings/CurveCatalog.cpp

    src/core/Result.cpp
    src/core/Logging.cpp
    src/core/Concurrency.cpp
    src/core/SessionConfig.cpp

    src/math/common/NumericArray.cpp
    src/math/common/CubicSpline.cpp
    src/math/common/GaussianKernel.cpp
    src/math/common/LinearSystem.cpp
    src/math/common/TrendEstimator.cpp
    src/math/common/CurveFactory.cpp
    src/math/common/TrendService.cpp
    ${WEIGHT_CURVE_SOURCES}

    src/data/TextSanitizer.cpp
    src/data/NumberParser.cpp
    src/data/NumericTextRepairChain.cpp
    src/data/CsvDialect.cpp
    src/data/CsvRecord.cpp
    src/data/AtomicFileWriter.cpp
    src/data/CsvSampleRepository.cpp
    src/data/CalendarUtils.cpp

    src/platform/Workspace.cpp
    src/platform/SingleInstance.cpp
    src/platform/SystemLocale.cpp

    src/plot/PlotScene.cpp
    src/plot/AxisWindow.cpp
    src/plot/CurveStyle.cpp
    src/plot/PlotSceneBuilder.cpp
    src/plot/PainterPlotRenderer.cpp
    src/plot/PlotImageWriter.cpp

    # Q_OBJECT headers are listed so AUTOMOC scans them: they live under src/
    # rather than beside their .cpp, and omitting them produces a library with
    # no vtables and a link that fails on every signal.
    src/core/SessionConfig.hpp
    src/platform/SingleInstance.hpp)

target_include_directories(weight_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(weight_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent)

# src/platform/SystemLocale.cpp reads the operating system's own UI-language
# list. On macOS that is CFLocaleCopyPreferredLanguages, which lives in
# CoreFoundation; Qt links it transitively today, but relying on that would
# mean this file breaks the day Qt stops. On Windows the equivalent call,
# GetUserPreferredUILanguages, is in kernel32, which both MSVC and MinGW link
# by default. On Linux setlocale is in the C library.
if(APPLE)
    find_library(WEIGHT_CORE_FOUNDATION CoreFoundation REQUIRED)
    target_link_libraries(weight_core PUBLIC "${WEIGHT_CORE_FOUNDATION}")
endif()

weight_apply_compiler_options(weight_core)

# ---------------------------------------------------------------------------
# UI library
# ---------------------------------------------------------------------------
add_library(weight_ui STATIC
    src/ui/FieldValidators.cpp
    src/ui/FontLibrary.cpp
    src/ui/InputPage.cpp
    src/ui/PreviewPage.cpp
    src/ui/MainWindow.cpp
    src/ui/DialogFactory.cpp
    src/app/CommandLine.cpp
    src/app/ApplicationController.cpp
    src/app/RenderEngine.cpp
    src/app/SelfTest.cpp

    src/ui/FieldValidators.hpp
    src/ui/InputPage.hpp
    src/ui/PreviewPage.hpp
    src/ui/MainWindow.hpp
    src/app/RenderEngine.hpp)

target_link_libraries(weight_ui PUBLIC weight_core Qt6::Widgets)
weight_apply_compiler_options(weight_ui)

# ---------------------------------------------------------------------------
# Executable
#
# The .qrc is compiled into the EXECUTABLE, not into a static library. In a
# static library the generated object contains only an initialiser that nothing
# references, so the linker discards it and the icon and fonts silently do not
# exist at run time. That is exactly the defect the previous release shipped.
# ---------------------------------------------------------------------------
set(WEIGHT_APP_SOURCES src/main.cpp assets/weight.qrc)

if(WIN32)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/program-icon.ico")
        list(APPEND WEIGHT_APP_SOURCES packaging/windows/weight.rc)
    endif()
endif()

if(APPLE)
    add_executable(weight MACOSX_BUNDLE ${WEIGHT_APP_SOURCES})
    # The bundle is a directory, and CMake places it with the RUNTIME output
    # rule, so Weight.app lands in target/ beside everything else.
    set_target_properties(weight PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Weight"
        MACOSX_BUNDLE_GUI_IDENTIFIER "invalid.weight.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "weight.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in")
else()
    add_executable(weight ${WEIGHT_APP_SOURCES})
endif()

if(WIN32)
    # No console window when launched from the shell.
    set_target_properties(weight PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(weight PRIVATE weight_ui)
weight_apply_compiler_options(weight)

# The only target that writes into target/, and therefore the only one that
# needs the directory guaranteed to exist at link time.
weight_ensure_output_directory(weight "${WEIGHT_TARGET_DIR}")


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(WEIGHT_BUILD_TESTS AND TARGET Qt6::Test)
    enable_testing()
    add_subdirectory(tests)
elseif(WEIGHT_BUILD_TESTS)
    message(STATUS "Qt6::Test not found; the test suite will not be built")
endif()

# ---------------------------------------------------------------------------
# Installation
#
# One rule per platform, and each uses the mechanism that platform actually
# provides rather than an approximation of it:
#
#   Linux   FHS layout under the prefix, plus a desktop entry and the icon
#           theme, which is what makes the taskbar icon appear under Wayland.
#   macOS   the .app bundle, plus a `weight` symlink on the PATH.
#   Windows a per-user install driven by install.ps1: no administrator rights,
#           a Start-menu entry, and the command on the user's PATH.
#
# No install rule ever writes into the workspace folder. The person's records
# are not ours to touch.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

if(WIN32)
    # windeployqt copies the Qt DLLs, the platform plugin and the image format
    # plugins next to the executable. Without it the program cannot even open a
    # window on a machine that has no Qt installed.
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET weight POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-compiler-runtime
                    "$<TARGET_FILE:weight>"
            COMMENT "Collecting the Qt runtime next to weight.exe")
    endif()

    install(TARGETS weight RUNTIME DESTINATION ".")
    # Whatever windeployqt produced travels with the executable.
    install(DIRECTORY "${WEIGHT_TARGET_DIR}/" DESTINATION "."
            FILES_MATCHING
                PATTERN "*.dll"
                PATTERN "platforms/*"
                PATTERN "styles/*"
                PATTERN "imageformats/*"
                PATTERN "iconengines/*"
                PATTERN "generic/*"
                PATTERN "networkinformation/*"
                PATTERN "tls/*")
    install(FILES README.md LICENSE assets/fonts/OpenRunde-LICENSE.txt DESTINATION "doc")
    install(FILES packaging/windows/install.ps1 DESTINATION ".")
    # `make install` on Windows runs the helper, which puts weight.exe on the
    # PATH and adds the Start-menu and desktop shortcuts. It needs no
    # administrator rights and never touches the workspace folder.
    #
    # WEIGHT_SKIP_USER_INSTALL exists so that `make dist`, which reuses these
    # same rules to stage a redistributable folder, does not also install the
    # program onto the machine doing the packaging.
    install(CODE "
        if(DEFINED ENV{WEIGHT_SKIP_USER_INSTALL})
            message(STATUS \"Staging only; install.ps1 was not run.\")
        else()
            execute_process(COMMAND powershell -ExecutionPolicy Bypass -File
                            \"\${CMAKE_INSTALL_PREFIX}/install.ps1\"
                            RESULT_VARIABLE ps_result)
            if(NOT ps_result EQUAL 0)
                message(WARNING \"install.ps1 did not complete; run it by hand from \${CMAKE_INSTALL_PREFIX}.\")
            endif()
        endif()")

elseif(APPLE)
    install(TARGETS weight BUNDLE DESTINATION "/Applications"
                           RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES README.md LICENSE assets/fonts/OpenRunde-LICENSE.txt
            DESTINATION "${CMAKE_INSTALL_DOCDIR}")
    # `sudo make install` must also leave a `weight` command on the PATH, which
    # a bundle alone does not provide. A symlink into the bundle keeps a single
    # copy of the binary: updating the app updates the command.
    install(CODE "
        set(bundle \"\${CMAKE_INSTALL_PREFIX}/Applications/Weight.app/Contents/MacOS/weight\")
        if(NOT EXISTS \"\${bundle}\")
            set(bundle \"/Applications/Weight.app/Contents/MacOS/weight\")
        endif()
        file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
                        \"\${bundle}\"
                        \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/weight\")")

else()
    install(TARGETS weight RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES packaging/debian/weight.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

    # The desktop entry's Icon=weight is resolved from the icon theme, and that
    # — not setWindowIcon — is what a Wayland shell uses for the taskbar. Each
    # size is installed from the matching source file rather than from one
    # oversized PNG, so the shell picks a correctly rasterised image instead of
    # downscaling 512x512 to 16x16.
    foreach(size 16 24 32 48 64 96 128 256 512)
        set(icon_source "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/program-icon-${size}.png")
        if(NOT EXISTS "${icon_source}")
            set(icon_source "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/program-icon-512.png")
        endif()
        install(FILES "${icon_source}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
                RENAME "weight.png")
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/program-icon.svg")
        install(FILES assets/icons/program-icon.svg
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
                RENAME "weight.svg")
    endif()
    install(FILES README.md LICENSE assets/fonts/OpenRunde-LICENSE.txt
            DESTINATION "${CMAKE_INSTALL_DOCDIR}")
endif()

# ---------------------------------------------------------------------------
# Extra make targets, so `cmake ..` produces everything the brief asks for.
#
# `all`, `clean`, `install` and `test` come from CMake itself; `deb`, `dmg`,
# `dist`, `package` and `uninstall` are added below. Every one of them stages
# inside the build tree and writes its result to target/packages.
# ---------------------------------------------------------------------------
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    "${WEIGHT_TARGET_DIR}"
    "${WEIGHT_STAGING_DIR}")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Uninstall.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/Uninstall.cmake" @ONLY)
add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/Uninstall.cmake"
    COMMENT "Removing installed files (your data is never touched)")

# Each packaging target creates the directories it writes into before it runs
# a single command, for the same reason the executable does: `make clean`
# removes target/, and a packaging run must not be the thing that discovers it.
if(UNIX AND NOT APPLE)
    add_custom_target(deb
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEIGHT_PACKAGE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEIGHT_STAGING_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E env
                "WEIGHT_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "WEIGHT_TARGET_DIR=${WEIGHT_TARGET_DIR}"
                "WEIGHT_STAGING_DIR=${WEIGHT_STAGING_DIR}"
                sh "${CMAKE_SOURCE_DIR}/packaging/debian/build-deb.sh"
        DEPENDS weight
        USES_TERMINAL VERBATIM
        COMMENT "Building a .deb package into target/packages (nothing is installed)")
    add_custom_target(package DEPENDS deb)
endif()

if(APPLE)
    add_custom_target(dmg
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEIGHT_PACKAGE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEIGHT_STAGING_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E env
                "WEIGHT_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "WEIGHT_TARGET_DIR=${WEIGHT_TARGET_DIR}"
                "WEIGHT_STAGING_DIR=${WEIGHT_STAGING_DIR}"
                sh "${CMAKE_SOURCE_DIR}/packaging/macos/build-dmg.sh"
        DEPENDS weight
        USES_TERMINAL VERBATIM
        COMMENT "Building a .dmg disk image into target/packages")
    add_custom_target(package DEPENDS dmg)
endif()

if(WIN32)
    # A folder holding weight.exe and its Qt runtime, plus a .zip of the same.
    # Not an MSI and not an AppImage-style single file: the brief asks for a
    # binary, and this is the binary with exactly what it needs beside it.
    add_custom_target(dist
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEIGHT_PACKAGE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEIGHT_STAGING_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E env
                "WEIGHT_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "WEIGHT_TARGET_DIR=${WEIGHT_TARGET_DIR}"
                "WEIGHT_STAGING_DIR=${WEIGHT_STAGING_DIR}"
                powershell -ExecutionPolicy Bypass -File
                "${CMAKE_SOURCE_DIR}/packaging/windows/build-dist.ps1"
        DEPENDS weight
        USES_TERMINAL VERBATIM
        COMMENT "Collecting a redistributable folder into target/packages")
    add_custom_target(package DEPENDS dist)
endif()

message(STATUS "Weight ${PROJECT_VERSION} | Qt ${Qt6_VERSION} | ${CMAKE_CXX_COMPILER_ID} | "
               "curves=${WEIGHT_CURVE_DIRS}")
message(STATUS "Deliverables go to: ${WEIGHT_TARGET_DIR}")
message(STATUS "Packages go to:     ${WEIGHT_PACKAGE_DIR}")
