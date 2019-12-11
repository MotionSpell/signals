MYDIR=$(call get-my-dir)

LIB_MEDIA_SRCS:=\
  $(MYDIR)/common/expand_vars.cpp\
  $(MYDIR)/common/picture.cpp\
  $(MYDIR)/common/iso8601.cpp\
  $(MYDIR)/common/http_puller.cpp\
  $(MYDIR)/common/http_sender.cpp\
  $(MYDIR)/common/mpeg_dash_parser.cpp\
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
  $(MYDIR)/transform/rectifier.cpp\
  $(MYDIR)/utils/recorder.cpp\

PKGS+=\
  libcurl\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/AVCC2AnnexBConverter.smd
$(BIN)/AVCC2AnnexBConverter.smd: \
  $(BIN)/$(SRC)/lib_media/transform/avcc2annexb.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LibavMuxHLSTS.smd
$(BIN)/LibavMuxHLSTS.smd: PKGS+=libavutil libavcodec
$(BIN)/LibavMuxHLSTS.smd: \
  $(BIN)/$(SRC)/lib_media/stream/hls_muxer_libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/VideoConvert.smd
$(BIN)/VideoConvert.smd: PKGS+=libavcodec libavutil libswscale
$(BIN)/VideoConvert.smd: \
  $(BIN)/$(SRC)/lib_media/transform/video_convert.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/AudioConvert.smd
$(BIN)/AudioConvert.smd: PKGS+=libavutil libavcodec libswresample
$(BIN)/AudioConvert.smd: \
  $(BIN)/$(SRC)/lib_media/transform/audio_convert.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/JPEGTurboDecode.smd
$(BIN)/JPEGTurboDecode.smd: PKGS+=libturbojpeg
$(BIN)/JPEGTurboDecode.smd: \
  $(BIN)/$(SRC)/lib_media/decode/jpegturbo_decode.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/JPEGTurboEncode.smd
$(BIN)/JPEGTurboEncode.smd: PKGS+=libturbojpeg
$(BIN)/JPEGTurboEncode.smd: \
  $(BIN)/$(SRC)/lib_media/encode/jpegturbo_encode.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/Encoder.smd
$(BIN)/Encoder.smd: PKGS+=libavcodec libavutil
$(BIN)/Encoder.smd: \
  $(BIN)/$(SRC)/lib_media/encode/libav_encode.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav_init.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/Decoder.smd
$(BIN)/Decoder.smd: PKGS+=libavcodec libavutil
$(BIN)/Decoder.smd: \
  $(BIN)/$(SRC)/lib_media/decode/decoder.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LibavDemux.smd
$(BIN)/LibavDemux.smd: PKGS+=libavformat libavcodec libavutil libavdevice
$(BIN)/LibavDemux.smd: \
  $(BIN)/$(SRC)/lib_media/common/libav_init.cpp.o\
  $(BIN)/$(SRC)/lib_media/demux/libav_demux.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\
  $(BIN)/$(SRC)/lib_media/transform/restamp.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LibavMux.smd
$(BIN)/LibavMux.smd: PKGS+=libavformat libavcodec libavutil
$(BIN)/LibavMux.smd: \
  $(BIN)/$(SRC)/lib_media/mux/libav_mux.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav_init.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/LibavFilter.smd
$(BIN)/LibavFilter.smd: PKGS+=libavfilter libavcodec libavutil
$(BIN)/LibavFilter.smd: \
  $(BIN)/$(SRC)/lib_media/transform/libavfilter.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/picture.cpp.o\
  $(BIN)/$(SRC)/lib_media/common/libav.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/GPACMuxMP4.smd
$(BIN)/GPACMuxMP4.smd: PKGS+=gpac
$(BIN)/GPACMuxMP4.smd: \
  $(BIN)/$(SRC)/lib_media/mux/gpac_mux_mp4.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/GPACMuxMP4MSS.smd
$(BIN)/GPACMuxMP4MSS.smd: PKGS+=gpac
$(BIN)/GPACMuxMP4MSS.smd: \
  $(BIN)/$(SRC)/lib_media/mux/gpac_mux_mp4_mss.cpp.o\
  $(BIN)/$(SRC)/lib_media/mux/gpac_mux_mp4.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/GPACDemuxMP4Simple.smd
$(BIN)/GPACDemuxMP4Simple.smd: PKGS+=gpac
$(BIN)/GPACDemuxMP4Simple.smd: \
  $(BIN)/$(SRC)/lib_media/demux/gpac_demux_mp4_simple.cpp.o\

#------------------------------------------------------------------------------
TARGETS+=$(BIN)/GPACDemuxMP4Full.smd
$(BIN)/GPACDemuxMP4Full.smd: PKGS+=gpac
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
