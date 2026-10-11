# Backend lane: C port of moonlight-qt's app/backend (HTTP/HTTPS, pairing,
# identity, serverinfo). Uses only SteamOS-shipped libcurl, OpenSSL, libxml2. GPLv3.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLXR_CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(MLXR_SSL REQUIRED IMPORTED_TARGET openssl)
pkg_check_modules(MLXR_XML2 REQUIRED IMPORTED_TARGET libxml-2.0)
find_package(Threads REQUIRED)

add_library(mlxr_backend STATIC
  util.c xml.c crypto.c identity.c computer.c nvhttp.c pairing.c store.c netpath.c netpath_sys.c)
target_include_directories(mlxr_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_backend PUBLIC PkgConfig::MLXR_CURL PkgConfig::MLXR_SSL PkgConfig::MLXR_XML2 Threads::Threads)
set_target_properties(mlxr_backend PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Optional glue that fills moonlight-common-c's SERVER_INFORMATION / STREAM_CONFIGURATION.
set(MLXR_BACKEND_HAVE_LL OFF)
if(TARGET moonlight-common-c)
  set(MLXR_BACKEND_HAVE_LL ON)
  add_library(mlxr_backend_ll STATIC limelight_glue.c)
  target_include_directories(mlxr_backend_ll PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party/moonlight-common-c/src)
  target_link_libraries(mlxr_backend_ll PUBLIC mlxr_backend moonlight-common-c)
endif()

add_executable(mlxr-cli cli.c)
target_link_libraries(mlxr-cli PRIVATE mlxr_backend)
if(MLXR_BACKEND_HAVE_LL)
  target_link_libraries(mlxr-cli PRIVATE mlxr_backend_ll)
  target_compile_definitions(mlxr-cli PRIVATE MLXR_HAVE_LIMELIGHT)
endif()

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/backend/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/backend ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
