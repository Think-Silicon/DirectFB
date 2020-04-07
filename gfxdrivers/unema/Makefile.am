## Makefile.am for DirectFB/src/core/gfxcards/unema

INCLUDES = \
	-I$(top_builddir)/include	\
	-I$(top_srcdir)/include	\
	-I$(top_builddir)/lib	\
	-I$(top_srcdir)/lib	\
	-I$(top_srcdir)/src	\
	-I$(top_srcdir)/systems \
	-I$(prefix)/include/tsi/NemaGFX \
	-I$(prefix)/include/tsi/common

unema_LTLIBRARIES = libdirectfb_unema.la

if BUILD_STATIC
unema_DATA = $(unema_LTLIBRARIES:.la=.o)
endif

unemadir = $(MODULEDIR)/gfxdrivers

libdirectfb_unema_la_SOURCES =	\
	unema.c	\
	unema.h	

libdirectfb_unema_la_LDFLAGS = \
	-module			\
	-avoid-version		\
	$(DFB_LDFLAGS) \
	-lNemaGFX

libdirectfb_unema_la_LIBADD = \
	$(top_builddir)/lib/direct/libdirect.la \
	$(top_builddir)/src/libdirectfb.la


include $(top_srcdir)/rules/libobject.make

