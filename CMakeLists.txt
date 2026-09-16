cmake_minimum_required(VERSION 4.3)
project(AlivePatcher C)

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
            "AlivePatcher must be built as 32-bit (void*=${CMAKE_SIZEOF_VOID_P}). "
            "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake, "
            "or select the mingw32 (i686) toolchain in CLion.")
endif()

# --- embed winmm_org.dll, vorbis.acm, and bgm/*.ogg as RCDATA ---
set(EMBED_WINMM_ORG "${CMAKE_SOURCE_DIR}/native/winmm_org.dll")
set(EMBED_VORBIS_ACM "${CMAKE_SOURCE_DIR}/native/vorbis.acm")
if(NOT EXISTS "${EMBED_WINMM_ORG}")
    message(FATAL_ERROR "Missing ${EMBED_WINMM_ORG} (needed to embed)")
endif()
if(NOT EXISTS "${EMBED_VORBIS_ACM}")
    message(FATAL_ERROR "Missing ${EMBED_VORBIS_ACM} (needed to embed)")
endif()

set(EMBED_RC "${CMAKE_CURRENT_BINARY_DIR}/embed_assets.rc")
set(EMBED_RC_BODY "")
# windres prefers forward slashes in paths
file(TO_CMAKE_PATH "${EMBED_WINMM_ORG}" _p_winmm)
file(TO_CMAKE_PATH "${EMBED_VORBIS_ACM}" _p_vorbis)
string(APPEND EMBED_RC_BODY "WINMM_ORG RCDATA \"${_p_winmm}\"\n")
string(APPEND EMBED_RC_BODY "VORBIS_ACM RCDATA \"${_p_vorbis}\"\n")
set(EMBED_DEPENDS "${EMBED_WINMM_ORG};${EMBED_VORBIS_ACM}")
foreach(i RANGE 1 30)
    if(i LESS 10)
        set(_id "M0${i}")
    else()
        set(_id "M${i}")
    endif()
    set(_ogg "${CMAKE_SOURCE_DIR}/bgm/${_id}.ogg")
    if(NOT EXISTS "${_ogg}")
        message(FATAL_ERROR "Missing ${_ogg}")
    endif()
    file(TO_CMAKE_PATH "${_ogg}" _p_ogg)
    string(APPEND EMBED_RC_BODY "${_id} RCDATA \"${_p_ogg}\"\n")
    list(APPEND EMBED_DEPENDS "${_ogg}")
endforeach()
file(WRITE "${EMBED_RC}" "${EMBED_RC_BODY}")
set_source_files_properties("${EMBED_RC}" PROPERTIES OBJECT_DEPENDS "${EMBED_DEPENDS}")

add_library(AlivePatcher SHARED
        AlivePatcher.c AlivePatcher.h
        ModConsole.c ModConsole.h
        Logger.c Logger.h
        IatHook.c IatHook.h
        CodePage.c CodePage.h
        ConfigDialog.c ConfigDialog.h
        CdCheck.c CdCheck.h
        CdAudio.c CdAudio.h
        VoiceAcm.c VoiceAcm.h
        Embed.c Embed.h
        WinmmProxy.c WinmmProxy.h
        "${EMBED_RC}"
        winmm.def)
target_compile_definitions(AlivePatcher PRIVATE BUILDING_DLL)
target_link_libraries(AlivePatcher PRIVATE comctl32 shell32 m)

# winmm.dll 代理：运行时抽出并 jmp 到 winmm_org（避免 PE forward 在 DllMain 前解析）
set_target_properties(AlivePatcher PROPERTIES
        OUTPUT_NAME "winmm"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/game"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/game"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/game"
)
