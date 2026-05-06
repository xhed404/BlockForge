cmake_minimum_required(VERSION 3.20)

project(BlockForge VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BLOCKFORGE_BUILD_UI "Build BlockForge Qt UI" ON)
option(BLOCKFORGE_BUILD_MC "Build Minecraft integration" ON)

add_library(blockforge_core
    src/core/AppPaths.cpp
    src/core/FsUtil.cpp
    src/core/Md5.cpp
    src/core/OfflineAuth.cpp
    src/core/Settings.cpp
    src/core/Instance.cpp
    src/core/InstanceStore.cpp
)

target_include_directories(blockforge_core PUBLIC src)
target_compile_definitions(blockforge_core PUBLIC BLOCKFORGE_VERSION="${PROJECT_VERSION}")

if(BLOCKFORGE_BUILD_MC)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    find_package(ZLIB REQUIRED)

    add_library(blockforge_minecraft
        src/auth/AccountStore.cpp
        src/auth/MicrosoftAuth.cpp
        src/minecraft/HttpClient.cpp
        src/minecraft/MinecraftInstaller.cpp
        src/minecraft/MinecraftLauncher.cpp
        src/minecraft/MinecraftManifest.cpp
        src/minecraft/Os.cpp
        src/minecraft/Zip.cpp
    )
    target_include_directories(blockforge_minecraft PUBLIC src)
    target_link_libraries(blockforge_minecraft PUBLIC Qt6::Core Qt6::Network ZLIB::ZLIB blockforge_core)
    target_compile_definitions(blockforge_minecraft PUBLIC BLOCKFORGE_BUILD_MC=1)
endif()

add_executable(blockforge-cli
    src/cli/main.cpp
)
target_link_libraries(blockforge-cli PRIVATE blockforge_core)
if(BLOCKFORGE_BUILD_MC)
    target_link_libraries(blockforge-cli PRIVATE blockforge_minecraft)
endif()

if(BLOCKFORGE_BUILD_UI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
    find_package(Qt6 QUIET COMPONENTS Svg)
    qt_standard_project_setup()

    qt_add_executable(BlockForge
        src/ui/main.cpp
        src/ui/MainWindow.cpp
        src/ui/LaunchWorker.cpp
        src/ui/AuthWorker.cpp
        src/ui/Theme.cpp
        src/ui/ModrinthDialog.cpp
        src/ui/resources.qrc
    )
    set_target_properties(BlockForge PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
    target_link_libraries(BlockForge PRIVATE Qt6::Widgets Qt6::Network blockforge_core)
    if(TARGET Qt6::Svg)
        target_link_libraries(BlockForge PRIVATE Qt6::Svg)
    endif()
    if(BLOCKFORGE_BUILD_MC)
        target_link_libraries(BlockForge PRIVATE blockforge_minecraft)
    endif()
endif()
