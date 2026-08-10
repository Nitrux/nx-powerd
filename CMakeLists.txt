cmake_minimum_required(VERSION 3.16)
project(nx-powerd VERSION 0.0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(nx-powerd main.cpp)
target_compile_options(nx-powerd PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(nx-powerd PRIVATE pthread)
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
 target_compile_options(nx-powerd PRIVATE -march=x86-64-v3 -mtune=generic)
endif()
install(TARGETS nx-powerd RUNTIME DESTINATION bin)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
"if(NOT EXISTS \"${CMAKE_BINARY_DIR}/install_manifest.txt\")\n"
" message(FATAL_ERROR \"Cannot find install manifest\")\n"
"endif()\n"
"file(READ \"${CMAKE_BINARY_DIR}/install_manifest.txt\" installed_files)\n"
"string(REPLACE \"\\n\" \";\" installed_files \"\${installed_files}\")\n"
"foreach(file \${installed_files})\n"
" if(EXISTS \"\${file}\" OR IS_SYMLINK \"\${file}\")\n file(REMOVE \"\${file}\")\n endif()\n"
"endforeach()\n")
add_custom_target(uninstall COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
set(CPACK_PACKAGE_NAME "nx-powerd")
set(CPACK_DEBIAN_PACKAGE_NAME "nx-powerd")
set(CPACK_PACKAGE_VENDOR "Nitrux Latinoamericana S.C.")
set(CPACK_PACKAGE_CONTACT "hello@nxos.org")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Nitrux power profile and battery notification daemon")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "uri_herrera@nxos.org")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "power-profiles-daemon, upower, libnotify-bin, brightnessctl")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
include(CPack)
