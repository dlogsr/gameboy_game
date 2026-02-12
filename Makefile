# Game Boy Color - Sliding Puzzle
# Built with GBDK-2020

GBDK_HOME = /opt/gbdk/
LCC = $(GBDK_HOME)bin/lcc

ROM_NAME = puzzle

# sm83:gb = Game Boy platform; -Wm-yC = CGB compatibility flag in ROM header
CFLAGS = -Wa-l -Wl-m -Wl-j -msm83:gb -Wm-yC
SRCDIR = src
RESDIR = res
BINDIR = bin

SOURCES = $(wildcard $(SRCDIR)/*.c)
RESOURCES = $(wildcard $(RESDIR)/*.c)
ALL_SRC = $(SOURCES) $(RESOURCES)

.PHONY: all clean

all: $(BINDIR)/$(ROM_NAME).gb

$(BINDIR)/$(ROM_NAME).gb: $(ALL_SRC) | $(BINDIR)
	$(LCC) $(CFLAGS) -o $@ $^

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR)
