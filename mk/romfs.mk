ROMDIR = $(DATDIR)/rom0
DAT += $(OUTDIR)/$(DATDIR)/rom0.o

$(OUTDIR)/$(ROMDIR).o: $(OUTDIR)/$(ROMDIR).bin
	@mkdir -p $(dir $@)
	@echo "    OBJCOPY "$@
	@$(CROSS_COMPILE)objcopy -I binary -O elf32-littlearm -B arm \
		--prefix-sections '.rom' $< $@

$(OUTDIR)/$(ROMDIR).bin: $(ROMDIR) $(OUTDIR)/$(TOOLDIR)/mkromfs
	@mkdir -p $(dir $@)
	@echo "    MKROMFS "$@
	@$(OUTDIR)/$(TOOLDIR)/mkromfs -d $< -o $@

$(ROMDIR):
	@mkdir -p $@

$(OUTDIR)/%/mkromfs: %/mkromfs.c
	@mkdir -p $(dir $@)
	@echo "    CC      "$@
	@gcc -Wall -o $@ $^
