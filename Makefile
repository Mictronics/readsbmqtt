DIALECT = -std=c11
CFLAGS += $(DIALECT) -O0 -g -W -D_DEFAULT_SOURCE -Wall -fno-common -Wmissing-declarations
LIBS = -lprotobuf-c -lpaho-mqtt3c
LDFLAGS =

all: protoc readsbmqtt

protoc: readsb.proto
	rm -f readsb.pb-c.c readsb.pb-c.h
	protoc-c --c_out=. $<

protoc-clean:
	rm -f readsb.pb-c.c readsb.pb-c.h
	
%.o: %.c *.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

readsb.pb-c.o: readsb.proto
	protoc-c --c_out=. $<
	$(CC) $(CPPFLAGS) $(CFLAGS) -c readsb.pb-c.c -o $@

readsbmqtt: readsb.pb-c.o readsbmqtt.o $(SDR_OBJ) $(COMPAT)
	$(CC) -g -o $@ $^ $(LDFLAGS) $(LIBS) $(LIBS_SDR)

clean:
	rm -f *.o  readsbmqtt readsb.pb-c.c readsb.pb-c.h
