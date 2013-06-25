if WIN32
no_undefined = -no-undefined
endif

INCLUDES = \
	-I$(top_srcdir) 				\
	-I$(top_builddir)				\
	$(WARN_CFLAGS) 					\
	$(LIBGNOMECANVAS_CFLAGS)			\
	$(LIBGNOMECANVAS_DEPRECATED_CFLAGS)		\
	@PACKAGE_CFLAGS@				\
        -DGNOMECANVASLIBDIR=\""$(libdir)"\" 		\
        -DGNOMECANVASDATADIR=\""$(datadir)"\" 		\
        -DGNOMECANVASPIXMAPDIR=\""$(datadir)/pixmaps"\"	\
        -DGNOMECANVASBINDIR=\""$(bindir)"\" 		\
        -DGNOMECANVASLOCALSTATEDIR=\""$(localstatedir)"\" 	\
	-DG_LOG_DOMAIN=\"GnomeCanvas\"


noinst_LIBRARIES = libgnomecanvas.a

libgnomecanvas_a_SOURCES = \
	$(libgnomecanvas_c_sources) $(libgnomecanvas_public_h_sources)

libgnomecanvas_public_h_sources =			\
	gnome-canvas-shape.h			\
	gnome-canvas-bpath.h			\
	gnome-canvas-clipgroup.h		\
	gnome-canvas-line.h			\
	gnome-canvas-pixbuf.h			\
	gnome-canvas-polygon.h			\
	gnome-canvas-rect-ellipse.h		\
	gnome-canvas-text.h			\
	gnome-canvas-rich-text.h		\
	gnome-canvas-util.h			\
	gnome-canvas-widget.h			\
	gnome-canvas-path-def.h			\
	gnome-canvas.h				\
	libgnomecanvas.h

libgnomecanvas_private_h_sources = 	\
	gnome-canvas-shape-private.h	\
	gailcanvas.h			\
	gailcanvasitem.h		\
	gailcanvasgroup.h		\
	gailcanvaswidget.h		\
	gailcanvastext.h		\
	gailcanvasitemfactory.h		\
	gailcanvasgroupfactory.h	\
	gailcanvaswidgetfactory.h	\
	gailcanvastextfactory.h

libgnomecanvas_c_sources =			\
	gnome-canvas-shape.c			\
	gnome-canvas-bpath.c			\
	gnome-canvas-path-def.c			\
	gnome-canvas-clipgroup.c		\
	gnome-canvas-i18n.h			\
	gnome-canvas-line.c			\
	gnome-canvas-pixbuf.c			\
	gnome-canvas-polygon.c			\
	gnome-canvas-rect-ellipse.c		\
	gnome-canvas-text.c			\
	gnome-canvas-rich-text.c		\
	gnome-canvas-util.c			\
	gnome-canvas-widget.c			\
	gnome-canvas.c				\
	libgnomecanvastypes.c			\
	gailcanvas.c				\
	gailcanvasitem.c			\
	gailcanvasgroup.c			\
	gailcanvaswidget.c			\
	gailcanvastext.c			\
	gailcanvasitemfactory.c			\
	gailcanvasgroupfactory.c		\
	gailcanvaswidgetfactory.c		\
	gailcanvastextfactory.c	


$(libgnomecanvas_a_OBJECTS): $(marshal_sources)

marshal_sources =				\
	gnome-canvas-marshal.c			\
	gnome-canvas-marshal.h

BUILT_SOURCES =					\
	$(marshal_sources)

gnome-canvas-marshal.h: gnome-canvas-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=gnome_canvas_marshal > $@
gnome-canvas-marshal.c: gnome-canvas-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --body --prefix=gnome_canvas_marshal > $@

CLEANFILES = $(marshal_sources)

EXTRA_DIST = \
	gnome-canvas-marshal.list
