# Makefile for colocation scheduler plugin

AUTOMAKE_OPTIONS = foreign

AM_CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = sched_colocation.la

sched_colocation_la_SOURCES = \
			colocation_wrapper.c \
			colocation.c	\
			colocation.h
sched_colocation_la_LDFLAGS = $(PLUGIN_FLAGS)
