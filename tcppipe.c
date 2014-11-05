#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <sys/syslog.h>
#include <stdlib.h>
#include <errno.h>
#include "tcppipe.h"
extern int optind;

long get_offset(FILE *sfp);
int save_offset(FILE *sfp, long offset);
void check_and_forward(FILE *fp,  FILE *sfp, int s);
int do_handshake(int s, char *filename);

static void sig_alrm_return(int signo);
static void sig_pipe(int signo);
static void sig_hup(int signo);
static void sig_term(int signo);

#define STATE_FILE_SUFFIX ".pipe"
#define PID_FILE_SUFFIX ".pid"

#define DEFAULT_TIME_INTERVAL 1  /* 1 second */

/************* global variables ***********/

/* global variables shared by sig_hup() and main() */
FILE *pFile, *pStatusFile, *pPIDFile;
int s; /* socket */

char full_filename[MAX_LINE]; /* file that needs to be forwarded */
char state_filename[MAX_LINE]; /* file that stores offset information */
char pid_filename[MAX_LINE];     /* file that stores pid of the process */
/* global variables used by sig_pipe() and main()*/
static char *server_name;

/*******************************************/

int main(int argc, char *argv[])
{
  	extern char *optarg;
  	int opt; /* command line option */
  	int DEBUG=0, new_status_file=0, show_pid=0, errflag=0, ret=0;
	
  	int server_port= DEFAULT_SERVER_PORT;
	struct sockaddr_in sin;
	struct hostent *hp;

	char *filename= NULL;
	char *hostname= NULL; /* host running tcppiped*/
	long offset=0;

	/* time interval for checking file status */
	int interval= DEFAULT_TIME_INTERVAL;
 
	int i;

	/* get the command options */
  	/* tcppipe -f <filename> -h <hostname> [-p <port number>] 
	   [-t <time interval(seconds)>] -n -d */
  	while((opt = getopt(argc, argv, "f:h:p:t:ndI")) != EOF)
    		switch(opt){
    		case 'd':
      			DEBUG=1;
      			break;
    		case 'f':
      			filename= optarg;
			break;
    		case 'h':
			hostname= optarg;
      			break;
		case 'p':
			server_port=atoi(optarg);
			break;
		case 't':
			interval= atoi(optarg);
			if( interval <1){
				fprintf(stderr,
				       "Invalid time interval, at least one second\n");
				exit(1);
			}
			break;
		case 'n':
			new_status_file++;
			break;

		case 'I':
			show_pid++;
			break;
		default:
      			errflag=1;
      			break;
    		}

  
	if(errflag|| filename==NULL || hostname==NULL|| argc> optind){
		fprintf(stderr, "Bad arguments, Usage:\ntcppipe -f <filename> -h <hostname> [-p port#] [-t] [-n] [-d] [-I]\n");
		exit(1);
	}

	/* get the full pathname of the file that needs forwarding */
	realpath(filename, full_filename);

	/* make up the names of status file and pid file */
	base_state_filename(state_filename, RUNTIME_FILE_DIR,
			    hostname, server_port, full_filename);
	
	strcpy(pid_filename, state_filename);

	/* make up state_filename, /tmp/"hostname-#.filename.hashvalue.pipe"*/
	strcat(state_filename, STATE_FILE_SUFFIX);

	
	/* make up state_filename, /tmp/"hostname-#.filename.hashvalue.pid"*/
	strcat(pid_filename, PID_FILE_SUFFIX);

	/* check if there is already a process running with the same parameters */
	if(test_file(pid_filename)){
		if(show_pid){
			char buf[MAX_LINE];
			pPIDFile= fopen(pid_filename, "r");
			fgets(buf, sizeof(buf),  pPIDFile);
			printf("%s", buf);
			fclose(pPIDFile);
			exit(0);
		}
		fprintf(stderr,
			"WARNING: Another copy of tcppipe is running with the same parameters\n");
		
	}else{
		/* there is no process running with the same parameters */
		if(show_pid)
			exit(1);
	}
	if((pFile= fopen(full_filename,"r"))==NULL){
                syslog(LOG_CRIT|FACILITY,
                       "can't open file %s", filename);
		fprintf(stderr,
		       "can't open file %s\n", filename);
		exit(1);
	}

	if( (hp = gethostbyname(hostname)) ==0){
                syslog(LOG_CRIT|FACILITY,
                       "unknown host %s", hostname);
		fprintf(stderr, 
		       "unknown host %s\n", hostname);
		exit(1);
	}

	/* initialize host address */	
	memset(&sin, 0, sizeof(struct sockaddr_in));
	memcpy((char*)&sin.sin_addr, hp->h_addr,hp->h_length);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(server_port);
	
	/* initialize socket */
	if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
                syslog(LOG_CRIT|FACILITY,
                       "can not open a socket");
		fprintf(stderr,
		       "can not open a socket\n");
		exit(1);
	}
	
	
	/* connect to host */
	if( connect( s, (struct sockaddr *)&sin, sizeof(sin))<0){
                syslog(LOG_CRIT|FACILITY,
                       "cannot connect to host %s: %s",
                       hostname, strerror(errno));
		fprintf(stderr,
		       "cannot connect to host %s: %s\n", 
		       hostname, strerror(errno));
		exit(1);
	}

	
	/* do handshake with host */
	if((ret=do_handshake(s, full_filename))<0){
                syslog(LOG_CRIT|FACILITY,
                       "handshake with host %s failed", hostname);
		fprintf(stderr,
		       "handshake with host %s failed\n", hostname);
		exit(1);
	}
	else if(ret==0){
                syslog(LOG_CRIT|FACILITY,
                       "the pipe connection is refused by host %s", hostname);
		fprintf(stderr,
		       "the pipe connection is refused by host %s\n", hostname);
		exit(1);
	}
        syslog(LOG_INFO|FACILITY,
        	"Established pipe (%s) to host %s", filename, hostname);
	fprintf(stderr,
                "Established pipe (%s) to host %s\n", filename, hostname);
		
	if( new_status_file || (pStatusFile= fopen(state_filename, "r+")) == NULL){
		if((pStatusFile=fopen(state_filename, "w+"))==NULL){
                        syslog(LOG_CRIT|FACILITY,
                       "error in opening state file %s, cannot continue",
                               state_filename);
 
			fprintf(stderr,
		       "error in opening state file %s, cannot continue\n",
			       state_filename);
			exit(1);
		}
	}

	/* load file, and seek to previous point */
	offset = get_offset(pStatusFile);

	if( offset<0){
                syslog(LOG_CRIT|FACILITY,
                       "corrupted status file %s, exit...",
                       state_filename);
		fprintf(stderr,
		       "corrupted status file %s, exit...\n",
		       state_filename);
		exit(1);
	}

	fseek(pFile, 0, SEEK_END);
	if( offset> ftell(pFile)){
		if(DEBUG){
			syslog(LOG_DEBUG|FACILITY,	
		       "file %s was truncated, reset reading position to 0",
		       full_filename);
			fprintf(stderr,
                       "file %s was truncated, reset reading position to 0\n",
                       full_filename);
		}
		offset=0;
		fseek(pFile, 0, SEEK_SET);
	}
	else
		fseek(pFile, offset, SEEK_SET);

	/* make it a daemon process */
	if(!DEBUG && fork()){
		    exit(0);
	}

	if(!DEBUG)
		setsid();

	if((pPIDFile= fopen(pid_filename, "w")) != NULL) {
		fprintf(pPIDFile, "%d\n", getpid());
		fclose(pPIDFile);
	}else{
		syslog(LOG_CRIT|FACILITY,
		       "failed to write pid file %s",
		       pid_filename);
		sig_term(1);
	}
	
	/* These  variables are only used by sig_pipe() */
	server_name = hostname;

	signal(SIGPIPE, sig_pipe);

	signal(SIGTERM, sig_term);

#ifdef __sun
	sigset(SIGHUP, sig_hup);
#else
	signal(SIGHUP, sig_hup);
#endif

	while(1){
		check_and_forward(pFile, pStatusFile, s);
		sleep(interval);
	}
}

/* return -1, if there is errors;
   return 0, if the connection is refused by the server;
   return 1, if handshake is sucessful
*/
int do_handshake(int s, char *filename)
{
	char buf[MAX_LINE];

	sprintf(buf, "%c%s\n", CMD_CHAR, filename);
	
	writen(s, buf, strlen(buf));
	signal(SIGALRM, sig_alrm_return);
	alarm(5);
	/* allow 5 seconds to do handshake */
	if( readline(s, buf, sizeof(buf))< 0){
		syslog(LOG_CRIT|FACILITY,"error: %s", strerror(errno));
		return -1;
	}
	alarm(0);
	if(buf[0]==CMD_CHAR)
		if(strncmp(buf+CMD_PREFIX_LEN, 
			   CMD_READY,strlen(CMD_READY))==0)
			return 1;
		else if(strncmp(buf+CMD_PREFIX_LEN, 
				CMD_REFUSE, strlen(CMD_REFUSE))==0)
			return 0;
	return -1;
}

/* return -1, if there is any error */
/* return the offset, if successful */
long get_offset(FILE * sfp)
{
	char buf[80];

	if( fseek(sfp, 0, SEEK_SET) <0)
		return -1L;	
	if(fgets(buf, 80, sfp)!= NULL)
		return atol(buf);
	else if(ferror(sfp))
		return -1L;
	else /* it is a empty file */
		return 0L; 
}

/* return -1, if there is any error */
/* return number of bytes that has been written, if successful*/
int save_offset(FILE *sfp, long offset)
{
	int n;

	if( fseek(sfp, 0, SEEK_SET) <0)
		return -1;
	n= fprintf(sfp, "%ld\n", offset);
	fflush(sfp);
	return n;
}

void check_and_forward(FILE *fp,  FILE *sfp, int s)
{
  	char buf[MAX_LINE];
	char esc_char= ESC_CHAR;
	int len=0, total_len=0, ack_len=0;

	total_len=0;

	/* reset the EOF */
	clearerr(fp);

	while(fgets(buf, sizeof(buf), fp)){
		len = strlen(buf);
		total_len += len;

		if(buf[0]==ESC_CHAR || buf[0]==CMD_CHAR || buf[0]== '\n'){
			total_len ++;
			if(writen(s, &esc_char, 1)<1){
				syslog(LOG_CRIT|FACILITY,
				       "sending messages failed: %s",strerror(errno));
				sig_term(1);
			}
		}
		if(writen(s, buf, len)<len){
			syslog(LOG_CRIT|FACILITY,
			       "sending messages failed: %s",strerror(errno));
			sig_term(1);
		}
	}
	if(total_len>0){
		/********************* IMPORTANT ************************/
		/*** Here send one additional '\n' to ensure boundary ***/
		/********************************************************/
		/* send "segment boundary information"
		   solicit acknowledgement */
		sprintf(buf, "\n%c\n", CMD_CHAR);
		len= 3;		
		if(writen(s, buf, len)<len){
			syslog(LOG_CRIT|FACILITY,
			       "sending messages failed: %s",strerror(errno));
			sig_term(1);
		}
		if((ack_len= readline(s, buf, sizeof(buf)))<0){
			syslog(LOG_CRIT|FACILITY,
			       "cannot get acks, %s",strerror(errno));
			sig_term(1);
		}
		if(buf[0]==CMD_CHAR && atoi(buf+1)== total_len+3)
			save_offset(sfp, ftell(fp));
		else{ /* this should not happen */
			syslog(LOG_CRIT|FACILITY,
			       "sending messages failed unexpectedly");
			sig_term(1);
		}
		
	}		
	
}
  
static void
sig_alrm_return(int signo)
{
	return;
}

/* catch signal SIGPIPE, when the server process closed the socket,
   and tcppipe attempted to write to the socket
*/
static void
sig_pipe(int signo)
{
	syslog(LOG_CRIT|FACILITY,"tcppiped process on %s terminated prematurely", server_name);
	sig_term(1);
}

/* reset offset to 0, reopen the file that needs to be forwarded */
static void
sig_hup(int signo)
{
	/* make sure, drain up the messages in old file */
	check_and_forward(pFile, pStatusFile, s);
	syslog(LOG_INFO|FACILITY,
	       "restart tcppipe and set reading position to 0");
	fclose(pFile);
	fclose(pStatusFile);
	
	if((pFile= fopen(full_filename,"r"))==NULL){
		syslog(LOG_CRIT|FACILITY,
		       "can't open file %s", full_filename);
		sig_term(1);
	}
	if((pStatusFile= fopen(state_filename, "w+"))==NULL){
		syslog(LOG_CRIT|FACILITY,
		       "error in opening state file %s, cannot continue",
		       state_filename);
		sig_term(1);
	}
}

static void
sig_term(int signo)
{
	unlink(pid_filename);
	syslog(LOG_CRIT|FACILITY,
		"tcppipe exit...");
	exit(signo);
}
