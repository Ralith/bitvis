#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

// get sockaddr, IPv4 or IPv6:
inline void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int do_connect(GtkWindow *root, const char *host, const char *port) {
  int fd;
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
    GtkWidget *dialog = gtk_message_dialog_new(
      root,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to resolve host %s:\n%s",
      host, gai_strerror(rv));
    gtk_dialog_run(GTK_DIALOG (dialog));
    gtk_widget_destroy(dialog);
    return -1;
  }

  // loop through all the results and connect to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if((fd = socket(p->ai_family, p->ai_socktype,
                        p->ai_protocol)) == -1) {
      perror("client: socket");
      continue;
    }

    if(connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
      close(fd);
      perror("client: connect");
      continue;
    }

    break;
  }

  if(p == NULL) {
    GtkWidget *dialog = gtk_message_dialog_new(
      root,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to connect to host %s",
      host);
    gtk_dialog_run(GTK_DIALOG (dialog));
    gtk_widget_destroy(dialog);
    return -1;
  }
  
  freeaddrinfo(servinfo); // all done with this structure

  return fd;
}
