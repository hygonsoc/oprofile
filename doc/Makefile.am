RM=rm
MANDIR=$(DESTDIR)@mandir@/man1

XSLTPROC=xsltproc
XHTML_STYLESHEET=xsl/xhtml.xsl
CHUNK_XHTML_STYLESHEET=xsl/xhtml-chunk.xsl
STYLESHEETS=$(CHUNK_XHTML_STYLESHEET) xsl/xhtml-common.xsl

generated_mans = oprofiled.1 op_start.1 op_stop.1 op_help.1 oprofpp.1 op_time.1 op_to_source.1 op_merge.1

man_MANS = oprofile.1 $(generated_mans)

htmldir = $(DESTDIR)$(prefix)/share/doc/oprofile
html_DATA = oprofile.html

$(generated_mans):
	echo ".so man1/oprofile.1" > $@

oprofile.html: oprofile.xml $(STYLESHEETS)
	$(XSLTPROC) $(XHTML_STYLESHEET) $< > $@

# rules to generate oprofile.sf.net/doc files
chunk: oprofile.xml $(STYLESHEETS)
	$(XSLTPROC) $(CHUNK_XHTML_STYLESHEET) $<

clean-local:
	$(RM) -f $(generated_mans)

# these files are not cleaned by make uninstall automake bug ?
uninstall-local:
	rm -f @mandir@/cat1/oprofile.1.gz
	@for f in $(LINK_LIST); do		\
		rm -f $(CATDIR)/cat1/$$f.gz;	\
	done

EXTRA_DIST = oprofile.1 oprofile.1.in oprofile.xml oprofile.html xsl/xhtml-chunk.xsl xsl/xhtml-common.xsl xsl/xhtml.xsl xsl/xhtml.xsl.in srcdoc/Doxyfile srcdoc/Makefile
