#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404

pthread_mutex_t the_mutex;
pthread_cond_t condc;
int buffer = 0;
int init_threads, thread_count, completed_threads, MAX, dispatch_count;
time_t startTime;
suseconds_t otherStart;
char *order;
struct queue bufferQueue;

void *producer(void*ptr);
void *consumer(void*ptr);

struct
{
	char *ext;
	char *filetype;
} extensions[] = {
	{"gif", "image/gif"},
	{"jpg", "image/jpg"},
	{"jpeg", "image/jpeg"},
	{"png", "image/png"},
	{"ico", "image/ico"},
	{"zip", "image/zip"},
	{"gz", "image/gz"},
	{"tar", "image/tar"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{0, 0}};

struct node
{
	struct node *next;
	int call;
	int hit;
	int arrival_time;
	int arrival_count;
};

struct queue
{
	struct node *head;
	struct node *tail;
	int counter;
};

struct Thread
{
	pthread_t thread;
	int id;
	int http_request;
	int html_request;
	int img_request;
};

struct Thread* threads = NULL;



void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd;
	char logbuffer[BUFSIZE * 2];

	switch (type)
	{
		
	case ERROR:
		// printf("Why you no go here?");
		// printf("%d", logbuffer[0]);
		// printf(s1)
		(void)sprintf(logbuffer, "ERROR: %s:%s Errno=%d exiting pid=%d", s1, s2, errno, getpid());
		break;
	case FORBIDDEN:
		if(write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n", 271)){};
		(void)sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2);
		break;
	case NOTFOUND:
		if(write(socket_fd, "/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n", 224)){};
		(void)sprintf(logbuffer, "NOT FOUND: %s:%s", s1, s2);
		break;
	case LOG:
		(void)sprintf(logbuffer, " INFO: %s:%s:%d", s1, s2, socket_fd);
		break;
	}
	/* No checks here, nothing can be done with a failure anyway */
	if ((fd = open("nweb.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		if(write(fd, logbuffer, strlen(logbuffer))){};
		if(write(fd, "\n", 1)){};
		(void)close(fd);
	}
	if (type == ERROR || type == NOTFOUND || type == FORBIDDEN)
		exit(3);
}

/* this is a child web server process, so we can exit on errors */
void web(int fd, int hit, int arrivalCount, int arrivalTime, int dispatchTime, int dispatchCount, struct Thread* thread)
{
	struct timeval current_time;
	int j, file_fd, buflen, complete_time;
	long i, ret, len;
	char *fstr;
	static char buffer[BUFSIZE + 1]; /* static so zero filled */

	ret = read(fd, buffer, BUFSIZE); /* read Web request in one go */
	if (ret == 0 || ret == -1)
	{ /* read failure stop now */
		logger(FORBIDDEN, "failed to read browser request", "", fd);
	}
	if (ret > 0 && ret < BUFSIZE) /* return code is valid chars */
		buffer[ret] = 0;		  /* terminate the buffer */
	else
		buffer[0] = 0;
	for (i = 0; i < ret; i++) /* remove CF and LF characters */
		if (buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i] = '*';
	logger(LOG, "request", buffer, hit);
	if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4))
	{
		logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd);
	}
	for (i = 4; i < BUFSIZE; i++)
	{ /* null terminate after the second space to ignore extra stuff */
		if (buffer[i] == ' ')
		{ /* string is "GET URL " +lots of other stuff */
			buffer[i] = 0;
			break;
		}
	}
	for (j = 0; j < i - 1; j++) /* check for illegal parent directory use .. */
		if (buffer[j] == '.' && buffer[j + 1] == '.')
		{
			logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd);
		}
	if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6)) /* convert no filename to index file */
		(void)strcpy(buffer, "GET /index.html");	

	/* work out the file type and check we support it */
	buflen = strlen(buffer);
	fstr = (char *)0;
	for (i = 0; extensions[i].ext != 0; i++)
	{
		len = strlen(extensions[i].ext);
		if (!strncmp(&buffer[buflen - len], extensions[i].ext, len))
		{
			fstr = extensions[i].filetype;
			break;
		}
	}
	if (fstr == 0)
		logger(FORBIDDEN, "file extension type not supported", buffer, fd);

	if ((file_fd = open(&buffer[5], O_RDONLY)) == -1)
	{ /* open the file for reading */
		logger(NOTFOUND, "failed to open file", &buffer[5], fd);
	}
	logger(LOG, "SEND", &buffer[5], hit);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END);	/* lseek to the file end to find the length */
	(void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
	(void)sprintf(buffer, "HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
	logger(LOG, "Header", buffer, hit);
	if(write(fd, buffer, strlen(buffer))){};

	if (!strcmp(fstr,"text/html")) {
		thread->html_request++;
	}
	if (!strcmp(fstr,"image/jpg") || !strcmp(fstr,"image/png") || !strcmp(fstr,"image/gif")){
		thread->img_request++;
	}
	thread->http_request++;

	
	completed_threads++;
	gettimeofday(&current_time, NULL);
	complete_time = (current_time.tv_sec * 1000 + current_time.tv_usec) - 
					(startTime * 1000 + otherStart);
	/* Send the statistical headers described in the paper, example below
    
    (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
	(void)write(fd,buffer,strlen(buffer));
    */
   	(void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", arrivalCount);
	logger(LOG, "X-stat-req-arrival-count", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-req-arrival-time: %d\r\n", arrivalTime);
	logger(LOG, "X-stat-req-arrival-time", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-req-dispatch-count: %d\r\n", dispatchCount);
	logger(LOG, "X-stat-req-dispatch-count", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-req-dispatch-time: %d\r\n", dispatchTime);
	logger(LOG, "X-stat-req-dispatch-time", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-req-complete-count: %d\r\n", completed_threads);
	logger(LOG, "X-stat-req-complete-count", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-req-complete-time: %d\r\n", complete_time);
	logger(LOG, "X-stat-req-complete-time", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-req-age: %d\r\n", (dispatchCount - arrivalCount));
	logger(LOG, "X-stat-req-age", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-thread-id: %d\r\n", thread->id);
	logger(LOG, "X-stat-thread-id", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-thread-count: %d\r\n", thread->http_request);
	logger(LOG, "X-stat-req-thread-count", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-thread-html: %d\r\n", thread->html_request);
	logger(LOG, "X-stat-req-thread-html", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	(void)sprintf(buffer,"X-stat-thread-image: %d\r\n", thread->img_request);
	logger(LOG, "X-stat-req-thread-image", buffer, hit);
	(void)write(fd,buffer,strlen(buffer));
	//Server Statistics
	//Thread Arrical Count
	// printf("X-stat-req-arrival-count: %d\n", arrivalCount);
	// printf("X-stat-req-arrival-time: %d\n", arrivalTime);
	// printf("X-stat-req-dispatch-count: %d\n", dispatchCount);
	// printf("X-stat-req-dispatch-time: %d\n", dispatchTime);
	// printf("X-stat-req-complete-count: %d\n", completed_threads);
	// printf("X-stat-req-complete-time: %d\n", complete_time);
	// printf("X-stat-req-age: %d\n", (dispatchCount - arrivalCount));
	// printf("X-stat-thread-id: %d\n", thread->id);
	// printf("X-stat-thread-count: %d\n", thread->http_request);
	// printf("X-stat-thread-html: %d\n", thread->html_request);
	// printf("X-stat-thread-image: %d\n", thread->img_request);

	
	/**
	 * todo:
	 * X-stat-req-age
	 * X-stat-req-dispatch-count
	 * All the thread specific attributes
	 */

	/* send file in 8KB block - last block may be smaller */
	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0)
	{
		if(write(fd, buffer, ret)){};
	}
	sleep(1); /* allow socket to drain before signalling the socket is closed */
	close(fd);
	// exit(1);
}
void *consumer(void *thread) {
	struct timeval current_time;
	int dispatch_time, dispatchCount;
	while(1){
		pthread_mutex_lock(&the_mutex); /* get exclusive access to buffer */
		if (bufferQueue.counter == 0){
			pthread_cond_wait(&condc, &the_mutex);
		}
		dispatchCount = dispatch_count++;
		gettimeofday(&current_time, NULL);
		dispatch_time = (current_time.tv_sec * 1000 + current_time.tv_usec) - 
						(startTime * 1000 + otherStart);
		bufferQueue.counter--; /* take item out of buffer */
		printf("this is the counter: %d\n", bufferQueue.counter);
		web(bufferQueue.head->call, bufferQueue.head->hit, bufferQueue.head->arrival_count, bufferQueue.head->arrival_time, dispatch_time, dispatchCount, thread); /* never returns */
		bufferQueue.head = bufferQueue.head->next;
		pthread_mutex_unlock(&the_mutex); /* release access to buffer */
	}
	pthread_exit(0);
}

int main(int argc, char **argv)
{
	struct timeval start_time, current_time;
	int i, port, listenfd, socketfd, hit, foo;
	long len;
	socklen_t length;
	static struct sockaddr_in cli_addr;	 /* static = initialised to zeros */
	static struct sockaddr_in serv_addr;
	
	gettimeofday(&start_time, NULL);
	startTime = start_time.tv_sec;
	otherStart = start_time.tv_usec;
	printf("%d ", argc);
	thread_count = 0;
	completed_threads = 0;
	MAX = atoi(argv[4]);
	init_threads = atoi(argv[3]);
	order = argv[5];
	bufferQueue.counter = 0;
	threads = calloc(init_threads, sizeof(struct Thread));
	// pthread_t threads[init_threads]; 
	// printf("1: %s, 2: %s, 3: %s, 4: %d, 5: %d, 6: %s", argv[0], argv[1], argv[2], atoi(argv[3]), MAX, order);
	
	pthread_mutex_init(&the_mutex, 0);
	pthread_cond_init(&condc, 0);
	for(i = 0; i < init_threads; i++){
		pthread_t pthread;
		threads[i].thread = pthread;
		threads[i].html_request = 0;
		threads[i].http_request = 0;
		threads[i].img_request = 0;
		threads[i].id = i;
		pthread_create(&pthread, NULL, consumer, threads+i);

	}

	if (argc != 6 || !strcmp(argv[1], "-?"))
	{
		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
					 "\tnweb is a small and very safe mini web server\n"
					 "\tnweb only servers out file/web pages with extensions named below\n"
					 "\t and only from the named directory or its sub-directories.\n"
					 "\tThere is no fancy features = safe and secure.\n\n"
					 "\tExample: nweb 8181 /home/nwebdir &\n\n"
					 "\tOnly Supports:",
					 VERSION);
		for (i = 0; extensions[i].ext != 0; i++)
			(void)printf(" %s", extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
					 "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
					 "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n");
		exit(0);
	}
	if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5) ||
		!strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5) ||
		!strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5) ||
		!strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6))
	{
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n", argv[2]);
		exit(3);
	}
	if (chdir(argv[2]) == -1)
	{
		(void)printf("ERROR: Can't Change to directory %s\n", argv[2]);
		exit(4);
	}

	//ERRORS
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		logger(ERROR, "system call", "socket", 0);

	//implement listenfd


	port = atoi(argv[1]);
	if (port < 0 || port > 60000)
		logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if ((foo = bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0){
		printf("I HOPE IT GETS TO HERE %d", foo);

		logger(ERROR, "system call", "bind", 0);
	}
	
	if (listen(listenfd, 64) < 0)
		logger(ERROR, "system call", "listen", 0);

	//RUN THE SERVER WITH FOREVER LOOP
	for (hit = 1;; hit++)
	{
		// if(bufferQueue.counter != 0) continue;
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0){
			fprintf(stderr,"error %s\n",strerror(errno));
			logger(ERROR, "system call", "accept", 0);
		}
		gettimeofday(&current_time, NULL);
		int arrivalTime = (current_time.tv_sec * 1000 + current_time.tv_usec) - 
						  (startTime * 1000 + otherStart);
		
		// printf("im here");
		printf("%s", argv[5]);
		if (!strcmp(argv[5],"FIFO") || !strcmp(argv[5],"ANY")){
			if (bufferQueue.counter == 0){
				struct node newNode;
				bufferQueue.head = &newNode;
				bufferQueue.tail = &newNode;
				bufferQueue.head->next = bufferQueue.tail;
				bufferQueue.head->call = socketfd;
				bufferQueue.head->hit = hit;
				bufferQueue.head->arrival_time = arrivalTime;
				bufferQueue.head->arrival_count = thread_count++;
		} else {
				struct node newNode;
				bufferQueue.tail->next = &newNode;
				bufferQueue.tail = bufferQueue.tail->next;
				bufferQueue.tail->call = socketfd;
				bufferQueue.tail->hit = hit;
				bufferQueue.tail->arrival_time = arrivalTime;
				bufferQueue.tail->arrival_count = thread_count++;
			}

			bufferQueue.counter++;
			pthread_cond_signal(&condc);
		}

		if(!strcmp(argv[5], "HPIC")) {
			int buflen;
			char *fstr;
			static char buffer[BUFSIZE + 1]; /* static so zero filled */

			if(read(socketfd, buffer, BUFSIZE)){};
			/* work out the file type and check we support it */
			buflen = strlen(buffer);
			fstr = (char *)0;
			for (i = 0; extensions[i].ext != 0; i++)
			{
				len = strlen(extensions[i].ext);
				if (!strncmp(&buffer[buflen - len], extensions[i].ext, len))
				{
					fstr = extensions[i].filetype;
					break;
				}
			}
			if(bufferQueue.counter==0){
				struct node newNode;
				bufferQueue.head = &newNode;
				bufferQueue.tail = &newNode;
				bufferQueue.head->next = bufferQueue.tail;
				bufferQueue.head->call = socketfd;
				bufferQueue.head->hit = hit;
				bufferQueue.head->arrival_time = arrivalTime;
				bufferQueue.head->arrival_count = thread_count++;
			} else if (strcmp(fstr, ".html")) {
				struct node *temp = bufferQueue.head;
				struct node newNode;
				bufferQueue.head = &newNode;
				bufferQueue.head->next = temp;
				bufferQueue.head->call = socketfd;
				bufferQueue.head->hit = hit;
				bufferQueue.head->arrival_time = arrivalTime;
				bufferQueue.head->arrival_count = thread_count++;
			} else /*is not a jpg*/ {
				struct node newNode;
				bufferQueue.tail->next = &newNode;
				bufferQueue.tail = bufferQueue.tail->next;
				bufferQueue.tail->call = socketfd;
				bufferQueue.tail->hit = hit;
				bufferQueue.tail->arrival_time = arrivalTime;
				bufferQueue.tail->arrival_count = thread_count++;
			}
		}

		if(!strcmp(argv[5], "HPHC")) {
			int buflen;
			char *fstr;
			static char buffer[BUFSIZE + 1]; /* static so zero filled */

			if(read(socketfd, buffer, BUFSIZE)){};
			/* work out the file type and check we support it */
			buflen = strlen(buffer);
			fstr = (char *)0;
			for (i = 0; extensions[i].ext != 0; i++)
			{
				len = strlen(extensions[i].ext);
				if (!strncmp(&buffer[buflen - len], extensions[i].ext, len))
				{
					fstr = extensions[i].filetype;
					break;
				}
			}
			if(bufferQueue.counter==0){
				struct node newNode;
				bufferQueue.head = &newNode;
				bufferQueue.tail = &newNode;
				bufferQueue.head->next = bufferQueue.tail;
				bufferQueue.head->call = socketfd;
				bufferQueue.head->hit = hit;
				bufferQueue.head->arrival_time = arrivalTime;
				bufferQueue.head->arrival_count = thread_count++;
			} else if (!strcmp(fstr, ".html")) {
				struct node *temp = bufferQueue.head;
				struct node newNode;
				bufferQueue.head = &newNode;
				bufferQueue.head->next = temp;
				bufferQueue.head->call = socketfd;
				bufferQueue.head->hit = hit;
				bufferQueue.head->arrival_time = arrivalTime;
				bufferQueue.head->arrival_count = thread_count++;
			} else /*is not a jpg*/ {
				struct node newNode;
				bufferQueue.tail->next = &newNode;
				bufferQueue.tail = bufferQueue.tail->next;
				bufferQueue.tail->call = socketfd;
				bufferQueue.tail->hit = hit;
				bufferQueue.tail->arrival_time = arrivalTime;
				bufferQueue.tail->arrival_count = thread_count++;
			}
		}

		// if ((pid = fork()) < 0)
		// {
		// 	logger(ERROR, "system call", "fork", 0);
		// }
		// else
		// {
		// 	if (pid == 0)
		// 	{ /* child */
		// 		(void)close(listenfd);
		// 		web(socketfd, hit); /* never returns */
		// 	}
		// 	else
		// 	{ /* parent */
		// 		(void)close(socketfd);
		// 	}
		// }
		// (void)close(socketfd);
		// (void)close(listenfd);

	}
	pthread_mutex_destroy(&the_mutex);

}
