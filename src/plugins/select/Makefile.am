# Makefile for node selection plugins

if WITH_CXX
BLUEGENE = bluegene
endif

SUBDIRS = other alps $(BLUEGENE) cons_res cons_tres cray linear serial

# Each plugin here needs a plugin_id, here are the currect plug_ids
# for each plugin.

# bluegene	   = 100
# cons_res	   = 101
# linear	   = 102
# bgq		   = 103 ==> Not currently used
# alps + linear    = 104
# alps + cons_res  = 105 ==> Not currently possible on Cray with ALPS/BASIL.
# serial           = 106
# cray + linear    = 107
# cray + cons_res  = 108
# cons_tres        = 109
# cray + cons_tres = 110
