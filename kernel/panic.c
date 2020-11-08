/*
 * panic() function used to show kernel error and enter loop forever.
 * This function is used through-out the kernel(include .h / mm / fs)
 * to indicate a major problem
 */

#include <linux/kernel.h>
#include <linux/sched.h>

void sys_sync(void);  /* it's really int, fs/buffer.c */ 

/* 
 * volatile tell gcc no return
 * equal void panic (const char *s) _attribute_ ((noreturn));
 */
volatile void panic(const char *s)
{
	printk("Kernel panic: % \n\r",s);
	if (current == task[0])
		printk("In swapper task - not syncing \n\r");
	else
		sys_sync();
	for(;;);
}

