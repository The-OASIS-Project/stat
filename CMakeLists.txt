cmake_minimum_required(VERSION 3.10)
project(oasis-stat VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release)
endif()

# Include directories
include_directories(include)

# Source files
set(SOURCES
   src/ark_detection.c
   src/i2c_utils.c
   src/ina238.c
   src/oasis-stat.c
)

# Header files (for IDE support)
set(HEADERS
   include/ark_detection.h
   include/i2c_utils.h
   include/ina238.h
   include/ina238_registers.h
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link libraries (if needed in the future)
# target_link_libraries(${PROJECT_NAME} pthread)

# Install target
install(TARGETS ${PROJECT_NAME}
   RUNTIME DESTINATION bin
)

# Package configuration
set(CPACK_PACKAGE_NAME "oasis-stat")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "STAT - System Telemetry and Analytics Tracker for OASIS")
set(CPACK_PACKAGE_VENDOR "OASIS Project")

include(CPack)

# Optional: Add uninstall target
if(NOT TARGET uninstall)
   configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
      IMMEDIATE @ONLY)

   add_custom_target(uninstall
      COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
