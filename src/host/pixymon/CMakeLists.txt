cmake_minimum_required(VERSION 3.16)

project(PixyMon)

# Find the Qt6 library components
find_package(Qt6 COMPONENTS Core Gui Widgets Xml Network REQUIRED)

# Enable C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add compiler flags
if(WIN32)
    add_definitions(-D__WINDOWS__)
    add_compile_options(-mno-ms-bitfields)
elseif(APPLE)
    add_definitions(-D__MACOS__)
    set(MACOSX_BUNDLE_ICON_FILE pixy.icns)
    set_source_files_properties(pixy.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
else()
    add_definitions(-D__LINUX__)
endif()

# Include directories
include_directories(../../common/inc)

if(WIN32)
    include_directories(../windows)
elseif(APPLE)
    include_directories(/opt/local/include/libusb-1.0)
else()
    include_directories(/usr/include/libusb-1.0)
    include_directories(../../../device/main_m4/inc/)
    include_directories(../../../device/libpixy_m4/inc/)
endif()

# Source files
set(SOURCES
    aboutdialog.cpp
    cccmodule.cpp
    chirpmon.cpp
    configdialog.cpp
    connectevent.cpp
    console.cpp
    dataexport.cpp
    debug.cpp
    dfu.cpp
    flash.cpp
    interpreter.cpp
    linemodule.cpp
    main.cpp
    mainwindow.cpp
    monmodule.cpp
    monparameterdb.cpp
    paramfile.cpp
    parameters.cpp
    reader.cpp
    renderer.cpp
    usblink.cpp
    videowidget.cpp

    ../../common/src/calc.cpp
    ../../common/src/chirp.cpp
)

# Header files
set(HEADERS
    aboutdialog.h
    cccmodule.h
    chirpmon.h
    configdialog.h
    connectevent.h
    console.h
    dataexport.h
    debug.h
    dfu.h
    dfu_info.h
    flash.h
    interpreter.h
    linemodule.h
    mainwindow.h
    monmodule.h
    monparameterdb.h
    paramfile.h
    parameters.h
    pixymon.h
    reader.h
    renderer.h
    sleeper.h
    usb_dfu.h
    usblink.h
    videowidget.h

    ../../common/inc/calc.h
    ../../common/inc/chirp.hpp
    ../../common/inc/link.h
    ../../common/inc/pixydefs.h
    ../../common/inc/pixytypes.h
    ../../common/inc/simplevector.h
)

# UI files
set(FORMS
    about.ui
    configdialog.ui
    mainwindow.ui
)

# Resource files
set(RESOURCES
    resources.qrc
)

# Run the MOC, UIC, and RCC
qt6_wrap_cpp(MOC_WRAPPERS ${HEADERS})
qt6_wrap_ui(UI_WRAPPERS ${FORMS})
qt6_add_resources(RCC_WRAPPERS ${RESOURCES})

# Add the executable
add_executable(${PROJECT_NAME} ${SOURCES} ${MOC_WRAPPERS} ${UI_WRAPPERS} ${RCC_WRAPPERS})

# Link against Qt6 libraries
target_link_libraries(${PROJECT_NAME} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml Qt6::Network)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} ../windows/libusb-1.0.dll.a)
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} -L/opt/local/lib -lusb-1.0)
else()
    target_link_libraries(${PROJECT_NAME} -lusb-1.0)
endif()

# Set up debugging flags
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
