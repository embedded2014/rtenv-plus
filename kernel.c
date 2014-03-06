#include "kconfig.h"
#include "kernel.h"
#include "stm32f10x.h"
#include "stm32_p103.h"
#include "RTOSConfig.h"

#include "syscall.h"

#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include "string.h"
#include "task.h"
#include "memory-pool.h"
#include "path.h"
#include "pipe.h"
#include "fifo.h"
#include "mqueue.h"
#include "block.h"
#include "romdev.h"
#include "event-monitor.h"
#include "romfs.h"

#define MAX_CMDNAME 19
#define MAX_ARGC 19
#define MAX_CMDHELP 1023
#define HISTORY_COUNT 8
#define CMDBUF_SIZE 64
#define MAX_ENVCOUNT 16
#define MAX_ENVNAME 15
#define MAX_ENVVALUE 63

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
void write_blank(int blank_num);

/* Command handlers. */
void export_envvar(int argc, char *argv[]);
void show_echo(int argc, char *argv[]);
void show_cmd_info(int argc, char *argv[]);
void show_task_info(int argc, char *argv[]);
void show_man_page(int argc, char *argv[]);
void show_history(int argc, char *argv[]);
void show_xxd(int argc, char *argv[]);

/* Enumeration for command types. */
enum {
	CMD_ECHO = 0,
	CMD_EXPORT,
	CMD_HELP,
	CMD_HISTORY,
	CMD_MAN,
	CMD_PS,
	CMD_XXD,
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
	[CMD_PS] = {.cmd = "ps", .func = show_task_info, .description = "List all the processes."},
	[CMD_XXD] = {.cmd = "xxd", .func = show_xxd, .description = "Make a hexdump."},
};

/* Structure for environment variables. */
typedef struct {
	char name[MAX_ENVNAME + 1];
	char value[MAX_ENVVALUE + 1];
} evar_entry;
evar_entry env_var[MAX_ENVCOUNT];
int env_count = 0;


struct task_control_block tasks[TASK_LIMIT];


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
			else if (isspace((int)*end) && !quo)
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
	int argc = 1;
	int i;

	find_events();
	fill_arg(cmdstr, cmd[cur_his]);
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
			while (*q && !isspace((int)*q))
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
		if (isalnum((int)*argv) || *argv == '_') {
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

void write_blank(int blank_num)
{
	char blank[] = " ";
	int blank_count = 0;

	while (blank_count <= blank_num) {
		write(fdout, blank, sizeof(blank));
		blank_count++;
	}
}

char hexof(int dec)
{
    const char hextab[] = "0123456789abcdef";

    if (dec < 0 || dec > 15)
        return -1;

    return hextab[dec];
}

char char_filter(char c, char fallback)
{
    if (c < 0x20 || c > 0x7E)
        return fallback;

    return c;
}

#define XXD_WIDTH 0x10

//xxd
void show_xxd(int argc, char *argv[])
{
    int readfd = -1;
    char buf[XXD_WIDTH];
    char ch;
    char chout[2] = {0};
    int pos = 0;
    int size;
    int i;

    if (argc == 1) { /* fallback to stdin */
        readfd = fdin;
    }
    else { /* open file of argv[1] */
        readfd = open(argv[1], 0);

        if (readfd < 0) { /* Open error */
            write(fdout, "xxd: ", 6);
            write(fdout, argv[1], strlen(argv[1]) + 1);
            write(fdout, ": No such file or directory\r\n", 31);
            return;
        }
    }

    lseek(readfd, 0, SEEK_SET);
    while ((size = read(readfd, &ch, sizeof(ch))) && size != -1) {
        if (ch != -1 && ch != 0x04) { /* has something read */

            if (pos % XXD_WIDTH == 0) { /* new line, print address */
                for (i = sizeof(pos) * 8 - 4; i >= 0; i -= 4) {
                    chout[0] = hexof((pos >> i) & 0xF);
                    write(fdout, chout, 2);
                }

                write(fdout, ":", 2);
            }

            if (pos % 2 == 0) { /* whitespace for each 2 bytes */
                write(fdout, " ", 2);
            }

            /* higher bits */
            chout[0] = hexof(ch >> 4);
            write(fdout, chout, 2);

            /* lower bits*/
            chout[0] = hexof(ch & 0xF);
            write(fdout, chout, 2);

            /* store in buffer */
            buf[pos % XXD_WIDTH] = ch;

            pos++;

            if (pos % XXD_WIDTH == 0) { /* end of line */
                write(fdout, "  ", 3);

                for (i = 0; i < XXD_WIDTH; i++) {
                    chout[0] = char_filter(buf[i], '.');
                    write(fdout, chout, 2);
                }

                write(fdout, "\r\n", 3);
            }
        }
        else { /* EOF */
            break;
        }
    }

    if (pos % XXD_WIDTH != 0) { /* rest */
        /* align */
        for (i = pos % XXD_WIDTH; i < XXD_WIDTH; i++) {
            if (i % 2 == 0) { /* whitespace for each 2 bytes */
                write(fdout, " ", 2);
            }
            write(fdout, "  ", 3);
        }

        write(fdout, "  ", 3);

        for (i = 0; i < pos % XXD_WIDTH; i++) {
            chout[0] = char_filter(buf[i], '.');
            write(fdout, chout, 2);
        }

        write(fdout, "\r\n", 3);
    }
}


void first()
{
	if (!fork()) setpriority(0, 0), pathserver();
	if (!fork()) setpriority(0, 0), romdev_driver();
	if (!fork()) setpriority(0, 0), romfs_server();
	if (!fork()) setpriority(0, 0), serialout(USART2, USART2_IRQn);
	if (!fork()) setpriority(0, 0), serialin(USART2, USART2_IRQn);
	if (!fork()) rs232_xmit_msg_task();
	if (!fork()) setpriority(0, PRIORITY_DEFAULT - 10), serial_test_task();

	setpriority(0, PRIORITY_LIMIT);

	mount("/dev/rom0", "/", ROMFS_TYPE, 0);

	while(1);
}

#define INTR_EVENT(intr) (FILE_LIMIT + (intr) + 15) /* see INTR_LIMIT */
#define INTR_EVENT_REVERSE(event) ((event) - FILE_LIMIT - 15)
#define TIME_EVENT (FILE_LIMIT + INTR_LIMIT)

int intr_release(struct event_monitor *monitor, int event,
                 struct task_control_block *task, void *data)
{
    return 1;
}

int time_release(struct event_monitor *monitor, int event,
                 struct task_control_block *task, void *data)
{
    int *tick_count = data;
    return task->stack->r0 == *tick_count;
}

/* System resources */
unsigned int stacks[TASK_LIMIT][STACK_SIZE];
char memory_space[MEM_LIMIT];
struct file *files[FILE_LIMIT];
struct file_request requests[TASK_LIMIT];
struct list ready_list[PRIORITY_LIMIT + 1];  /* [0 ... 39] */
struct event events[EVENT_LIMIT];


int main()
{
	//struct task_control_block tasks[TASK_LIMIT];
	struct memory_pool memory_pool;
	struct event_monitor event_monitor;
	//size_t task_count = 0;
	size_t current_task = 0;
	int i;
	struct list *list;
	struct task_control_block *task;
	int timeup;
	unsigned int tick_count = 0;

	SysTick_Config(configCPU_CLOCK_HZ / configTICK_RATE_HZ);

	init_rs232();
	__enable_irq();

    /* Initialize memory pool */
    memory_pool_init(&memory_pool, MEM_LIMIT, memory_space);

	/* Initialize all files */
	for (i = 0; i < FILE_LIMIT; i++)
		files[i] = NULL;

	/* Initialize ready lists */
	for (i = 0; i <= PRIORITY_LIMIT; i++)
		list_init(&ready_list[i]);

    /* Initialise event monitor */
    event_monitor_init(&event_monitor, events, ready_list);

	/* Initialize fifos */
	for (i = 0; i <= PATHSERVER_FD; i++)
		file_mknod(i, -1, files, S_IFIFO, &memory_pool, &event_monitor);

    /* Register IRQ events, see INTR_LIMIT */
	for (i = -15; i < INTR_LIMIT - 15; i++)
	    event_monitor_register(&event_monitor, INTR_EVENT(i), intr_release, 0);

	event_monitor_register(&event_monitor, TIME_EVENT, time_release, &tick_count);

    /* Initialize first thread */
	tasks[task_count].stack = (void*)init_task(stacks[task_count], &first);
	tasks[task_count].pid = 0;
	tasks[task_count].priority = PRIORITY_DEFAULT;
	list_init(&tasks[task_count].list);
	list_push(&ready_list[tasks[task_count].priority], &tasks[task_count].list);
	task_count++;

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
				list_init(&tasks[task_count].list);
				list_push(&ready_list[tasks[task_count].priority], &tasks[task_count].list);
				/* There is now one more task */
				task_count++;
			}
			break;
		case 0x2: /* getpid */
			tasks[current_task].stack->r0 = current_task;
			break;
		case 0x3: /* write */
		    {
		        /* Check fd is valid */
		        int fd = tasks[current_task].stack->r0;
		        if (fd < FILE_LIMIT && files[fd]) {
		            /* Prepare file request, store reference in r0 */
		            requests[current_task].task = &tasks[current_task];
		            requests[current_task].buf =
		                (void*)tasks[current_task].stack->r1;
		            requests[current_task].size = tasks[current_task].stack->r2;
		            tasks[current_task].stack->r0 =
		                (int)&requests[current_task];

                    /* Write */
			        file_write(files[fd], &requests[current_task],
			                   &event_monitor);
			    }
			    else {
			        tasks[current_task].stack->r0 = -1;
			    }
			} break;
		case 0x4: /* read */
            {
		        /* Check fd is valid */
		        int fd = tasks[current_task].stack->r0;
		        if (fd < FILE_LIMIT && files[fd]) {
		            /* Prepare file request, store reference in r0 */
		            requests[current_task].task = &tasks[current_task];
		            requests[current_task].buf =
		                (void*)tasks[current_task].stack->r1;
		            requests[current_task].size = tasks[current_task].stack->r2;
		            tasks[current_task].stack->r0 =
		                (int)&requests[current_task];

                    /* Read */
			        file_read(files[fd], &requests[current_task],
			                  &event_monitor);
			    }
			    else {
			        tasks[current_task].stack->r0 = -1;
			    }
			} break;
		case 0x5: /* interrupt_wait */
			/* Enable interrupt */
			NVIC_EnableIRQ(tasks[current_task].stack->r0);
			/* Block task waiting for interrupt to happen */
			event_monitor_block(&event_monitor,
			                    INTR_EVENT(tasks[current_task].stack->r0),
			                    &tasks[current_task]);
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
				if (who > 0 && who < (int)task_count) {
					tasks[who].priority = value;
					if (tasks[who].status == TASK_READY)
					    list_push(&ready_list[value], &tasks[who].list);
				}
				else if (who == 0) {
					tasks[current_task].priority = value;
				    list_unshift(&ready_list[value], &tasks[current_task].list);
				}
				else {
					tasks[current_task].stack->r0 = -1;
					break;
				}
				tasks[current_task].stack->r0 = 0;
			} break;
		case 0x8: /* mknod */
			tasks[current_task].stack->r0 =
				file_mknod(tasks[current_task].stack->r0,
				           tasks[current_task].pid,
				           files,
					       tasks[current_task].stack->r2,
					       &memory_pool,
					       &event_monitor);
			break;
		case 0x9: /* sleep */
			if (tasks[current_task].stack->r0 != 0) {
				tasks[current_task].stack->r0 += tick_count;
				tasks[current_task].status = TASK_WAIT_TIME;
			    event_monitor_block(&event_monitor, TIME_EVENT,
			                        &tasks[current_task]);
			}
			break;
		case 0xa: /* lseek */
            {
		        /* Check fd is valid */
		        int fd = tasks[current_task].stack->r0;
		        if (fd < FILE_LIMIT && files[fd]) {
		            /* Prepare file request, store reference in r0 */
		            requests[current_task].task = &tasks[current_task];
		            requests[current_task].buf = NULL;
		            requests[current_task].size = tasks[current_task].stack->r1;
		            requests[current_task].whence = tasks[current_task].stack->r2;
		            tasks[current_task].stack->r0 =
		                (int)&requests[current_task];

                    /* Read */
			        file_lseek(files[fd], &requests[current_task],
			                   &event_monitor);
			    }
			    else {
			        tasks[current_task].stack->r0 = -1;
			    }
			} break;
		default: /* Catch all interrupts */
			if ((int)tasks[current_task].stack->r7 < 0) {
				unsigned int intr = -tasks[current_task].stack->r7 - 16;

				if (intr == SysTick_IRQn) {
					/* Never disable timer. We need it for pre-emption */
					timeup = 1;
					tick_count++;
					event_monitor_release(&event_monitor, TIME_EVENT);
				}
				else {
					/* Disable interrupt, interrupt_wait re-enables */
					NVIC_DisableIRQ(intr);
				}
				event_monitor_release(&event_monitor, INTR_EVENT(intr));
			}
		}

        /* Rearrange ready list and event list */
		event_monitor_serve(&event_monitor);

		/* Check whether to context switch */
		task = &tasks[current_task];
		if (timeup && ready_list[task->priority].next == &task->list)
		    list_push(&ready_list[task->priority], &tasks[current_task].list);

		/* Select next TASK_READY task */
		for (i = 0; list_empty(&ready_list[i]); i++);

		list = ready_list[i].next;
		task = list_entry(list, struct task_control_block, list);
		current_task = task->pid;
	}

	return 0;
}
