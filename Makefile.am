## Process this file with automake to produce Makefile.in

SUBDIRS = src

EXTRA_DIST = \
	autogen.sh \
	xournal.glade \
	xournal.gladep

install-data-local:
	@$(NORMAL_INSTALL)
	if test -d $(srcdir)/pixmaps; then \
	  $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/pixmaps; \
	  for pixmap in $(srcdir)/pixmaps/*; do \
	    if test -f $$pixmap; then \
	      $(INSTALL_DATA) $$pixmap $(DESTDIR)$(pkgdatadir)/pixmaps; \
	    fi \
	  done \
	fi; \
	if test -d $(srcdir)/html-doc; then \
	  $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/html-doc; \
	  for docfile in $(srcdir)/html-doc/*; do \
	    if test -f $$docfile; then \
	      $(INSTALL_DATA) $$docfile $(DESTDIR)$(pkgdatadir)/html-doc; \
	    fi \
	  done; \
	  if test ! -e $(DESTDIR)$(pkgdatadir)/html-doc/pixmaps; then \
	    ln -s ../pixmaps $(DESTDIR)$(pkgdatadir)/html-doc/pixmaps; \
	  fi \
	fi

dist-hook:
	if test -d pixmaps; then \
	  mkdir $(distdir)/pixmaps; \
	  for pixmap in pixmaps/*; do \
	    if test -f $$pixmap; then \
	      cp -p $$pixmap $(distdir)/pixmaps; \
	    fi \
	  done \
	fi; \
	if test -d html-doc; then \
	  mkdir $(distdir)/html-doc; \
	  for docfile in html-doc/*; do \
	    if test -f $$docfile; then \
	      cp -p $$docfile $(distdir)/html-doc; \
	    fi \
	  done; \
	  if test ! -e $(distdir)/html-doc/pixmaps; then \
	    ln -s ../pixmaps $(distdir)/html-doc/pixmaps; \
	  fi \
	fi

