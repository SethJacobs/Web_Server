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
#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404

pthread_mutex_t the_mutex;
pthread_cond_t condc, condp;
int buffer = 0;
int init_threads;
int MAX;
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
};

struct queue
{
	struct node *head;
	struct node *tail;
	int counter;
};



void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd;
	char logbuffer[BUFSIZE * 2];

	switch (type)
	{
	case ERROR:
		(void)sprintf(logbuffer, "ERROR: %s:%s Errno=%d exiting pid=%d", s1, s2, errno, getpid());
		break;
	case FORBIDDEN:
		if(write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n", 271)){};
		(void)sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2);
		break;
	case NOTFOUND:
		if(write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n", 224)){};
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
void web(int fd, int hit)
{
	int j, file_fd, buflen;
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

	/* Send the statistical headers described in the paper, example below
    
    (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
	(void)write(fd,buffer,strlen(buffer));
    */

	/* send file in 8KB block - last block may be smaller */
	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0)
	{
		if(write(fd, buffer, ret)){};
	}
	sleep(1); /* allow socket to drain before signalling the socket is closed */
	close(fd);
	exit(1);
}
void *consumer(void *ptr) {
	pthread_mutex_lock(&the_mutex); /* get exclusive access to buffer */
	// if(!strcmp(order,"FIFO") || !strcmp(order, "ANY") || !strcmp(order, "HPIC")){
		while (bufferQueue.counter == 0) pthread_cond_wait(&condc, &the_mutex);
		bufferQueue.counter--; /* take item out of buffer */
		web(bufferQueue.head->call, bufferQueue.head->hit); /* never returns */
		bufferQueue.head = bufferQueue.head->next;
	// }
	pthread_mutex_unlock(&the_mutex); /* release access to buffer */
	pthread_exit(0);
}
void *producer(void *ptr) {
	int i;
	for (i = 1; i < init_threads; i++) {
		pthread_t con;
		pthread_mutex_lock(&the_mutex); /* get exclusive access to buffer */
		// while (buffer != 0) pthread_cond_wait(&condp, &the_mutex);
		// buffer = i; /* put item in buffer */
		// pthread_cond_signal(&condc); /* wake up consumer */
		
		pthread_create(&con, 0, consumer, 0);
		pthread_join(con, 0);
		pthread_mutex_unlock(&the_mutex); /* release access to buffer */
	}
	pthread_exit(0);
}



int main(int argc, char **argv)
{
	pthread_t pro;
	pthread_mutex_init(&the_mutex, 0);
	pthread_cond_init(&condc, 0);
	pthread_cond_init(&condp, 0);
	// pthread_create(&con, 0, consumer, 0);
	pthread_create(&pro, 0, producer, 0);
	pthread_join(pro, 0);
	// pthread_join(con, 0);
	pthread_cond_destroy(&condc);
	pthread_cond_destroy(&condp);
	pthread_mutex_destroy(&the_mutex);
	int i, port, listenfd, socketfd, hit, fd;
	long len;
	socklen_t length;
	static struct sockaddr_in cli_addr;	 /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

	MAX = atoi(argv[3]);
	init_threads = atoi(argv[2]);
	order = argv[4];
	bufferQueue.counter = 0;
	// struct node *newNode;
	// bufferQueue.head = newNode;
	// bufferQueue.head->next = newNode;

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
	producer(NULL);
	pthread_cond_wait(&condc,&the_mutex);
	
	// /* Become deamon + unstopable and no zombies children (= no wait()) */
	// if (fork() != 0)
	// 	return 0;					/* parent returns OK to shell */
	// (void)signal(SIGCHLD, SIG_IGN); /* ignore child death */
	// (void)signal(SIGHUP, SIG_IGN);	/* ignore terminal hangups */
	// for (i = 0; i < 32; i++)
	// 	(void)close(i); /* close open files */
	// (void)setpgrp();	/* break away from process group */
	// logger(LOG, "nweb starting", argv[1], getpid());
	/* setup the network socket */

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
	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		logger(ERROR, "system call", "bind", 0);
	if (listen(listenfd, 64) < 0)
		logger(ERROR, "system call", "listen", 0);

	//RUN THE SERVER WITH FOREVER LOOP
	for (hit = 1;; hit++)
	{
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR, "system call", "accept", 0);
		(void)close(listenfd);
		if (!strcmp(argv[4],"FIFO")){
			if (bufferQueue.counter == 0){
				struct node *newNode;
				bufferQueue.head = newNode;
				bufferQueue.tail = newNode;
				bufferQueue.head->next = bufferQueue.tail;
				bufferQueue.head->call = socketfd;
				bufferQueue.head->hit = hit;
			} else {
				struct node *newNode;
				bufferQueue.tail->next = newNode;
				bufferQueue.tail = bufferQueue.tail->next;
				bufferQueue.tail->call = socketfd;
				bufferQueue.tail->hit = hit;
			}
			bufferQueue.counter++;
		}

		if(!strcmp(argv[4], "HPIC")) {
			int buflen;
			char *fstr;
			static char buffer[BUFSIZE + 1]; /* static so zero filled */

			if(read(fd, buffer, BUFSIZE)){};
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
				struct node *newNode;
				bufferQueue.head = newNode;
				bufferQueue.tail = newNode;
				bufferQueue.head->next = bufferQueue.tail;
				bufferQueue.head->call = socketfd;
				bufferQueue.head->hit = hit;
			} else if (strcmp(fstr, ".html")) {
				struct node *temp = bufferQueue.head;
				struct node *newNode;
				newNode->next = temp;
				newNode->call = socketfd;
				newNode->hit = hit;
				bufferQueue.head=newNode;
			} else /*is not a jpg*/ {
				struct node *newNode;
				bufferQueue.tail->next = newNode;
				bufferQueue.tail = bufferQueue.tail->next;
				bufferQueue.tail->call = socketfd;
				bufferQueue.tail->hit = hit;
			}
		}

		if(!strcmp(argv[4], "HPHC")) {
			int buflen;
			char *fstr;
			static char buffer[BUFSIZE + 1]; /* static so zero filled */

			if(read(fd, buffer, BUFSIZE)){};
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
				struct node *newNode;
				bufferQueue.head = newNode;
				bufferQueue.tail = newNode;
				bufferQueue.head->next = bufferQueue.tail;
				bufferQueue.head->call = socketfd;
				bufferQueue.head->hit = hit;
			} else if (!strcmp(fstr, ".html")) {
				struct node *temp = bufferQueue.head;
				struct node *newNode;
				newNode->next = temp;
				newNode->call = socketfd;
				newNode->hit = hit;
				bufferQueue.head=newNode;
			} else /*is not a jpg*/ {
				struct node *newNode;
				bufferQueue.tail->next = newNode;
				bufferQueue.tail = bufferQueue.tail->next;
				bufferQueue.tail->call = socketfd;
				bufferQueue.tail->hit = hit;
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
				// (void)close(listenfd);
				// web(socketfd, hit); /* never returns */
		// 	}
		// 	else
		// 	{ /* parent */
				(void)close(socketfd);
		// 	}
		// }
	}
}
