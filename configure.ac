#############################################################################
#
#	WhiteBoard SIB access component
#
#	Copyright (c) 2007 Nokia Corporation
#
#############################################################################

AC_INIT()
AC_CONFIG_AUX_DIR(config)

AM_INIT_AUTOMAKE(whiteboard-sib-access, 2.0-beta1)
AM_CONFIG_HEADER(config.h)

#############################################################################
# Constants
#############################################################################

#############################################################################
# Checks for some common programs needed for configure/compile/install
#############################################################################

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_AWK
AC_PROG_LIBTOOL
AC_PROG_RANLIB

#############################################################################
# Check whether debug prints should be used
#############################################################################
AC_ARG_WITH(debug, 
	AS_HELP_STRING([--with-debug],
		       [Print additional debug messages (default = no)]),
	[AC_DEFINE([WHITEBOARD_DEBUG],[1],[Print debug messages])],
	[with_debug=no])


#############################################################################
# Check whether control point tracing should be used
#############################################################################
AC_ARG_WITH(cp-lock-trace, 
	AS_HELP_STRING([--with-cp-lock-trace],
		       [Trace cp locking and fail fast on errors, errors can be found also from /tmp/zenit* (default = no)]),
	[AC_DEFINE([WITH_THREAD_LOCK_TRACE],[1],[Trace cp locking])],
	[with_cp_lock_trace=no])

AC_ARG_ENABLE(avahi,
	AS_HELP_STRING([--enable-avahi],
			[enable Avahi service discovery (default = no)]),
	[
		if test $enableval = no; then
			USE_AVAHI=no
		else
			USE_AVAHI=yes
			AC_DEFINE([AVAHI],[1],[Use Avahi service discovery])
			PKG_CHECK_MODULES(AVAHI,
			[
				avahi-client,
				avahi-glib,
				avahi-gobject
			])
		fi
	],[
		USE_AVAHI=no
	]
)
AM_CONDITIONAL(USEAVAHI, test $USE_AVAHI=yes)
AC_SUBST(AVAHI_CFLAGS)
AC_SUBST(AVAHI_LIBS)

#############################################################################
# Check for some header files (TODO: Add more?)
#############################################################################
AC_HEADER_STDC
AC_CHECK_HEADERS([limits.h stdlib.h string.h unistd.h])

##############################################################################
# Check for iconv
##############################################################################
AC_CHECK_FUNCS([iconv])
AC_CHECK_HEADERS([iconv.h])
AC_CHECK_LIB([iconv],[main])

##############################################################################
# Check for pthread presence
##############################################################################
AC_CHECK_HEADERS([pthread.h],
		 [],
		 [AC_MSG_ERROR(WhiteBoard node needs pthread library)])
AC_CHECK_LIB([pthread],[main])

##############################################################################
# Check for CyberLink C & AV extension
##############################################################################
#[
#	clinkc >= 1.0-21
#	clinkcav >= 1.0-14
#])

#AC_SUBST(CLINKC_CFLAGS)
#AC_SUBST(CLINKC_LIBS)

##############################################################################
# Check for GNOME environment
##############################################################################
PKG_CHECK_MODULES(GNOME,
[
	glib-2.0 >= 2.8.6
	gobject-2.0 >= 2.8.6
	gthread-2.0 >= 2.8.6
	dbus-1 >= 0.61
	dbus-glib-1 >= 0.61
])

AC_SUBST(GNOME_CFLAGS)
AC_SUBST(GNOME_LIBS)

##############################################################################
# Check for OSSO/Hildon environment
##############################################################################
#case $host in
#	arm-*)
#PKG_CHECK_MODULES(MAEMO, 
#[
#	libosso >= 0.21
#])

#AC_SUBST(MAEMO_CFLAGS)
#AC_SUBST(MAEMO_LIBS)
#	;;
#esac


#PKG_CHECK_MODULES(CONIC, 
#[
#	conic >= 0.13
#])

#AC_SUBST(CONIC_CFLAGS)
#AC_SUBST(CONIC_LIBS)

##############################################################################
# Check for WhiteBoard library
##############################################################################
PKG_CHECK_MODULES(LIBWHITEBOARD,
[
	libwhiteboard >= 2.0-beta1,
	libwhiteboard-util >= 2.0-beta1
])

AC_SUBST(LIBWHITEBOARD_CFLAGS)
AC_SUBST(LIBWHITEBOARD_LIBS)

PKG_CHECK_MODULES(LIBSSAP_PNG,
[
	libssap_parse_n_gen >= 2.0-beta1
])

AC_SUBST(LIBSSAP_PNG_CFLAGS)
AC_SUBST(LIBSSAP_PNG_LIBS)

##############################################################################
# Checks for typedefs, structures, and compiler characteristics.
##############################################################################

AC_C_CONST
AC_C_INLINE
AC_STRUCT_TM

##############################################################################
# Locale stuff
##############################################################################

AC_DEFINE_UNQUOTED([LOCALEDIR], 
		   "${runtime_localedir}", [Runtime locale catalog files path])

##############################################################################
# Check compile target to be able to disable local player in emulator
##############################################################################

case $host in
        i386-* | i686-*)
               CFLAGS="$CFLAGS -Di686"
	       USE_i686=yes
	       USE_ARM=no
               ;;
        arm-*)
               CFLAGS="$CFLAGS -DARM"
	       USE_ARM=yes
	       USE_i686=no
               ;;
esac

AM_CONDITIONAL(ARM, test ${USE_ARM}=yes)
AM_CONDITIONAL(i686, test ${USE_i686}=yes)

##############################################################################
# Files to create
##############################################################################

AC_OUTPUT(
	Makefile \
	include/Makefile \
	src/Makefile \
)
echo "Debug logs: " ${with_debug}
echo "Avahi: " ${USE_AVAHI}
