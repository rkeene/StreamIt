/*
 * Copyright (C) 2001  Roy Keene
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *      email: dact@rkeene.org
 */


/*
 *	Network related functions
 */
#include "net.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>

extern int errno;

/*
 *	Create a listening port on tcp port PORT
 */
int createlisten(int port)
{
	struct sockaddr_in localname;
	int sockFd;
	sockFd=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);
	localname.sin_family=AF_INET;
	localname.sin_port=htons(port);
	localname.sin_addr.s_addr=INADDR_ANY;
	if (bind(sockFd,(struct sockaddr *) & localname,sizeof(localname))==-1) { perror("bind"); close(sockFd); return(-1); }
	if (listen(sockFd,1024)==-1) { perror("listen"); return(-1); }
	return(sockFd);
}


/*
 *	Close that socket, yeah.
 */
void closeconnection(int sockfd) {
	shutdown(sockfd, 1);
	close(sockfd);
}

int createconnection_tcp(char *host, int port) {
	int sockid;
	struct hostent *hostinfo;
	struct sockaddr_in sock;

	if ((hostinfo=gethostbyname(host))==NULL) {
#ifdef HAVE_INET_ATON
		if (!inet_aton(host,&sock.sin_addr))
#else
		if ( (sock.sin_addr.s_addr=inet_addr(host) )==-1)
#endif
			return(-1);
	} else {
		memcpy(&sock.sin_addr.s_addr,hostinfo->h_addr_list[0],hostinfo->h_length);
	}
	sock.sin_family=AF_INET;
	sock.sin_port=htons(port);
	if ((sockid=socket(AF_INET, SOCK_STREAM, 0))<0)
		return(-1);
	if (connect(sockid, (struct sockaddr *) &sock, sizeof(sock))<0) {
		close(sockid);
		return(-1);
	}
	return(sockid);
}
