cmake_minimum_required(VERSION 3.19)
# One version, in the spec: the RPM, the About page and the release tag all follow it
# (tools/check_version.sh holds the tag to it).
file(STRINGS rpm/harbour-mahlebike.spec APP_VERSION_LINE REGEX "^Version:")
string(REGEX REPLACE "^Version:[ \t]*" "" APP_VERSION "${APP_VERSION_LINE}")
project(harbour-mahlebike VERSION ${APP_VERSION} LANGUAGES CXX)

# std::optional in the Qt-free core (src/protocol, src/ride) needs C++17. The core is compiled and
# tested with plain g++ as well; see tests/README.md.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No "Bluetooth" Qt component: Sailfish OS 5.1's SDK target has no pkgconfig(Qt5Bluetooth) at all
# (QtConnectivity is not part of its Qt5), so the bike is reached through BlueZ's D-Bus API
# (org.bluez.*) with Qt5::DBus, the way harbour-ourasync and harbour-suuntosync do it.
find_package(Qt5 COMPONENTS Core Sql DBus Qml Gui Quick Positioning LinguistTools REQUIRED)

include(FindPkgConfig)
pkg_search_module(SAILFISH sailfishapp REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# The Qt-free core: the protocol decoders and the ride statistics / GPX. Built and tested with plain
# g++ in CI, so a bug in it never needs a phone.
set(CORE_SOURCES
    src/protocol/mahle.cpp
    src/protocol/demo.cpp
    src/ride/ride.cpp
    src/export/fit.cpp
    src/export/fitcrc.cpp
)

add_executable(harbour-mahlebike
    src/harbour-mahlebike.cpp
    src/controller/appcontroller.cpp
    src/controller/models.cpp
    src/ble/bluezmanagedobjects.cpp
    src/ble/bikescanner.cpp
    src/ble/bikelink.cpp
    src/store/ridestore.cpp
    ${CORE_SOURCES}
)

target_compile_definitions(harbour-mahlebike PRIVATE
    APP_VERSION="${APP_VERSION}"
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
)

target_include_directories(harbour-mahlebike PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    $<BUILD_INTERFACE:
    ${SAILFISH_INCLUDE_DIRS}
>)

target_link_libraries(harbour-mahlebike
    Qt5::Quick
    Qt5::Sql
    Qt5::DBus
    Qt5::Positioning
    ${SAILFISH_LDFLAGS}
)

install(TARGETS harbour-mahlebike RUNTIME DESTINATION bin)
install(DIRECTORY qml DESTINATION share/harbour-mahlebike)
install(FILES harbour-mahlebike.desktop DESTINATION share/applications)
foreach(size 86x86 108x108 128x128 172x172)
    install(FILES icons/${size}/harbour-mahlebike.png
        DESTINATION share/icons/hicolor/${size}/apps)
endforeach()

# Source strings (qsTr()/tr()) are English. translations/harbour-mahlebike.ts tracks them as the
# extraction base and each harbour-mahlebike-<lang>.ts is a translated copy. qt5_add_translation only
# compiles .ts to .qm; a normal build never touches the committed .ts files (run lupdate by hand,
# see README).
file(GLOB TS_FILES "${CMAKE_SOURCE_DIR}/translations/*.ts")
file(GLOB LANG_TS_FILES "${CMAKE_SOURCE_DIR}/translations/harbour-mahlebike-*.ts")
if(LANG_TS_FILES)
    qt5_add_translation(QM_FILES ${LANG_TS_FILES})
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
    add_dependencies(harbour-mahlebike translations)
    install(FILES ${QM_FILES} DESTINATION share/harbour-mahlebike/translations)
endif()

# Get the other files reachable from the project tree in Qt Creator.
file(GLOB_RECURSE QML_FILES "${CMAKE_SOURCE_DIR}/qml/*.qml" "${CMAKE_SOURCE_DIR}/qml/*.js")
add_custom_target(distfiles
    SOURCES
        harbour-mahlebike.desktop
        rpm/harbour-mahlebike.spec
        ${QML_FILES}
        ${TS_FILES})

# Tell Qt Creator where the application executable would be located on the device.
file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
    "${CMAKE_INSTALL_PREFIX}
${CMAKE_BINARY_DIR}/harbour-mahlebike:bin
")
