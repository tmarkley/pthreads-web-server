/* 
  Server-side use of Berkeley socket calls 
    Uses pthreads and IPC methods to receive and respond
*/

#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#define MAX 1000        // all of these numbers are large to handle browser requests
#define MAXTOKS 1000
#define MAXTOKSIZE 1000

#define MAXBUFF 1000

// the tdata struct is used for the pthreads to send data
//  to process_threads
struct tdata {
  int ssockd;                    // server socket file descriptor
  int *next_request_idp;         // pointer to next request id
  int thread_index;              // index of the thread when created
  pthread_mutex_t *accept_mutex; // mutex used for locking
  FILE *logfile;                 // file for logging
};

// this struct holds all of the tokens sent by the client
struct command {
  char **tok;
  int count;
  int status;
};

enum status_value {
  NORMAL, EOF_FOUND, INPUT_OVERFLOW, OVERSIZE_TOKEN, TOO_MANY_TOKENS
};

/********************** EXTRA FEATURE **************************

read_config reads the configuration file and sets
  the port, the logfile, and the number of workers,
  which are all passed by reference from main()

****************************************************************/

void read_config(int *port, FILE **logfile, int *NWORKERS) {
  
  FILE *config = fopen("config.txt", "r");
  if (config == NULL) {
    perror ("Error opening config file");
    exit(1);
  }
  // first we get the port
  char buffer[100];
  if (fgets(buffer, 100, config) == NULL) {
    perror ("Error reading config");
    exit(1);
  }
  // and replace the newline with a nullbyte
  int i;
  for (i = 0; i < 100; i++)
    if (buffer[i] == '\n') {
      buffer[i] = '\0';
      break;
    }

  *port = atoi(buffer+6); // add 6 because of 'port: '
  
  // now we get the log file name
  char buffer2[100];
  if (fgets(buffer2, 100, config) == NULL) {
    perror ("Error reading config");
    exit(1);
  }

  for (i = 0; i < 100; i++)
    if (buffer2[i] == '\n') {
      buffer2[i] = '\0';
      break;
    }
  
  // open the log file for appending
  FILE *log;
  log = fopen(buffer2+9, "a+");
  if (log == NULL) {
    perror("Error opening log file");
    exit(1);
  }
  else
    *logfile = log;

  // lastly, we get the number of workers
  char buffer3[20];
  if (fgets(buffer3, 20, config) == NULL) {
    perror("Error reading config");
    exit(1);
  }

  for (i = 0; i < 20; i++)
    if (buffer3[i] == '\n') {
      buffer3[i] = '\0';
      break;
    }

  *NWORKERS = atoi(buffer3+10);

  printf("Server connected on port %s, logging in %s.\n", buffer+6, buffer2+9);

  fclose(config);
}

/*
  @Name: read_command
  
  @arguments:
    struct command *n  A pointer to a command object that is
                         filled in this function to be used
                         later to fulfil requests

    const char *buff   A character array that represents the 
                         request that the user sent from the
			 client

  @return:
    int                int describing success (1) or failure (0)

  @Description:
    The main functionality of read_command is to get the user
    input, parse it, and fill the members of the command
    object.

*/

int read_command(struct command *n, const char *buff) {

  // each time we read a new line we need to reallocate
  //   memory for tok and set count and pipe_index
  //   equal to zero
  n->tok = (char **) calloc((MAXTOKS+1),sizeof(char *));
  n->count = 0;

  int j;
  int t = 0;
  int ti = 0;
  n->tok[0] = (char *) calloc((MAXTOKSIZE+1),sizeof(char));
  for (j=0; buff[j] != '\0'; j++) {
    // if the next character is not a space, copy it into tok
    if (!isspace(buff[j])) {
      n->tok[t][ti] = buff[j];
      ti++;
    }
    /* 
       else if the next token has not been filled with any
       characters (and we're reading a space), then skip 
       it because it's just a space after the previous token
    */
    else if (ti == 0)
      continue;
    /* 
       else we're reading a space right after the token, so
       stop adding characters to the current token and move
       on to the next one
    */
    else {
      // make sure that we can add a nullbyte
      if (ti < MAXTOKSIZE)
        n->tok[t][ti] = '\0';
      // if we can't then the token is too large
      else {
	n->status = OVERSIZE_TOKEN;
	return 0;
      }
      /* 
	 if we have room to read in another tok and the
	 next character in buff isn't the nullbyte, then
	 allocate memory for another token.
      */
      if ((t+1) < MAXTOKS && buff[j+1] != '\0') {
	n->tok[t+1] = (char *) malloc((MAXTOKSIZE+1) * sizeof(char));
      }
      /* 
	 else if we have read in too many tokens and there
	 is more input to be read, set status accordingly
      */
      else if ((t+1) == MAXTOKS && buff[j+1] != '\0') {
	n->status = TOO_MANY_TOKENS;
	return 0;
      }
      t++;
      ti = 0;
      n->count++;
    }
  }
  // if the last character in buff wasn't a newline character, then
  //   the input was too large (more than MAX)
  if (buff[(strlen(buff)-1)] != '\n') {
    n->status = INPUT_OVERFLOW;
    return 0;
  }
  // else everything was normal
  else {
    n->status = NORMAL;
    return 1;
  }

}

// sendHeader is used for all of the requests to send the header
//   information and log that information in the logfile
void sendHeader(int clientd, int code, const char *str, const char *time, int count, FILE *logfile, int request_id, int closeClient) {
  
  char protocol[MAXBUFF]; // array to store the protocol used
  char logbuff[MAXBUFF]; // array used for sending to log file
  int ret; // return value from a call
  // put the status code and status string in a char array
  ret = sprintf(protocol, "HTTP/1.1 %d %s", code, str);
  if (ret < 0) {
    perror("log file");
  }
  // add the request id and put everything in the logfile
  ret = sprintf(logbuff, "%d %s\n", request_id, protocol);
  if (ret < 0) {
    perror("log file");
  }
  else {
    if(fputs(logbuff, logfile) == EOF) {
      printf("errno: %d\n", errno);
      perror("fputs()");
      return;
    }
  }
  char buffer[MAXBUFF];
  // this puts all of the header information into one string
  ret = sprintf (buffer, "HTTP/1.1 %d %s\nDate: %s\nConnection: close\nContent-Type: text/html; charset=utf-8\nContent-Length: %d\n\n", code, str, time, count);
  if (ret < 0) {
    perror("sendHeader()");
  }
  // send the header back to the client
  if ((ret = send(clientd, buffer, strlen(buffer), 0)) < 0) {
    printf("sendHeader ");
    perror("send()");
  }
  // close the client connect if told so
  if (closeClient) {
    if ((ret = close(clientd)) < 0) {
      perror("close(clientd)");
    }
  }
}

// this function carries out a GET request
void process_GET(char **tok, int clientd, int request_id, FILE *logfile, char *timestamp) {

  FILE *inFile; // represents the file that the user is requesting
  char buffer[100];
  int response = 200; // response code
  
  inFile = fopen(tok[1]+1, "r");
  // if inFile returned NULL, we couldn't find the file
  if (inFile == NULL) {
    response = 404;
    sendHeader(clientd, response, "Not Found", timestamp, 0, logfile, request_id, 0);
    perror ("Error opening input file");
  }
  // otherwise, send the contents back to the client
  else {
    int ret;
    struct stat st; // this struct is used to get file statistics
    int fd = fileno(inFile);
    fstat(fd, &st);
    int size = st.st_size; // st_size is the number of chars
    sendHeader(clientd, response, "OK", timestamp, size, logfile, request_id, 0);
    while (!feof(inFile)) {
      if (fgets(buffer, 100, inFile) != NULL) {
	if ((ret = send(clientd, buffer, strlen(buffer), 0)) < 0) {
	  perror("send()");
	  break;
	}
      }
    }
    
    fclose(inFile);
  }
  shutdown(clientd, 2);
  
}

/******************* EXTRA FEATURE *************************

process_HEAD carries out a HEAD request

This is almost the same as process_GET except that it doesn't
send the file contents back to the client

************************************************************/

void process_HEAD(char **tok, int clientd, int request_id, FILE *logfile, char *timestamp) {

  FILE *inFile;
  
  inFile = fopen(tok[1]+1, "r");
  if (inFile == NULL) {
    sendHeader(clientd, 404, "Not Found", timestamp, 0, logfile, request_id, 0);
    perror ("Error opening input file");
  }
  else {
    struct stat st;
    int fd = fileno(inFile);
    fstat(fd, &st);
    int size = st.st_size;
    sendHeader(clientd, 200, "OK", timestamp, size, logfile, request_id, 0);
    fclose(inFile);
  }
  shutdown(clientd, 2);

}

/******************** EXTRA FEATURE ***********************

process_OPTIONS carries out an OPTIONS request.

This sends back a header that includes the request options
that the server implements.

***********************************************************/

void process_OPTIONS(int clientd, int request_id, FILE *logfile, char *time) {

  char protocol[MAXBUFF]; // array to store the protocol used
  char logbuff[MAXBUFF]; // array used for sending to log file
  int ret; // return value from a call
  ret = sprintf(protocol, "HTTP/1.1 200 OK");
  if (ret < 0) {
    perror("log file");
  }
  ret = sprintf(logbuff, "%d %s\n", request_id, protocol);
  if (ret < 0) {
    perror("log file");
  }
  else {
    if(fputs(logbuff, logfile) == EOF) {
      perror("fputs()");
    }
  }
  char buffer[MAXBUFF];

  ret = sprintf (buffer, "HTTP/1.1 200 OK\nDate: %s\nConnection: close\nAllow: OPTIONS, GET, HEAD, TRACE\nContent-Length: 0\n\n", time);
  if (ret < 0) {
    perror("sendHeader()");
  }

  if ((ret = send(clientd, buffer, strlen(buffer), 0)) < 0) {
    printf("sendHeader ");
    perror("send()");
  }
  shutdown(clientd, 2);

}

/******************** EXTRA FEATURE ***********************

process_TRACE carries out a TRACE request.

This sends the clients request back to it in the same format
that was sent, with an added header

***********************************************************/

void process_TRACE(char *message, int clientd, int request_id, FILE *logfile, char *timestamp) {

  int ret;
  sendHeader(clientd, 200, "OK", timestamp, strlen(message), logfile, request_id, 0);

  // message is the entire request (including hostname_
  if ((ret = send(clientd, message, strlen(message), 0)) < 0) {
    printf("trace ");
    perror("send()");
  }
  shutdown(clientd, 2);
}

/* process_requests is the pthread function that accepts all
   of the incoming messages from the client. pArg is cast
   as void *, but it holds a tdata struct. It has an infinite
   loop that doesn't end until the socket is shut down.
*/

void *process_requests(void *pArg) {

  struct tdata td = *((struct tdata *)pArg);
  FILE *logfile = td.logfile;
  int request_id;
  int response = 200; // HTML response code
  int clientd;  /* socket descriptor for communicating with client */
  struct sockaddr_in ca;
  socklen_t size = sizeof(struct sockaddr);

  while(1) {
    
    if ((clientd = accept(td.ssockd, (struct sockaddr*) &ca, &size)) < 0) {
      /**************** EXTRA FEATURE ****************
        The pthread only exits when accept return errno
        22, which means that the socket file descriptor
        is bad, meaning that it has been shut down.
        This is initiated when the user types 'exit'
        into the server.

        9 = Bad File Descriptor
        22 = Invalid Argument
        53 = Software caused connection abort

      ************************************************/
      if (errno == 22 || errno == 9 || errno == 53) { 
	      //errno returned for invalid argument when socket is closed
	      void *retval;
	      pthread_exit(retval);
	      free(retval);
      }
      printf("error %d", errno);
      perror("accept()");
      continue;
    }

    // make the assignment of request_id thread safe
    pthread_mutex_lock(td.accept_mutex);
    
    request_id = *(td.next_request_idp);
    (*(td.next_request_idp)) += 1;
    
    pthread_mutex_unlock(td.accept_mutex);
    
    char buff[MAXBUFF];  /* message receive buffer */
    int ret;  /* return value from a call */
    if ((ret = recv(clientd, buff, MAXBUFF-1, 0)) < 0) {
      perror("recv()");
      continue;
    }

    else if (ret == 0)
      continue;
    
    buff[ret] = '\0';  // add terminating nullbyte to received array of char
    
    printf("---------------Received message (%d chars):---------------\n%s\n", ret, buff);

    char logbuff[MAXBUFF];
    char protocol[MAXBUFF];
    // get the timestamp for log file and return to client
    char timestamp[30];
    time_t now = time(NULL);
    int ret_time = strftime(timestamp, 30, "%a, %d %b %Y %T %Z", gmtime(&now));
    if (!ret_time) {
      perror("strftime()");
      continue;
    }
    // add the first line of the command to the log file
    ret = sprintf (logbuff, "%d %d %s\n", request_id, td.thread_index, timestamp);
    if (ret < 0) {
      perror("log file");
    }
    else
      fputs(logbuff, logfile);
    
    // handle the received command
    struct command Tommy;
    
    int read = read_command(&Tommy, buff);
    
    // add the necessary line to the log file
    ret = sprintf(logbuff, "%d %s %s %s\n", request_id, Tommy.tok[0], Tommy.tok[1], Tommy.tok[2]);
    if (ret < 0) {
      perror("log file");
    }
    else
      fputs(logbuff, logfile);
    
    // if the user doesn't have a slash in the path, it's not a valid
    //   request.
    // the request also isn't valid if they don't include HTTP/1.1 as
    //   the third token
    if (Tommy.tok[1][0] != '/' ||
	strcmp("HTTP/1.1",Tommy.tok[2])) {
      // tell the client that they sent a bad request
      response = 400;
      sendHeader(clientd, response, "Bad Request", timestamp, 0, logfile, request_id, 1);
      //printf("\nIncorrect syntax!\n");
      shutdown(clientd, 2);
      continue;
    }
    
    if (!read) {
      // if read returns 0, there was a problem
      response = 400;
      sendHeader(clientd, response, "Bad Request", timestamp, 0, logfile, request_id, 1);
      // tell the server user what happened.
      switch(Tommy.status) {
      case(EOF_FOUND):
	printf("\nError: EOF_FOUND\n");
	break;
      case(INPUT_OVERFLOW):
	printf("Error: INPUT_OVERFLOW\n");
	break;
      case(OVERSIZE_TOKEN):
	printf("Error: OVERSIZE_TOKEN\n");
	break;
      case(TOO_MANY_TOKENS):
	printf("Error: TOO_MANY_TOKENS\n");
	break;
      default:
	printf("Unknown Error!\n");
      }
      shutdown(clientd, 2);
      continue;
    }
    else {
      // if read returned 1, then we successfully parsed through the input
      // now determine which request the client asked for
      if (!(strcmp("GET", Tommy.tok[0])))
	process_GET(Tommy.tok, clientd, request_id, logfile, timestamp);
      else if (!(strcmp("HEAD", Tommy.tok[0])))
	process_HEAD(Tommy.tok, clientd, request_id, logfile, timestamp);
      else if (!(strcmp("OPTIONS", Tommy.tok[0])))
	process_OPTIONS(clientd, request_id, logfile, timestamp);
      else if (!(strcmp("TRACE", Tommy.tok[0])))
	process_TRACE(buff, clientd, request_id, logfile, timestamp);
      else {
	// and if it wasn't one of the four that are implemented, tell them
	sendHeader(clientd, 501, "Not Implemented", timestamp, 0, logfile, request_id, 0);
	shutdown(clientd, 2);
      }
      
    }
    fputs("\n", logfile);
  }
}

int main(int argc, char **argv) {
  FILE *logfile; // log file descriptor
  int port;      // port number for communication
  int NWORKERS;  // number of worker threads to use
  int serverd;   // socket descriptor for receiving new connections
  int ret;       // return value from call
  
  // get information from configuration file
  read_config(&port, &logfile, &NWORKERS);

  // set up the socket
  if ((serverd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket()");
    return 1;
  }
  
  struct sockaddr_in sa;
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = INADDR_ANY;
  
  // have to bind and listen before we can accept a message
  if (bind(serverd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
    perror("bind()");
    return 1;
  }
  if (listen(serverd, 5) < 0) {
    perror("listen()");
    return 1;
  }

  printf("Waiting for an incoming connection...\n");
  printf("(type 'exit' to terminate server)\n");

  int next_request_id = 0;
  pthread_mutex_t gLock;

  // fill all of the data for the threads to use
  struct tdata tdarray[NWORKERS];
  int i;
  for (i = 0; i < NWORKERS; i++) {
    tdarray[i].ssockd = serverd;
    tdarray[i].next_request_idp = &next_request_id;
    tdarray[i].accept_mutex = &gLock;
    tdarray[i].thread_index = i;
    tdarray[i].logfile = logfile;
  }

  // initialize the pthreads
  pthread_t tHandles[NWORKERS];
  pthread_mutex_init(&gLock, NULL);
  int n;
  for (n = 0; n < NWORKERS; n++) {
    ret = pthread_create(&(tHandles[n]),        // Returned thread handle
			 NULL,                  // Thread Attributes
			 process_requests,      // Thread function
			 (void*)&(tdarray[n])); // Data for process_requests
  
    if (ret != 0) {
      perror("Error creating thread");
      return 1;
    }
  }

  /****************** EXTRA FEATURE ***********************

    the main thread waits for the user to type exit, and
    until then the workers keep waiting for a message
    in process_requests

  *********************************************************/
  char exit[10];
  while(1) {
    if (fgets(exit, 10, stdin) != NULL) {
      if (!strcmp("exit\n", exit)) {
	// once exit is typed, shut down socket
	// (this triggers the error in accept)
	shutdown(serverd, 2);
	if ((ret = close(serverd)) < 0) {
	  perror("close(serverd)");
	  return 1;
	}
	// and then join the threads to finish
	int j;
	for (j = 0; j < NWORKERS; j++) {
	  ret = pthread_join(tHandles[j], NULL);
	  if (ret != 0) {
	    perror("pthread_join() ");
	    return 1;
	  }
	}
	break;
      }
    }
  }

  printf("Closed server socket: Exiting.\n");
  fclose(logfile);
  
  return 0;
}
