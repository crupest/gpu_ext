CHROME = google-chrome-stable
CHROME_FLAGS = --headless --disable-gpu --no-sandbox --print-to-pdf-no-header
PAPER_IMG = ../paper/img

FIGURES = gpu_stack_figure

PDFS = $(FIGURES:=.pdf)

all: $(PDFS)

%.pdf: %.html
	$(CHROME) $(CHROME_FLAGS) --print-to-pdf=$@ $<

install: $(PDFS)
	cp $(PDFS) $(PAPER_IMG)/

clean:
	rm -f $(PDFS)

.PHONY: all install clean
