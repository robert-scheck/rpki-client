include Makefile.configure

# If set to zero, privilege-dropping is disabled and RPKI_PRIVDROP_USER
# is not used.  Otherwise, privileges are dropped.

RPKI_PRIVDROP		= 1
RPKI_PRIVDROP_USER	= "_rpki-client"

# Command to invoke for rsync.  Must be in user's path.

RPKI_RSYNC_COMMAND	= "openrsync"

# Where to place output files.

RPKI_PATH_OUT_DIR	= "/var/db/rpki-client"

# Where repositories are stored.

RPKI_PATH_BASE_DIR	= "/var/cache/rpki-client"

# Where TAL files are found.

RPKI_TAL_DIR		= "/etc/rpki"

# OpenBSD uses the "eopenssl11" pkg-config package.
# Most other operating systems use just "openssl".

PKGNAME_OPENSSL		!= if [ "`uname -s`" = "OpenBSD" ]; then echo "eopenssl11"; else echo "openssl" ; fi
PKG_OPENSSL		?= $(PKGNAME_OPENSSL)

# You don't want to change anything after this.

OBJS		 = as.o \
		   cert.o \
		   cms.o \
		   compats.o \
		   crl.o \
		   io.o \
		   ip.o \
		   log.o \
		   mft.o \
		   output.o \
		   output-bird.o \
		   output-bgpd.o \
		   output-csv.o \
		   output-json.o \
		   roa.o \
		   rsync.o \
		   tal.o \
		   validate.o \
		   x509.o
ALLOBJS		 = $(OBJS) \
		   main.o \
		   test-cert.o \
		   test-mft.o \
		   test-roa.o \
		   test-tal.o
BINS		 = rpki-client \
		   test-cert \
		   test-mft \
		   test-roa \
		   test-tal
CFLAGS_OPENSSL	!= pkg-config --cflags $(PKG_OPENSSL) 2>/dev/null || echo ""
LIBS_OPENSSL	!= pkg-config --libs $(PKG_OPENSSL) 2>/dev/null || echo "-lssl -lcrypto"
CFLAGS		+= $(CFLAGS_OPENSSL)
LDADD		+= $(LIBS_OPENSSL)

# Linux needs its -lresolv for b64_* functions.
# IllumOS needs its socket library.

LDADD		+= $(LDADD_B64_NTOP) $(LDADD_LIB_SOCKET)

all: $(BINS) rpki-client.install.8

site.h: Makefile
	(echo "#define RPKI_RSYNC_COMMAND \"${RPKI_RSYNC_COMMAND}\"" ; \
	 echo "#define RPKI_PATH_OUT_DIR \"${RPKI_PATH_OUT_DIR}\"" ; \
	 echo "#define RPKI_PATH_BASE_DIR \"${RPKI_PATH_BASE_DIR}\"" ; \
	 echo "#define RPKI_PRIVDROP ${RPKI_PRIVDROP}" ; \
	 echo "#define RPKI_PRIVDROP_USER \"${RPKI_PRIVDROP_USER}\"" ; \
	 echo "#define RPKI_TAL_DIR \"${RPKI_TAL_DIR}\"" ; ) >$@

site.sed: Makefile
	(echo "s!@RPKI_RSYNC_COMMAND@!${RPKI_RSYNC_COMMAND}!g" ; \
	 echo "s!@RPKI_PATH_OUT_DIR@!${RPKI_PATH_OUT_DIR}!g" ; \
	 echo "s!@RPKI_PATH_BASE_DIR@!${RPKI_PATH_BASE_DIR}!g" ; \
	 echo "s!@RPKI_PRIVDROP_USER@!${RPKI_PRIVDROP_USER}!g" ; \
	 echo "s!@RPKI_TAL_DIR@!${RPKI_TAL_DIR}!g" ; ) >$@

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	$(INSTALL_PROGRAM) rpki-client $(DESTDIR)$(BINDIR)
	$(INSTALL_MAN) rpki-client.install.8 $(DESTDIR)$(MANDIR)/man8/rpki-client.8

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/rpki-client
	rm -f $(DESTDIR)$(MANDIR)/man8/rpki-client.8

rpki-client: $(OBJS) main.o
	$(CC) -o $@ main.o $(OBJS) $(LDFLAGS) $(LDADD)

test-tal: $(OBJS) test-tal.o
	$(CC) -o $@ test-tal.o $(OBJS) $(LDFLAGS) $(LDADD)

test-mft: $(OBJS) test-mft.o
	$(CC) -o $@ test-mft.o $(OBJS) $(LDFLAGS) $(LDADD)

test-roa: $(OBJS) test-roa.o
	$(CC) -o $@ test-roa.o $(OBJS) $(LDFLAGS) $(LDADD)

test-cert: $(OBJS) test-cert.o
	$(CC) -o $@ test-cert.o $(OBJS) $(LDFLAGS) $(LDADD)

clean:
	rm -f $(BINS) $(ALLOBJS) rpki-client.install.8 site.sed site.h

distclean: clean
	rm -f config.h config.log Makefile.configure

distcheck:
	mandoc -Tlint -Werror rpki-client.8
	rm -rf .distcheck
	mkdir .distcheck
	cp *.c extern.h rpki-client.8 configure Makefile .distcheck
	( cd .distcheck && ./configure PREFIX=prefix )
	( cd .distcheck && $(MAKE) )
	( cd .distcheck && $(MAKE) install )
	rm -rf .distcheck

regress:
	# Do nothing.

$(ALLOBJS): extern.h config.h site.h

rpki-client.install.8: rpki-client.8 site.sed
	sed -f site.sed rpki-client.8 >$@
