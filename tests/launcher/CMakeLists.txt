# Flat launcher tests (ctest names launcher_*). GPLv3.
# From the top level: add_subdirectory(tests/launcher) (one line, the orchestrator's to add).
# Standalone:  cmake -S tests/launcher -B build-launcher/tests -DMLXR_CLI=<path to mlxr-cli> && ctest --test-dir build-launcher/tests
# They skip (77) when launcher/build.sh has not been run. The screenshots go to
# build-launcher/shots, the comparison with stock moonlight-qt needs tests/launcher/build-stock.sh.
cmake_minimum_required(VERSION 3.16)
if(NOT PROJECT_NAME)
  project(launcher_tests NONE)
  enable_testing()
endif()
get_filename_component(LROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MLXR_LAUNCHER_BIN "${LROOT}/build-launcher/moonlight-xr-launcher" CACHE FILEPATH "the built flat launcher")
if(TARGET mlxr-cli)
  set(_cli $<TARGET_FILE:mlxr-cli>)
else()
  set(MLXR_CLI "${LROOT}/build-lnative/backend/mlxr-cli" CACHE FILEPATH "mlxr-cli")
  set(_cli ${MLXR_CLI})
endif()
add_test(NAME launcher_handoff
  COMMAND ${CMAKE_CURRENT_LIST_DIR}/run_tests.sh ${MLXR_LAUNCHER_BIN} ${_cli} ${LROOT}/build-launcher/shots)
add_test(NAME launcher_vs_stock
  COMMAND ${CMAKE_CURRENT_LIST_DIR}/compare-stock.sh ${MLXR_LAUNCHER_BIN} ${_cli} ${LROOT}/build-launcher/cmp)
set_tests_properties(launcher_handoff launcher_vs_stock PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
# lane artemis-settings: the VR client's own config.ini parser checks the Settings page's edits
if(TARGET mlxr-settings-dump)
  set_property(TEST launcher_handoff APPEND PROPERTY ENVIRONMENT "MLXR_SETTINGS_DUMP=$<TARGET_FILE:mlxr-settings-dump>")
endif()
# lane launcher-bundle: a Moonlight host on 127.0.0.1 (tests/backend's fake host) that the launcher pairs with, for the
# command line's stream "PC" "Desktop" and the On start setting end to end; skipped without it
if(TARGET backend_fake_host_main)
  set_property(TEST launcher_handoff APPEND PROPERTY ENVIRONMENT "MLXR_FAKE_HOST=$<TARGET_FILE:backend_fake_host_main>")
endif()
