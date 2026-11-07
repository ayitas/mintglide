cmake_minimum_required(VERSION 3.16)
project(mintglide VERSION 1.0.2 LANGUAGES CXX)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
find_package(X11 REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(XTST REQUIRED IMPORTED_TARGET xtst)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mintglide-daemon
    src/daemon/main.cpp
    src/daemon/input_device.cpp
    src/daemon/gesture_engine.cpp
    src/daemon/action_dispatcher.cpp
)
target_include_directories(mintglide-daemon PRIVATE src)
target_link_libraries(mintglide-daemon PRIVATE
    PkgConfig::LIBEVDEV
    PkgConfig::LIBUDEV
    PkgConfig::XTST
    X11::X11
    nlohmann_json::nlohmann_json
)
target_compile_options(mintglide-daemon PRIVATE -Wall -Wextra -Wpedantic)
add_executable(mintglide-gui src/gui/main.cpp)
target_include_directories(mintglide-gui PRIVATE src)
target_link_libraries(mintglide-gui PRIVATE
    PkgConfig::GTKMM
    nlohmann_json::nlohmann_json
)
target_compile_options(mintglide-gui PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(mintglide-gui PRIVATE
    MINTGLIDE_LOGO_INSTALL_PATH="${CMAKE_INSTALL_FULL_DATADIR}/mintglide/mintglide-logo.png"
    MINTGLIDE_LOGO_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/mintglide-logo.png"
)

install(TARGETS mintglide-daemon mintglide-gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(PROGRAMS scripts/setup_udev.sh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mintglide
)
install(FILES assets/mintglide-logo.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mintglide
)
install(FILES data/mintglide.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES data/mintglide-daemon.desktop
    DESTINATION /etc/xdg/autostart
)
install(FILES udev/70-mintglide-trackpad.rules
    DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d"
)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME mintglide)
set(CPACK_PACKAGE_VENDOR "Mintglide")
set(CPACK_PACKAGE_CONTACT "Ayitas <39111829+ayitas@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Apple trackpad gestures for Linux Mint")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "mintglide_${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ayitas <39111829+ayitas@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "udev")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postrm"
)
include(CPack)
