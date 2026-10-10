cmake_minimum_required(VERSION 3.16)
project(komutracker VERSION 1.0.0 LANGUAGES C)
if(APPLE)
    enable_language(OBJC)
elseif(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

set(AFK_SOURCES
    src/main.c
    src/afk.c
    src/http.c
    src/auth.c
    src/dirs.c
    src/instance.c
    src/tracker.c
    src/tray_app.c
)

if(WIN32)
    list(APPEND AFK_SOURCES src/idle_windows.c src/tray_windows.c src/komutracker.rc)
elseif(APPLE)
    list(APPEND AFK_SOURCES src/idle_macos.c src/tray_macos.m assets/komutracker.icns)
else()
    find_package(X11 REQUIRED)
    if(NOT X11_Xss_FOUND)
        message(FATAL_ERROR "XScreenSaver development files are required (libxss-dev)")
    endif()
    list(APPEND AFK_SOURCES src/idle_linux.c src/tray_linux.c)
endif()

add_executable(komutracker ${AFK_SOURCES})
if(WIN32)
    # GUI subsystem: no console window when launched from Explorer or at login.
    set_target_properties(komutracker PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME KomuTracker)
    if(MSVC)
        target_link_options(komutracker PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()
target_include_directories(komutracker PRIVATE src)
target_compile_definitions(komutracker PRIVATE KOMUTRACKER_VERSION="${PROJECT_VERSION}")
target_link_libraries(komutracker PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(komutracker PRIVATE ws2_32 bcrypt shell32)
elseif(APPLE)
    find_library(APPLICATION_SERVICES ApplicationServices REQUIRED)
    find_library(COCOA Cocoa REQUIRED)
    target_link_libraries(komutracker PRIVATE ${APPLICATION_SERVICES} ${COCOA})
    set_source_files_properties(src/tray_macos.m PROPERTIES COMPILE_FLAGS -fobjc-arc)
    # Menu-bar-only app bundle (LSUIElement: no Dock icon); ad-hoc signed so permissions stick to one identity.
    set_source_files_properties(assets/komutracker.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(komutracker PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME KomuTracker
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/assets/Info.plist.in
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    # Separate ALL target: bundle resources are copied after POST_BUILD, which would invalidate the seal.
    add_custom_target(sign_app ALL
        COMMAND codesign --force --deep --sign - $<TARGET_BUNDLE_DIR:komutracker>
        DEPENDS komutracker)
else()
    target_include_directories(komutracker PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(komutracker PRIVATE ${X11_LIBRARIES} ${X11_Xss_LIB} ${CMAKE_DL_LIBS} pthread)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(afk-state-tests tests/test_state.c src/afk.c)
    target_include_directories(afk-state-tests PRIVATE src)
    add_test(NAME afk-state-tests COMMAND afk-state-tests)

    add_executable(auth-tests tests/test_auth.c src/http.c)
    target_include_directories(auth-tests PRIVATE src)
    target_link_libraries(auth-tests PRIVATE CURL::libcurl)
    add_test(NAME auth-tests COMMAND auth-tests)
endif()

if(WIN32)
    install(TARGETS komutracker RUNTIME DESTINATION .)
else()
    install(TARGETS komutracker RUNTIME DESTINATION bin BUNDLE DESTINATION .)
endif()

if(WIN32)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "KomuTracker-${PROJECT_VERSION}-windows-x64")
elseif(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_PACKAGE_FILE_NAME "KomuTracker-${PROJECT_VERSION}-macOS")
else()
    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_FILE_NAME "komutracker-${PROJECT_VERSION}-ubuntu-amd64")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Komu")
    set(CPACK_DEBIAN_PACKAGE_SECTION utils)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libx11-6, libxss1")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
endif()
set(CPACK_PACKAGE_NAME "KomuTracker")
set(CPACK_PACKAGE_VENDOR "Komu")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native active and AFK time tracker")
include(CPack)
