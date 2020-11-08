/*
 * linux/kernel/printk.c
 */

/*
 * when in kernel-mode, we cannot use printf, as fs is liable to point to 'user data'
 * printf will use fs to get information, here, we need usr 'printk' to save fs 
 * and use it , then restore it.
 */

#include <stdarg.h>   // type of va_list, and va_start, va_arg, va_end
#include <stddef.h>   // define NULL, offsetof(TYPE,MEMBER)

#include <linux/kernel.h>
static char buf[1024];

extern int vsprintf(char *buf, const char *fmt, va_list args);  // in linux/kernel/vsprintf.c

int printk(const char *fmt, ...)
{
	va_list args;
	int i;
	va_start(args,fmt);      // include/stdarg.h
	i = vsprintf(buf,fmt,args);
	va_end(args);
	_asm_("push %%fs\n\t"
	      "push %%ds \n\t"
		  "pop %%fs \n\t"    // fs = ds
		  "pushl %0\n\t"             // push string length
		  "pushl $_buf\n\t"          // push buf address
		  "pushl $0\n\t"             // push 0, num of show channel 
		  "call _tty_write\n\t"      // call tty_write() , chr_drv/tty_io.c
		  "addl $8,%%esp\n\t"
		  "popl %0\n\t"              // pop string length as return value
		  "pop %%fs"                 // restore
		  ::"r"(i):"ax","cx","dx");    
	return i;                       // return length of string
}
