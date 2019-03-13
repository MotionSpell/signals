$(BIN)/render-config.mk: $(SRC)/../scripts/configure
	@mkdir -p $(BIN)
	$(SRC)/../scripts/configure --scope RENDER_ libavcodec sdl2 > "$@"

ifneq ($(MAKECMDGOALS),clean)
include $(BIN)/render-config.mk
endif

TARGETS+=$(BIN)/SDLVideo.smd
$(BIN)/$(SRC)/lib_media/render/sdl_video.cpp.o: CFLAGS+=$(RENDER_CFLAGS)
$(BIN)/SDLVideo.smd: LDFLAGS+=$(RENDER_LDFLAGS)
$(BIN)/SDLVideo.smd: \
  $(BIN)/$(SRC)/lib_media/render/sdl_video.cpp.o

TARGETS+=$(BIN)/SDLAudio.smd
$(BIN)/$(SRC)/lib_media/render/sdl_audio.cpp.o: CFLAGS+=$(RENDER_CFLAGS)
$(BIN)/SDLAudio.smd: LDFLAGS+=$(RENDER_LDFLAGS)
$(BIN)/SDLAudio.smd: \
  $(BIN)/$(SRC)/lib_media/render/sdl_audio.cpp.o \
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o

