
O_FILES: hostser.o crc16.o sric.o

all: ${O_FILES}

include depend

depend: *.c
	rm -f depend
	for file in $^; do \
		${CC} ${CFLAGS} -MM $$file -o - >> $@ ; \
	done ;

PHONY: clean

clean:
	-rm *.o
