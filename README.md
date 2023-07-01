# Asynchronous-Web-Server

- Implements a web server that uses advanced input/output operations:
    
       asynchronous operations on files;
       non-blocking operations on sockets;
       zero-copying;
       multiplexing I/O operations.

- The advanced I/O operations API specific to the Linux operating system is used for implementation:

       sendfile
       io_setup & friends
       epoll
- The web server uses the modern multiplexing API to wait for connections from clients: epoll (Linux). Requests from customers are received on the connections made and then the answers are distributed to them.

- The server implements a limited functionality of the HTTP protocol, that of passing files to clients. The server will provide files from the AWS_DOCUMENT_ROOT directory. Files are only found in subdirectories AWS_DOCUMENT_ROOT/static/ , respectively AWS_DOCUMENT_ROOT/dynamic/ , and corresponding request paths will be, for example, AWS_DOCUMENT_ROOT/static/test.dat , respectively AWS_DOCUMENT_ROOT/dynamic/test.dat.  File processing will be as follows:
  - The files in the AWS_DOCUMENT_ROOT/static/ directory are static files that will be sent to clients using the zero-copying API (sendfile)
  - Files in the AWS_DOCUMENT_ROOT/dynamic/ directory are files that are supposed to require a server-side post-processing phase. These files will be read from disk using the asynchronous API and then pushed to the clients. Streaming will use non-blocking sockets (Linux).
  -  An HTTP 404 message will be sent for invalid request paths.
 
- After transmitting a file, according to the HTTP protocol, the connection is closed.


## run_all.sh
	* script to run all tests defined in scripts in _test/

## == BUILDING ==

The local directory must contain the asynchronous web server executable
(aws). Use the Makefile to properly build the sockop_preload.so
library:

	make run

## == RUNNING ==

In order to run the test suite run the run_all.sh script.

The run_all.sh script runs all tests :

	./run_all.sh

In order to run a specific test ... pass the test number (1 .. 35) to
the _test/run_test.sh script.

Tests use the static/ and dynamic/ folders. These folders are created and
removed using the "init" and "cleanup" arguments to _test/run_test.sh.

Manually:

    ./aws
    in another terminal an http request is sent to the server

## Implementation
- in the connection structure I added:
  
         err variable: keeps if the http request sent by the socket contains a non-existent file
         newfd variable: descriptor for the file sent by request
         filesize variable: keep the size of the file
         variable after_parser: I keep whether the http request sent by the socket was parsed
         stat buffer structure: used to store the size of the file (used by the stat function)
  
- in the handle_new_connection function  added the fcntl function to make a socket non-blocking
- receive_message function

      check if the request for the socket has already been parsed (if so, I exit the function)
      read from the socket using the recv function and put it in the recv_buffer vector
      if recv_buffer contains two newlines ("\r\n\r\n") then  parse the request
      put in the path variable the relative path from the request path
      try to open the file with the created path
                 : if the file is opened, put in the vector send_buffer the message that the file exists and find out its size
                 : if the file does not open, change the err variable and put in the send_buffer vector the message that the file does not exist
      if the http request was a valid one (it contained "\r\n\r\n") return the message "STATE_DATA_RECEIVED"

- function handle_client_request
  
      use the receive_message function
      if the return value is "STATE_DATA_RECEIVED", then  add the socket for writing ("w_epoll_update_ptr_inout")

- send_message function:
  
      regardless of the message in send_buffer, send it on the socket (send function)
      if the message was an error (invalid file), stop the connection with the socket
      after sent the whole message that the requested file exists, send the content of the file with zero-copy (sendfile)
      after each use of the sendfile function, the return value of the sendfile function is subtracted from the size of the file
      change the offset of the file every time using the lseek function
      when the number of bytes read is equal to the number of bytes in the file
                      (that is, I sent all the content)  stop the connection with the socket

  
