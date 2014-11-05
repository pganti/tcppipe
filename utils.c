#include <stdio.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <errno.h> 
#include "tcppipe.h"

/* these two functions are adapted from Unix Network Programming, 
   W. Richard Stevens
*/

ssize_t
writen(int fd, const void *vptr, size_t n)
{
	size_t nleft;
	ssize_t nwritten;
	const char *ptr;

	ptr = vptr;
	nleft =n;
	while( nleft >0) {
		if ((nwritten =write(fd, ptr, nleft)) <=0) {
			if(errno==EINTR)
				nwritten =0; /* and call write() again */
			else
				return (-1); /* error */
		}
		nleft -= nwritten;
		ptr += nwritten;
	}
	return (n);
}

ssize_t
readline(int fd, void *vptr, size_t maxlen)
{
	ssize_t n, rc;
	char c, *ptr;
	
	ptr= vptr;
	for(n=1; n<maxlen; n++) {
	again:
		if( (rc=read(fd, &c, 1))==1) {
  			*ptr++ =c;
			if(c=='\n')
				break; /* newline is stored, like fgets() */
		}else if( rc==0){
			if(n==1)
				return (0); /* EOF, no data read */
			else
				break; /* EOF, some data was read */
		} else {
			if( errno==EINTR)
				goto again;
			
			return -1; /* error, errno set by read() */
		}
	}
	*ptr =0; /* null terminate like fgets() */
	return (n);
}

int test_file(char *filename){
	FILE *pf;
	if((pf=fopen(filename, "r"))==NULL)
		return 0;
	fclose(pf);
	return 1;
}

int hash(char *filename){
	int i;
	int total=1;
	for(i=0; i<strlen(filename); i++){
		total *= filename[i];
		total = total % (0x1fff);
	}
	return total;
}

/* get rid of trailing spaces */
char *chop(char *str)
{
	int i, len;
	len= strlen(str);
	for(i=len-1; i>=0; i--)
		if(str[i]==' '||str[i]=='\t'||str[i]=='\n')
			str[i]='\0';
		else
			break;
	return str;
}

char *base_state_filename(char *buf, char *dir, char *hostname, int port,
			  char *filename)
{
	sprintf(buf, "%s/%s-%d.%s.%4d", dir,
		hostname, port,  basename(filename), hash(filename));
	return buf;
}
