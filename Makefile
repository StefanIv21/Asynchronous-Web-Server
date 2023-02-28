CC = gcc -g -Wall


build: aws

aws: aws.o sock_util.o http_parser.o
	$(CC)  aws.o sock_util.o http_parser.o -o aws 

aws.o: aws.c sock_util.h debug.h util.h
	 $(CC) -c aws.c

sock_util.o: sock_util.c sock_util.h
	$(CC) -c sock_util.c

http_parser.o:http_parser.c http_parser.h
	$(CC) -c http_parser.c

.PHONY: all clean

clean:
	-rm -f *.o aws

