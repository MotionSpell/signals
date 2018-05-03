MYDIR=$(call get-my-dir)
OUTDIR:=$(BIN)/$(call get-my-dir)
TESTOUTDIR:=$(CURDIR)/$(OUTDIR)

TEST_COMMON_SRCS:=\
	$(MYDIR)/tests.cpp

#---------------------------------------------------------------
# test_utils.exe
#---------------------------------------------------------------
EXE_UTILS_SRCS:=\
	$(MYDIR)/utils.cpp\
	$(LIB_UTILS_SRCS)\
	$(TEST_COMMON_SRCS)
TARGETS+=$(OUTDIR)/test_utils.exe
$(OUTDIR)/test_utils.exe: $(EXE_UTILS_SRCS:%=$(BIN)/%.o)
TESTS+=$(TESTOUTDIR)/test_utils.exe
TESTS_DIR+=$(CURDIR)/$(SRC)/tests

#---------------------------------------------------------------
# test_signals.exe
#---------------------------------------------------------------
EXE_SIGNALS_SRCS:=\
	$(MYDIR)/signals.cpp\
	$(TEST_COMMON_SRCS)
TARGETS+=$(OUTDIR)/test_signals.exe
$(OUTDIR)/test_signals.exe: $(EXE_SIGNALS_SRCS:%=$(BIN)/%.o)
TESTS+=$(TESTOUTDIR)/test_signals.exe
TESTS_DIR+=$(CURDIR)/$(SRC)/tests

#---------------------------------------------------------------
# test_modules.exe
#---------------------------------------------------------------
EXE_MODULES_SRCS:=\
	$(TEST_COMMON_SRCS)\
	$(LIB_MEDIA_SRCS)\
	$(LIB_MODULES_SRCS)\
	$(LIB_UTILS_SRCS)

ifeq ($(SIGNALS_HAS_X11), 1)
  EXE_MODULES_SRCS+=$(MYDIR)/modules_generator.cpp
  EXE_MODULES_SRCS+=$(MYDIR)/modules_player.cpp
  EXE_MODULES_SRCS+=$(MYDIR)/modules_render.cpp
endif

EXE_MODULES_SRCS+=$(MYDIR)/modules_simple.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_converter.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_decode.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_demux.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_encoder.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_erasure.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_metadata.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_mux.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_rectifier.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_streamer.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_timings.cpp
EXE_MODULES_SRCS+=$(MYDIR)/modules_transcoder.cpp


TARGETS+=$(OUTDIR)/test_modules.exe
$(OUTDIR)/test_modules.exe: $(EXE_MODULES_SRCS:%=$(BIN)/%.o)
TESTS+=$(TESTOUTDIR)/test_modules.exe
TESTS_DIR+=$(CURDIR)/$(SRC)/tests

#---------------------------------------------------------------
# test_pipeline.exe
#---------------------------------------------------------------
EXE_PIPELINE_SRCS:=\
	$(MYDIR)/pipeline.cpp\
	$(TEST_COMMON_SRCS)\
	$(LIB_MEDIA_SRCS)\
	$(LIB_MODULES_SRCS)\
	$(LIB_PIPELINE_SRCS)\
	$(LIB_UTILS_SRCS)
TARGETS+=$(OUTDIR)/test_pipeline.exe
$(OUTDIR)/test_pipeline.exe: $(EXE_PIPELINE_SRCS:%=$(BIN)/%.o)
TESTS+=$(TESTOUTDIR)/test_pipeline.exe
TESTS_DIR+=$(CURDIR)/$(SRC)/tests

#---------------------------------------------------------------
# run tests
#---------------------------------------------------------------
pairup=$(if $1$2, cd $(firstword $1) ; $(firstword $2) || exit ; $(call pairup,$(wordlist 2,$(words $1),$1),$(wordlist 2,$(words $2),$2)))

run: targets
	$(call pairup, $(TESTS_DIR), $(TESTS))
