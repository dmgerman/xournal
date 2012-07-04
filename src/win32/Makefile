all: xournal.res
all-recursive: all
install-recursive:
install:

xournal.res: xournal.rc
	windres xournal.rc -O coff -o xournal.res

distdir:
	cp -fp xournal.ico xournal.rc Makefile $(distdir)

distclean:
	rm -f xournal.res
