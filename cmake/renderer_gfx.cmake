# renderer_gfx.cmake - renderer_gfx, built into the client (new file, USE_GFX; modelled on
# renderer_gl1.cmake)
#
# renderer_gfx (code/renderer_gfx) is renderergl1's front end with its back end on gfx. It is a
# static library linked into ioquake3, compiled the way client.cmake compiles a static renderer:
# without USE_RENDERER_DLOPEN, so it calls the engine's Com_Printf & co. directly and
# tr_subs.c / q_shared.c / q_math.c are not compiled twice. The client keeps
# USE_RENDERER_DLOPEN for the GL renderer dlls, and calls renderer_gfx's GfxGetRefAPI for
# cl_renderer gfx (cl_main.c). gfx (cmake/gfx.cmake) is linked once, into the client, so the
# renderer and the client's input share it.

if(NOT USE_GFX OR NOT BUILD_CLIENT)
    return()
endif()

include(renderer_common)

set(RENDERER_GFX_SOURCES
    ${SOURCE_DIR}/renderer_gfx/tr_altivec.c
    ${SOURCE_DIR}/renderer_gfx/tr_animation.c
    ${SOURCE_DIR}/renderer_gfx/tr_backend.c
    ${SOURCE_DIR}/renderer_gfx/tr_bsp.c
    ${SOURCE_DIR}/renderer_gfx/tr_cmds.c
    ${SOURCE_DIR}/renderer_gfx/tr_curve.c
    ${SOURCE_DIR}/renderer_gfx/tr_flares.c
    ${SOURCE_DIR}/renderer_gfx/tr_image.c
    ${SOURCE_DIR}/renderer_gfx/tr_init.c
    ${SOURCE_DIR}/renderer_gfx/tr_light.c
    ${SOURCE_DIR}/renderer_gfx/tr_main.c
    ${SOURCE_DIR}/renderer_gfx/tr_marks.c
    ${SOURCE_DIR}/renderer_gfx/tr_mesh.c
    ${SOURCE_DIR}/renderer_gfx/tr_model.c
    ${SOURCE_DIR}/renderer_gfx/tr_model_iqm.c
    ${SOURCE_DIR}/renderer_gfx/tr_scene.c
    ${SOURCE_DIR}/renderer_gfx/tr_shade.c
    ${SOURCE_DIR}/renderer_gfx/tr_shade_calc.c
    ${SOURCE_DIR}/renderer_gfx/tr_shader.c
    ${SOURCE_DIR}/renderer_gfx/tr_shadows.c
    ${SOURCE_DIR}/renderer_gfx/tr_sky.c
    ${SOURCE_DIR}/renderer_gfx/tr_surface.c
    ${SOURCE_DIR}/renderer_gfx/tr_world.c
    ${SOURCE_DIR}/renderer_gfx/gfx_glimp.c
)

set(RENDERER_GFX_BINARY renderer_gfx)

# sdl_glimp.c / sdl_gamma.c still make the window until gfx_glimp.c does (GFX_STATUS.md)
list(APPEND RENDERER_GFX_BINARY_SOURCES
    ${RENDERER_COMMON_SOURCES}
    ${RENDERER_GFX_SOURCES}
    ${SDL_RENDERER_SOURCES}
    ${RENDERER_LIBRARY_SOURCES})

set(RENDERER_GFX_DEFINITIONS ${RENDERER_DEFINITIONS})
list(REMOVE_ITEM RENDERER_GFX_DEFINITIONS USE_RENDERER_DLOPEN)

add_library(${RENDERER_GFX_BINARY} STATIC ${RENDERER_GFX_BINARY_SOURCES})

target_include_directories( ${RENDERER_GFX_BINARY} PRIVATE ${RENDERER_INCLUDE_DIRS})
target_compile_definitions( ${RENDERER_GFX_BINARY} PRIVATE ${RENDERER_GFX_DEFINITIONS} USE_GFX)
target_compile_options(     ${RENDERER_GFX_BINARY} PRIVATE ${RENDERER_COMPILE_OPTIONS})
target_link_libraries(      ${RENDERER_GFX_BINARY} PRIVATE gfx ${RENDERER_LIBRARIES})

target_compile_definitions( ${CLIENT_BINARY} PRIVATE USE_GFX)
target_link_libraries(      ${CLIENT_BINARY} PRIVATE ${RENDERER_GFX_BINARY} gfx)

# the compiled shaders next to the client, as shaders/<dialect>/
set(RENDERER_GFX_SHADERS_DIR ${SOURCE_DIR}/renderer_gfx/shaders)
set(RENDERER_GFX_SHADER_DIALECTS shaders shaders_d3 shaders_gles3_dark shaders_vk)

foreach(DIALECT IN LISTS RENDERER_GFX_SHADER_DIALECTS)
    add_custom_command(TARGET ${CLIENT_BINARY} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${RENDERER_GFX_SHADERS_DIR}/${DIALECT}
            $<TARGET_FILE_DIR:${CLIENT_BINARY}>/shaders/${DIALECT})

    install(DIRECTORY ${RENDERER_GFX_SHADERS_DIR}/${DIALECT} DESTINATION shaders)
endforeach()
