cmake_minimum_required(VERSION 3.20)
project(GMTools VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
find_package(nlohmann_json 3.10 CONFIG REQUIRED)
option(GMTOOLS_BUILD_GUI "Build the Qt Widgets application" ON)
option(GMTOOLS_ENABLE_MSEED "Build the experimental libmseed 3 adapter" OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(gmtools_core STATIC
    src/core/data.cpp
    src/core/files.cpp
    src/core/profile.cpp
    src/formats/registry.cpp
    src/formats/text.cpp
    src/formats/peer.cpp
    src/formats/knet.cpp
    src/formats/sac.cpp
)
target_include_directories(gmtools_core PUBLIC include)
target_compile_features(gmtools_core PUBLIC cxx_std_20)
target_link_libraries(gmtools_core PRIVATE nlohmann_json::nlohmann_json)

if(GMTOOLS_ENABLE_MSEED)
    find_package(libmseed 3.5.3 CONFIG REQUIRED)
    target_sources(gmtools_core PRIVATE src/formats/mseed.cpp)
    target_compile_definitions(gmtools_core PRIVATE GMTOOLS_HAVE_LIBMSEED)
    target_link_libraries(gmtools_core PRIVATE mseed::mseed)
endif()

add_executable(gmtools src/cli/main.cpp)
target_link_libraries(gmtools PRIVATE gmtools_core)
if(MINGW)
    target_link_options(gmtools PRIVATE -municode)
endif()
install(TARGETS gmtools RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(GMTOOLS_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    add_executable(gmtools_gui WIN32 src/gui/main.cpp src/gui/plot.cpp)
    target_link_libraries(gmtools_gui PRIVATE gmtools_core Qt6::Widgets)
    install(TARGETS gmtools_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
