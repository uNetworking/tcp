default:
	rm -f *.o
	$(CC) $(CFLAGS) -c loop.c context.c socket.c
	$(AR) rvs uSockets_userspace.a *.o
