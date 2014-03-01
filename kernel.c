#include "stm32f10x.h"
#include "stm32_p103.h"
#include "RTOSConfig.h"

#include "syscall.h"

#include <stddef.h>
#include <ctype.h>
#include <string.h>

void *memcpy(void *dest, const void *src, size_t n);

int strcmp(const char *a, const char *b) __attribute__ ((naked));
int strcmp(const char *a, const char *b)
{
	asm(
        "strcmp_lop:                \n"
        "   ldrb    r2, [r0],#1     \n"
        "   ldrb    r3, [r1],#1     \n"
        "   cmp     r2, #1          \n"
        "   it      hi              \n"
        "   cmphi   r2, r3          \n"
        "   beq     strcmp_lop      \n"
		"	sub     r0, r2, r3  	\n"
        "   bx      lr              \n"
		:::
	);
}

int strncmp(const char *a, const char *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (a[i] != b[i])
			return a[i] - b[i];

	return 0;
}

size_t strlen(const char *s) __attribute__ ((naked));
size_t strlen(const char *s)
{
	asm(
		"	sub  r3, r0, #1			\n"
        "strlen_loop:               \n"
		"	ldrb r2, [r3, #1]!		\n"
		"	cmp  r2, #0				\n"
        "   bne  strlen_loop        \n"
		"	sub  r0, r3, r0			\n"
		"	bx   lr					\n"
		:::
	);
}

void puts(char *s)
{
	while (*s) {
		while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
			/* wait */ ;
		USART_SendData(USART2, *s);
		s++;
	}
}

#define MAX_CMDNAME 19
#define MAX_ARGC 19
#define MAX_CMDHELP 1023
#define HISTORY_COUNT 20
#define CMDBUF_SIZE 100
#define MAX_ENVCOUNT 30
#define MAX_ENVNAME 15
#define MAX_ENVVALUE 127
#define STACK_SIZE 512 /* Size of task stacks in words */
#define TASK_LIMIT 8  /* Max number of tasks we can handle */
#define PIPE_BUF   64 /* Size of largest atomic pipe message */
#define PATH_MAX   32 /* Longest absolute path */
#define PIPE_LIMIT (TASK_LIMIT * 2)

#define PATHSERVER_FD (TASK_LIMIT + 3) 
	/* File descriptor of pipe to pathserver */

#define PRIORITY_DEFAULT 20
#define PRIORITY_LIMIT (PRIORITY_DEFAULT * 2 - 1)

#define TASK_READY      0
#define TASK_WAIT_READ  1
#define TASK_WAIT_WRITE 2
#define TASK_WAIT_INTR  3
#define TASK_WAIT_TIME  4

#define S_IFIFO 1
#define S_IMSGQ 2

#define O_CREAT 4

/*Global Variables*/
char next_line[3] = {'\n','\r','\0'};
size_t task_count = 0;
char cmd[HISTORY_COUNT][CMDBUF_SIZE];
int cur_his=0;
int fdout;
int fdin;

void check_keyword();
void find_events();
int fill_arg(char *const dest, const char *argv);
void itoa(int n, char *dst, int base);
int write_blank(int blank_num);

/* Command handlers. */
void export_envvar(int argc, char *argv[]);
void show_echo(int argc, char *argv[]);
void show_cmd_info(int argc, char *argv[]);
void show_task_info(int argc, char *argv[]);
void show_man_page(int argc, char *argv[]);
void show_history(int argc, char *argv[]);

/* Enumeration for command types. */
enum {
	CMD_ECHO = 0,
	CMD_EXPORT,
	CMD_HELP,
	CMD_HISTORY,
	CMD_MAN,
	CMD_PS,
	CMD_COUNT
} CMD_TYPE;
/* Structure for command handler. */
typedef struct {
	char cmd[MAX_CMDNAME + 1];
	void (*func)(int, char**);
	char description[MAX_CMDHELP + 1];
} hcmd_entry;
const hcmd_entry cmd_data[CMD_COUNT] = {
	[CMD_ECHO] = {.cmd = "echo", .func = show_echo, .description = "Show words you input."},
	[CMD_EXPORT] = {.cmd = "export", .func = export_envvar, .description = "Export environment variables."},
	[CMD_HELP] = {.cmd = "help", .func = show_cmd_info, .description = "List all commands you can use."},
	[CMD_HISTORY] = {.cmd = "history", .func = show_history, .description = "Show latest commands entered."}, 
	[CMD_MAN] = {.cmd = "man", .func = show_man_page, .description = "Manual pager."},
	[CMD_PS] = {.cmd = "ps", .func = show_task_info, .description = "List all the processes."}
};

/* Structure for environment variables. */
typedef struct {
	char name[MAX_ENVNAME + 1];
	char value[MAX_ENVVALUE + 1];
} evar_entry;
evar_entry env_var[MAX_ENVCOUNT];
int env_count = 0;

/* Stack struct of user thread, see "Exception entry and return" */
struct user_thread_stack {
	unsigned int r4;
	unsigned int r5;
	unsigned int r6;
	unsigned int r7;
	unsigned int r8;
	unsigned int r9;
	unsigned int r10;
	unsigned int fp;
	unsigned int _lr;	/* Back to system calls or return exception */
	unsigned int _r7;	/* Backup from isr */
	unsigned int r0;
	unsigned int r1;
	unsigned int r2;
	unsigned int r3;
	unsigned int ip;
	unsigned int lr;	/* Back to user thread code */
	unsigned int pc;
	unsigned int xpsr;
	unsigned int stack[STACK_SIZE - 18];
};

/* Task Control Block */
struct task_control_block {
    struct user_thread_stack *stack;
    int pid;
    int status;
    int priority;
    struct task_control_block **prev;
    struct task_control_block  *next;
};
struct task_control_block tasks[TASK_LIMIT];

/* 
 * pathserver assumes that all files are FIFOs that were registered
 * with mkfifo.  It also assumes a global tables of FDs shared by all
 * processes.  It would have to get much smarter to be generally useful.
 *
 * The first TASK_LIMIT FDs are reserved for use by their respective tasks.
 * 0-2 are reserved FDs and are skipped.
 * The server registers itself at /sys/pathserver
*/
#define PATH_SERVER_NAME "/sys/pathserver"
void pathserver()
{
	char paths[PIPE_LIMIT - TASK_LIMIT - 3][PATH_MAX];
	int npaths = 0;
	int i = 0;
	unsigned int plen = 0;
	unsigned int replyfd = 0;
	char path[PATH_MAX];

	memcpy(paths[npaths++], PATH_SERVER_NAME, sizeof(PATH_SERVER_NAME));

	while (1) {
		read(PATHSERVER_FD, &replyfd, 4);
		read(PATHSERVER_FD, &plen, 4);
		read(PATHSERVER_FD, path, plen);

		if (!replyfd) { /* mkfifo */
			int dev;
			read(PATHSERVER_FD, &dev, 4);
			memcpy(paths[npaths], path, plen);
			mknod(npaths + 3 + TASK_LIMIT, 0, dev);
			npaths++;
		}
		else { /* open */
			/* Search for path */
			for (i = 0; i < npaths; i++) {
				if (*paths[i] && strcmp(path, paths[i]) == 0) {
					i += 3; /* 0-2 are reserved */
					i += TASK_LIMIT; /* FDs reserved for tasks */
					write(replyfd, &i, 4);
					i = 0;
					break;
				}
			}

			if (i >= npaths) {
				i = -1; /* Error: not found */
				write(replyfd, &i, 4);
			}
		}
	}
}

int mkfile(const char *pathname, int mode, int dev)
{
	size_t plen = strlen(pathname)+1;
	char buf[4+4+PATH_MAX+4];
	(void) mode;

	*((unsigned int *)buf) = 0;
	*((unsigned int *)(buf + 4)) = plen;
	memcpy(buf + 4 + 4, pathname, plen);
	*((int *)(buf + 4 + 4 + plen)) = dev;
	write(PATHSERVER_FD, buf, 4 + 4 + plen + 4);

	return 0;
}

int mkfifo(const char *pathname, int mode)
{
	mkfile(pathname, mode, S_IFIFO);
	return 0;
}

int open(const char *pathname, int flags)
{
	unsigned int replyfd = getpid() + 3;
	size_t plen = strlen(pathname) + 1;
	unsigned int fd = -1;
	char buf[4 + 4 + PATH_MAX];
	(void) flags;

	*((unsigned int *)buf) = replyfd;
	*((unsigned int *)(buf + 4)) = plen;
	memcpy(buf + 4 + 4, pathname, plen);
	write(PATHSERVER_FD, buf, 4 + 4 + plen);
	read(replyfd, &fd, 4);

	return fd;
}

int mq_open(const char *name, int oflag)
{
	if (oflag & O_CREAT)
		mkfile(name, 0, S_IMSGQ);
	return open(name, 0);
}

void serialout(USART_TypeDef* uart, unsigned int intr)
{
	int fd;
	char c;
	int doread = 1;
	mkfifo("/dev/tty0/out", 0);
	fd = open("/dev/tty0/out", 0);

	while (1) {
		if (doread)
			read(fd, &c, 1);
		doread = 0;
		if (USART_GetFlagStatus(uart, USART_FLAG_TXE) == SET) {
			USART_SendData(uart, c);
			USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
			doread = 1;
		}
		interrupt_wait(intr);
		USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
	}
}

void serialin(USART_TypeDef* uart, unsigned int intr)
{
	int fd;
	char c;
	mkfifo("/dev/tty0/in", 0);
	fd = open("/dev/tty0/in", 0);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

	while (1) {
		interrupt_wait(intr);
		if (USART_GetFlagStatus(uart, USART_FLAG_RXNE) == SET) {
			c = USART_ReceiveData(uart);
			write(fd, &c, 1);
		}
	}
}

void greeting()
{
	int fdout = open("/dev/tty0/out", 0);
	char *string = "Hello, World!\n";
	while (*string) {
		write(fdout, string, 1);
		string++;
	}
}

void echo()
{
	int fdout;
	int fdin;
	char c;

	fdout = open("/dev/tty0/out", 0);
	fdin = open("/dev/tty0/in", 0);

	while (1) {
		read(fdin, &c, 1);
		write(fdout, &c, 1);
	}
}

void rs232_xmit_msg_task()
{
	int fdout;
	int fdin;
	char str[100];
	int curr_char;

	fdout = open("/dev/tty0/out", 0);
	fdin = mq_open("/tmp/mqueue/out", O_CREAT);
	setpriority(0, PRIORITY_DEFAULT - 2);

	while (1) {
		/* Read from the queue.  Keep trying until a message is
		 * received.  This will block for a period of time (specified
		 * by portMAX_DELAY). */
		read(fdin, str, 100);

		/* Write each character of the message to the RS232 port. */
		curr_char = 0;
		while (str[curr_char] != '\0') {
			write(fdout, &str[curr_char], 1);
			curr_char++;
		}
	}
}

void queue_str_task(const char *str, int delay)
{
	int fdout = mq_open("/tmp/mqueue/out", 0);
	int msg_len = strlen(str) + 1;

	while (1) {
		/* Post the message.  Keep on trying until it is successful. */
		write(fdout, str, msg_len);

		/* Wait. */
		sleep(delay);
	}
}

void queue_str_task1()
{
	queue_str_task("Hello 1\n", 200);
}

void queue_str_task2()
{
	queue_str_task("Hello 2\n", 50);
}

void serial_readwrite_task()
{
	int fdout;
	int fdin;
	char str[100];
	char ch;
	int curr_char;
	int done;

	fdout = mq_open("/tmp/mqueue/out", 0);
	fdin = open("/dev/tty0/in", 0);

	/* Prepare the response message to be queued. */
	memcpy(str, "Got:", 4);

	while (1) {
		curr_char = 4;
		done = 0;
		do {
			/* Receive a byte from the RS232 port (this call will
			 * block). */
			read(fdin, &ch, 1);

			/* If the byte is an end-of-line type character, then
			 * finish the string and inidcate we are done.
			 */
			if (curr_char >= 98 || ch == '\r' || ch == '\n') {
				str[curr_char] = '\n';
				str[curr_char+1] = '\0';
				done = -1;
			}
			/* Otherwise, add the character to the
			 * response string. */
			else
				str[curr_char++] = ch;
		} while (!done);

		/* Once we are done building the response string, queue the
		 * response to be sent to the RS232 port.
		 */
		write(fdout, str, curr_char+1 + 1);
	}
}

void serial_test_task()
{
	char put_ch[2]={'0','\0'};
	char hint[] =  USER_NAME "@" USER_NAME "-STM32:~$ ";
	int hint_length = sizeof(hint);
	char *p = NULL;
	int cmd_count = 0;

	fdout = mq_open("/tmp/mqueue/out", 0);
	fdin = open("/dev/tty0/in", 0);

	for (;; cur_his = (cur_his + 1) % HISTORY_COUNT) {
		p = cmd[cur_his];
		write(fdout, hint, hint_length);

		while (1) {
			read(fdin, put_ch, 1);

			if (put_ch[0] == '\r' || put_ch[0] == '\n') {
				*p = '\0';
				write(fdout, next_line, 3);
				break;
			}
			else if (put_ch[0] == 127 || put_ch[0] == '\b') {
				if (p > cmd[cur_his]) {
					p--;
					write(fdout, "\b \b", 4);
				}
			}
			else if (p - cmd[cur_his] < CMDBUF_SIZE - 1) {
				*p++ = put_ch[0];
				write(fdout, put_ch, 2);
			}
		}
		check_keyword();	
	}
}

/* Split command into tokens. */
char *cmdtok(char *cmd)
{
	static char *cur = NULL;
	static char *end = NULL;
	if (cmd) {
		char quo = '\0';
		cur = cmd;
		for (end = cmd; *end; end++) {
			if (*end == '\'' || *end == '\"') {
				if (quo == *end)
					quo = '\0';
				else if (quo == '\0')
					quo = *end;
				*end = '\0';
			}
			else if (isspace(*end) && !quo)
				*end = '\0';
		}
	}
	else
		for (; *cur; cur++)
			;

	for (; *cur == '\0'; cur++)
		if (cur == end) return NULL;
	return cur;
}

void check_keyword()
{
	char *argv[MAX_ARGC + 1] = {NULL};
	char cmdstr[CMDBUF_SIZE];
	char buffer[CMDBUF_SIZE * MAX_ENVVALUE / 2 + 1];
	char *p = buffer;
	int argc = 1;
	int i;

	find_events();
	strcpy(cmdstr, cmd[cur_his]);
	argv[0] = cmdtok(cmdstr);
	if (!argv[0])
		return;

	while (1) {
		argv[argc] = cmdtok(NULL);
		if (!argv[argc])
			break;
		argc++;
		if (argc >= MAX_ARGC)
			break;
	}

	for(i = 0; i < argc; i++) {
		int l = fill_arg(p, argv[i]);
		argv[i] = p;
		p += l + 1;
	}

	for (i = 0; i < CMD_COUNT; i++) {
		if (!strcmp(argv[0], cmd_data[i].cmd)) {
			cmd_data[i].func(argc, argv);
			break;
		}
	}
	if (i == CMD_COUNT) {
		write(fdout, argv[0], strlen(argv[0]) + 1);
		write(fdout, ": command not found", 20);
		write(fdout, next_line, 3);
	}
}

void find_events()
{
	char buf[CMDBUF_SIZE];
	char *p = cmd[cur_his];
	char *q;
	int i;

	for (; *p; p++) {
		if (*p == '!') {
			q = p;
			while (*q && !isspace(*q))
				q++;
			for (i = cur_his + HISTORY_COUNT - 1; i > cur_his; i--) {
				if (!strncmp(cmd[i % HISTORY_COUNT], p + 1, q - p - 1)) {
					strcpy(buf, q);
					strcpy(p, cmd[i % HISTORY_COUNT]);
					p += strlen(p);
					strcpy(p--, buf);
					break;
				}
			}
		}
	}
}

char *find_envvar(const char *name)
{
	int i;

	for (i = 0; i < env_count; i++) {
		if (!strcmp(env_var[i].name, name))
			return env_var[i].value;
	}

	return NULL;
}

/* Fill in entire value of argument. */
int fill_arg(char *const dest, const char *argv)
{
	char env_name[MAX_ENVNAME + 1];
	char *buf = dest;
	char *p = NULL;

	for (; *argv; argv++) {
		if (isalnum(*argv) || *argv == '_') {
			if (p)
				*p++ = *argv;
			else
				*buf++ = *argv;
		}
		else { /* Symbols. */
			if (p) {
				*p = '\0';
				p = find_envvar(env_name);
				if (p) {
					strcpy(buf, p);
					buf += strlen(p);
					p = NULL;
				}
			}
			if (*argv == '$')
				p = env_name;
			else
				*buf++ = *argv;
		}
	}
	if (p) {
		*p = '\0';
		p = find_envvar(env_name);
		if (p) {
			strcpy(buf, p);
			buf += strlen(p);
		}
	}
	*buf = '\0';

	return buf - dest;
}

//export
void export_envvar(int argc, char *argv[])
{
	char *found;
	char *value;
	int i;

	for (i = 1; i < argc; i++) {
		value = argv[i];
		while (*value && *value != '=')
			value++;
		if (*value)
			*value++ = '\0';
		found = find_envvar(argv[i]);
		if (found)
			strcpy(found, value);
		else if (env_count < MAX_ENVCOUNT) {
			strcpy(env_var[env_count].name, argv[i]);
			strcpy(env_var[env_count].value, value);
			env_count++;
		}
	}
}

//ps
void show_task_info(int argc, char* argv[])
{
	char ps_message[]="PID STATUS PRIORITY";
	int ps_message_length = sizeof(ps_message);
	int task_i;
	int task;

	write(fdout, &ps_message , ps_message_length);
	write(fdout, &next_line , 3);

	for (task_i = 0; task_i < task_count; task_i++) {
		char task_info_pid[2];
		char task_info_status[2];
		char task_info_priority[3];

		task_info_pid[0]='0'+tasks[task_i].pid;
		task_info_pid[1]='\0';
		task_info_status[0]='0'+tasks[task_i].status;
		task_info_status[1]='\0';			

		itoa(tasks[task_i].priority, task_info_priority, 10);

		write(fdout, &task_info_pid , 2);
		write_blank(3);
			write(fdout, &task_info_status , 2);
		write_blank(5);
		write(fdout, &task_info_priority , 3);

		write(fdout, &next_line , 3);
	}
}

//this function helps to show int

void itoa(int n, char *dst, int base)
{
	char buf[33] = {0};
	char *p = &buf[32];

	if (n == 0)
		*--p = '0';
	else {
		char *q;
		unsigned int num = (base == 10 && num < 0) ? -n : n;

		for (; num; num/=base)
			*--p = "0123456789ABCDEF" [num % base];
		if (base == 10 && n < 0)
			*--p = '-';
	}

	strcpy(dst, p);
}

//help

void show_cmd_info(int argc, char* argv[])
{
	const char help_desp[] = "This system has commands as follow\n\r\0";
	int i;

	write(fdout, &help_desp, sizeof(help_desp));
	for (i = 0; i < CMD_COUNT; i++) {
		write(fdout, cmd_data[i].cmd, strlen(cmd_data[i].cmd) + 1);
		write(fdout, ": ", 3);
		write(fdout, cmd_data[i].description, strlen(cmd_data[i].description) + 1);
		write(fdout, next_line, 3);
	}
}

//echo
void show_echo(int argc, char* argv[])
{
	const int _n = 1; /* Flag for "-n" option. */
	int flag = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-n"))
			flag |= _n;
		else
			break;
	}

	for (; i < argc; i++) {
		write(fdout, argv[i], strlen(argv[i]) + 1);
		if (i < argc - 1)
			write(fdout, " ", 2);
	}

	if (~flag & _n)
		write(fdout, next_line, 3);
}

//man
void show_man_page(int argc, char *argv[])
{
	int i;

	if (argc < 2)
		return;

	for (i = 0; i < CMD_COUNT && strcmp(cmd_data[i].cmd, argv[1]); i++)
		;

	if (i >= CMD_COUNT)
		return;

	write(fdout, "NAME: ", 7);
	write(fdout, cmd_data[i].cmd, strlen(cmd_data[i].cmd) + 1);
	write(fdout, next_line, 3);
	write(fdout, "DESCRIPTION: ", 14);
	write(fdout, cmd_data[i].description, strlen(cmd_data[i].description) + 1);
	write(fdout, next_line, 3);
}

void show_history(int argc, char *argv[])
{
	int i;

	for (i = cur_his + 1; i <= cur_his + HISTORY_COUNT; i++) {
		if (cmd[i % HISTORY_COUNT][0]) {
			write(fdout, cmd[i % HISTORY_COUNT], strlen(cmd[i % HISTORY_COUNT]) + 1);
			write(fdout, next_line, 3);
		}
	}
}

int write_blank(int blank_num)
{
	char blank[] = " ";
	int blank_count = 0;

	while (blank_count <= blank_num) {
		write(fdout, blank, sizeof(blank));
		blank_count++;
	}
}

void first()
{
	setpriority(0, 0);

	if (!fork()) setpriority(0, 0), pathserver();
	if (!fork()) setpriority(0, 0), serialout(USART2, USART2_IRQn);
	if (!fork()) setpriority(0, 0), serialin(USART2, USART2_IRQn);
	if (!fork()) rs232_xmit_msg_task();
	if (!fork()) setpriority(0, PRIORITY_DEFAULT - 10), serial_test_task();

	setpriority(0, PRIORITY_LIMIT);

	while(1);
}

struct pipe_ringbuffer {
	int start;
	int end;
	char data[PIPE_BUF];

	int (*readable) (struct pipe_ringbuffer*, struct task_control_block*);
	int (*writable) (struct pipe_ringbuffer*, struct task_control_block*);
	int (*read) (struct pipe_ringbuffer*, struct task_control_block*);
	int (*write) (struct pipe_ringbuffer*, struct task_control_block*);
};

#define RB_PUSH(rb, size, v) do { \
		(rb).data[(rb).end] = (v); \
		(rb).end++; \
		if ((rb).end >= size) (rb).end = 0; \
	} while (0)

#define RB_POP(rb, size, v) do { \
		(v) = (rb).data[(rb).start]; \
		(rb).start++; \
		if ((rb).start >= size) (rb).start = 0; \
	} while (0)

#define RB_PEEK(rb, size, v, i) do { \
		int _counter = (i); \
		int _src_index = (rb).start; \
		int _dst_index = 0; \
		while (_counter--) { \
			((char*)&(v))[_dst_index++] = (rb).data[_src_index++]; \
			if (_src_index >= size) _src_index = 0; \
		} \
	} while (0)

#define RB_LEN(rb, size) (((rb).end - (rb).start) + \
	(((rb).end < (rb).start) ? size : 0))

#define PIPE_PUSH(pipe, v) RB_PUSH((pipe), PIPE_BUF, (v))
#define PIPE_POP(pipe, v)  RB_POP((pipe), PIPE_BUF, (v))
#define PIPE_PEEK(pipe, v, i)  RB_PEEK((pipe), PIPE_BUF, (v), (i))
#define PIPE_LEN(pipe)     (RB_LEN((pipe), PIPE_BUF))

unsigned int *init_task(unsigned int *stack, void (*start)())
{
	stack += STACK_SIZE - 9; /* End of stack, minus what we're about to push */
	stack[8] = (unsigned int)start;
	return stack;
}

int
task_push (struct task_control_block **list, struct task_control_block *item)
{
	if (list && item) {
		/* Remove itself from original list */
		if (item->prev)
			*(item->prev) = item->next;
		if (item->next)
			item->next->prev = item->prev;
		/* Insert into new list */
		while (*list) list = &((*list)->next);
		*list = item;
		item->prev = list;
		item->next = NULL;
		return 0;
	}
	return -1;
}

struct task_control_block*
task_pop (struct task_control_block **list)
{
	if (list) {
		struct task_control_block *item = *list;
		if (item) {
			*list = item->next;
			if (item->next)
				item->next->prev = list;
			item->prev = NULL;
			item->next = NULL;
			return item;
		}
	}
	return NULL;
}

void _read(struct task_control_block *task, struct task_control_block *tasks, size_t task_count, struct pipe_ringbuffer *pipes);
void _write(struct task_control_block *task, struct task_control_block *tasks, size_t task_count, struct pipe_ringbuffer *pipes);

void _read(struct task_control_block *task, struct task_control_block *tasks, size_t task_count, struct pipe_ringbuffer *pipes)
{
	task->status = TASK_READY;
	/* If the fd is invalid */
	if (task->stack->r0 > PIPE_LIMIT) {
		task->stack->r0 = -1;
	}
	else {
		struct pipe_ringbuffer *pipe = &pipes[task->stack->r0];

		if (pipe->readable(pipe, task)) {
			size_t i;

			pipe->read(pipe, task);

			/* Unblock any waiting writes */
			for (i = 0; i < task_count; i++)
				if (tasks[i].status == TASK_WAIT_WRITE)
					_write(&tasks[i], tasks, task_count, pipes);
		}
	}
}

void _write(struct task_control_block *task, struct task_control_block *tasks, size_t task_count, struct pipe_ringbuffer *pipes)
{
	task->status = TASK_READY;
	/* If the fd is invalid */
	if (task->stack->r0 > PIPE_LIMIT) {
		task->stack->r0 = -1;
	}
	else {
		struct pipe_ringbuffer *pipe = &pipes[task->stack->r0];

		if (pipe->writable(pipe, task)) {
			size_t i;

			pipe->write(pipe, task);

			/* Unblock any waiting reads */
			for (i = 0; i < task_count; i++)
				if (tasks[i].status == TASK_WAIT_READ)
					_read(&tasks[i], tasks, task_count, pipes);
		}
	}
}

int
fifo_readable (struct pipe_ringbuffer *pipe,
			   struct task_control_block *task)
{
	/* Trying to read too much */
	if (task->stack->r2 > PIPE_BUF) {
		task->stack->r0 = -1;
		return 0;
	}
	if ((size_t)PIPE_LEN(*pipe) < task->stack->r2) {
		/* Trying to read more than there is: block */
		task->status = TASK_WAIT_READ;
		return 0;
	}
	return 1;
}

int
mq_readable (struct pipe_ringbuffer *pipe,
			 struct task_control_block *task)
{
	size_t msg_len;

	/* Trying to read too much */
	if ((size_t)PIPE_LEN(*pipe) < sizeof(size_t)) {
		/* Nothing to read */
		task->status = TASK_WAIT_READ;
		return 0;
	}

	PIPE_PEEK(*pipe, msg_len, 4);

	if (msg_len > task->stack->r2) {
		/* Trying to read more than buffer size */
		task->stack->r0 = -1;
		return 0;
	}
	return 1;
}

int
fifo_read (struct pipe_ringbuffer *pipe,
		   struct task_control_block *task)
{
	size_t i;
	char *buf = (char*)task->stack->r1;
	/* Copy data into buf */
	for (i = 0; i < task->stack->r2; i++) {
		PIPE_POP(*pipe, buf[i]);
	}
	return task->stack->r2;
}

int
mq_read (struct pipe_ringbuffer *pipe,
		 struct task_control_block *task)
{
	size_t msg_len;
	size_t i;
	char *buf = (char*)task->stack->r1;
	/* Get length */
	for (i = 0; i < 4; i++) {
		PIPE_POP(*pipe, *(((char*)&msg_len)+i));
	}
	/* Copy data into buf */
	for (i = 0; i < msg_len; i++) {
		PIPE_POP(*pipe, buf[i]);
	}
	return msg_len;
}

int
fifo_writable (struct pipe_ringbuffer *pipe,
			   struct task_control_block *task)
{
	/* If the write would be non-atomic */
	if (task->stack->r2 > PIPE_BUF) {
		task->stack->r0 = -1;
		return 0;
	}
	/* Preserve 1 byte to distiguish empty or full */
	if ((size_t)PIPE_BUF - PIPE_LEN(*pipe) - 1 < task->stack->r2) {
		/* Trying to write more than we have space for: block */
		task->status = TASK_WAIT_WRITE;
		return 0;
	}
	return 1;
}

int
mq_writable (struct pipe_ringbuffer *pipe,
			 struct task_control_block *task)
{
	size_t total_len = sizeof(size_t) + task->stack->r2;

	/* If the write would be non-atomic */
	if (total_len > PIPE_BUF) {
		task->stack->r0 = -1;
		return 0;
	}
	/* Preserve 1 byte to distiguish empty or full */
	if ((size_t)PIPE_BUF - PIPE_LEN(*pipe) - 1 < total_len) {
		/* Trying to write more than we have space for: block */
		task->status = TASK_WAIT_WRITE;
		return 0;
	}
	return 1;
}

int
fifo_write (struct pipe_ringbuffer *pipe,
			struct task_control_block *task)
{
	size_t i;
	const char *buf = (const char*)task->stack->r1;
	/* Copy data into pipe */
	for (i = 0; i < task->stack->r2; i++)
		PIPE_PUSH(*pipe,buf[i]);
	return task->stack->r2;
}

int
mq_write (struct pipe_ringbuffer *pipe,
		  struct task_control_block *task)
{
	size_t i;
	const char *buf = (const char*)task->stack->r1;
	/* Copy count into pipe */
	for (i = 0; i < sizeof(size_t); i++)
		PIPE_PUSH(*pipe,*(((char*)&task->stack->r2)+i));
	/* Copy data into pipe */
	for (i = 0; i < task->stack->r2; i++)
		PIPE_PUSH(*pipe,buf[i]);
	return task->stack->r2;
}

int
_mknod(struct pipe_ringbuffer *pipe, int dev)
{
	switch(dev) {
	case S_IFIFO:
		pipe->readable = fifo_readable;
		pipe->writable = fifo_writable;
		pipe->read = fifo_read;
		pipe->write = fifo_write;
		break;
	case S_IMSGQ:
		pipe->readable = mq_readable;
		pipe->writable = mq_writable;
		pipe->read = mq_read;
		pipe->write = mq_write;
		break;
	default:
		return 1;
	}
	return 0;
}

int main()
{
	unsigned int stacks[TASK_LIMIT][STACK_SIZE];
	//struct task_control_block tasks[TASK_LIMIT];
	struct pipe_ringbuffer pipes[PIPE_LIMIT];
	struct task_control_block *ready_list[PRIORITY_LIMIT + 1];  /* [0 ... 39] */
	struct task_control_block *wait_list = NULL;
	//size_t task_count = 0;
	size_t current_task = 0;
	size_t i;
	struct task_control_block *task;
	int timeup;
	unsigned int tick_count = 0;

	SysTick_Config(configCPU_CLOCK_HZ / configTICK_RATE_HZ);

	init_rs232();
	__enable_irq();

	tasks[task_count].stack = (void*)init_task(stacks[task_count], &first);
	tasks[task_count].pid = 0;
	tasks[task_count].priority = PRIORITY_DEFAULT;
	task_count++;

	/* Initialize all pipes */
	for (i = 0; i < PIPE_LIMIT; i++)
		pipes[i].start = pipes[i].end = 0;

	/* Initialize fifos */
	for (i = 0; i <= PATHSERVER_FD; i++)
		_mknod(&pipes[i], S_IFIFO);

	/* Initialize ready lists */
	for (i = 0; i <= PRIORITY_LIMIT; i++)
		ready_list[i] = NULL;

	while (1) {
		tasks[current_task].stack = activate(tasks[current_task].stack);
		tasks[current_task].status = TASK_READY;
		timeup = 0;

		switch (tasks[current_task].stack->r7) {
		case 0x1: /* fork */
			if (task_count == TASK_LIMIT) {
				/* Cannot create a new task, return error */
				tasks[current_task].stack->r0 = -1;
			}
			else {
				/* Compute how much of the stack is used */
				size_t used = stacks[current_task] + STACK_SIZE
					      - (unsigned int*)tasks[current_task].stack;
				/* New stack is END - used */
				tasks[task_count].stack = (void*)(stacks[task_count] + STACK_SIZE - used);
				/* Copy only the used part of the stack */
				memcpy(tasks[task_count].stack, tasks[current_task].stack,
				       used * sizeof(unsigned int));
				/* Set PID */
				tasks[task_count].pid = task_count;
				/* Set priority, inherited from forked task */
				tasks[task_count].priority = tasks[current_task].priority;
				/* Set return values in each process */
				tasks[current_task].stack->r0 = task_count;
				tasks[task_count].stack->r0 = 0;
				tasks[task_count].prev = NULL;
				tasks[task_count].next = NULL;
				task_push(&ready_list[tasks[task_count].priority], &tasks[task_count]);
				/* There is now one more task */
				task_count++;
			}
			break;
		case 0x2: /* getpid */
			tasks[current_task].stack->r0 = current_task;
			break;
		case 0x3: /* write */
			_write(&tasks[current_task], tasks, task_count, pipes);
			break;
		case 0x4: /* read */
			_read(&tasks[current_task], tasks, task_count, pipes);
			break;
		case 0x5: /* interrupt_wait */
			/* Enable interrupt */
			NVIC_EnableIRQ(tasks[current_task].stack->r0);
			/* Block task waiting for interrupt to happen */
			tasks[current_task].status = TASK_WAIT_INTR;
			break;
		case 0x6: /* getpriority */
			{
				int who = tasks[current_task].stack->r0;
				if (who > 0 && who < (int)task_count)
					tasks[current_task].stack->r0 = tasks[who].priority;
				else if (who == 0)
					tasks[current_task].stack->r0 = tasks[current_task].priority;
				else
					tasks[current_task].stack->r0 = -1;
			} break;
		case 0x7: /* setpriority */
			{
				int who = tasks[current_task].stack->r0;
				int value = tasks[current_task].stack->r1;
				value = (value < 0) ? 0 : ((value > PRIORITY_LIMIT) ? PRIORITY_LIMIT : value);
				if (who > 0 && who < (int)task_count)
					tasks[who].priority = value;
				else if (who == 0)
					tasks[current_task].priority = value;
				else {
					tasks[current_task].stack->r0 = -1;
					break;
				}
				tasks[current_task].stack->r0 = 0;
			} break;
		case 0x8: /* mknod */
			if (tasks[current_task].stack->r0 < PIPE_LIMIT)
				tasks[current_task].stack->r0 =
					_mknod(&pipes[tasks[current_task].stack->r0],
						   tasks[current_task].stack->r2);
			else
				tasks[current_task].stack->r0 = -1;
			break;
		case 0x9: /* sleep */
			if (tasks[current_task].stack->r0 != 0) {
				tasks[current_task].stack->r0 += tick_count;
				tasks[current_task].status = TASK_WAIT_TIME;
			}
			break;
		default: /* Catch all interrupts */
			if ((int)tasks[current_task].stack->r7 < 0) {
				unsigned int intr = -tasks[current_task].stack->r7 - 16;

				if (intr == SysTick_IRQn) {
					/* Never disable timer. We need it for pre-emption */
					timeup = 1;
					tick_count++;
				}
				else {
					/* Disable interrupt, interrupt_wait re-enables */
					NVIC_DisableIRQ(intr);
				}
				/* Unblock any waiting tasks */
				for (i = 0; i < task_count; i++)
					if ((tasks[i].status == TASK_WAIT_INTR && tasks[i].stack->r0 == intr) ||
					    (tasks[i].status == TASK_WAIT_TIME && tasks[i].stack->r0 == tick_count))
						tasks[i].status = TASK_READY;
			}
		}

		/* Put waken tasks in ready list */
		for (task = wait_list; task != NULL;) {
			struct task_control_block *next = task->next;
			if (task->status == TASK_READY)
				task_push(&ready_list[task->priority], task);
			task = next;
		}
		/* Select next TASK_READY task */
		for (i = 0; i < (size_t)tasks[current_task].priority && ready_list[i] == NULL; i++);
		if (tasks[current_task].status == TASK_READY) {
			if (!timeup && i == (size_t)tasks[current_task].priority)
				/* Current task has highest priority and remains execution time */
				continue;
			else
				task_push(&ready_list[tasks[current_task].priority], &tasks[current_task]);
		}
		else {
			task_push(&wait_list, &tasks[current_task]);
		}
		while (ready_list[i] == NULL)
			i++;
		current_task = task_pop(&ready_list[i])->pid;
	}

	return 0;
}
