# hxn2vr: flat (non-VR) OpenGL Hexen II engine, Windows x64 / MSVC.
# Source list, defines, includes and libs mirror engine/hexen2/VisualStudio.zip -> glhexen2.vcxproj
# (Debug|x64 / Release|x64). snd_mpg123.c is ExcludedFromBuild in every configuration there.

set(HXN2VR_SOURCES
    engine/h2shared/bgmusic.c
    engine/h2shared/cd_win.c
    engine/h2shared/cfgfile.c
    engine/hexen2/chase.c
    engine/hexen2/cl_cmd.c
    engine/hexen2/cl_demo.c
    engine/hexen2/cl_effect.c
    engine/hexen2/cl_inlude.c
    engine/hexen2/cl_input.c
    engine/hexen2/cl_main.c
    engine/hexen2/cl_parse.c
    engine/hexen2/cl_string.c
    engine/hexen2/cl_tent.c
    engine/h2shared/cmd.c
    engine/h2shared/common.c
    engine/hexen2/console.c
    common/crc.c
    engine/h2shared/cvar.c
    engine/h2shared/debuglog.c
    engine/h2shared/gl_draw.c
    engine/h2shared/gl_mesh.c
    engine/h2shared/gl_model.c
    engine/h2shared/gl_refrag.c
    engine/h2shared/gl_rlight.c
    engine/hexen2/gl_rmain.c
    engine/hexen2/gl_rmisc.c
    engine/hexen2/gl_rsurf.c
    engine/h2shared/gl_screen.c
    engine/h2shared/gl_vidnt.c
    engine/h2shared/gl_warp.c
    engine/h2shared/hashindex.c
    engine/hexen2/host.c
    engine/hexen2/host_cmd.c
    engine/h2shared/host_string.c
    engine/h2shared/in_win.c
    engine/hexen2/keys.c
    engine/h2shared/link_ops.c
    engine/h2shared/mathlib.c
    engine/hexen2/menu.c
    engine/h2shared/mid2strm.c
    engine/h2shared/midi_win.c
    engine/h2shared/msg_io.c
    engine/hexen2/net_dgrm.c
    engine/hexen2/net_loop.c
    engine/hexen2/net_main.c
    engine/hexen2/net_win.c
    engine/hexen2/net_wins.c
    engine/hexen2/net_wipx.c
    engine/h2shared/pr_cmds.c
    engine/h2shared/pr_edict.c
    engine/h2shared/pr_exec.c
    common/q_endian.c
    common/qsnprint.c
    engine/h2shared/quakefs.c
    engine/hexen2/r_part.c
    engine/hexen2/sbar.c
    engine/h2shared/sizebuf.c
    engine/h2shared/snd_codec.c
    engine/h2shared/snd_dma.c
    engine/h2shared/snd_dsound.c
    engine/h2shared/snd_flac.c
    engine/h2shared/snd_mem.c
    engine/h2shared/snd_mikmod.c
    engine/h2shared/snd_mix.c
    engine/h2shared/snd_modplug.c
    engine/h2shared/snd_mp3.c
    engine/h2shared/snd_mp3tag.c
    engine/h2shared/snd_opus.c
    engine/h2shared/snd_sys.c
    engine/h2shared/snd_umx.c
    engine/h2shared/snd_vorbis.c
    engine/h2shared/snd_wave.c
    engine/h2shared/snd_xmp.c
    engine/h2shared/snd_win.c
    common/strlcat.c
    common/strlcpy.c
    engine/hexen2/sv_effect.c
    engine/hexen2/sv_main.c
    engine/hexen2/sv_move.c
    engine/hexen2/sv_phys.c
    engine/hexen2/sv_user.c
    engine/hexen2/sys_win.c
    engine/hexen2/view.c
    engine/h2shared/wad.c
    engine/hexen2/world.c
    engine/h2shared/zone.c

    engine/h2shared/win32res.rc
)

set(HXN2VR_CODEC_DIR "${CMAKE_SOURCE_DIR}/oslibs/windows/codecs/x64")
file(GLOB HXN2VR_CODEC_DLLS "${HXN2VR_CODEC_DIR}/*.dll")

add_executable(hxn2vr WIN32 ${HXN2VR_SOURCES})
set_target_properties(hxn2vr PROPERTIES OUTPUT_NAME hxn2vr)

# Upstream is C; the vcxproj builds everything with CompileAsC.
set_source_files_properties(engine/h2shared/win32res.rc PROPERTIES LANGUAGE RC)

target_include_directories(hxn2vr PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/hexen2
    ${CMAKE_SOURCE_DIR}/engine/h2shared
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/oslibs/windows/misc/include
    ${CMAKE_SOURCE_DIR}/oslibs/windows/dxsdk/include
    ${CMAKE_SOURCE_DIR}/oslibs/windows/codecs/include
)

target_compile_definitions(hxn2vr PRIVATE
    WIN32 _WINDOWS GLQUAKE GL_DLSYM DX_DLSYM _USE_WINSOCK2
    WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS
    USE_CODEC_MP3 USE_CODEC_VORBIS USE_CODEC_WAVE USE_CODEC_FLAC
    USE_CODEC_OPUS USE_CODEC_MIKMOD USE_CODEC_UMX
    $<$<CONFIG:Debug>:DEBUG DEBUG_BUILD>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# The resource script only needs these (vcxproj ResourceCompile settings).
set_source_files_properties(engine/h2shared/win32res.rc PROPERTIES
    COMPILE_DEFINITIONS "GLQUAKE;WIN32_LEAN_AND_MEAN"
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/engine/hexen2;${CMAKE_SOURCE_DIR}/engine/h2shared")

# Debug: /MDd (MultiThreadedDebugDLL), Release: /MT (MultiThreaded), as in the vcxproj.
set_property(TARGET hxn2vr PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")
if(MSVC)
    target_compile_options(hxn2vr PRIVATE /W3)
endif()

target_link_directories(hxn2vr PRIVATE ${HXN2VR_CODEC_DIR})
target_link_libraries(hxn2vr PRIVATE
    libvorbisfile libvorbis libopusfile libopus libFLAC libogg libmad libmikmod
    ws2_32 winmm
)

add_custom_command(TARGET hxn2vr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HXN2VR_CODEC_DLLS} "$<TARGET_FILE_DIR:hxn2vr>"
    COMMENT "Copying codec DLLs next to hxn2vr.exe"
    VERBATIM)
