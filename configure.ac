AC_PREREQ([2.71])
AC_INIT([ufser],[1.0],[zoltan.turanyi@ericsson.com])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects -Wno-portability])
AM_SILENT_RULES([yes])

: ${CXXFLAGS=""}
AC_PROG_CXX
LT_INIT

UF_CPPFLAGS=
UF_CXXFLAGS="-std=c++20 -ggdb3 -Wall -Wextra -Wswitch-enum"

AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [compile with debug options]))
AS_IF([test "x$enable_debug" = "xyes"],
      [UF_CXXFLAGS="$UF_CXXFLAGS -O0"],
      [UF_CPPFLAGS="$UF_CPPFLAGS -DNDEBUG"
       UF_CXXFLAGS="$UF_CXXFLAGS -Ofast"])

AC_ARG_ENABLE([sanitizer], AS_HELP_STRING([--enable-sanitizer], [build with AddressSanitizer @<:@default=no@:>@]))
AM_CONDITIONAL([NEED_SANITIZER], [test "$enable_sanitizer" = yes])

AC_CHECK_HEADER([boost/pfr.h], [AC_DEFINE([HAVE_BOOST_PFR])], [AC_MSG_WARN(['boost/pfr.h' not found on the path; omitting some convenience helper functions])])

dnl TODO tools for ser/deser, show, etc

AM_PATH_PYTHON([3],, [:])
AC_ARG_ENABLE([python], [AS_HELP_STRING([--enable-python], [build Python bindings @<:@default=no@:>@])])dnl , [], [with_python=no])
AC_ARG_VAR([PYTHON_CONFIG], [Path to python-config (autodetected)])
AS_IF([test "$enable_python" = yes],
      [AS_IF([test -z "$PYTHON_CONFIG"],
             [AC_PATH_PROGS([PYTHON_CONFIG], [python$PYTHON_VERSION-config python-config], [no])
              AS_IF([test "$PYTHON_CONFIG" = no], AC_MSG_ERROR([cannot find python-config; either install Python or specify PYTHON_CONFIG.]))
             ])
       AC_MSG_CHECKING([Python include flags])
       PYTHON_INCLUDE=`$PYTHON_CONFIG --includes`
       AC_MSG_RESULT([$PYTHON_INCLUDE])
       AC_SUBST([PYTHON_INCLUDE])
      ])
AM_CONDITIONAL([NEED_PYTHON], [test "$enable_python" = yes])

AC_ARG_ENABLE([benchmark], AS_HELP_STRING([--enable-benchmark], [build benchmark test @<:@default=no@:>@]))
AS_IF([test "$enable_benchmark"],
      [AC_CHECK_HEADER([benchmark/benchmark.h], [], [AC_MSG_ERROR(['benchmark/benchmark.h' is needed for the benchmarks])])
       AC_LANG_PUSH([C++])
       save="$LIBS"
       LIBS="$LIBS -lbenchmark"
       AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <benchmark/benchmark.h>], [benchmark::Shutdown()])],
                      [],
                      [AC_MSG_ERROR([benchmark program cannot be compiled])])
       LIBS="$save"
       AC_LANG_POP
      ])
AM_CONDITIONAL([NEED_BM], [test "$enable_benchmark" = yes])

AC_SUBST([AM_CPPFLAGS], [$UF_CPPFLAGS])
AC_SUBST([AM_CXXFLAGS], [$UF_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_CONFIG_FILES([
    Makefile
    Doxyfile
    tests/Makefile
])

AC_OUTPUT
