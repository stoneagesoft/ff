.PHONY: all doxygen pdf docx clean changelog dots puml install-deps last-modified

timestamp := $(shell TZ=PST git log -1 --remove-empty --no-merges --format='%cd' --date="format-local:%Y%m%d" .)

all: doxygen

install-deps:
	sudo apt install doxygen doxygen-gui graphviz build-essential clang plantuml pandoc pandoc-plantuml-filter texlive-fonts-extra texlive-fonts-recommended texlive-lang-cyrillic texlive-latex-extra texlive-pstricks texlive-science

doxygen: dots puml copy-img
	doxygen

pdf-pandoc:	changelog dots puml
	pandoc -F pandoc-plantuml --resource-path=md -f markdown+smart md/*.md -o ff-$(timestamp).pdf

pdf: changelog puml dots copy-img last-modified
	doxygen
	cp tex/ff.sty latex/
	cd latex; make; mv refman.pdf ../ff-$(timestamp).pdf

docx:	changelog dots puml
	pandoc -F pandoc-plantuml --resource-path=md -f markdown+smart md/*.md -o ff-$(timestamp).docx

last-modified:
	$(shell env TZ=PST git log -1 --remove-empty --no-merges --format='%cd' --date="format:%B %d, %Y" . > last-modified.tex)

dots:
	if [ ! -d html/images ]; then \
		mkdir -p html/images; \
	fi
	for i in `find dot -name '*.dot'`; \
	do \
		j=html/images/`basename $$i`.png; \
		dot -Tpng "$$i" -o$$j; \
	done

puml:
	if [ ! -d html/images ]; then \
		mkdir -p html/images; \
	fi
	for i in `find puml -name '*.puml'`; \
	do \
		java -jar $(HOME)/bin/plantuml.jar -tpng "$$i" -o../html/images; \
	done

copy-img:
	mkdir -p html/images
	for i in `find md/images -type f`; \
	do \
		cp "$$i" html/images/; \
	done

changelog:
	echo '\\newpage' > md/01-changelog.md
	echo >> md/01-changelog.md
	echo "# Document Revisions" >> md/01-changelog.md
	echo "table: Document Revisions" >> md/01-changelog.md
	echo >> md/01-changelog.md
	echo "| **Date**               | **Author** | **Comments** |" >> md/01-changelog.md
	echo "| ----------------------------- | -------------------- | ----------------------------------------------- |" >> md/01-changelog.md
	git log --no-merges --date-order --format="| %cd | %cN | %s |" --date="format-local:%d/%m/%y %I:%M %p %Z" . >> md/01-changelog.md

clean:
	rm -rf html latex plantuml-images qch *.pdf *.epub *.docx md/01-changelog.md *.log tex/*.aux tex/*.fdb_latexmk tex/*.fls tex/*.log symbols.sql last-modified.tex
