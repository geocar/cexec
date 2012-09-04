CFLAGS=-O9 $(EXTRA_CFLAGS)
all: cproxy ckeygen cservice cexec crat multipipe

ifeq ("$(HOSTCC)","")
HOSTCC=$(CC)
endif

CRYPTO=ec_crypt.o ec_curve.o ec_field.o ec_param.o ec_vlong.o \
       hash.o keyfile.o parity.o netnum.o sha1.o cio.o

cproxy: cproxy.o sockop.o $(LIBS)
crat: crat.o $(CRYPTO) strline.o sockop.o $(LIBS)
ckeygen: ckeygen.o sockop.o $(CRYPTO) $(LIBS)
cservice: cservice.o pair.o fdset_copy.o bio.o load.o strline.o sockop.o $(CRYPTO) $(LIBS)
cexec: cexec.o fdm.o netnum.o fdset_copy.o bio.o strline.o sockop.o $(CRYPTO) $(LIBS)
multipipe: multipipe.o pair.o fdset_copy.o fdm.o sockop.o $(LIBS)


cryptotest: cryptotest.o $(CRYPTO)

strline.o: strline.c strline.h
sockop.o: sockop.c sockop.h

ec_field.o: ec_field.c ecf_tables.c
ecf_tables.c: ec_field.c ec_vlong.c
	echo '#include <stdlib.h>' > ecfmaker.c
	echo 'main() { gf_init(); exit(0); }' >> ecfmaker.c
	$(HOSTCC) -DPRECOMPUTE_TABLES -o ecfmaker ecfmaker.c ec_field.c ec_vlong.c
	./ecfmaker > ecf_tables.c
	rm -f ecfmaker ecfmaker.c

test: cryptotest ckeygen
	$(HOSTCC) -DTEST_HARNESS -o fdm-test fdm.c
	touch x
	env MAXFD=50 ./fdm-test 5< x
	$(HOSTCC) -DLINUX -DTEST_HARNESS -o load load.c && ./load
	$(HOSTCC) -DSOLARIS -DFAKE_SOLARIS=1.00 -DTEST_HARNESS -o load load.c && ./load
	$(HOSTCC) -DSOLARIS -DFAKE_SOLARIS=1.01 -DTEST_HARNESS -o load load.c && ./load
	$(HOSTCC) -DSOLARIS -DFAKE_SOLARIS=1.10 -DTEST_HARNESS -o load load.c && ./load
	$(HOSTCC) -DSOLARIS -DFAKE_SOLARIS=9.10 -DTEST_HARNESS -o load load.c && ./load
	$(HOSTCC) -DSOLARIS -DFAKE_SOLARIS=90.10 -DTEST_HARNESS -o load load.c && ./load
	rm -f test.key test.key.pub test.key.pub2
	./ckeygen test.key test.key.pub
	./ckeygen test.key test.key.pub2
	cmp test.key.pub test.key.pub2
	./cryptotest
	rm -f x test.key test.key.pub test.key.pub2 cryptotest fdm-test load

clean:
	rm -f x fdm-test cexec cservice cryptotest ckeygen cproxy crat \
		test.key test.key.pub test.key.pub2 \
		multipipe *.o load ecf_tables.c ecfmaker ecfmaker.c
