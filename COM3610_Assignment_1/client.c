/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 100
#define PORT 5003
#define IP_ADDRESS 157.245.133.201

pthread_mutex_t the_mutex;
pthread_cond_t condc;
pthread_t* threads;
int clientfd;
char* file_name;
char* file_name_2;
void *multithreaded(void *ptr);
void *multithreaded2(void *ptr);

// Get host information (used to establishConnection)
struct addrinfo *getHostInfo(char* host, char* port) {
  int r;
  struct addrinfo hints, *getaddrinfo_res;
  // Setup hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if ((r = getaddrinfo(host, port, &hints, &getaddrinfo_res))) {
    fprintf(stderr, "[getHostInfo:21:getaddrinfo] %s\n", gai_strerror(r));
    return NULL;
  }

  return getaddrinfo_res;
}

// Establish connection with host
int establishConnection(struct addrinfo *info) {
  if (info == NULL) return -1;

  int clientfd;
  for (;info != NULL; info = info->ai_next) {
    if ((clientfd = socket(info->ai_family,
                           info->ai_socktype,
                           info->ai_protocol)) < 0) {
      perror("[establishConnection:35:socket]");
      continue;
    }

    if (connect(clientfd, info->ai_addr, info->ai_addrlen) < 0) {
      close(clientfd);
      perror("[establishConnection:42:connect]");
      continue;
    }

    freeaddrinfo(info);
    return clientfd;
  }

  freeaddrinfo(info);
  return -1;
}

// Send GET request
void GET(int clientfd, char *path) {
	char req[1000] = {0};
	sprintf(req, "GET %s HTTP/1.0\r\n\r\n", path);
	send(clientfd, req, strlen(req), 0);
}

int main(int argc, char **argv){
	if(argc != 6 && argc != 7){
		fprintf(stderr, "USAGE: ./httpclient <hostname> <port> <threads> <schedalg> <request path> <optional secondary request path>\n");
		return 1;
	}
	pthread_mutex_init(&the_mutex, 0);
	pthread_cond_init(&condc, 0);
	int threadNum = atoi(argv[3]);
	threads = calloc(threadNum, sizeof(pthread_t));
	clientfd = establishConnection(getHostInfo(argv[1], argv[2]));
	file_name = argv[5];
	if (clientfd == -1) {
		fprintf(stderr,
				"[main:73] Failed to connect to: %s:%s \n",
				argv[1], argv[2]);
		return 3;
	}
	if(argc == 7){
		file_name_2 = argv[6];
		for (int i = 0; i < threadNum; i++)
		{
			if(i % 2 == 0) pthread_create(threads+i, NULL, multithreaded, threads+i);
			else pthread_create(threads+i, NULL, multithreaded2, threads+i);
		}
	}else{
		for (int i = 0; i < threadNum; i++)
		{
			pthread_create(threads+i, NULL, multithreaded, threads+i);
		}
	}
	return 0;
}

void *multithreaded(void *thread) {
	char buf[BUF_SIZE];
	// Establish connection with <hostname>:<port>
	// Send GET request > stdout
	GET(clientfd, file_name);
	while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
		fputs(buf, stdout);
		memset(buf, 0, BUF_SIZE);
	}
	close(clientfd);
	pthread_exit(0);
}

void *multithreaded2(void *thread) {
	char buf[BUF_SIZE];
	// Establish connection with <hostname>:<port>
	// Send GET request > stdout
	GET(clientfd, file_name_2);
	while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
		fputs(buf, stdout);
		memset(buf, 0, BUF_SIZE);
	}
	close(clientfd);
	pthread_exit(0);
}
