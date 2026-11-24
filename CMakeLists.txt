cmake_minimum_required(VERSION 3.24)

project(ccqtupon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
option(INSTALL_PLASMOID "Install the Plasma 6 fan control plasmoid" ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_library(ccqtupon_core STATIC
    src/api/CoolerControlClient.h
    src/api/CoolerControlClient.cpp
    src/models/CoolerControlModels.h
    src/models/CoolerControlModels.cpp
    src/ui/AppTranslator.h
    src/ui/AppTranslator.cpp
)

target_include_directories(ccqtupon_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ccqtupon_core
    PUBLIC
        Qt6::Widgets
        Qt6::Network
)

target_compile_options(ccqtupon_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(ccqtupon
    src/main.cpp
    src/ui/AboutDialog.h
    src/ui/AboutDialog.cpp
    src/ui/AuthDialog.h
    src/ui/AuthDialog.cpp
    src/ui/CoolingTabWidget.h
    src/ui/CoolingTabWidget.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/widgets/StatusBanner.h
    src/widgets/StatusBanner.cpp
    src/widgets/MetricBarWidget.h
    src/widgets/MetricBarWidget.cpp
    src/widgets/DeviceCardWidget.h
    src/widgets/DeviceCardWidget.cpp
    src/widgets/FanGaugeWidget.h
    src/widgets/FanGaugeWidget.cpp
    src/widgets/FanHistoryChartWidget.h
    src/widgets/FanHistoryChartWidget.cpp
    src/widgets/FanCurveEditorWidget.h
    src/widgets/FanCurveEditorWidget.cpp
)

target_link_libraries(ccqtupon
    PRIVATE
        ccqtupon_core
)

target_compile_options(ccqtupon
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

enable_testing()

add_executable(ccqtupon_parser_tests
    tests/ParserTests.cpp
)

target_link_libraries(ccqtupon_parser_tests
    PRIVATE
        ccqtupon_core
)

target_compile_options(ccqtupon_parser_tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME parser_tests COMMAND ccqtupon_parser_tests)

install(TARGETS ccqtupon
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES packaging/ccqtupon.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

if(INSTALL_PLASMOID)
    install(DIRECTORY plasmoid/org.ccqtupon.fan.plasmoid/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/org.ccqtupon.fan.plasmoid
    )
endif()
