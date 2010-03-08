/*
 * Copyright (C) 2010 Tildeslash Ltd. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 *
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#include <config.h>

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_POLL_H
#include <poll.h>
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef NEED_SOCKLEN_T_DEFINED
#define _BSD_SOCKLEN_T_
#endif

#ifdef HAVE_SOL_IP
#define ICMP_SIZE sizeof(struct icmphdr)
#else
#define ICMP_SIZE sizeof(struct icmp)
#endif

#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif 

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif 

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif

#ifdef HAVE_NETINET_IP_ICMP_H
#include <netinet/ip_icmp.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifndef __dietlibc__
#ifdef HAVE_STROPTS_H
#include <stropts.h>
#endif
#endif

#include "monitor.h"
#include "net.h"
#include "ssl.h"

/**
 *  General purpose Network and Socket methods.
 *
 *  @author Jan-Henrik Haukeland, <hauk@tildeslash.com>
 *  @author Christian Hopp, <chopp@iei.tu-clausthal.de>
 *  @author Martin Pala, <martinp@tildeslash.com>
 *
 *  @file
 */


/* -------------------------------------------------------------- Prototypes */


static int do_connect(int, const struct sockaddr *, socklen_t, int);
static unsigned short checksum_ip(unsigned char *, int);


/* ------------------------------------------------------------------ Public */


/**
 * Check if the hostname resolves
 * @param hostname The host to check
 * @return TRUE if hostname resolves, otherwise FALSE
 */
int check_host(const char *hostname) {

  struct addrinfo hints;
  struct addrinfo *res;

  ASSERT(hostname);

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = PF_INET; /* we support just IPv4 currently */

  if(getaddrinfo(hostname, NULL, &hints, &res) != 0)
    return FALSE;

  freeaddrinfo(res);

  return TRUE;

}


/**
 * Verify that the socket is ready for i|o
 * @param socket A socket
 * @return TRUE if the socket is ready, otherwise FALSE.
 */
int check_socket(int socket) {

  return (can_read(socket, 0) || can_write(socket, 0));
  
}


/**
 * Verify that the udp server is up. The given socket must be a 
 * connected udp socket if we should be able to test the udp server. 
 * The test is conducted by sending a datagram to the server and
 * check for a returned ICMP error when reading from the socket.
 * @param socket A socket
 * @return TRUE if the socket is ready, otherwise FALSE.
 */
int check_udp_socket(int socket) {
  
  char buf[STRLEN]= {0};

  /* We have to send something and if the UDP server is down/unreachable
   *  the remote host should send an ICMP error. We then need to call read
   *  to get the ICMP error as a ECONNREFUSED errno. This test is asynchronous
   *  so we must wait, but we do not want to block to long either and it is
   *  probably better to report a server falsely up than to block to long.
   */
  sock_write(socket, buf, 1, 0);
  if(sock_read(socket, buf, STRLEN, 2) < 0) {
    switch(errno) {
      case ECONNREFUSED: return FALSE;
      default:           break;
    }
  }
  
  return TRUE;
  
}


/**
 * Create a non-blocking socket against hostname:port with the given
 * type. The type should be either SOCK_STREAM or SOCK_DGRAM.
 * @param hostname The host to open a socket at
 * @param port The port number to connect to
 * @param type Socket type to use (SOCK_STREAM|SOCK_DGRAM)
 * @param timeout If not connected within timeout seconds abort and return -1
 * @return The socket or -1 if an error occured.
 */
int create_socket(const char *hostname, int port, int type, int timeout) {

  int s;
  struct sockaddr_in sin;
  struct sockaddr_in *sa;
  struct addrinfo hints;
  struct addrinfo *result;
  
  ASSERT(hostname);

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  if(getaddrinfo(hostname, NULL, &hints, &result) != 0) {
    return -1;
  }

  if((s= socket(AF_INET, type, 0)) < 0) {
    freeaddrinfo(result);
    return -1;
  }

  sa = (struct sockaddr_in *)result->ai_addr;
  memcpy(&sin, sa, result->ai_addrlen);
  sin.sin_family= AF_INET;
  sin.sin_port= htons(port);
  freeaddrinfo(result);
  
  if(! set_noblock(s)) {
    goto error;
  }
 
  if(fcntl(s, F_SETFD, FD_CLOEXEC) == -1)
    goto error; 
  
  if(do_connect(s, (struct sockaddr *)&sin, sizeof(sin), timeout) < 0) {
    goto error;
  }

  return s;

  error:
  close_socket(s);
  return -1;
 
}


/**
 * Open a socket using the given Port_T structure. The protocol,
 * destination and type are selected appropriately.
 * @param p connection description
 * @return The socket or -1 if an error occured.
 */
int create_generic_socket(Port_T p) {

  int socket_fd= -1;

  ASSERT(p);

  switch(p->family) {
  case AF_UNIX:
      socket_fd= create_unix_socket(p->pathname, p->timeout);
      break;
  case AF_INET:
      socket_fd= create_socket(p->hostname, p->port, p->type, p->timeout);
      break;
  default:
      socket_fd= -1;
  }
  
  return socket_fd;
  
}


/**
 * Create a non-blocking UNIX socket.
 * @param pathname The pathname to use for the unix socket
 * @param timeout If not connected within timeout seconds abort and return -1
 * @return The socket or -1 if an error occured.
 */
int create_unix_socket(const char *pathname, int timeout) {

  int s;
  struct sockaddr_un unixsocket;
  
  ASSERT(pathname);

  if((s= socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
    LogError("%s: Cannot create socket -- %s\n", prog, STRERROR);
    return -1;
  }

  unixsocket.sun_family= AF_UNIX;
  snprintf(unixsocket.sun_path, sizeof(unixsocket.sun_path), "%s", pathname);
  
  if(! set_noblock(s)) {
    goto error;
  }
  
  if(do_connect(s, (struct sockaddr *)&unixsocket, sizeof(unixsocket), timeout) < 0) {
    goto error;
  }
  
  return s;
  
  error:
  close_socket(s);
  return -1;

}


/**
 * Create a non-blocking server socket and bind it to the specified local
 * port number, with the specified backlog. Set a socket option to
 * make the port reusable again. If a bind address is given the socket
 * will only accept connect requests to this addresses. If the bind
 * address is NULL it will accept connections on any/all local
 * addresses
 * @param port The localhost port number to open
 * @param backlog The maximum queue length for incomming connections
 * @param bindAddr the local address the server will bind to
 * @return The socket ready for accept, or -1 if an error occured.
 */
int create_server_socket(int port, int backlog, const char *bindAddr) {
  int s;
  int status;
  int flag= 1;
  struct sockaddr_in myaddr;

  if((s= socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    LogError("%s: Cannot create socket -- %s\n", prog, STRERROR);
    return -1;
  }

  memset(&myaddr, 0, sizeof(struct sockaddr_in));
  
  if(bindAddr) {
    struct sockaddr_in *sa;
    struct addrinfo hints;
    struct addrinfo *result;    

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    if((status = getaddrinfo(bindAddr, NULL, &hints, &result)) != 0) {
      LogError("%s: Cannot translate '%s' to IP address -- %s\n", prog, bindAddr, gai_strerror(status));
      goto error;
    }
    sa = (struct sockaddr_in *)result->ai_addr;
    memcpy(&myaddr, sa, result->ai_addrlen);
    freeaddrinfo(result);
  } else {
    myaddr.sin_addr.s_addr= htonl(INADDR_ANY);
  }
  myaddr.sin_family= AF_INET;
  myaddr.sin_port= htons(port);

  if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag)) < 0)  {
    LogError("%s: Cannot set reuseaddr option -- %s\n", prog, STRERROR);
    goto error;
  }
  
  if(! set_noblock(s))
    goto error;
  
  if(fcntl(s, F_SETFD, FD_CLOEXEC) == -1) {
    LogError("%s: Cannot set close on exec option -- %s\n", prog, STRERROR);
    goto error; 
  }
  
  if(bind(s, (struct sockaddr *)&myaddr, sizeof(struct sockaddr_in)) < 0) {
    LogError("%s: Cannot bind -- %s\n", prog, STRERROR);
    goto error;
  }
  
  if(listen(s, backlog) < 0) {
    LogError("%s: Cannot listen -- %s\n", prog, STRERROR);
    goto error;
  }
  
  return s;

  error:
  close(s);

  return -1;

}


/**
 * Shutdown a socket and close the descriptor.
 * @param socket The socket to shutdown and close
 * @return TRUE if the close succeed otherwise FALSE
 */
int close_socket(int socket) {

  int r;

  shutdown(socket, 2);
  
  do {
    r= close(socket);
  } while(r == -1 && errno == EINTR);
  
  return r;

}


/**
 * Enable nonblocking i|o on the given socket.
 * @param socket A socket
 * @return TRUE if success, otherwise FALSE
 */
int set_noblock(int socket) {
  int flags = fcntl(socket, F_GETFL, 0);

  if (fcntl(socket, F_SETFL, flags|O_NONBLOCK) == -1) {
    LogError("%s: Cannot set nonblocking -- %s\n", prog, STRERROR);
    return FALSE;
  }
  return TRUE;
}


/**
 * Disable nonblocking i|o on the given socket
 * @param socket A socket
 * @return TRUE if success, otherwise FALSE
 */
int set_block(int socket) {

  int flags;

  flags= fcntl(socket, F_GETFL, 0);
  flags &= ~O_NONBLOCK;

  return (fcntl(socket, F_SETFL, flags) == 0);

}


/**
 * Check if data is available, if not, wait timeout seconds for data
 * to be present.
 * @param socket A socket
 * @param timeout How long to wait before timeout (value in seconds)
 * @return Return TRUE if the event occured, otherwise FALSE.
 */
int can_read(int socket, int timeout) {
  int r = 0;
  struct pollfd fds[1];

  fds[0].fd = socket;
  fds[0].events = POLLIN;
  do {
    r = poll(fds, 1, timeout * 1000);
  } while (r == -1 && errno == EINTR);
  return (r > 0);
}


/**
 * Check if data can be sent to the socket, if not, wait timeout
 * seconds for the socket to be ready.
 * @param socket A socket
 * @param timeout How long to wait before timeout (value in seconds)
 * @return Return TRUE if the event occured, otherwise FALSE.
 */
int can_write(int socket, int timeout) {
  int r = 0;
  struct pollfd fds[1];

  fds[0].fd = socket;
  fds[0].events = POLLOUT;
  do {
    r = poll(fds, 1, timeout * 1000);
  } while (r == -1 && errno == EINTR);
  return (r > 0);
}


/**
 * Write <code>size</code> bytes from the <code>buffer</code> to the
 * <code>socket</code> 
 * @param socket the socket to write to
 * @param buffer The buffer to write
 * @param size Number of bytes to send
 * @param timeout Seconds to wait for data to be written
 * @return The number of bytes sent or -1 if an error occured.
 */
int sock_write(int socket, const void *buffer, int size, int timeout) {

  ssize_t n= 0;
  
  if(size<=0)
      return 0;
  
  errno= 0;
  do {
    n= write(socket, buffer, size);
  } while(n == -1 && errno == EINTR);
  
  if(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if(! can_write(socket, timeout)) {
      return -1;
    }
    do {
      n= write(socket, buffer, size);
    } while(n == -1 && errno == EINTR);
  }
  
  return n;

}


/**
 * Read up to size bytes from the <code>socket</code> into the
 * <code>buffer</code>. If data is not available wait for
 * <code>timeout</code> seconds.
 * @param socket the Socket to read data from
 * @param buffer The buffer to write the data to
 * @param size Number of bytes to read from the socket
 * @param timeout Seconds to wait for data to be available
 * @return The number of bytes read or -1 if an error occured. 
 */
int sock_read(int socket, void *buffer, int size, int timeout) {
  
  ssize_t n;

  if(size<=0)
      return 0;
  
  errno= 0;
  do {
    n= read(socket, buffer, size);
  } while(n == -1 && errno == EINTR);
  
  if(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if(! can_read(socket, timeout)) {
      return -1;
    }
    do {
      n= read(socket, buffer, size);
    } while(n == -1 && errno == EINTR);
  }

  return n;

}


/**
 * Write <code>size</code> bytes from the <code>buffer</code> to the
 * <code>socket</code>. The given socket <b>must</b> be a connected
 * UDP socket
 * @param socket the socket to write to
 * @param buffer The buffer to write
 * @param size Number of bytes to send
 * @param timeout Seconds to wait for data to be written
 * @return The number of bytes sent or -1 if an error occured.
 */
int udp_write(int socket, void *b, int len, int timeout) {
  
  int i, n;
  
  ASSERT(timeout>=0);
  
  for(i= 4; i>=1; i--) {
    
    do {
      n= sock_write(socket, b, len, 0);
    } while(n == -1 && errno == EINTR);
    
    if(n == -1 && (errno != EAGAIN || errno != EWOULDBLOCK))
      return -1;
    
    /* Simple retransmit scheme, wait for the server to reply 
    back to our socket. This assume a request-response pattern, 
    which really is the only pattern we can support */
    if(can_read(socket, timeout/i)) return n;
    DEBUG("udp_write: Resending request\n");
    
  }
  
  errno= EHOSTUNREACH;
  
  return -1;
  
}


/**
 * Create a ICMP socket against hostname, send echo and wait for response.
 * The 'count' echo requests  is send and we expect at least one reply.
 * @param hostname The host to open a socket at
 * @param timeout If response will not come within timeout seconds abort
 * @param count How many pings to send
 * @return response time on succes, -1 on error
 */
double icmp_echo(const char *hostname, int timeout, int count) {
  struct sockaddr_in sin;
  struct sockaddr_in sout;
  struct sockaddr_in *sa;
  struct addrinfo hints;
  struct addrinfo *result;
#ifdef HAVE_SOL_IP
  struct iphdr *iphdrin;
  struct icmphdr *icmphdrin = NULL;
  struct icmphdr *icmphdrout[ICMP_MAX_COUNT];
#else
  struct ip *iphdrin;
  struct icmp *icmphdrin = NULL;
  struct icmp *icmphdrout[ICMP_MAX_COUNT];
#endif
  int i;
  int s;
  int n = 0;
  int sol_ip;
  unsigned ttl = 255;
  char buf[STRLEN];
  struct timeval t1[ICMP_MAX_COUNT];
  struct timeval t2;
  double response= -1.;
  
  ASSERT(hostname);   
  ASSERT(count <= ICMP_MAX_COUNT);

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  if (getaddrinfo(hostname, NULL, &hints, &result) != 0)
    return response;

  if ((s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
    freeaddrinfo(result);
    return response;
  }

#ifdef HAVE_SOL_IP
  sol_ip = SOL_IP;
#else
  {
    struct protoent *pent;
    pent = getprotobyname("ip");
    sol_ip = pent ? pent->p_proto : 0;
  }
#endif

  if (setsockopt(s, sol_ip, IP_TTL, (char *)&ttl, sizeof(ttl)) < 0) {
    freeaddrinfo(result);
    goto error2;
  }

  for (i = 0; i < count; i++) {
    NEW(icmphdrout[i]);
#ifdef HAVE_SOL_IP
    icmphdrout[i]->code             = 0;
    icmphdrout[i]->type             = ICMP_ECHO;
    icmphdrout[i]->un.echo.id       = getpid() + (long)hostname + time(NULL);
    icmphdrout[i]->un.echo.sequence = i;
    icmphdrout[i]->checksum         = checksum_ip((unsigned char *)icmphdrout[i], ICMP_SIZE);
#else
    icmphdrout[i]->icmp_code  = 0;
    icmphdrout[i]->icmp_type  = ICMP_ECHO;
    icmphdrout[i]->icmp_id    = getpid() + (long)hostname + time(NULL);
    icmphdrout[i]->icmp_seq   = i;
    icmphdrout[i]->icmp_cksum = checksum_ip((unsigned char *)icmphdrout[i], ICMP_SIZE);
#endif
    sa = (struct sockaddr_in *)result->ai_addr;
    memcpy(&sout, sa, result->ai_addrlen);
    sout.sin_family = AF_INET;
    sout.sin_port   = 0;

    /* Get time of particular connection attempt beginning */
    gettimeofday(&t1[i], NULL);

    do {
      n = sendto(s, (char *)icmphdrout[i], ICMP_SIZE, 0, (struct sockaddr *)&sout, sizeof(struct sockaddr));
    } while(n == -1 && errno == EINTR);
  }
  freeaddrinfo(result);
  
  if (can_read(s, timeout)) {
    socklen_t size = sizeof(struct sockaddr_in);

    do {
      n = recvfrom(s, buf, STRLEN, 0, (struct sockaddr *)&sin, &size);
    } while(n == -1 && errno == EINTR);
    
    if (n < 0)
      goto error1;

    for (i = 0; i < count; i++) {
#ifdef HAVE_SOL_IP
      iphdrin = (struct iphdr *)buf;
      icmphdrin = (struct icmphdr *)(buf + iphdrin->ihl * 4);
      if ( (icmphdrin->un.echo.id == icmphdrout[i]->un.echo.id) && (icmphdrin->type == ICMP_ECHOREPLY) && (icmphdrin->un.echo.sequence == icmphdrout[i]->un.echo.sequence) ) {
#else
      iphdrin = (struct ip *)buf;
      icmphdrin = (struct icmp *)(buf + iphdrin->ip_hl * 4);
      if ( (icmphdrin->icmp_id == icmphdrout[i]->icmp_id) && (icmphdrin->icmp_type == ICMP_ECHOREPLY) && (icmphdrin->icmp_seq == icmphdrout[i]->icmp_seq) ) {
#endif

        /* Get time of connection attempt finish */
        gettimeofday(&t2, NULL);

        /* Get the response time */
        response = (double)(t2.tv_sec - t1[i].tv_sec) + (double)(t2.tv_usec - t1[i].tv_usec) / 1000000;
      }
    }
  }

  error1:
  for (i = 0; i < count; i++)
    FREE(icmphdrout[i]);
  error2:
  close_socket(s);

  return response;
}


/* ----------------------------------------------------------------- Private */


/*
 * Do a non blocking connect, timeout if not connected within timeout seconds
 */
static int do_connect(int s, const struct sockaddr *addr, socklen_t addrlen, int timeout) {
  int error;
  struct pollfd fds[1];

  switch (connect(s, addr, addrlen)) {
    case 0:
      return 0;
    default:
      if (errno != EINPROGRESS)
        return -1;
      break;
  }
  fds[0].fd = s;
  fds[0].events = POLLIN|POLLOUT;
  if (poll(fds, 1, timeout * 1000) == 0) {
    errno = ETIMEDOUT;
    return -1;
  }
  if (fds[0].events & POLLIN || fds[0].events & POLLOUT) {
    socklen_t len = sizeof(error);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
      return -1; // Solaris pending error
  } else {
    return -1;
  }
  if (error) {
    errno = error;
    return -1;
  }
  return 0;
}


/*
 * Compute Internet Checksum for "count" bytes beginning at location "addr".
 * Based on RFC1071.
 */
static unsigned short checksum_ip(unsigned char *_addr, int count) {

  register long sum= 0;
  unsigned short *addr= (unsigned short *)_addr;

  while(count > 1) {
    sum += *addr++;
    count -= 2;
  }

  /* Add left-over byte, if any */
  if(count > 0)
    sum += *(unsigned char *)addr;

  /* Fold 32-bit sum to 16 bits */
  while(sum >> 16)
    sum= (sum & 0xffff) + (sum >> 16);

  return ~sum;

}

