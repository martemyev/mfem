cmake_minimum_required(VERSION 2.8)

#-------------------------------------------------------------------------------
# Build type
#-------------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "prelease" CACHE STRING
      "Choose the type of build: debug | release | pdebug | prelease" FORCE)
endif()
string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE)

#-------------------------------------------------------------------------------
# Project
#-------------------------------------------------------------------------------
project(mfem CXX)
message(STATUS "C++ compiler ID = ${CMAKE_CXX_COMPILER_ID}")
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
  #set(MY_CXX_FLAGS "-Wall -Wextra -Wshadow -Weffc++ -ansi -pedantic -Werror")
  set(MY_CXX_FLAGS "-Wall")
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  #set(MY_CXX_FLAGS "-Wall -Wextra -Wshadow -Weffc++ -pedantic -Werror")
  set(MY_CXX_FLAGS "-Wall")
else()
  #set(MY_CXX_FLAGS "-Wall -pedantic -Werror")
  set(MY_CXX_FLAGS "-Wall")
endif()

#-------------------------------------------------------------------------------
# MFEM_DEBUG
#-------------------------------------------------------------------------------
if(BUILD_TYPE STREQUAL DEBUG OR BUILD_TYPE STREQUAL PDEBUG)
  set(MFEM_DEBUG ON)
else()
  set(MFEM_DEBUG OFF)
endif()

#-------------------------------------------------------------------------------
# MFEM_TIMER_TYPE
#-------------------------------------------------------------------------------
set(MFEM_TIMER_TYPE 2)

#-------------------------------------------------------------------------------
# MFEM_USE_MPI, HYPRE, METIS
#-------------------------------------------------------------------------------
if(BUILD_TYPE STREQUAL PDEBUG OR BUILD_TYPE STREQUAL PRELEASE)
  set(MFEM_USE_MPI ON)
  # MPI
  find_package(MPI REQUIRED)
  include_directories(${MPI_CXX_INCLUDE_PATH})
  set(CMAKE_CXX_COMPILER ${MPI_CXX_COMPILER})
  # Hypre
  set(HYPRE_DIR "/u/artemyev/software/hypre-2.10.1/src" CACHE PATH
      "Path to the Hypre root directory")
  include_directories(${HYPRE_DIR}/hypre/include)
  if(BUILD_TYPE STREQUAL PDEBUG)
    set(HYPRE_PATH ${HYPRE_DIR}/debugbuild)
  else()
    set(HYPRE_PATH ${HYPRE_DIR}/cmbuild)
  endif()
  link_directories(${HYPRE_PATH})
  # Metis
  set(MFEM_USE_METIS_5 ON)
  set(METIS_DIR "/u/artemyev/software/metis-5.1.0/build/Linux-x86_64" CACHE PATH
      "Path to the Metis root directory")
  set(METIS_PATH ${METIS_DIR}/lib)
  link_directories(${METIS_PATH})
else()
  set(MFEM_USE_MPI OFF)
endif()

#-------------------------------------------------------------------------------
# Config file
#-------------------------------------------------------------------------------
configure_file(
  "${PROJECT_SOURCE_DIR}/config/config.hpp.in_cmake"
  "${PROJECT_BINARY_DIR}/config.hpp")
include_directories(${PROJECT_BINARY_DIR})

#-------------------------------------------------------------------------------
# Headers and sources
#-------------------------------------------------------------------------------
set(DIRS general linalg mesh fem)
foreach(dir IN LISTS DIRS)
  FILE(GLOB SRC "${PROJECT_SOURCE_DIR}/${dir}/*.cpp")
  FILE(GLOB HDR "${PROJECT_SOURCE_DIR}/${dir}/*.hpp")
  list(APPEND SRC_LIST ${SRC})
  list(APPEND HDR_LIST ${HDR})
endforeach()

#-------------------------------------------------------------------------------
# Put the information about the configuration on the screen
#-------------------------------------------------------------------------------
  message("\n--------------- configuration information ----------------")
  message("build type     : " ${BUILD_TYPE})
  message("compiler       : " ${CMAKE_CXX_COMPILER})
  message("hypre          : " ${HYPRE_PATH})
  message("metis          : " ${METIS_PATH})
if(MFEM_DEBUG)
  message("compiler flags : " ${CMAKE_CXX_FLAGS_DEBUG})
else()
  message("compiler flags : " ${CMAKE_CXX_FLAGS_RELEASE})
endif()

#-------------------------------------------------------------------------------
# Declaring the library
#-------------------------------------------------------------------------------
add_library(${PROJECT_NAME} ${SRC_LIST} ${HDR_LIST})
target_link_libraries(${PROJECT_NAME} rt)
if(MFEM_USE_MPI)
  target_link_libraries(${PROJECT_NAME} ${MPI_CXX_LIBRARIES} HYPRE metis)
endif()
