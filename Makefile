
# make -p -f /dev/null will tell you the default behavior
LDLIBS=-lmicrohttpd
CPPFLAGS=-g

all: svr

clean:
	rm -rf svr a.out
