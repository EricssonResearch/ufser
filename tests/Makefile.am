check_PROGRAMS = serialization wany
if NEED_BM
check_PROGRAMS += serperf
endif
AM_DEFAULT_SOURCE_EXT = .cc
AM_CPPFLAGS += -I$(top_srcdir) -UNDEBUG
AM_CXXFLAGS += -ggdb3 -O0
AM_LDFLAGS += -L$(top_builddir)
LDADD = ../libufser.la

serperf_CXXFLAGS = $(AM_CXXFLAGS) -Ofast
serperf_LDADD = $(LDADD) -lbenchmark

if NEED_SANITIZER
AM_CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer 
AM_LDFLAGS += -fsanitize=address -static-libasan
endif

export PYTHONPATH = $(top_builddir)/python/.libs

TESTS = $(check_PROGRAMS)

if NEED_PYTHON
check-local:
	@$(PYTHON) -m doctest $(srcdir)/uf.doctest
endif
