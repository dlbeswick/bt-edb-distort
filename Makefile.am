ACLOCAL_AMFLAGS = -I m4

SRC = src/machine.c src/properties_simple.c

plugin_LTLIBRARIES = libbt_edb_distort.la

libbt_edb_distort_la_SOURCES = $(SRC)
libbt_edb_distort_la_CFLAGS = $(PKGCONFIG_DEPS_CFLAGS) $(OPTIMIZE_CFLAGS) \
	-std=gnu99 -Werror -Wno-error=unused-variable -Wall -Wshadow -Wpointer-arith -Wstrict-prototypes \
	-fvisibility=hidden
libbt_edb_distort_la_LDFLAGS = $(PKGCONFIG_DEPS_LIBS) -module -avoid-version

# Remove 'la' file as the generated lib isn't intended to be linked with others.
install-data-hook:
	$(RM) $(DESTDIR)$(plugindir)/libbt_edb_distort.la
#	$(LN_S) -f TBD.html  $(DESTDIR)$(userdocdir)/TBD.html

#userdoc_DATA = TBD.html
