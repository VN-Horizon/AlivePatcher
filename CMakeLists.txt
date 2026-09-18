cmake_minimum_required(VERSION 4.3)
project(AlivePatcher C)

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
            "AlivePatcher must be built as 32-bit (void*=${CMAKE_SIZEOF_VOID_P}). "
            "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake, "
            "or select the mingw32 (i686) toolchain in CLion.")
endif()

set(ALIVE_SRC "${CMAKE_SOURCE_DIR}/src")
set(ALIVE_ASSETS "${CMAKE_SOURCE_DIR}/assets")
set(ALIVE_THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party")

# --- embed winmm_org.dll, vorbis.acm, translation.bin, credits.png, and bgm/*.ogg as RCDATA ---
set(EMBED_WINMM_ORG "${ALIVE_ASSETS}/native/winmm_org.dll")
set(EMBED_VORBIS_ACM "${ALIVE_ASSETS}/native/vorbis.acm")
set(EMBED_TRANSLATION "${ALIVE_ASSETS}/translation.bin")
set(EMBED_CREDITS "${ALIVE_ASSETS}/credits.png")
if(NOT EXISTS "${EMBED_WINMM_ORG}")
    message(FATAL_ERROR "Missing ${EMBED_WINMM_ORG} (needed to embed)")
endif()
if(NOT EXISTS "${EMBED_VORBIS_ACM}")
    message(FATAL_ERROR "Missing ${EMBED_VORBIS_ACM} (needed to embed)")
endif()
if(NOT EXISTS "${EMBED_TRANSLATION}")
    message(FATAL_ERROR "Missing ${EMBED_TRANSLATION} (needed to embed)")
endif()
if(NOT EXISTS "${EMBED_CREDITS}")
    message(FATAL_ERROR "Missing ${EMBED_CREDITS} (needed to embed)")
endif()

set(EMBED_RC "${CMAKE_CURRENT_BINARY_DIR}/embed_assets.rc")
set(EMBED_RC_BODY "")
# windres prefers forward slashes in paths
file(TO_CMAKE_PATH "${EMBED_WINMM_ORG}" _p_winmm)
file(TO_CMAKE_PATH "${EMBED_VORBIS_ACM}" _p_vorbis)
file(TO_CMAKE_PATH "${EMBED_TRANSLATION}" _p_trans)
file(TO_CMAKE_PATH "${EMBED_CREDITS}" _p_credits)
string(APPEND EMBED_RC_BODY "WINMM_ORG RCDATA \"${_p_winmm}\"\n")
string(APPEND EMBED_RC_BODY "VORBIS_ACM RCDATA \"${_p_vorbis}\"\n")
string(APPEND EMBED_RC_BODY "TRANSLATION RCDATA \"${_p_trans}\"\n")
string(APPEND EMBED_RC_BODY "CREDITS RCDATA \"${_p_credits}\"\n")
set(EMBED_DEPENDS "${EMBED_WINMM_ORG};${EMBED_VORBIS_ACM};${EMBED_TRANSLATION};${EMBED_CREDITS}")
foreach(i RANGE 1 30)
    if(i LESS 10)
        set(_id "M0${i}")
    else()
        set(_id "M${i}")
    endif()
    set(_ogg "${ALIVE_ASSETS}/bgm/${_id}.ogg")
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
        ${ALIVE_SRC}/AlivePatcher.c ${ALIVE_SRC}/AlivePatcher.h
        ${ALIVE_SRC}/ui/ModConsole.c ${ALIVE_SRC}/ui/ModConsole.h
        ${ALIVE_SRC}/util/Logger.c ${ALIVE_SRC}/util/Logger.h
        ${ALIVE_SRC}/hook/IatHook.c ${ALIVE_SRC}/hook/IatHook.h
        ${ALIVE_SRC}/compat/CodePage.c ${ALIVE_SRC}/compat/CodePage.h
        ${ALIVE_SRC}/ui/ConfigDialog.c ${ALIVE_SRC}/ui/ConfigDialog.h
        ${ALIVE_SRC}/ui/UiText.c ${ALIVE_SRC}/ui/UiText.h
        ${ALIVE_SRC}/ui/CreditsOverlay.c ${ALIVE_SRC}/ui/CreditsOverlay.h
        ${ALIVE_SRC}/cd/CdCheck.c ${ALIVE_SRC}/cd/CdCheck.h
        ${ALIVE_SRC}/audio/CdAudio.c ${ALIVE_SRC}/audio/CdAudio.h
        ${ALIVE_SRC}/audio/VoiceAcm.c ${ALIVE_SRC}/audio/VoiceAcm.h
        ${ALIVE_SRC}/util/Embed.c ${ALIVE_SRC}/util/Embed.h
        ${ALIVE_SRC}/compat/WinmmProxy.c ${ALIVE_SRC}/compat/WinmmProxy.h
        ${ALIVE_SRC}/hook/InlineHook.c ${ALIVE_SRC}/hook/InlineHook.h
        ${ALIVE_SRC}/text/TransCodec.c ${ALIVE_SRC}/text/TransCodec.h
        ${ALIVE_SRC}/text/TransTable.c ${ALIVE_SRC}/text/TransTable.h
        ${ALIVE_SRC}/text/DialogText.c ${ALIVE_SRC}/text/DialogText.h
        ${ALIVE_SRC}/text/Backlog.c ${ALIVE_SRC}/text/Backlog.h
        ${ALIVE_SRC}/text/Translator.c ${ALIVE_SRC}/text/Translator.h
        "${EMBED_RC}"
        ${ALIVE_SRC}/compat/winmm.def)
target_compile_definitions(AlivePatcher PRIVATE BUILDING_DLL)
target_include_directories(AlivePatcher PRIVATE
        ${ALIVE_SRC}
        ${ALIVE_SRC}/audio
        ${ALIVE_SRC}/cd
        ${ALIVE_SRC}/compat
        ${ALIVE_SRC}/hook
        ${ALIVE_SRC}/text
        ${ALIVE_SRC}/ui
        ${ALIVE_SRC}/util
        ${ALIVE_THIRD_PARTY})
target_link_libraries(AlivePatcher PRIVATE comctl32 shell32 gdi32 ole32 m)

# winmm.dll 代理：抽出 winmm_org 到可写目录并 jmp（避免 PE forward 在 DllMain 前解析）
set_target_properties(AlivePatcher PROPERTIES
        OUTPUT_NAME "winmm"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/game"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/game"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/game"
)
