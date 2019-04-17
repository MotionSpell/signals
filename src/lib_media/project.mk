MYDIR=$(call get-my-dir)

LIB_MEDIA_SRCS:=\
  $(MYDIR)/common/picture.cpp\
  $(MYDIR)/common/iso8601.cpp\
  $(MYDIR)/common/http_puller.cpp\
  $(MYDIR)/common/http_sender.cpp\
  $(MYDIR)/demux/dash_demux.cpp\
  $(MYDIR)/in/file.cpp\
  $(MYDIR)/in/mpeg_dash_input.cpp\
  $(MYDIR)/in/sound_generator.cpp\
  $(MYDIR)/in/video_generator.cpp\
  $(MYDIR)/out/file.cpp\
  $(MYDIR)/out/http.cpp\
  $(MYDIR)/out/http_sink.cpp\
  $(MYDIR)/out/null.cpp\
  $(MYDIR)/out/print.cpp\
  $(MYDIR)/stream/apple_hls.cpp\
  $(MYDIR)/stream/ms_hss.cpp\
  $(MYDIR)/stream/adaptive_streaming_common.cpp\
  $(MYDIR)/transform/audio_gap_filler.cpp\
  $(MYDIR)/transform/restamp.cpp\
  $(MYDIR)/transform/telx2ttml.cpp\
  $(MYDIR)/transform/rectifier.cpp\
  $(MYDIR)/utils/recorder.cpp\
  $(MYDIR)/utils/repeater.cpp

PKGS+=\
  libcurl\

$(BIN)/media-config.mk: $(SRC)/../scripts/configure
	@mkdir -p $(BIN)
	$(SRC)/../scripts/configure --scope MEDIA_ gpac libswresample libavutil libavcodec libavdevice libavformat libavfilter libswscale libturbojpeg > "$@"

ifneq ($(MAKECMDGOALS),clean)
include $(BIN)/media-config.mk
endif

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/AVCC2AnnexBConverter.smd
$(BIN)/AVCC2AnnexBConverter.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/AVCC2AnnexBConverter.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/AVCC2AnnexBConverter.smd: \
  $(BIN)/$(SRC)/lib_media/transform/avcc2annexb.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LibavMuxHLSTS.smd
$(BIN)/LibavMuxHLSTS.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/LibavMuxHLSTS.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/LibavMuxHLSTS.smd: \
  $(BIN)/$(SRC)/lib_media/stream/hls_muxer_libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/VideoConvert.smd
$(BIN)/VideoConvert.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/VideoConvert.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/VideoConvert.smd: \
  $(BIN)/$(SRC)/lib_media/transform/video_convert.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/AudioConvert.smd
$(BIN)/AudioConvert.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/AudioConvert.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/AudioConvert.smd: \
  $(BIN)/$(SRC)/lib_media/transform/audio_convert.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/JPEGTurboDecode.smd
$(BIN)/JPEGTurboDecode.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/JPEGTurboDecode.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/JPEGTurboDecode.smd: \
  $(BIN)/$(SRC)/lib_media/decode/jpegturbo_decode.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/JPEGTurboEncode.smd
$(BIN)/JPEGTurboEncode.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/JPEGTurboEncode.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/JPEGTurboEncode.smd: \
  $(BIN)/$(SRC)/lib_media/encode/jpegturbo_encode.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/Encoder.smd
$(BIN)/Encoder.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/Encoder.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/Encoder.smd: \
  $(BIN)/$(SRC)/lib_media/encode/libav_encode.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav_init.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/Decoder.smd
$(BIN)/Decoder.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/Decoder.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/Decoder.smd: \
  $(BIN)/$(SRC)/lib_media/decode/decoder.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LibavDemux.smd
$(BIN)/LibavDemux.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/LibavDemux.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/LibavDemux.smd: \
  $(BIN)/$(SRC)/lib_media/common/libav_init.cpp.o\
  $(BIN)/$(SRC)/lib_media/demux/libav_demux.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/transform/restamp.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LibavMux.smd
$(BIN)/LibavMux.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/LibavMux.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/LibavMux.smd: \
  $(BIN)/$(SRC)/lib_media/mux/libav_mux.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav_init.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LibavFilter.smd
$(BIN)/LibavFilter.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/LibavFilter.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/LibavFilter.smd: \
  $(BIN)/$(SRC)/lib_media/transform/libavfilter.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/GPACMuxMP4.smd
$(BIN)/GPACMuxMP4.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/GPACMuxMP4.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/GPACMuxMP4.smd: \
  $(BIN)/$(SRC)/lib_media/mux/gpac_mux_mp4.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/GPACMuxMP4MSS.smd
$(BIN)/GPACMuxMP4MSS.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/GPACMuxMP4MSS.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/GPACMuxMP4MSS.smd: \
  $(BIN)/$(SRC)/lib_media/mux/gpac_mux_mp4_mss.cpp.o\
  $(BIN)/$(SRC)/lib_media/mux/gpac_mux_mp4.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/MPEG_DASH.smd
$(BIN)/MPEG_DASH.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/MPEG_DASH.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/MPEG_DASH.smd: \
  $(BIN)/$(SRC)/lib_media/stream/mpeg_dash.cpp.o\
  $(BIN)/$(SRC)/lib_media/stream/adaptive_streaming_common.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/GPACDemuxMP4Simple.smd
$(BIN)/GPACDemuxMP4Simple.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/GPACDemuxMP4Simple.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/GPACDemuxMP4Simple.smd: \
  $(BIN)/$(SRC)/lib_media/demux/gpac_demux_mp4_simple.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/GPACDemuxMP4Full.smd
$(BIN)/GPACDemuxMP4Full.smd: LDFLAGS+=$(MEDIA_LDFLAGS)
$(BIN)/GPACDemuxMP4Full.smd: CFLAGS+=$(MEDIA_CFLAGS)
$(BIN)/GPACDemuxMP4Full.smd: \
  $(BIN)/$(SRC)/lib_media/demux/gpac_demux_mp4_full.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/FileSystemSink.smd
$(BIN)/FileSystemSink.smd: \
  $(BIN)/$(SRC)/lib_media/out/filesystem.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LogoOverlay.smd
$(BIN)/LogoOverlay.smd: \
  $(BIN)/$(SRC)/lib_media/transform/logo_overlay.cpp.o\

#------------------------------------------------------------------------------
# Warning derogations. TODO: make this list empty
$(BIN)/$(SRC)/lib_media/demux/libav_demux.cpp.o: CFLAGS+=-Wno-deprecated-declarations
