AC_PREREQ([2.69])
AC_INIT([tpanar], [0.1.0], [miroslav.shaltev@shaltev.de])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

# Detect host OS for platform-specific configuration
AC_CANONICAL_HOST

# On FreeBSD, GCC mishandles wxWidgets' wchar_t/mbstate conversions at
# runtime.  Clang is the system compiler and must be used instead.
case "$host_os" in
    freebsd*)
        if test -z "$CXX"; then
            AC_CHECK_PROGS([CXX], [clang++ g++])
        fi
        if test -z "$CC"; then
            AC_CHECK_PROGS([CC], [clang gcc])
        fi
        ;;
esac

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Require C++17
AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])

# Enable debug option
AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug], [Enable debug build]),
    [debug_build=yes],
    [debug_build=no]
)

if test "x$debug_build" = "xyes"; then
    CXXFLAGS="$CXXFLAGS -g -O0 -DDEBUG"
else
    CXXFLAGS="$CXXFLAGS -O2"
fi

# pkg-config
PKG_PROG_PKG_CONFIG

# nlohmann/json
PKG_CHECK_MODULES([NLOHMANN_JSON], [nlohmann_json])

# JACK
PKG_CHECK_MODULES([JACK], [jack >= 1.9.0])

# wxWidgets
AC_CHECK_PROGS([WX_CONFIG], [wx-config wxgtk3u-3.2-config wxgtk3u-config])
if test -n "$WX_CONFIG"; then
    WXWIDGETS_CFLAGS=`$WX_CONFIG --cxxflags`
    WXWIDGETS_LIBS=`$WX_CONFIG --libs`
    AC_SUBST([WXWIDGETS_CFLAGS])
    AC_SUBST([WXWIDGETS_LIBS])
else
    AC_MSG_ERROR([wxWidgets not found.])
fi

# libsndfile
PKG_CHECK_MODULES([SNDFILE], [sndfile])

# libsamplerate
PKG_CHECK_MODULES([SAMPLERATE], [samplerate])

# FFTW3
PKG_CHECK_MODULES([FFTW3], [fftw3])

# libarchive
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive])

# ALSA
case "$host_os" in
    freebsd*)
        PKG_CHECK_MODULES([ALSA], [alsa], [], [AC_MSG_WARN([ALSA not found])])
        ;;
    *)
        PKG_CHECK_MODULES([ALSA], [alsa])
        ;;
esac

# FluidSynth
PKG_CHECK_MODULES([FLUIDSYNTH], [fluidsynth])

# SoundTouch
PKG_CHECK_MODULES([SNDTOUCH], [soundtouch])
AC_DEFINE([HAVE_SNDTOUCH], [1], [Define if sndtouch is available])

# Output files
AC_CONFIG_FILES([
Makefile
src/Makefile
])

AC_OUTPUT
