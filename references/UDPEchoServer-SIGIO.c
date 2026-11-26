#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

enum sizeConstants {
  MAXSTRINGLENGTH = 128,
  BUFSIZE = 512,
};

void UseIdleTime();                // Execution during idle time
void SIGIOHandler(int signalType); // Handle SIGIO

int servSock; // Socket -- GLOBAL for signal handler

int main(int argc, char *argv[]) {

  if (argc != 2) { 
    fprintf(stderr, "Parameter(s): <Server Port/Service>");
    return 1;
  }

  char *service = argv[1]; // First arg:  local port

  // Construct the server address structure
  struct addrinfo addrCriteria;                   // Criteria for address
  memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
  addrCriteria.ai_family = AF_UNSPEC;             // Any address family
  addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
  addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram sockets
  addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP protocol

  struct addrinfo *servAddr; // List of server addresses
  int rtnVal = getaddrinfo(NULL, service, &addrCriteria, &servAddr);
  if (rtnVal != 0) {
    fprintf(stderr, "getaddrinfo() failed: %s\n", gai_strerror(rtnVal));
    return 1;
  }

  // Create socket for incoming connections
  servSock = socket(servAddr->ai_family, servAddr->ai_socktype, servAddr->ai_protocol);
  if (servSock < 0) {
    fprintf(stderr, "socket() failed");
    return 1;
  }

  // Bind to the local address
  if (bind(servSock, servAddr->ai_addr, servAddr->ai_addrlen) < 0) {
    fprintf(stderr, "bind() failed");
    close(servSock);
  }

  // Free address list allocated by getaddrinfo()
  freeaddrinfo(servAddr);

  struct sigaction handler;
  handler.sa_handler = SIGIOHandler; // Set signal handler for SIGIO
  // Create mask that mask all signals
  sigfillset(&handler.sa_mask);
  handler.sa_flags = 0;              // No flags

  if (sigaction(SIGIO, &handler, 0) < 0)
    fprintf(stderr, "sigaction() failed for SIGIO");

  // We must own the socket to receive the SIGIO message
  if (fcntl(servSock, F_SETOWN, getpid()) < 0)
    fprintf(stderr, "Unable to set process owner to us");

  // Arrange for nonblocking I/O and SIGIO delivery
  if (fcntl(servSock, F_SETFL, O_NONBLOCK | FASYNC) < 0)
    fprintf(stderr, "Unable to put client sock into non-blocking/async mode");

  // Go off and do real work; echoing happens in the background

  for (;;)
    UseIdleTime();
  // NOT REACHED
}

void UseIdleTime() {
  puts(".");
  sleep(3); // 3 seconds of activity
}

void SIGIOHandler(int signalType) {
  ssize_t numBytesRcvd;
  do { // As long as there is input...
    struct sockaddr_storage clntAddr;  // Address of datagram source
    socklen_t clntLen = sizeof(clntAddr); // Address length in-out parameter
    char buffer[MAXSTRINGLENGTH];      // Datagram buffer

    numBytesRcvd = recvfrom(servSock, buffer, MAXSTRINGLENGTH, 0, (struct sockaddr *) &clntAddr, &clntLen);
    if (numBytesRcvd < 0) {
      // Only acceptable error: recvfrom() would have blocked
      if (errno != EWOULDBLOCK){
        fprintf(stderr, "recvfrom() failed");
        exit(1);
      }
    } else {

      ssize_t numBytesSent = sendto(servSock, buffer, numBytesRcvd, 0,
          (struct sockaddr *) &clntAddr, sizeof(clntAddr));
        if (numBytesSent < 0)
          fprintf(stderr, "sendto() failed");
        else if (numBytesSent != numBytesRcvd)
          fprintf(stderr, "sendto() sent unexpected number of bytes");
    }
  } while (numBytesRcvd >= 0);
  // Nothing left to receive
}
