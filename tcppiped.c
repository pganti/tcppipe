#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include "tcppipe.h"

/* # of connections from clients allowed pending at the master socket */
#define MAX_PENDING 10 
#define MAX_PATHNAME_LEN 256

#define DEFAULT_CONF_FILENAME "/etc/tcppipe.conf"

/* structure that describe a connection (pipe) */
struct entry_t{
	char hostname[MAX_PATHNAME_LEN];
	struct sockaddr_in sin; /* client host address */
	char remote_filename[MAX_PATHNAME_LEN];
	char local_filename[MAX_PATHNAME_LEN];
} entry;
 
char * confname= DEFAULT_CONF_FILENAME;
char pid_filename[MAX_LINE];

FILE *f; /* file stream of target file, NOT configuration file */

FILE* do_handshake(int socket, char *conf_file, struct entry_t * pentry);
char *find_local_filename(char *cfname, struct entry_t *pentry);
static void sig_alrm(int signo);
static void sig_chld(int signo);
static void sig_hup(int signo);
static void sig_term(int signo);
static void sig_resend_hup(int signo);

int main(int argc, char *argv[])
{
	extern char *optarg;
	int opt; /* command line option */
	int errflag=0, DEBUG=0, ret; 

	struct sockaddr_in sin;
	int server_port= DEFAULT_SERVER_PORT;
	int s; /* master listening socket */
	int s1; /* actual socket to receive messages */

	char buf[MAX_LINE];
	int len=0, total_len=0;

	/* get the command options        */
	/* server [-f <configuration file>] */

	while((opt = getopt(argc, argv, "f:p:d")) != EOF)
		switch(opt){
		case 'f':
			confname= optarg;
			break;
		case 'p':
			server_port= atoi(optarg);
			break;
		case 'd':
			DEBUG=1;
			break;
		default:
			errflag=1;
		}
	if(errflag){
		syslog(LOG_CRIT|FACILITY, 
		       "Bad arguments, usage: tcppiped [-f configuration file] [-p port#] [-d]");
		fprintf(stderr, 
		       "Bad arguments, usage: tcppiped [-f configuration file] [-p port#] [-d]\n");
		exit(1);
	}	
	
 
	if(!test_file(confname)){
		syslog(LOG_CRIT|FACILITY, 
		       "configuration file %s does not exist, abort...",
		       confname);
		fprintf(stderr, 
		       "configuration file %s does not exist, abort...\n",
		       confname);
		exit(1);
	}

	/* build socket address structure */	
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr =INADDR_ANY;
	sin.sin_port = htons(server_port);

	/* open the socket */
	if ((s=socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
		syslog(LOG_CRIT|FACILITY,
		       "error in creating master socket, abort...");
		fprintf(stderr,
		       "error in creating master socket, abort...\n");
		exit(1);
	}
			
	/* bind the socket */	
	if(bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0 ){
		syslog(LOG_CRIT|FACILITY,
		       "error in binding master socket, abort...");
		fprintf(stderr,
		       "error in binding master socket, abort...\n");
		exit(1);
	}
	
	if(!DEBUG && fork()){
		exit(0);
	}
	if(!DEBUG)
		setsid();


	/* write pid to a file */
	sprintf(pid_filename, "%s/tcppiped.pid", RUNTIME_FILE_DIR);

	if((f=fopen(pid_filename, "w"))== NULL){
		syslog(LOG_CRIT|FACILITY,
			"can't write to %s, abort...", pid_filename);
		fprintf(stderr,
			"can't write to %s\n, abort...", pid_filename);
		exit(1);	
	}else{
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
        syslog(LOG_INFO|FACILITY,
                        "tcppiped started...");

			
	/* start listening on the socket for incoming data */	
	listen(s, MAX_PENDING);
  
	#ifdef __sun
	sigset(SIGTERM, sig_term);
	#else
	signal(SIGTERM, sig_term);
	#endif

	#ifdef __sun
	sigset(SIGALRM, sig_alrm);
	#else
	signal(SIGALRM, sig_alrm);
	#endif

	#ifdef __sun
	sigset(SIGCHLD, sig_chld);
	#else
	signal(SIGCHLD, sig_chld);
	#endif

	/* the parent process resends SIGHUP to children processes*/
	#ifdef __sun
	sigset(SIGHUP, sig_resend_hup);
	#else
	signal(SIGHUP, sig_resend_hup);
	#endif

	while(1){
		len = sizeof(sin);  

		/* len also gets a return value from accept */  
		/* accept() blocks until someone connects */
		if((s1 = accept(s, (struct sockaddr *)&sin, &len)) < 0){
			/* interrupt system call most likely is caused
			   by SIG_PIPE signal, which is not an error
			*/
			if(errno!= EINTR)
				syslog(LOG_CRIT|FACILITY, 
				       "error in accepting a connection: %s",
				       strerror(errno));
			continue;
		}
		memcpy(&entry.sin, &sin, sizeof(sin));
		inet_ntop(AF_INET, &sin.sin_addr,
			  entry.hostname, MAX_PATHNAME_LEN);
		if(fork()==0){ /* child process */
			close(s);
			signal(SIGTERM, SIG_DFL);

			/* handshake to see whether to accept the connection*/
			alarm(5); /* only allow 5 seconds to do handshake*/
			if((f= do_handshake(s1, confname, &entry)) == NULL){
				syslog(LOG_CRIT|FACILITY,
				       "handshake with host %s failed",
				       inet_ntop(AF_INET, &sin.sin_addr,
						 buf, sizeof(buf)));
				close(s1);
				exit(1);
			}
			alarm(0);
			
                        syslog(LOG_INFO|FACILITY,
                                "Established connection with host %s(%s) updating (local) file %s",
				entry.hostname, entry.remote_filename,
				entry.local_filename); 

			#ifdef __sun
			sigset(SIGHUP, sig_hup);
			#else 
			signal(SIGHUP, sig_hup);
			#endif

			/* begin to read from the socket */	
			total_len=0;
			while ( (len = readline(s1, buf, sizeof(buf)))>0){
				total_len +=len;
				/* ignore empty lines */
				if(buf[0]=='\n')
					continue;

				/* client requested a confirmation */
				if(buf[0]== CMD_CHAR){
					sprintf(buf, "%c%d\n", CMD_CHAR,
						total_len);

					writen(s1, buf, strlen(buf));
					total_len=0;
					continue;
				}
				
				/* process data, write it to the local file */
				/* remember to process ESC character */
				if(buf[0]== ESC_CHAR)
					ret= fputs(buf+1, f);
				else
					ret= fputs(buf, f);
				
				/* if there is error in writing to file */
				if( ret == EOF){
					syslog(LOG_CRIT|FACILITY,
					       "cannot write to file%s, stop logging from %s:%s",
					       entry.local_filename, entry.hostname, entry.remote_filename);
					close(s1);
					exit(1);
				}
				fflush(f);
			}
			
			/* if client closed the pipe, len will be 0 */
			/* if there is an error, len will be -1     */
			if(len<0 ){
				syslog(LOG_CRIT|FACILITY, 
				       "Error: %s, stop logging from %s (%s)",
				       strerror(errno),
				       entry.hostname, entry.remote_filename);
				exit(1);
			}
			
			syslog(LOG_INFO|FACILITY,
			       "connection was closed by %s (%s)",
			       entry.hostname, entry.remote_filename);
			exit(0);
		}
		close(s1);
	}
}

/* return -1 if there is any errors */
/* return  1 if it is sucessful     */
/* return the file stream of local destination file */
/* NULL if failed */
FILE* do_handshake(int s, char *cf, struct entry_t * pentry){

	char buf[MAX_LINE];	

	if( readline(s, buf, sizeof(buf))< 0){
			syslog(LOG_CRIT|FACILITY,
			       "Error in waiting handshake messages from %s): %s",
			       pentry->hostname, strerror(errno));
			return NULL;
	}

	/* all commands begin with a CMD_CHAR */
	if(buf[0]== CMD_CHAR){
		strncpy(pentry->remote_filename, 
			buf+CMD_PREFIX_LEN, MAX_PATHNAME_LEN);
		pentry->remote_filename[MAX_PATHNAME_LEN-1]='\0';

		/* looking for an entry in configuration file */
		if( find_local_filename(cf, pentry)){
			f= fopen(entry.local_filename, "a");
			if(f!=NULL){
	                        /* send ready command*/
	                        sprintf(buf, "%c%s\n", CMD_CHAR, CMD_READY);
       	                	writen(s, buf, strlen(buf));
                        	return f;
			}else{
                                syslog(LOG_CRIT|FACILITY,
                                    "can't open file %s for pipe from %s (%s)",
                                    pentry->local_filename, pentry->hostname,
                                    pentry->remote_filename);
			}
		}
		/* send refuse command */
		syslog(LOG_CRIT|FACILITY, 
		       "connection from %s (%s) is refused",
		       pentry->hostname, pentry->remote_filename);
		sprintf(buf, "%c%s\n", CMD_CHAR, CMD_REFUSE);
		writen(s, buf, strlen(buf));
		return NULL;
	}
	syslog(LOG_CRIT|FACILITY,
	       "unkown command: %s",buf);
	return NULL;
}
	
/* return local filename corresponding to the hostname:remote_filename */
/* which is also copied to local_filename in entry structure           */
/* return NULL, if there is errors in reading configuration file       */
/* or there is no such an entry in configuration file                  */

char * find_local_filename(char *cfname, struct entry_t *pentry)
{
	FILE *pcf; /* file stream of configuration file */
	char buf[MAX_LINE];
	int nl=0;  /* line counter */

	/* temporary variables for parsing */
	char *hostname, *remote_filename, *local_filename;
	struct hostent *hp;
	
	/* open configuration file */
	if((pcf= fopen(cfname, "r"))==NULL){
		syslog(LOG_CRIT|FACILITY, 
		       "can't open configuration file %s", cfname);
		pentry->local_filename[0]= '\0';
		return NULL;
	}
	nl=0;
	/* read one line*/
	while(fgets(buf, sizeof(buf), pcf)){
		int i, buflen;
		/* pointer to three tokens in one line */
		/* hostname:filename | localfilename   */
		char* plist[3]; 
		int k=0; /* state of finite automata */

		buflen= strlen(buf);
		hostname=remote_filename=local_filename= NULL;

		nl++;
		/* skip empty lines and lines begin with # */
		if(buf[0]=='\n' || buf[0]=='#')
			continue;
		for (i=0; i<buflen; i++){
			if(buf[i]==' '||buf[i]=='\t'|| buf[i]=='\n')
				continue;
			if(k==0){
				hostname= buf+i;
				k++;
			}
			else if(k==1){
				if(buf[i]==':'){
					k++;
					buf[i]='\0';
				}
			}
			else if(k==2){
				remote_filename= buf+i;
				k++;
			}
			else if(k==3){
				if(buf[i]=='|'){
					k++;
					buf[i]='\0';
				}
			}
			else if(k==4){
				local_filename= buf+i;
				break;
			}
		}
					
		if( hostname==NULL || 
		    remote_filename==NULL || local_filename==NULL){
			syslog(LOG_CRIT|FACILITY,
			       "invalid field in line %d of file %s", nl, cfname);
			continue;
		}
			

		/* check if hostname:remote_filename pair matches */
		chop(pentry->remote_filename);
		chop(remote_filename);
		chop(local_filename);

		if(local_filename[0]!='/'){
			syslog(LOG_CRIT|FACILITY,
				"invalid field in line %d of file %s: filename must begin with '/'", nl, cfname);
			continue;
		} 
		if(strcmp(pentry->remote_filename,remote_filename)!=0)
			continue;
		
		/* if pathname matches, check client IP address */
		if( (hp = gethostbyname(hostname)) ==NULL){
			syslog(LOG_CRIT|FACILITY, 
			       "unknown host %s in %s", hostname, cfname);
			continue;
		}
		
		if(memcmp(&pentry->sin.sin_addr, hp->h_addr,
			  hp->h_length)==0){
				/* match found */
				strncpy(pentry->local_filename, 
					local_filename, MAX_PATHNAME_LEN);
				strncpy(pentry->hostname, hostname,
					MAX_PATHNAME_LEN);
				fclose(pcf);
				return pentry->local_filename;
		}
	}
	syslog(LOG_CRIT|FACILITY, 
	       "no entry is found in %s for %s:%s",
	       cfname, pentry->hostname, pentry->remote_filename);
	pentry->local_filename[0]= '\0';
	fclose(pcf);
	return NULL;
}


static void
sig_alrm(int signo)
{
	return;
}

static void
sig_chld(int signo)
{
	int stat;
	while( waitpid(-1, &stat, WNOHANG)>0)
		; /* DO NOTHING */
	return;
}
		     
static void
sig_hup(int signo)
{
	if(find_local_filename(confname, &entry)==NULL){
		/* the connection is not valid in the new configuration */
		syslog(LOG_CRIT|FACILITY,
		       "connection from %s (%s) is closed by tcppiped",
		       entry.hostname, entry.remote_filename);
		exit(1);
	}
	/* reopen the target file*/
	if(freopen(entry.local_filename, "a", f) == NULL){
		syslog(LOG_CRIT|FACILITY,
		       "can't open local file %s for pipe from %s (%s)", 
		       entry.local_filename, entry.hostname, 
		       entry.remote_filename);
		exit(1);
	}

}

static void
sig_pipe(int signo)
{
	syslog(LOG_CRIT|FACILITY,
	       "connection to %s (%s) terminated prematurely, abort...", 
	       entry.hostname, entry.remote_filename);
	exit(1);
}
static void
sig_resend_hup(int signo)
{

	#ifdef __sun
	sigset(SIGHUP, SIG_IGN);
	#else
	signal(SIGHUP, SIG_IGN);
	#endif

	killpg(getpgrp(), SIGHUP);

	#ifdef __sun
	sigset(SIGHUP, sig_resend_hup);
	#else
	signal(SIGHUP, sig_resend_hup);
	#endif

}
static void
sig_term(int signo)
{
	unlink(pid_filename);
	#ifdef __sun
	sigset(SIGTERM, SIG_IGN);
	#else
	signal(SIGTERM, SIG_IGN);
	#endif

	killpg(getpgrp(), SIGTERM);

	sleep(1);
	syslog(LOG_CRIT|FACILITY, "tcppiped exit ... ");
	exit(signo);
}

