/*
 * linux/kernel/sched.c
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>       // sytem_call
#include <linux/fdreg.h>     // floppy drive header
#include <asm/system.h>      // GDT IDT
#include <asm/io.h>
#include <asm/segment.h>     // segment reg

#include <signal.h>          // struct of sigaction

// get signal 'nr' 's binary num in signal bitmap
// 1-32 signal 5 =  1<<(5-1) = 16 = 00010000b
#define _S(nr) (1<<((nr)-1))

// all signal can be blocked except SIGKILL and SIGSTOP
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))


/*
 * kernel debug function 
 * shor task num 'nr' 's process num, state ,sand stack free bytes
 */
void show_task(int nr,struct task_struct *p)
{
	int i,j = 4096 - sizeof(struct task_struct);
	printk ("%d: pid = %d, state = %d, ", nr, p->pid,p->state);
	i = 0;
	while (i<j && !((char *)(p+1))[i])   // check 0 bytes at task_struct end, (p + 1)  
		i++;
	printk("%d (of %d) chars free in kernel stack \n\r",i,j);
}

/*
 * show all task's nr, process num, state , stack free bytes
 * NR_TASKS is the system's max process num(64), include/kernel/sched.h
 */
void show_stat(void)
{
	int i;
	for (int i = 0; i < NR_TASKS; i++)
		if (task[i])
			show_task(i,task[i]);
}

// PC 8253 chip's frequency is 1.193180MHz, linux kernel need 100Hz(10ms)
#define LATCH (1193180/HZ)

extern void mem_use(void);

extern int timer_interrupt(void);   // kernel/system_call.s
extern int system_call(void);


/*
 * the kernel stack struct
 */
union task_union            //union, task_struct and stack char array
{
	struct task_struct task;   
	char stack[PAGE_SIZE];       // ss can obtain data segment selector, because union in same memory
};

static union task_union init_task = {INIT_TASK,};  // define init task, sched.h

long volatile jiffies = 0;       // di-da, every 10 ms

long startup_time = 0;     // start_up time, from 1970:0:0:0 seconds
 
struct task_struct *current = &(init_task.task);   // current task pointer, init to task0
struct task_struct *last_taks_used_math = NULL;    // the pointer of who used copessor

struct task_struct *task[NR_TASKS] = {&(init_task.task),};  // task pointer array


long user_stack [PAGE_SIZE >> 2]; // user stack, 1K num, 4K bytes
                                  // in kernel init, used as kernel stack
								  // after init, it used as task0 and task1 's user stack

// this struct used to set ss:esp ( data segment selector, pointer), see head.s
struct 
{
	long *a;
	short b;
}stack_start = {&user_stack[PAGE_SIZE>>2],0x10};    // ss set to 0x10, esp point user_stack array last term
                                                    // Intel 's stack is down direction

/*
 * saves the current math information in the old math state array
 * and gets the new ones from the current task.
 *
 * when task is be swapped, this function save original task's copretor state(up down text)
 * and reschedule current task's coprator state.
 */
void math_state_restore()
{
	if (last_task_used_math == current)
		return;
	_asm_("fwait");                // send WAIT command
    if (last_task__used_math)      // if used, save
	{
		_asm_("fnave %0"::"m"(last_task_ued_math -> tss.i387));
	}
	last_task_used_math = current;
	if (current -> used_math)     // if current used, restore
	{
		_asm_("frstor %0"::"m"(current->ts.i387));
	}
	else                          // else, init , and set flag
	{
		_asm_("fninit"::);
		current -> used_math = 1;
	}
}


/*
 * this the scheduler function
 * NOTE:
 * task0 is the 'idle' task, which gets called when no other tasks can run.
 * is can not be skilled, and it cannot sleep.
 * the 'state' information in task[0] is never used.
 */
void schedule(void)
{
	int i,next,c;
	struct task_struct **p;        // the pointer to task_struct pointer
	/* check alarm, wake up interruptible tasks that have got a signal 
	 * alarm ,the value of warning time
	 */
	for(p = &LAST_TASK ; p > &FIRST_TASK; --p)
	{
		if (*p) // p != NULL
		{
			if ((*p)->alarm && (*p)->alarm < jiffies)  // if set alarm value, and out of date(<jiffies) 
			{
				(*p)->signal |= (1<<(SIGALRM-1)); // set SIGALRM and send SIGALRM to task  (_S(nr)
				(*p)->alarm = 0;                  // clear alarm, the SIGALRM will terminate process
			}
			// if has other signal except blocked signal, and task is interruptible
			// set task state to running (ready)
			if (((*P)->signal &~ (_BLOCKABLE & (*p)->blocked)) && (*p)->state == TASK_INTERRUPTIBLE)
				(*p) ->state = TASK_RUNNING;
		}
	}

	/* this is the scheduler proper: */
	while (1)
	{
		c = -1;
		next = 0;
		i = NR_TASKS;
		p = &task[NR_TSAKS]; 
		/* this code also from task array end, and skip no task location 
		 * compare every running(ready) task's counter(dec --), whoes value bigger, running time is smaller
		 * next point to it 
		 */
		while (--i)
		{
			if (!*--p)      // -- priority == *, but associativity is from right to left
				continue;
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)  
				c = (*p)->counter,next= i;
		}
		if (c)   // if c != 0, or c = -1, break, exexute switch_to
			break;
		for (p= &LAST_TASK; p > &FIRST_TASK; --p) // update all task's state and re compare.
		{
			if (*P)
				(*p)-> counter = ((*p)->counter >> 1) + (*p)->priority;  // counter=counter/2 + priority
		}
	}	
	switch_to(next);  // in sched.h, set current_task pointer to next, and switch to, next is init to 0
	                  // so, if c = -1 and next = 0, switch_to idel(task0), only pause() system_call.
					  // and task0 will call this function again.
}


/*
 * turn current task's state to wait and interruptible. re schedule
 * this function will set process to sleep and wait a signal.
 * the signal used to terminate a process or call signal trap function.
 * only when signal is trapped and handle function return, the pause will return.
 * this function is functioned after linux 0.95
 */
int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
    return 0;
}

/*
 * set current task to wait and uninterruptible
 * set sleep queue's head pointer to current task.
 * only when the task been wake_up, will return. this can realize synchronization.
 * the 'p' is head pointer of wait queue , becaue *p will be modified, so use **p
 */
void sleep_on (struct task_struct **p)
{
	struct task_struct *tmp;
	if (!p)
		return ;
	if (current == &(init_task.task))   // task0 can't sleep
		panic("task[0] truint to sleep");
	tmp = *p;
	*p = current;
	current -> state = TASK_UNITERRUPTIBLE;
	schedule();         // only when this function be awake, the program will return here.
	*p = tmp;
	if (tmp)            // awake other waiting task before.
		tmp -> state = 0;
}


void interruptible_sleep_on (struct task_struct **p)
{
	struct taks_struct *tmp;
	if (!p)
		return ;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp = *p;
	*p = current;
repeat: current->state = TASK_INTERRRUPTIBLE;
		schedule();
		if (*p && *p != current)  // indicate have new task insert to  the wait queue head,awake them
		{
			(**p).state = 0;   
			goto repeat;         // wait other task to awake current task
		}
		if (tmp)
			tmp -> state = 0;

}

// *p is the task wait queue's head pointer.
// because new wait will insert at head pointer, so wake the last task who enter wait queue
void wake_up(struct task_struct **p)
{
	if (p && *p)
	{
		(**p).state = 0;    // set running(ready) state, TASK_RUNNING
	}
}


/*
 * some floppy things 
 * they are here because the floppy needs a timer, and this was the easiest way of doing it.
 * index 0-3 corresponds to floppy A-D
 */
// save process pointer that wait moter turn to normal speed.
static struct task_struct *wait_motor[4] = {NULL, NULL, NULL, NULL};
// safe every floppy motor 's timer that needed in start up, set 50 jiffies (0.5s)
static int mon_timer[4] = {0,0,0,0};
// the sustain time before off, set 10000 jiffies(100 s)
static int moff_timer[4] = {0,0,0,0};

// output register
// bit 7-4 : D - A start_up   1-start, 0-off
// bit 3: 1 - allow DMA and interrupt request, 0 - prohibit DMA and interrupt
// bit 2: 1 - start floppy controller, 0-reset floppy controller
// bit 1-0: 00-11, select which floppy(A-D) to control
unsigned char current_DOR = 0x0C;  // allow DMA and interrupt, start_up FDC(controller)

int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected;  // the flag of selected, blk_drv/floppy.c
    unsigned char mask = 0x10 << nr;  // mask is the start_up bit for selected floppy
	if (nr >3 )
		panic("floppy_on: nr > 3");
	moff_timer[nr] = 10000;  
	cli();                   // close interrupt
	mask |= currrent_DOR;    // start_up
	if (!selected)
	{
		mask &= 0xFC;    // if not selected, reset
		maks |= nr;      // select 
	}

	if (mask != current_DOR)  // if not equal, output new value
	{
		outb(mask,FD_DOR);    
		if ((mask ^ current_DOR) & 0xf0)  // if not start_up, set timer to HZ/2, 0.5s, 50 jiffies
			mon_timer[nr] = HZ/2;
		else if (mon_timer[nr] <2)   // if start_up, set to 2 jiffies. to satify do_floppy_timer()
			mon_timer[nr] = 2;
		current_DOR = mask; 
	}
	sti();     // open interrupt
	return mon_timer[nr];  // return start_up timmer
}

/*
 * wait the delay. in sleep_on, will dec the timer.
 * when timer up, wake_up the wait process.
 */
void floppy_on(unsigned int nr)
{
	cli();
	while (ticks_to_floppy_on(nr))
		sleep_on(nr+wait_motor);
	sti();
}

/*
 * if not call this function, motor will off after 100 s
 */
void floppy_off(unsigned int nr)
{
	moff_timer[nr] = 3*HZ;   // 3 s
}


void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;
	for (i = 0; i < 4; i++,maks <<= 1)
	{
		if (!(mask & current_DOR))       // not current_DOR motor 
			continue;
		if (mon_timer[i])                // if timer end, wakeup 
		{
			if (!--mon_timer[i])        // !0 
                wake_up(i+wait_motor);
		}
		else if (!moff_timer[i])       // off timer end
		{
			current_DOR &= ~mask;      // reset corresponds start_up bit.
			outb(current_DOR,FD_DOR);
		}
		else
			moff_timer[i]--;     
	}
}

// max timer num is 64
#define TIME_REQUESTS 64

/*
 * only used for kernel
 */
static struct timer_list
{
	long jiffies;
	void (*fn)();                  // handle function
	struct timer_list *next;       
}timer_list[TIME_REQUESTS], *next_timer = NULL;

/*
 * the flopy.c call this function to start_up or close motor.
 * 'fn', the function will be called when timer up.
 */
void add_timer(long jiffies, void (*fn)(void ))
{
	struct timer_list *p;
	if (!fn)
		return;
	cli();
	if (jiffies <= 0)   // if < 0, call fn immediately ,and return.
		(fn)();
	else
	{
		for (p = timer_list; p<timer_list + TIME_REQUESTS;p++)
			if (!p->fn) // find a free timer.traverse from head. the timer before will be free. so unorder
				break;
		if (p >= timer_list + TIME_REQUESTS)   // if timer exceed max num
			panic("No more time requests free");
		p -> fn = fn;
		p -> jiffies = jiffies;
		p -> next = next_timer;   // head insert, the new timer will insert in head
		next_timer = p;
		// sort by jiffies from small to large
		// !! when new timer's jiffies smaller than next, next->jiffies -= jiffies,and wouldn't enter loop
		while (p -> next && p -> next -> jiffies < p -> jiffies)
		{
			p -> jiffies -= p -> next -> jiffies;
			fn = p -> fn;
			p -> fn = p -> next -> fn;
			p -> next -> fn = fn;
			jiffies = p -> jiffies;
			p -> jiffies = p -> next -> jiffies;
			p -> next -> jiffies = jiffies;
			p = p -> next;
		}
	}
	sti();
}

/*
 * the timer interrupt handled
 * call in system_call.s _timer_interrupt.
 * cpl is currnt privalege 0 or 3
 * cpl = 0 is in kernel, cpl = 3 is user.
 * when a process's time slice empty, switch_to and re timing
 */
void do_timer(long cpl)
{
	extern int beepcount;             // chr_drv/console.c
	extern void sysbeepstop(void);    // kernel/chr_drv/console.c
	if (beepcount)
		if (!--beepcount)
			sysbeepstop();
	if (cpl)
		current -> utime++;
	else
		current -> stime++;   // kernel, supervisor 

	if (next_timer)
	{
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0)
		{
			void (*fn)(void);
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();
		}
	}
	if (current_DOR & 0xf0)
		do_floppy_timer();
	if ((--current->counter)>0)   // if have run time
		return ;
	current->counter = 0;  // counter, return or schedule()
	if (!cpl)
		return ;
	schedule();
}


/*
 * system_call , set warning time
 *
 */
int sys_alarm(long seconds)
{
	int old = current -> alarm;
	if (old)
		old = (old - jiffies) / HZ;    // HZ = 100, include/sched.h  
	current -> alarm = (seconds > 0) ? (jiffies + HZ*seconds):0;  // warning time is jiffies + seconds*HZ
	return (old);
}

int sys_getpid(void)
{
	return current->pid;
}

int sys_getppid(void)
{
	return current -> father;
}

int sys_getuid(void)
{
	return current -> uid;
}

int sys_geteuid(void)
{
	return current -> euid;
}

int sys_getgid(void)
{
	return current -> gid;
}

int sys_getegid(void)
{
	return current -> egid;
}

int sys_nice(long increment)
{
	if (current -> priority - increment >0)
		current -> priority -= increment;
	return 0;
}

void sched_init(void)
{
	int i;
	struct desc_struct *p;        // descriptor table
	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");
	// FIRST_TSS_ENTRY FIRST_LDT_ENTRY  in include/linux/sched.h,  value is 4 and 5.
	// gdt in include/linux/head.h, map with _gdt in head.s
	set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss));   // gdt[4]
	set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));   // gdt[5]                               
	p = gdt + 2 + FIRST_TSS_ENTRY;
	for (i = 1; i < NR_TASKS;i++)  // clear task array and decriptor
	{
		task[i] = NULL;
		p-> a = p ->b = 0;      // tss
		p++;                      
		p-> a = p -> b = 0;     // ldt
		p++;
	}
	/* clear NT, NT flag used control Nested Task, NT = 1, iret will cause task switch */
	__asm__("pushfl; and; $0xffffbfff,(%esp);popfl");

	ltr(0);   // task0 's TSS
	lldt(0);  // ldt table, load the segment selector in  GDT. only load once.  others are automatically 
	outb_p(0x36,0x43);   // init 8253, channel 0, mode 3 LSB/MSB, binary, 
	outb_p(LATCH & 0xff, 0x40);  // LSB, LATCH is init count value.
	putb(LATCH >> 8, 0x40);      // MSB
	set_intr_gate(0x20,&timer_interrupt);  // set timer interrupt gate
	outb(inb_p(0x21)&~0x01,0x21);         // allow timer IRQ
	set_system_gate(0x80,&system_call);  // system_call intrrupt gate  .  include/asm/system.h
}

   


