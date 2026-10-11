# gfx.cmake - the gfx library as a static library (new file, USE_GFX)
#
# gfx (GFX_DIR, by default $code_root_dir/Code2/General/gfx/gfx_dll/gfx) abstracts the GL3, GL4,
# GLES3, Vulkan, D3D11 and D3D12 devices and the SDL, GLFW, Win32 and X11 window systems. Its
# sources are compiled here with the per-platform source lists, definitions and system
# libraries of gfx's own CMakeLists.txt (the dll build), in the configuration ioq3 is built in;
# the gfx tree is only read. renderer_gfx and the client link it, so they share one copy of gfx.
#
# gfx's SDL window is compiled against ioq3's own SDL2 (libraries/sdl.cmake: the internal
# headers and import library on Windows, the system SDL2 elsewhere), so one SDL2 library serves
# gfx and the client's audio.

if(NOT USE_GFX OR NOT BUILD_CLIENT)
    return()
endif()

if(NOT MSVC AND NOT (UNIX AND NOT APPLE AND NOT EMSCRIPTEN))
    message(FATAL_ERROR "gfx builds with MSVC on Windows and on Linux; configure with -DUSE_GFX=OFF")
endif()

if(NOT USE_RENDERER_DLOPEN)
    message(FATAL_ERROR "USE_GFX needs USE_RENDERER_DLOPEN: renderer_gfx is built into the client and the GL renderers stay dlls")
endif()

if(NOT BUILD_RENDERER_GL1 AND NOT BUILD_RENDERER_GL2)
    # libraries/jpeg.cmake only sets up jpeg for a GL renderer
    message(FATAL_ERROR "USE_GFX needs BUILD_RENDERER_GL1 or BUILD_RENDERER_GL2")
endif()

set(GFX_DIR "$ENV{code_root_dir}/Code2/General/gfx/gfx_dll/gfx" CACHE PATH "gfx library source tree")

if(NOT EXISTS ${GFX_DIR}/src/window.c)
    message(FATAL_ERROR "gfx not found in GFX_DIR (${GFX_DIR}); set GFX_DIR or configure with -DUSE_GFX=OFF")
endif()

# vk_mem_alloc.cpp
enable_language(CXX)

set(GFX_SOURCES
    ${GFX_DIR}/src/device.c
    ${GFX_DIR}/src/window.c
    ${GFX_DIR}/src/devices/gl.c
    ${GFX_DIR}/src/devices/gl3.c
    ${GFX_DIR}/src/devices/gl4.c
    ${GFX_DIR}/src/devices/gles3.c
    ${GFX_DIR}/src/devices/vk.c
    ${GFX_DIR}/src/devices/vk_mem_alloc.cpp
    ${GFX_DIR}/src/windows/sdl.c
    ${GFX_DIR}/src/windows/gl_sdl.c
    ${GFX_DIR}/src/windows/vk_sdl.c
    ${GFX_DIR}/src/windows/glfw.c
    ${GFX_DIR}/src/windows/gl_glfw.c
    ${GFX_DIR}/src/windows/vk_glfw.c
)

if(WIN32)
    list(APPEND GFX_SOURCES
        ${GFX_DIR}/src/windows/win32.c
        ${GFX_DIR}/src/windows/gl_win32.c
        ${GFX_DIR}/src/windows/vk_win32.c
        ${GFX_DIR}/src/windows/d3d.c
        ${GFX_DIR}/src/devices/d3d11.c
        ${GFX_DIR}/src/devices/d3d12.c
    )
else()
    list(APPEND GFX_SOURCES
        ${GFX_DIR}/src/windows/x11.c
        ${GFX_DIR}/src/windows/gl_x11.c
        ${GFX_DIR}/src/windows/vk_x11.c
        ${GFX_DIR}/src/devices/jkg.c
    )
endif()

# GLFW as gfx gets it: FetchContent on Windows (from the gfx build's checkout when it has one,
# so no download is needed), the system package elsewhere
if(WIN32)
    include(FetchContent)

    if(EXISTS ${GFX_DIR}/build/_deps/glfw-src/CMakeLists.txt AND NOT FETCHCONTENT_SOURCE_DIR_GLFW)
        set(FETCHCONTENT_SOURCE_DIR_GLFW ${GFX_DIR}/build/_deps/glfw-src)
    endif()

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
else()
    find_package(glfw3 REQUIRED)
endif()

add_library(gfx STATIC ${GFX_SOURCES})

# code using gfx includes "gfx/window.h" & co.
target_include_directories(gfx PUBLIC ${GFX_DIR}/include)
target_include_directories(gfx PRIVATE
    ${GFX_DIR}/src
    ${GFX_DIR}/include/gfx
    ${GFX_DIR}/lib/jks/include
    ${GFX_DIR}/src/headers
    ${SDL2_INCLUDE_DIRS}
)

# PUBLIC: it changes the layout of struct gfx_pipeline_create_info, as in gfx's dll build
target_compile_definitions(gfx PUBLIC GFX_ENABLE_TARGET_FRAMEBUFFER)

if(MSVC)
    target_compile_definitions(gfx PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)

    # gfx's warning level rather than ioq3's /W4 (compilers/msvc.cmake)
    get_target_property(GFX_COMPILE_OPTIONS gfx COMPILE_OPTIONS)
    list(REMOVE_ITEM GFX_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C>:/W4>")
    set_target_properties(gfx PROPERTIES COMPILE_OPTIONS "${GFX_COMPILE_OPTIONS}")
    target_compile_options(gfx PRIVATE /W3 /sdl /permissive-)

    target_link_libraries(gfx PUBLIC opengl32 d3d11 d3dcompiler dxgi glfw ${SDL2_LIBRARIES})
else()
    # gfx's dll build uses the compiler's default C standard, not ioq3's C99
    set_target_properties(gfx PROPERTIES C_STANDARD 11)

    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)

    target_link_libraries(gfx PUBLIC
        X11::X11
        OpenGL::GL
        glfw
        ${SDL2_LIBRARIES}
        Threads::Threads
        ${CMAKE_DL_LIBS})

    if(TARGET X11::Xi)
        target_link_libraries(gfx PUBLIC X11::Xi)
    else()
        target_link_libraries(gfx PUBLIC Xi)
    endif()

    if(TARGET X11::Xcursor)
        target_link_libraries(gfx PUBLIC X11::Xcursor)
    else()
        target_link_libraries(gfx PUBLIC Xcursor)
    endif()
endif()
