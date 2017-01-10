OUTDIR:=$(BIN)/$(ProjectName)

TARGETS+=$(OUTDIR)/dashcastx.exe
EXE_DASHCASTX_OBJS:=\
	$(LIB_MEDIA_OBJS)\
	$(LIB_MODULES_OBJS)\
	$(LIB_PIPELINE_OBJS)\
	$(UTILS_OBJS)\
 	$(OUTDIR)/dashcastx.o\
 	$(OUTDIR)/main.o\
 	$(OUTDIR)/options.o\
 	$(OUTDIR)/pipeliner.o
$(OUTDIR)/dashcastx.exe: $(EXE_DASHCASTX_OBJS)
DEPS+=$(EXE_DASHCASTX_OBJS:%.o=%.deps)
