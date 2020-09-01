INCLUDES = \
        -I$(top_srcdir)/libdisasm

TESTS = \
        run-test.sh

EXTRA_DIST = \
	README \
	run-test.sh \
	asmdisasm.pl \
	ia32_test_insn.S

check_PROGRAMS = \
	insn-test \
	testdis \
	quikdis

testdis_SOURCES = \
	testdis.c
testdis_LDADD = \
	$(top_builddir)/libdisasm/libdisasm.la

quikdis_SOURCES = \
	quikdis.c
quikdis_LDADD = \
	$(top_builddir)/libdisasm/libdisasm.la

insn_test_SOURCES = \
	insn-test.c
insn_test_LDADD = \
	$(top_builddir)/libdisasm/libdisasm.la
