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
 *      email: streamit@rkeene.org
 */

#ifndef _STREAM_NET_H 
#define _STREAM_NET_H
#include <sys/types.h>
#include <unistd.h>

int createlisten(int port);
void closeconnection(int sockfd);
int createconnection_tcp(char *host, int port);
#endif
