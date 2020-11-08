/*
 *  linux/init/main.c
 * 
 */

#define _LIBRARY_      // to include as code in unistd.h
// default dictionery include/
// if not standard , need "" and path
#include <unistd.h>    // standard symbol and tpye, and many functions ,_LIBRARY_ to include system call 
#include <time.h>      // tm struct and time  functions

/*
 * we need this inline , prevent function call resulting use stack
 * --forking from kernel space will result in NO COPY ON WRITH(!!!),until an execve is executed. 调度后
 * this is handled by not letting main() ue the stack at all after fork().
 * Thus, no functioon calls, which means fork should inline, otherwise exit from fork() will use stack
 * Actually only paue and fork are needed inline, but we define some others too.
 */

// num 0 indicate no parameter, 1 indicate one parameter, etc.
// syscall, inline as, call int 0x80, define in include/unistd.h
static inline _syscall0(int,fork) 
static inline _syscall0(int,pause)    // pause() system call, pause process until receive a signal
static inline _syscall1(int,setup,void *,BIOS)  // setup(void * BIOS) system call, only used for linux init
static inline _syscall0(int,sync)    // sync() system call, used to update file system

#include <linux/tty.h>        // define:tty_io, uart 
#include <linux/sched.h>      // schedule, define 'task_struct', data for first init task, micro
#include <linux/head.h>       // simple struct for segment descriptor, and several select const
#include <asm/system.h>       // asm for system setup or modify descriptor interrupt Gate 
#include <asm/io.h>           // asm for IO operate 
#include <stddef.h>           // standard define file, define NULL, offsetof(TYPE，MEMBER)
#include <stdarg.h>           // standard arg file, type 'va_list' and micro 'va _start', 'va_arg', 'va_end'
	                          // vsprintf, vprintf, vfprintf
#include <unistd.h>           
#include <fcntl.h>            // file control head file,define of  file and related descriptor 's control symbol
#include <sys/types.h>        // type head file, define system data type
#include <linux/fs.h>         // file system head file, define 'file' 'buffer_head' 'm_inode'.. struct
 
static char printbuf[1024];   // static  char array, buffer used for kernel show information

extern int vsprintf();        // format printf to an string, define in 'vsprintf.c'
extern void init(void);       
extern void blk_dev_init(void);   // bulk dev init, in 'blk_drv/ll_rw_blk.c'
extern void chr_dev_init(void);   // char dev init, in 'char_drv/tty_io.c'
extern void hd_init(void);        // hard disk init, in 'blk_drv/hd.c'
extern void floppy_init(void);    // soft drive init, in 'blk_drv/floppy.c'
extern void mem_init(long start, long end);       // memory init, in mm/memory.c'
extern long rd_init(long mem_start, int length);  // virtual disk init, in 'blk_drv/ramdisk.c'
extern long kernel_mktime(struct tm *tm);  // calculate system startup time, in second
extern long startup_time;         // kernel start time(boot time), in second

/*
 * This is set up by the setup-routine at boot-time
 * set by 'setup.s'
 * 0x90002 ,2 bytes, extend memory num
 * 0x90080 ,32 bytes, harddisk parameter
 * 0x901FC ,2 bytes, root file system 's dev num, set in bootec.s
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC) 

/*
 * read COMS realtime information, outb_p and inb_p is defined in 'include/am/io.h'
 * 0x70 is write address, 0x80|addr is COMOS memory address to be read
 * 0x71 is read data address

 */
#define CMOS_READ(addr)  ({ \      
		outb_p(0x80|addr,0x70);\         // or 0x80 is not required. smaller than 128 bytes. 
		inb_p(0x71);\               
		})

// BCD code to BIN , BCD use 4 bit indicate one dec num, so one byte is two dec num
#define BCD_TO_BIN(val) ((val) = ((val)&15) + ((val)>>4)*10)


/*
 * read COMOS realtime information to convert to boot time, save to variable startup_time(s)
 * kernel_mktime() calculate second elapsed from 1970.01.01 0 hour to current time, save as boot time
 * 'kernel/mktime.c'
 */
static void time_init(void)
{
	struct tm time;       // struct tm defined in include/time.h

	do{
		time.tm_sec = CMOS_READ(0);          // CMOS buffer table
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	}while (time.tm_sec != CMOS_READ(0)); // read CMOS is low, so if sec change, re read

	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;                       // range 0 - 11
	startup_time = kernel_mktime(&time);   // calculate start-up time

static long memory_end = 0;                        // machine memory capacity
static long buffer_memory_end = 0;                 // fast buffer end address
static long main_memory_start = 0;                 // main memory start address
struct drive_info { char dummy[32];}drive_info;    // save harddisk parameter

/*
 * when init done, main will run in idle task (task 0)
 */
void main(void)
{
	/*Interrupts are still disabled. Do neceary setup, then
	 * enable them.
	 */
	ROOT_DEV = ORIG_ROOT_DEV;                 // ROOT_DEV is extern int in 'include/linux/fs.h',defined in 'fs/super.c'
	drive_info = DRIVE_INFO;                  // copy drive_info at address 0x90080
	memory_end = (1<<20) + (EXT_MEM_K<<10);   // 1Mb + extend memory * 1024 bytes
	memory_end &= 0xffff0000;                 // ignore lower than 4kb
	if (memory_end > 16 * 1024 * 1024)
		memory_end = 16 * 1024 & 1024;
	if (memory_end > 12 * 1024 * 1024)
		buffer_memory_end = 4  * 1024 * 1024；
	else if (*memory_end > 6 * 1024 * 1024)
		buffer_memory_end = 2 * 1024 * 1024;
	else
		buffer_memory_end = 1 * 1024 * 1024;
	main_memory_start = buffer_memory_end;     // start = buffer_end

#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK * 1024);  //if define RAMDISK, main memory will reduce
#endif

	mem_init(main_memory_start,memory_end);   // main_memory init  (mm/memory.c)
	trap_init();                              // trap(hardware interrupt) init, (kernel/traps.c)
	blk_dev_init();                           // kernel/blk_drv/ll_rw_blk.c
	chr_dev_init();                           // kernel/chr_drv/tty_io.c
	tty_init();                               // tty init, kernel/chr_drv/tty_io.c
	time_init();                              // set startup_time
	sched_init();                             // kernel/sched.c, loat task0's tr,ldtr
	buffer_init(buffer_memory_end);           // fs/buffer.c, establish memory list
	hd_init();                                // kernel/blk_drv/hd.c , hard disk init
	floppy_init();                            // kernel/blk_drv/floppy.c, soft disk init
	sti();                                    // open interrupt

// below process setup parameter in stack, and through iret to start task0
	move_to_user_mode();                      // include/asm/system.h, first line
	if (!fork())                              // if ok
	{ 
		init();                               // in new process to execute task1
	}

/*
 * for any other task's 'pause' would mean we have to get a signal to awaken, but task0 is the sole exception(see schedule())
 * as task 0 gets activated at every idle moment(when no other tasks can run).
 * for task0 'pause()' just means we go check if some other task can run, and if not we return here.
 */
	// pause() systemcall(kernel/sched.c) will convert task0 to wait status which can be interrupted, then execute sched.c
	// when no other task to run ,return task0.
	for(;;) pause();
}

/* create format information and output to standard device stdout(1), here is screen.
 * '*fmt' point format
 * this program use vsprintf() to pur format string to printbuf buffer, then use write() to output to stdout.
 * vsprintf() is in kernel/vsprintf.c
 */
static int printf(const char *fmt, ...)  // **arv, arg
{
	va_list args;                        // define pointer, type is va_list
	int i;
	va_start(args,fmt);                  // pointer point to the last fixed parameter, here is fmt
	write(1,printbuf,i=vsprintf(*printbuf, fmt, args));  // use va_arg to obtain varied parameters, here is in vsprintf
	va_end(args);                        // end obtain parameter
	return i;
}

// the command and environment parameters used when read and execute /etc/rc file
static char * argv_rc[] = { "/bin/sh",NULL };            // program parameter
static char * envp_rc[] = { "HOME=/", NULL};             // environment parameter

// the command and environment parameters when run shell
// the argv[0] char '-' is a flag pass to shell 'sh' program
// by identify the flag, the 'sh' program will execute as log shell.
static char * argv[] = { "-/bin/sh", NULL};
static char * envp[] = { "HOME=/usr/root", NULL };

/* main function execute system init, memory init, hard device init, and driver init.
 * init() will run in first children process (task 1)
 * it will init the environment for shell. then log in shell and load program and execute.
 */
void init(void)
{
	int pid, i;
	setup((void *) &drive_info);        // system call, read hard disk parameters which include partition table and load virtual disk
	                                    // and root file system device.  correspond to sys_setup(), in kernel/blk_drv/hd.c 
										// drive_info struct is 2 hard disk parapmesters table
	(void) open("/dev/tty0",O_RDWR,0); // open /dev/tty0 in read/write mode, this is terminal console, because this is fist open,so file descripter
	                                    // is 0 ,the descripter is stdin,
	// here dupplicate to create stdout and stderr(standard err output), void to indecate no return 
	(void) dup(0);                      // descripter 1 , stdout
	(void) dup(0);                      // descripter 2 , stderr 
	printf("%d buffers = %d bytes buffer space \n\r",NR_BUFFERS,
			NR_BUFFERS*BLOCK_SIZE);     // printf buffer information 
	printf("FREE mem: %d bytes\n\r",memory_end - main_memory_start); // printf free memmory information

/*
 * this function is close 0 and open /etc/rc immediately, it is re locate stdin to /etc/rc file, so shell /bin/sh can run command in rc file.
 * here shell in no communicate, so it will exit when done rc file command, and task2 is terminate.
 */
	if (!(pid = fork()))                // fork() to create task2, for new process, fork() will return 0, for father, will will new process pid
	{                                   // this function is execute in task2
		close(0);                       // close stdin
		if (open("/etc/rc",O_RDONLY,0)) // open /etc/rc file in read only, use execve() function to replace process itself to /bin/sh (shell)
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);  // execute /bin/sh , the parameters is argv_rc and envp_rc, in fs/exec.c 
		_exit(2);                       // if execve() failed, then exit.  -2, file or directory not exist.
	}

	if (pid > 0)                        // this is father process(task 1), wait() subprocess terminatre, return value is subprocess pid
		while (pid != wait(&i));        // &i is the locate to save status information, wait until equal subprocess pid
/*
 * if process execute here, indicate the subprocess is terminated,  below loop first create an subprocess
 * if failed, printf failed information, and continue.
 * new subprocess is to close (stdin stdout stderr) before, and create a new dialog and set group number
 * then re open /dev/tty0 as stdin, and dup to stdout and stderr.
 * execute /bin/sh again, if subprocess terminated, show err information in stdout, then retry.
 * wait() here is to process orphan process(no father process), 
 * */
	while (1)
	{
		if ((pid = fork()) < 0 )
		{
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid)                                    // new subprocess
		{
			close(0);
			close(1);
			close(2);                               // close descripter before
			setid();                                // create new dialog
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);                          // stdout
			(void) dup(0);                          // stderr
			_exit(execve("/bin/sh",argv,envp));     // another paratermeters
		}
		while (1)
			if (pid == wait(&i))                    // wait(), if terminated, return big loop
				break;
		printf("\n\r child %d died with code %04x \n\r",pid,i); // err information
		sync();                                     // sync buffer
	}  
	_exit(0);                                       // _exit() is sys_exit call, but exit() is an function, process some clean action, then sys_exit.
}
