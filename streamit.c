#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include "net.h"
#include "version.h"

extern int errno;

#define _STREAMIT_MAX_BLOCK_WAIT 100000
#define _STREAMIT_MAX_NUM_CONNS 30

/* Not used */
#define _STREAMIT_BLOCK_BACKOFF 600

#ifndef _STREAMIT_HTTP_RESP 
//#define _STREAMIT_HTTP_RESP "HTTP/1.1 200 OK\r\nDate: " __DATE__ "\r\nServer: StreamIt/" VERSION "\r\nLast-Modified: Tue, 05 Nov 2002 13:08:25 CST\r\nTransfer-Encoding: chunked\r\nContent-Type: application/octet-stream\r\n\r\n"
#define _STREAMIT_HTTP_RESP "HTTP/1.1 200 OK\r\nDate: " __DATE__ "\r\nServer: StreamIt/" VERSION "\r\nLast-Modified: Tue, 05 Nov 2002 13:08:25 CST\r\nContent-Type: application/octet-stream\r\n\r\n"
#endif

#ifdef DEBUG
#define LOG_INIT(x, y) /**/
#define LOG_MSG(y...) { fprintf(stderr, y); fprintf(stderr, "\n"); }
#define CHECKPOINT fprintf(stderr, "%i: x=%i, i=%i, errcnt[i]=%i, clnt_instr_buf_len[i]=%i\n", __LINE__, x, i, errcnt[i], clnt_instr_buf_len[i])
#define DEBUG_LOG(y...) { fprintf(stderr, __FILE__ ":%i:%s(): ", __LINE__, __func__); fprintf(stderr, y); fprintf(stderr, "\n"); }
#else
#define LOG_INIT(x, y) openlog(x, LOG_PID, y)
#define LOG_MSG(y...) syslog(LOG_INFO, y)
#define CHECKPOINT /**/
#define DEBUG_LOG(y...) /**/ 
#endif

void sighandler(int sig) {
	return;
}

int print_help(void) {
	fprintf(stderr, "Usage: streamit -p port <files>\n");
	return(-1);
}

int main(int argc, char **argv) {
	struct sockaddr_in peer, peers[_STREAMIT_MAX_NUM_CONNS];
	char buf[10240], smbuf[256];
	float percent_served=0.0;
	int stream_port=-1;
	int instr_fd=-1, instr_buf_len=0, curr_file=2, clnt_instr_buf_len[_STREAMIT_MAX_NUM_CONNS];
	int master_fd, sock_fd, sock_cnt=0;
	int clients[_STREAMIT_MAX_NUM_CONNS], clients_http[_STREAMIT_MAX_NUM_CONNS], clients_cnt=0, clients_cnt_max=-1;
	int peerlen=sizeof(peer), sockflags, i, x, errcnt[_STREAMIT_MAX_NUM_CONNS], write_res, sock_cnt_serv;
	int block_backoff=1, block_backoff_total=1;
	unsigned long backoff_mult=30, backoff_mult_incr=0;

	if (argc<4) return(print_help());
	if (strcmp(argv[1], "-p")!=0) return(print_help());
	stream_port=atoi(argv[2]);
	if (stream_port<=0) return(print_help());

#ifndef DEBUG
	if (fork()!=0) {
		/* Parent */
		wait(NULL);
		return(0);
	} else {
		/* Child */
		if (fork()!=0) return(0);
		/* Grand-child */
	}
#endif

        signal(SIGPIPE, sighandler);
	LOG_INIT("streamit", LOG_DAEMON);
	LOG_MSG("Starting up.");

	for (i=0;i<(sizeof(clients)/sizeof(int));i++) clients[i]=-1;
	for (i=0;i<(sizeof(clients_http)/sizeof(int));i++) clients_http[i]=0;

	while ((master_fd=createlisten(stream_port))<0) {
		LOG_MSG("Couldn't bind to socket.");
		sleep(1);
	}
	sockflags=fcntl(master_fd, F_GETFL);
	fcntl(master_fd, F_SETFL, sockflags|O_NONBLOCK);

	while (1) {
		if (instr_buf_len<=0) {
			if (argv[++curr_file]==NULL) curr_file=3;
			if (instr_fd!=-1) { close(instr_fd); instr_fd=-1; }
			if ((instr_fd=open(argv[curr_file], O_RDONLY))<0) {
				continue;
			}
			LOG_MSG("Playing %s", argv[curr_file]);
		}
		instr_buf_len=read(instr_fd, buf, sizeof(buf));
		if (instr_buf_len==0) continue;

		/* Accept incoming connections, if there are any. */
		if ((sock_fd=accept(master_fd, (struct sockaddr *) &peer, &peerlen))>=0) {
			sockflags=fcntl(sock_fd, F_GETFL);
			fcntl(sock_fd, F_SETFL, sockflags|O_NONBLOCK);
			/* Locate a suitable location within the arrays. */
			if (clients[clients_cnt]!=-1) {
				for (i=0;i<(sizeof(clients)/sizeof(int));i++) {
					if (clients[i]==-1) clients_cnt=i;
				}
			}
			if (clients_cnt<(sizeof(clients)/sizeof(int))) {
				clients[clients_cnt]=sock_fd;
				peers[clients_cnt]=peer;
				clients_http[clients_cnt]=0;
				if (clients_cnt>clients_cnt_max) clients_cnt_max=clients_cnt;
				clients_cnt++;
				sock_cnt++;
				LOG_MSG("Accepted new connection from %s", inet_ntoa(peer.sin_addr));
			} else {
				LOG_MSG("Ran out of descriptors, ignoring connection from %s.", inet_ntoa(peer.sin_addr));
				close(sock_fd);
			}
		}
		sock_cnt_serv=sock_cnt;
		for (i=0;i<=clients_cnt_max;i++) {
			clnt_instr_buf_len[i]=instr_buf_len;
			errcnt[i]=0;
		}

		backoff_mult_incr=backoff_mult/2;

		/* Loop until we've sent 1 block to all the socks or we run out of time for the block. */
//		for (x=0;x<(_STREAMIT_MAX_BLOCK_WAIT/block_backoff);x++) {
		for (x=0;x<backoff_mult;x++) {
			if (sock_cnt_serv==0) break;
			
			for (i=0;i<=clients_cnt_max;i++) {
				if (clnt_instr_buf_len[i]<=0) continue;
				if (clients[i]==-1) continue;

				/* If the client hasn't recieved an HTTP reply yet, don't talk to it. */
				if (clients_http[i]==0) {
					write_res=read(clients[i], smbuf, sizeof(smbuf));
					if (write_res>0) {
						DEBUG_LOG("Client [fd=%i] recieved HTTP response.", clients[i]);
						write(clients[i], _STREAMIT_HTTP_RESP, strlen(_STREAMIT_HTTP_RESP));
						clients_http[i]=1;
					}

					clnt_instr_buf_len[i]=0;
					write_res=0;
				} else {
					/* Otherwise pass it the data */
					write_res=write(clients[i], buf+(instr_buf_len-clnt_instr_buf_len[i]), clnt_instr_buf_len[i]);
				} 

				/* If we wrote something to the socket, note where we stopped writing. */
				if (write_res>=0) {
					clnt_instr_buf_len[i]-=write_res;
					if (clnt_instr_buf_len[i]==0) sock_cnt_serv--;
				} else {
					/* If we got an error note it (don't close here, it will be done below) */
					if (errno==EPIPE || errno==-EPIPE) {
						clnt_instr_buf_len[i]=-1;
						sock_cnt_serv-=1;
					}
					errcnt[i]++;
				}
			}
			if (sock_cnt_serv==0) break;
			/* If we haven't served atleast 75% of our sockets, we're going too fast, slow down. */
			percent_served=(((float) (sock_cnt-sock_cnt_serv))/((float) sock_cnt))*100.0;
			if (percent_served < 75 && x >= (backoff_mult/2)) {
				DEBUG_LOG("Percent served: %f; sock_cnt=%i, sock_cnt_serv=%i", percent_served, sock_cnt, sock_cnt_serv);
				block_backoff_total+=500;
				if (block_backoff_total>_STREAMIT_MAX_BLOCK_WAIT) block_backoff_total=_STREAMIT_MAX_BLOCK_WAIT;
				block_backoff=block_backoff_total*sock_cnt_serv;
				DEBUG_LOG("Increasing block_backoff=%i", block_backoff);
			}

//			backoff_mult=(_STREAMIT_MAX_BLOCK_WAIT-(block_backoff*sock_cnt_serv));
			if (backoff_mult<=4) backoff_mult=5;
			usleep(block_backoff/sock_cnt_serv);
		}

		/* If we haven't served atleast 75% of our sockets, we're going too fast, slow down. */
		percent_served=(((float) (sock_cnt-sock_cnt_serv))/((float) sock_cnt))*100.0;
		if (percent_served < 75) {
			DEBUG_LOG("Percent served: %f; sock_cnt=%i, sock_cnt_serv=%i", percent_served, sock_cnt, sock_cnt_serv);
			backoff_mult+=backoff_mult_incr;
			DEBUG_LOG("Increasing backoff_mult=%i", backoff_mult);
		}

		/* Kill sockets who were unable to keep up. */
		for (i=0;i<=clients_cnt_max;i++) {
			if (clnt_instr_buf_len[i]!=0) {
				if (clients[i]==-1) continue;
//				LOG_MSG("Closing client, write_res=%i, fd=%i, i=%i, errno=%i: ", write_res, clients[i], i, errno);
//				perror("write");
				if (errno==11) {
					/* Don't kill slow HTTP clients. */
					if (clients_http[i]==0) continue;
					LOG_MSG("Closing connection from %s, client unable to keep up.", inet_ntoa(peers[i].sin_addr));
				} else {
					LOG_MSG("Closing connection from %s", inet_ntoa(peers[i].sin_addr));
				}
				close(clients[i]);
				clients[i]=-1;
				clients_http[i]=0;
				clients_cnt=i;
				sock_cnt--;
			}
		}
/* This will be replaced with a dynamic wait from tv.usec */
		if (sock_cnt==0) {
			usleep(700000);
		} else {
			usleep(5000);
		}
	}
}
