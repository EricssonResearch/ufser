ACLOCAL_AMFLAGS = -Im4
SUBDIRS = . tests
AM_CPPFLAGS += $(PYTHON_INCLUDE)

if NEED_SANITIZER
AM_CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
AM_LDFLAGS += -fsanitize=address
endif

include_HEADERS = ufser.h wany.h

noinst_LTLIBRARIES = libufser.la
libufser_la_SOURCES = ufser.cpp 

pyexec_LTLIBRARIES =
python_ufser_la_SOURCES = python/uf_py_module.cpp python/uf_py.cpp
python_ufser_la_LIBADD = libufser.la
python_ufser_la_CXXFLAGS = $(AM_CXXFLAGS) -Wno-register -Wno-missing-field-initializers -Wno-cast-function-type
python_ufser_la_LDFLAGS = $(AM_LDFLAGS) -avoid-version -module
PYTHON_OTHER_DIR = $(DESTDIR)$(subst site-packages,dist-packages,$(pyexecdir))# different path is used in eg. Debian vs. Ubuntu

if NEED_PYTHON
pyexec_LTLIBRARIES += python/ufser.la
install-exec-hook:
	install -Dp -t $(PYTHON_OTHER_DIR) $(DESTDIR)$(pyexecdir)/ufser.so
uninstall-hook:
	$(RM) $(PYTHON_OTHER_DIR)/ufser.so || :
endif
