/* Client-side use of Berkeley socket calls with proper HTTP protocols
   Uses config.txt for settings.
   Implemented HTTP requests are:
     1. GET
     2. OPTIONS
     3. TRACE
     4. HEAD

   Must include the host name. Ex. request:
     GET /client.c HTTP/1.1
     Host: myNetwork2000-100
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define MAXBUFF 250 // maximum size of input to be sent/received

/****************** EXTRA FEATURE ***********************

read_port reads the configuration file to get the port number

*********************************************************/

int read_port() {

  // change this line if the name of the configuration file
  //   is different
  FILE *config = fopen("config.txt", "r");
  if (config == NULL) {
    perror ("Error opening config file");
    return 0;
  }

  char buffer[100];
  if (fgets(buffer, 100, config) == NULL) {
    perror ("Error reading port");
    return 0;
  }

  fclose(config);

  return atoi(buffer+6); // add 6 to pointer because of 'port: '
}

int main(int argc, char **argv) {
  char host[MAXBUFF]; // holds the name of the host
  int port; // port to send/receive on
  int sd;  /* socket descriptor */
  int ret;  /* return value from a call */

  if (!(port = read_port())) {
    printf ("Error reading the port.\n");
    return 1;
  }

  // get request line
  char buff[MAXBUFF];  /* message buffer */
  printf("Enter your request:\n");
  if (fgets(buff, MAXBUFF, stdin) == NULL) {
    printf("Error or end of input -- aborting\n");
    return 1;
  }
  // get hostname line
  if (fgets(host, MAXBUFF, stdin) == NULL) {
    printf("Error or end of input -- aborting\n");
    return 1;
  }

  // combine request and hostname into one array
  char message[MAXBUFF];
  if ((ret = sprintf(message, "%s%s", buff, host)) < 0) {
    perror("sprintf()");
    return 1;
  }
  
  // need to replace the newline character with a nullbyte
  int i=0;
  for (; i < strlen(host); i++) 
    if (host[i] == '\n') {
      host[i] = 0;
      break;
    }

  printf("\n");
  
  // get the socket descriptor
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket()");
    return 1;
  }
  
  // set up host name
  struct hostent *hp;
  struct sockaddr_in sa;
  // we add six to pointer because they enter 'Host: ' first
  if ((hp = gethostbyname(host+6)) == NULL) {
    perror("gethostbyname()");
    return 1;
  }
  memset((char *) &sa, '\0', sizeof(sa));
  memcpy((char *) &sa.sin_addr.s_addr, hp->h_addr, hp->h_length);

  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  
  // connect to the server
  if ((ret = connect(sd, (struct sockaddr *) &sa, sizeof(sa))) < 0) {
    perror("connect()");
    return 1;
  }
  
  // send the entire message to the server
  if ((ret = send(sd, message, strlen(message), 0)) < 0) {
    perror("send()");
    return 1;
  }
  
  // receive message from the server
  do {
    char buffer[MAXBUFF];
    if ((ret = recv(sd, buffer, MAXBUFF-1, 0)) < 0) {
      perror("recv()");
      return 1;
    }
    buffer[ret] = '\0';

    fputs(buffer, stdout);
  } while(ret != 0); // ret == 0 when message is complete

  // close the socket
  if ((ret = close(sd)) < 0) {
    perror("close()");
    return 1;
  }
  
  return 0;
}
