
O_FILES := hostser.o crc16.o sric.o sric-gw.o sric-client.o \
	token-dummy.o token-dir.o token-msp.o token-10f.o \
	version-buf.o version-buf-data.o

all: libsric.a

libsric.a: ${O_FILES}
	msp430-ar r $@ $^

include depend

depend: *.c
	rm -f depend
	for file in $^; do \
		${CC} ${CFLAGS} -MM $$file -o - >> $@ ; \
	done ;

version-buf-data.c version-buf-data.h: make-version-buf.py
	./make-version-buf.py ../ version-buf-data

.PHONY: clean

clean:
	-rm -f *.o *.a
	-rm -f version-buf-data.c version-buf-data.h