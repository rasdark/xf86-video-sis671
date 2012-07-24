if DRI
lib_LTLIBRARIES=libSiSXvMC.la
libSiSXvMC_la_SOURCES = SiSXvMC.c \
			 SiSXvMC.h

libSiSXvMC_la_CFLAGS = @XORG_CFLAGS@ @DRI_CFLAGS@ -I$(top_srcdir)/src -DTRUE=1 -DFALSE=0
libSiSXvMC_la_LDFLAGS = -version-number 1:0:0
endif