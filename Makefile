
# make -p -f /dev/null will tell you the default behavior
LDLIBS=-lmicrohttpd

all: svr

clean:
	rm -rf svr a.out
