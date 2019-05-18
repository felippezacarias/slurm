# Makefile for select/linear_colocation plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_linear_colocation.la

# Linear node selection plugin.
select_linear_colocation_la_SOURCES = select_linear_colocation.c select_linear_colocation.h
select_linear_colocation_la_LDFLAGS = $(PLUGIN_FLAGS)

