
libexec_PROGRAMS = whiteboard-sib-access
libexecdir = $(prefix)/lib/whiteboard/libexec

# Compiler flags
whiteboard_sib_access_CFLAGS  = -Wall -I$(top_srcdir)/include
whiteboard_sib_access_CFLAGS += @LIBWHITEBOARD_CFLAGS@ @LIBSSAP_PNG_CFLAGS@
whiteboard_sib_access_CFLAGS += @GNOME_CFLAGS@ 

if USEAVAHI
whiteboard_sib_access_CFLAGS += @AVAHI_CFLAGS@	
endif

whiteboard_sib_access_LDFLAGS = @LIBWHITEBOARD_LIBS@ @LIBSSAP_PNG_LIBS@ @GNOME_LIBS@
if USEAVAHI
whiteboard_sib_access_LDFLAGS += @AVAHI_LIBS@
endif

AM_CFLAGS = $(whiteboard_sib_access_CFLAGS) -g

# Put these in alphabetical order so that they are easy to find.
whiteboard_sib_access_SOURCES = \
	main.c \
	serverthread.c \
	sib_access.c \
	sib_controller.c \
	sib_server.c \
	sib_service.c 
