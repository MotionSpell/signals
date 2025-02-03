vcpkg_minimum_required(VERSION 2022-10-12) # for ${VERSION}

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO  gpac/gpac 
    REF c02c9a2fb0815bfd28b7c4c46630601ad7fe291d
    SHA512 e0ae99684e54f4862edf53238f3bd095f451cb689878c6f9fff0a2aff882fe2eed28a723ac7596a541ff509d96e64582431b9c145c278444e3e5f5caa1b4f612
    HEAD_REF master
    PATCHES
        install-header-and-pc.patch
)

if (VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    vcpkg_list(SET EXTRA_INCLUDE_DIRS)
    list(APPEND EXTRA_INCLUDE_DIRS "${CURRENT_INSTALLED_DIR}/include")
    vcpkg_list(SET EXTRA_LIB_DIRS)
    list(APPEND EXTRA_LIB_DIRS "${CURRENT_INSTALLED_DIR}/lib")
    vcpkg_list(SET OPTIONS)
    # Enable this line to get the msbuild configuration variable dump in the vcpkg output file
    #list(APPEND OPTIONS /v:diag)
    list(APPEND OPTIONS
        /p:IncludePath=${EXTRA_INCLUDE_DIRS}
        /p:AdditionalLibraryDirectories=${EXTRA_LIB_DIRS}
    )
    vcpkg_install_msbuild(
        SOURCE_PATH "${SOURCE_PATH}"
        PROJECT_SUBPATH "build/msvc14/gpac.sln"
        OPTIONS
            ${OPTIONS}
    )
else()
    vcpkg_list(SET OPTIONS)

    if(VCPKG_TARGET_IS_OSX)

        
    #    list(APPEND OPTIONS
    #        --target-os=Darwin
    #        --extra-cflags=-I${CURRENT_INSTALLED_DIR}/include
    #		--extra-ldflags=-L${CURRENT_INSTALLED_DIR}/lib
    #    )
    elseif(VCPKG_TARGET_IS_LINUX)
    elseif(VCPKG_TARGET_IS_MINGW)
    else()
        message(ERROR "Unknown target ${TARGET_TRIPLET}")
    endif()


    vcpkg_configure_make(
        SOURCE_PATH "${SOURCE_PATH}"
        DETERMINE_BUILD_TRIPLET
        NO_ADDITIONAL_PATHS
        OPTIONS
            --disable-player
            --disable-ssl
            --disable-alsa
            --disable-jack
            --disable-oss-audio
            --disable-pulseaudio
            --use-ffmpeg=no
            --use-png=no
            --use-jpeg=no
            --disable-3d
            --disable-atsc
            --disable-avi
            --disable-bifs
            --disable-bifs-enc
            --disable-crypt
            --disable-dvb4linux
            --disable-dvbx
            --disable-ipv6
            --disable-laser
            --disable-loader-bt
            --disable-loader-isoff
            --disable-loader-xmt
            --disable-m2ps
            --disable-mcrypt
            --disable-od-dump
            --disable-odf
            --disable-ogg
            --disable-opt
            --disable-platinum
            --disable-qtvr
            --disable-saf
            --disable-scene-dump
            --disable-scene-encode
            --disable-scene-stats
            --disable-scenegraph
            --disable-seng
            --disable-sman
            --disable-streaming
            --disable-svg
            --disable-swf
            --disable-vobsub
            --disable-vrml
            --disable-wx
            --disable-x11
            --disable-x11-shm
            --disable-x11-xv
            --disable-x3d
            --enable-export
            --enable-import
            --enable-parsers
            --enable-m2ts
            --enable-m2ts-mux
            --enable-ttxt
            --enable-hevc
            --enable-isoff
            --enable-isoff-frag
            --enable-isoff-hds
            --enable-isoff-hint
            --enable-isoff-write
            --enable-isom-dump
            ${OPTIONS}
    )

    vcpkg_install_make()

    vcpkg_fixup_pkgconfig()
endif()