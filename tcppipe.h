/* tcppipe.h */

/* these definition can be changed by user at compiling time */
#define DEFAULT_SERVER_PORT 6514
#define FACILITY LOG_LOCAL5
#define RUNTIME_FILE_DIR "/var/run" 


#define MAX_LINE 1024

/* Commands used between tcppipe and tcppiped*/
#define CMD_READY "ready"
#define CMD_REFUSE "refuse"

/* commands will start with CMD_CHAR */
/* if messages start with the same CMD_CHAR, it is escaped with ESC_CHAR */
/* if messages start with the ESC_CHAR, it is also escaped */
#define ESC_CHAR '#'
#define CMD_CHAR '$'
#define CMD_PREFIX_LEN 1

/* write n bytes to a file or socket*/
ssize_t writen(int fd, const void *vptr, size_t n);

/* read one line from a file or a socket */
ssize_t readline(int fd, void *vptr, size_t maxlen);

/* test if the file exists */
/* return 1 if it exists, return 0 if else */
int test_file(char *filename);

/* get a hash number from a string ending with '\0' */
int hash(char *string);

/* get rid of the `\n' at the end of a line */
char *chop(char *str);

/* generate the base_state_filename */
char *base_state_filename(char *buf, char *dir, char *hostname, int port,
			  char *filename);
