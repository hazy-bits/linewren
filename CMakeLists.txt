cmake_minimum_required(VERSION 3.27)
project(Linewren C)

option(LINEWREN_SMOKE_TEST "Run a self-test and exit instead of waiting for input" OFF)
if(LINEWREN_SMOKE_TEST)
    add_subdirectory(vendor/classic-mac-app/guest
                     ${CMAKE_CURRENT_BINARY_DIR}/classic-test)
endif()

set(editor_source src/editor-atsui.c src/document.c src/markdown.c src/markdown-presentation.c
                  src/text-files.c src/settings.c src/workspace.c vendor/md4c/md4c.c)

option(LINEWREN_FONT_SPECIMENS "Capture the bundled fonts in a running ATSUI editor" OFF)
set(linewren_resources src/Linewren.r assets/icons/bundle.r assets/icons/classic.r)
if(CMAKE_SYSTEM_NAME STREQUAL "RetroCarbon")
    set(LINEWREN_OSX_ICONS tiger CACHE STRING "Icon artwork for the Carbon build")
    set_property(CACHE LINEWREN_OSX_ICONS PROPERTY STRINGS tiger leopard)
    if(NOT LINEWREN_OSX_ICONS MATCHES "^(tiger|leopard)$")
        message(FATAL_ERROR "LINEWREN_OSX_ICONS must be tiger or leopard")
    endif()
    set(icon_resources "${CMAKE_CURRENT_BINARY_DIR}/osx-icons.r")
    file(WRITE "${icon_resources}" "")
    set(icon_id 128)
    foreach(kind app document)
        set(icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/${LINEWREN_OSX_ICONS}/Linewren-${kind}.icns")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${icon}")
        file(READ "${icon}" icon_data HEX)
        file(APPEND "${icon_resources}" "data 'icns' (${icon_id}) { $\"${icon_data}\" };\n")
        math(EXPR icon_id "${icon_id} + 1")
    endforeach()
    list(APPEND linewren_resources "${icon_resources}")
endif()
if(LINEWREN_FONT_SPECIMENS)
    if(LINEWREN_SMOKE_TEST)
        message(FATAL_ERROR "Font specimens require the interactive ATSUI backend")
    endif()
    list(APPEND editor_source src/font-specimens.c)
endif()
list(APPEND editor_source src/bundled-fonts.c)
set(font_resources "${CMAKE_CURRENT_BINARY_DIR}/bundled-fonts.r")
file(WRITE "${font_resources}" "")
set(resource_id 200)
foreach(family "Atkinson Hyperlegible" "Linewren Humanist" "Linewren Grotesk")
    set(suitcase_path "${CMAKE_CURRENT_SOURCE_DIR}/fonts/${family}.bin")
    set(license_path "${CMAKE_CURRENT_SOURCE_DIR}/fonts/${family}-OFL.txt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${suitcase_path}" "${license_path}")
    file(READ "${suitcase_path}" suitcase HEX)
    file(READ "${license_path}" license HEX)
    file(APPEND "${font_resources}" "data 'LwFt' (${resource_id}) { $\"${suitcase}\" };\n"
                "data 'LwFL' (${resource_id}, \"${family} OFL\") { $\"${license}\" };\n")
    math(EXPR resource_id "${resource_id} + 1")
endforeach()
list(APPEND linewren_resources "${font_resources}")

add_application(Linewren
    src/main.c
    ${editor_source}
    ${linewren_resources}
    CREATOR 'Lnwr'
)

target_compile_definitions(Linewren PRIVATE MD4C_USE_UTF16)
target_compile_features(Linewren PRIVATE c_std_11)
target_include_directories(Linewren PRIVATE vendor/md4c)
if(CMAKE_SYSTEM_NAME STREQUAL "RetroCarbon")
    target_sources(Linewren PRIVATE src/carbon-alloc.c)
    target_compile_definitions(Linewren PRIVATE realloc=LinewrenCarbonRealloc)
else()
    target_link_libraries(Linewren PRIVATE NavigationLib InputSprocketLib ATSUnicodeLib
                                        UnicodeConverter FontManager InternetConfigLib)
endif()

if(LINEWREN_FONT_SPECIMENS)
    target_compile_definitions(Linewren PRIVATE LINEWREN_FONT_SPECIMENS)
endif()

set_target_properties(Linewren PROPERTIES COMPILE_OPTIONS -ffunction-sections)
set_target_properties(Linewren PROPERTIES LINK_FLAGS "-Wl,-gc-sections")

if(LINEWREN_SMOKE_TEST)
    target_link_libraries(Linewren PRIVATE classic-test)
    target_compile_definitions(Linewren PRIVATE LINEWREN_SMOKE_TEST)
endif()

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Linewren.bin DESTINATION .)
if(CMAKE_SYSTEM_NAME STREQUAL "RetroCarbon")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Linewren.dsk DESTINATION . RENAME Linewren.img)
endif()
