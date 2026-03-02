cmake_minimum_required(VERSION 3.15)
project(EUSkillReader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC compiler options (applied to all targets)
if(MSVC)
    add_compile_options(/W4 /O2 /EHsc /sdl /Gw /guard:cf)
    add_compile_options($<$<CONFIG:Release>:/GL>)
    add_compile_options($<$<CONFIG:Publish>:/GL> $<$<CONFIG:Publish>:/WX>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
    # Release/Publish builds: no debug info, no PDB, no Rich header, whole-program optimization
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG:NONE>
        $<$<CONFIG:Release>:/emittoolversioninfo:no>
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Publish>:/DEBUG:NONE>
        $<$<CONFIG:Publish>:/emittoolversioninfo:no>
        $<$<CONFIG:Publish>:/LTCG>
        $<$<CONFIG:Publish>:/INCREMENTAL:NO>
        $<$<CONFIG:Publish>:/guard:cf>
    )
endif()

# Register Publish as a valid configuration (mirrors Release flags)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Publish" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_PUBLISH "${CMAKE_C_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_PUBLISH "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_PUBLISH "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_PUBLISH "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS_PUBLISH "${CMAKE_MODULE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_PUBLISH "${CMAKE_STATIC_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)

add_executable(eu_skill_reader WIN32
    src/main.cpp
    src/app.cpp
    src/capture.cpp
    src/font_atlas.cpp
    src/text_reader.cpp
    src/skill_window.cpp
    src/skill_data.cpp
    resources/app.rc
)

# We provide our own manifest in app.rc — disable MSVC auto-generated manifest
set_target_properties(eu_skill_reader PROPERTIES LINK_FLAGS "/MANIFEST:NO")

target_link_libraries(eu_skill_reader PRIVATE
    user32
    gdi32
    dwmapi
    comctl32
    comdlg32
    shell32
    ole32
    advapi32
)

# Generate SHA-512 hash after building (Release and Publish)
add_custom_command(TARGET eu_skill_reader POST_BUILD
    COMMAND certutil -hashfile "$<TARGET_FILE:eu_skill_reader>" SHA512
        | findstr /v "hash" > "$<TARGET_FILE_DIR:eu_skill_reader>/eu_skill_reader.sha512"
    COMMENT "Generating SHA-512 hash..."
)

# Custom "clean_all" target — removes the entire build directory
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/Release"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/Debug"
    COMMAND ${CMAKE_COMMAND} -E echo "Clean complete."
    COMMENT "Removing build output directories..."
)

# Exclude debug and test targets from Publish builds
set(_EXCLUDE_FROM_PUBLISH "")
if("Publish" IN_LIST CMAKE_CONFIGURATION_TYPES)
    set(_EXCLUDE_FROM_PUBLISH EXCLUDE_FROM_ALL)
endif()

# Optional: also build a console version for debugging
add_executable(eu_skill_reader_debug ${_EXCLUDE_FROM_PUBLISH}
    src/main.cpp
    src/app.cpp
    src/capture.cpp
    src/font_atlas.cpp
    src/text_reader.cpp
    src/skill_window.cpp
    src/skill_data.cpp
)

target_link_libraries(eu_skill_reader_debug PRIVATE
    user32
    gdi32
    dwmapi
    comctl32
    comdlg32
    shell32
    ole32
    advapi32
)

target_compile_definitions(eu_skill_reader_debug PRIVATE DEBUG_CONSOLE)

# Number recognition regression tests (console app)
add_executable(test_numbers ${_EXCLUDE_FROM_PUBLISH}
    src/test_numbers.cpp
    src/text_reader.cpp
    src/font_atlas.cpp
)

target_link_libraries(test_numbers PRIVATE
    user32
    gdi32
)

target_link_options(test_numbers PRIVATE /SUBSYSTEM:CONSOLE)

# Run tests after build (not in Publish)
add_custom_command(TARGET test_numbers POST_BUILD
    COMMAND test_numbers
    WORKING_DIRECTORY $<TARGET_FILE_DIR:test_numbers>
    COMMENT "Running number recognition regression tests..."
)

# PNG-based number recognition tests (console app, requires testdata/)
add_executable(test_png_numbers ${_EXCLUDE_FROM_PUBLISH}
    src/test_png_numbers.cpp
    src/text_reader.cpp
    src/font_atlas.cpp
)

target_link_libraries(test_png_numbers PRIVATE
    user32
    gdi32
)

target_link_options(test_png_numbers PRIVATE /SUBSYSTEM:CONSOLE)

# No post-build auto-run: depends on testdata/ files that may not be present
