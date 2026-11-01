set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logfiltereddata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filtersearch_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/windowname_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/revealfile_test.cpp
)

if(NOT APPLE)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow_test.cpp)
endif()

add_executable(klogg_itests ${UI_TEST_SOURCES} ${BACKWARD_ENABLE})

target_link_libraries(klogg_itests klogg_ui klogg_utils Catch2 Qt${QT_VERSION_MAJOR}::Test test_utils)
set_target_properties(klogg_itests PROPERTIES AUTOMOC ON)

# optionsdialog.h includes the header generated for klogg_ui
get_target_property(KLOGG_UI_BINARY_DIR klogg_ui BINARY_DIR)
target_include_directories(klogg_itests PRIVATE
    "${KLOGG_UI_BINARY_DIR}/klogg_ui_autogen/include"
    "${KLOGG_UI_BINARY_DIR}/klogg_ui_autogen/include_$<CONFIG>"
)

add_backward(klogg_itests)

add_test(
    NAME klogg_itests
    COMMAND klogg_itests -platform offscreen
)