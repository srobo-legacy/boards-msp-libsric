
O_FILES := hostser.o crc16.o sric.o sric-gw.o

all: libsric.a

libsric.a: ${O_FILES}
	msp430-ar r $@ $^

include depend

depend: *.c
	rm -f depend
	for file in $^; do \
		${CC} ${CFLAGS} -MM $$file -o - >> $@ ; \
	done ;

PHONY: clean

clean:
	-rm *.o
