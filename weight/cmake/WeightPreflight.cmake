# ---------------------------------------------------------------------------
# WeightPreflight.cmake — everything that is checked before a single file is
# compiled.
#
# Why this exists
# ---------------
# A build that fails at 87% with a linker error nobody can read costs far more
# than a build that refuses to start with one sentence naming the missing
# package. Every check here answers a question that has actually broken a build
# of this project at some point:
#
#   * is the compiler new enough for the C++20 features the code uses?
#   * are all five Qt modules present, and is the Qt new enough?
#   * are the assets that get compiled into the binary actually on disk?
#   * can we write to target/ at all?
#   * are the packaging tools for THIS platform available?
#
# Missing packaging tools are a warning, not an error: you can build and run
# the program perfectly well without ever making a .deb. Missing compile-time
# requirements are errors, because continuing would only waste the caller's
# time.
#
# Every message names the package to install, per distribution where the name
# differs. The names were verified against each distribution's own index; they
# are not guesses.
# ---------------------------------------------------------------------------

set(WEIGHT_PREFLIGHT_NOTES "" CACHE INTERNAL "Preflight findings")

function(_weight_note level message)
    if(level STREQUAL "OK")
        message(STATUS "  [ ok ] ${message}")
    elseif(level STREQUAL "WARN")
        message(STATUS "  [warn] ${message}")
    else()
        message(STATUS "  [FAIL] ${message}")
    endif()
endfunction()

# --- Compiler ---------------------------------------------------------------
#
# The minimums are the versions that implement the C++20 subset in use here:
# designated initialisers, <span>, <concepts>, three-way comparison in the
# numeric code, and std::optional in the estimator interfaces.
function(weight_check_compiler)
    set(too_old FALSE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
            set(too_old TRUE)
            set(needed "GCC 10 or newer")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
            set(too_old TRUE)
            set(needed "Clang 12 or newer")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # 19.29 is Visual Studio 2019 16.11; earlier versions miscompile the
        # designated initialisers used throughout the settings file.
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.29)
            set(too_old TRUE)
            set(needed "Visual Studio 2019 16.11 or newer")
        endif()
    endif()

    if(too_old)
        message(FATAL_ERROR
            "This project needs ${needed}.\n"
            "  Found: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
            "  Debian/Ubuntu: sudo apt install g++-12\n"
            "  Fedora/RHEL:   sudo dnf install gcc-c++\n"
            "  Arch:          sudo pacman -S gcc\n"
            "  macOS:         xcode-select --install\n"
            "  Windows:       install the \"Desktop development with C++\" workload")
    endif()
    _weight_note(OK "compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endfunction()

# --- Qt ---------------------------------------------------------------------
#
# find_package has already run by the time this is called; what is checked here
# is that every module the code actually links is present, so a partial Qt
# installation is named up front instead of failing at link time with an
# undefined reference to a Qt symbol.
function(weight_check_qt)
    set(missing "")
    foreach(component Core Gui Widgets Network Concurrent)
        if(NOT TARGET Qt6::${component})
            list(APPEND missing ${component})
        endif()
    endforeach()

    if(missing)
        string(REPLACE ";" ", " pretty "${missing}")
        message(FATAL_ERROR
            "Qt 6 is installed but these modules are missing: ${pretty}\n"
            "  Debian/Ubuntu: sudo apt install qt6-base-dev\n"
            "  Fedora/RHEL:   sudo dnf install qt6-qtbase-devel\n"
            "  Arch:          sudo pacman -S qt6-base\n"
            "  openSUSE:      sudo zypper install qt6-base-devel\n"
            "  Alpine:        doas apk add qt6-qtbase-dev\n"
            "  macOS:         brew install qt\n"
            "  Windows:       install Qt 6 with the MSVC kit, then pass\n"
            "                 -DCMAKE_PREFIX_PATH=C:/Qt/6.x.y/msvc2019_64")
    endif()
    _weight_note(OK "Qt ${Qt6_VERSION} with Core, Gui, Widgets, Network, Concurrent")

    if(NOT TARGET Qt6::Test)
        _weight_note(WARN "Qt6::Test is absent; the unit suites will not be built "
                          "(Debian/Ubuntu: the qt6-base-dev package provides it)")
    endif()

    # The offscreen platform plugin is what lets the test suite and --self-test
    # run without a display server. Its absence is not fatal for the build, but
    # it turns `make test` into a failure with a confusing message, so it is
    # worth naming now.
    if(UNIX AND NOT APPLE)
        get_target_property(qt_core_location Qt6::Core LOCATION)
        get_filename_component(qt_lib_dir "${qt_core_location}" DIRECTORY)
        find_file(WEIGHT_OFFSCREEN_PLUGIN
            NAMES libqoffscreen.so
            HINTS "${qt_lib_dir}/qt6/plugins/platforms"
                  "${qt_lib_dir}/qt6/plugins/platforms"
                  "${qt_lib_dir}/../plugins/platforms"
                  "/usr/lib/qt6/plugins/platforms"
                  "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/qt6/plugins/platforms"
            NO_CACHE)
        if(WEIGHT_OFFSCREEN_PLUGIN)
            _weight_note(OK "offscreen platform plugin (tests can run without a display)")
        else()
            _weight_note(WARN "the offscreen Qt platform plugin was not found; "
                              "`make test` needs it (Debian/Ubuntu: qt6-qpa-plugins)")
        endif()
    endif()
endfunction()

# --- Assets -----------------------------------------------------------------
#
# The fonts and the icon are compiled into the executable through the .qrc.
# A missing file there produces an rcc error that names a path and nothing
# else; naming it here says what the file is for.
function(weight_check_assets)
    set(required
        "assets/fonts/OpenRunde-Regular.otf|the bundled interface font"
        "assets/fonts/OpenRunde-Medium.otf|the bundled interface font"
        "assets/fonts/OpenRunde-Semibold.otf|the bundled interface font"
        "assets/fonts/OpenRunde-Bold.otf|the bundled interface font"
        "assets/fonts/OpenRunde-LICENSE.txt|the third-party font licence, which must ship"
        "assets/icons/program-icon-512.png|the window and taskbar icon"
        "assets/weight.qrc|the resource manifest")

    set(missing "")
    foreach(entry IN LISTS required)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 relative)
        list(GET parts 1 purpose)
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${relative}")
            list(APPEND missing "${relative} (${purpose})")
        endif()
    endforeach()

    if(missing)
        string(REPLACE ";" "\n    " pretty "${missing}")
        message(FATAL_ERROR
            "These files are compiled into the binary and are not on disk:\n"
            "    ${pretty}\n"
            "  This normally means the archive was unpacked partially.")
    endif()
    _weight_note(OK "assets: 4 font faces, the icon set and the resource manifest")

    # Optional, platform-specific icon formats. Their absence only means the
    # packaged artefact falls back to a generic icon, so it is a warning.
    if(WIN32 AND NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/icons/program-icon.ico")
        _weight_note(WARN "assets/icons/program-icon.ico is missing; weight.exe will "
                          "carry the default Windows icon")
    endif()
    if(APPLE AND NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/icons/weight.icns")
        _weight_note(WARN "assets/icons/weight.icns is missing; Weight.app will carry "
                          "the default macOS icon")
    endif()
endfunction()

# --- Output directory -------------------------------------------------------
#
# target/ is created here rather than at build time so that a permission
# problem — the classic being a target/ left behind by a `sudo make` — is
# reported with the command that fixes it, instead of appearing as a link
# failure much later.
function(weight_check_target_directory directory)
    file(MAKE_DIRECTORY "${directory}")
    if(NOT IS_DIRECTORY "${directory}")
        message(FATAL_ERROR "Could not create the artefact directory: ${directory}")
    endif()

    set(probe "${directory}/.weight-write-probe")
    file(WRITE "${probe}" "probe")
    if(NOT EXISTS "${probe}")
        message(FATAL_ERROR
            "The artefact directory is not writable: ${directory}\n"
            "  This is what a previous `sudo make` leaves behind. Fix it with:\n"
            "      sudo rm -rf ${directory}")
    endif()
    file(REMOVE "${probe}")
    _weight_note(OK "artefact directory is writable: ${directory}")
endfunction()

# --- Packaging tools --------------------------------------------------------
#
# Warnings only. Building and running the application requires none of these.
function(weight_check_packaging_tools)
    if(UNIX AND NOT APPLE)
        find_program(WEIGHT_DPKG_DEB dpkg-deb)
        find_program(WEIGHT_DPKG_SHLIBDEPS dpkg-shlibdeps)
        if(WEIGHT_DPKG_DEB AND WEIGHT_DPKG_SHLIBDEPS)
            _weight_note(OK "dpkg-deb and dpkg-shlibdeps found; `make deb` is available")
        else()
            _weight_note(WARN "dpkg-deb or dpkg-shlibdeps is missing; `make deb` will "
                              "explain what to install (Debian/Ubuntu: dpkg-dev)")
        endif()
        find_program(WEIGHT_OBJDUMP objdump)
        if(NOT WEIGHT_OBJDUMP)
            _weight_note(WARN "objdump is missing; dpkg-shlibdeps needs it "
                              "(Debian/Ubuntu: binutils)")
        endif()
    elseif(APPLE)
        find_program(WEIGHT_HDIUTIL hdiutil)
        find_program(WEIGHT_MACDEPLOYQT macdeployqt HINTS "${Qt6_DIR}/../../../bin")
        if(WEIGHT_HDIUTIL AND WEIGHT_MACDEPLOYQT)
            _weight_note(OK "hdiutil and macdeployqt found; `make dmg` is available")
        elseif(NOT WEIGHT_MACDEPLOYQT)
            _weight_note(WARN "macdeployqt was not found; a .dmg built now would need Qt "
                              "installed on the target machine. Set -DCMAKE_PREFIX_PATH to "
                              "your Qt installation.")
        endif()
        find_program(WEIGHT_ICONUTIL iconutil)
        if(NOT WEIGHT_ICONUTIL)
            _weight_note(WARN "iconutil is missing; the .icns cannot be regenerated "
                              "from the PNG set")
        endif()
    elseif(WIN32)
        find_program(WEIGHT_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
        if(WEIGHT_WINDEPLOYQT)
            _weight_note(OK "windeployqt found; the Qt runtime will be collected next to "
                            "weight.exe")
        else()
            _weight_note(WARN "windeployqt was not found; weight.exe will need the Qt DLLs "
                              "on PATH. It normally sits in the Qt bin directory.")
        endif()
    endif()
endfunction()

# --- Entry point ------------------------------------------------------------
function(weight_preflight target_directory)
    message(STATUS "")
    message(STATUS "Pre-build checks")
    weight_check_compiler()
    weight_check_qt()
    weight_check_assets()
    weight_check_target_directory("${target_directory}")
    weight_check_packaging_tools()
    message(STATUS "")
endfunction()
