$(BIN)/render-config.mk: $(SRC)/../scripts/configure
	@mkdir -p $(BIN)
	$(SRC)/../scripts/configure --scope RENDER_ sdl2 > "$@"

ifneq ($(MAKECMDGOALS),clean)
include $(BIN)/render-config.mk
endif

TARGETS+=$(BIN)/SDLVideo.smd
$(BIN)/SDLVideo.smd: $(BIN)/$(SRC)/lib_media/render/sdl_video.cpp.o
$(BIN)/SDLVideo.smd: LDFLAGS+=$(RENDER_LDFLAGS)
$(BIN)/SDLVideo.smd: CFLAGS+=$(RENDER_CFLAGS)

TARGETS+=$(BIN)/SDLAudio.smd
$(BIN)/SDLAudio.smd: \
	$(BIN)/$(SRC)/lib_media/render/sdl_audio.cpp.o \
	$(BIN)/$(SRC)/lib_media/common/libav.cpp.o
$(BIN)/SDLAudio.smd: LDFLAGS+=$(RENDER_LDFLAGS)
$(BIN)/SDLAudio.smd: CFLAGS+=$(RENDER_CFLAGS)

