/*
 * linux/kernel/system_call.s
 *
 * this file contains system_call and timer_interrupt
 * and hd- and floppy-interrupts are also here
 *
 * stack layout in 'ret_from_system_call:
 *  0(%esp) %eax
 *  4(%esp) %ebx
 *  8       %ecx
 *  C       %edx
 *  10      %fs
 *  14      %es
 *  18      %ds
 *  1c      %eip
 *  20      %cs
 *  24      %eflags
 *  28      %oldesp
 *
 */

# NOTE: only int 0x80 and timer-interrupt int 0x20 can handle signal

SIG_CHLD  = 17       # subprocess terminate or end, signal 
EAX = 0x00           # offset of reg in stack
EBX = 0x04
ECX = 0x08
EDX = 0x0C
FS  = 0x10
ES  = 0x14
DS  = 0x18
EIP = 0x1C
CS  = 0x20
EFLAGS = 0x24
OLDESP = 0x28        # when privilege level change, the user stack pointer will save in kernel stack
OLDSS  = 0x2C

/*
 * below variable are offset in task_struct, see include/linux/sched.h
 */
state = 0             # process state code , run block end ...
counter = 4           # runtime slice, count runtime ( --)
priority = 8          # counter = priority at start, the bigger, run longer
signal = 12           # signal bitmap, every bit represent one signal, signal num = bitoffset +1
sigaction = 16        # must be 16(len of sigaction)

blocked = (33*16)     # blocked signal offset

/*
 * below are offset in sigaction struct, see include/signal.h
 */
sa_handler = 0         # handler, descriptor
sa_mask = 4            # mask code
sa_flags = 8           # signal set
sa_restorer = 12       # pointer of restore function, see kernel/signal.c
nr_system_calls = 72   # total system_call nums in linux 0.11


# define of entry 
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available,_coprocesor_error

# error syscall num
.align 2               # align in 4 bytes  2 ^ 2
bad_sys_call:
    movl $-1,%eax      # set %eax to -1, and exit
	iret

# schedule in kernel/sched.c
.align 2
reschedule:
    pushl $ret_from_sys_call     # push in stack, when schedule() return, run from ret_from_sys_call
	jmp _schedule

# int 0x80 - system_call entry
.align 2
_system_call:
    cmpl $nr_system_calls-1,%eax    # if overflow
	ja   bad_sys_call
	push %ds                        # save segment value
	push %es
	push %fs
# one system_call can have 3 parameters or no parameters
# ebx ecx edx save function call 's parameters, the push order is defined by GNU GCC
	pushl %edx
	pushl %ecx
	pushl %ebx
# set segment reg
	movl  $0x10,%edx           # set up ds,es to kernel space, defined in GDT
	mov   %dx,%ds
	mov   %dx,%es
	movl  %0x17,%edx           # set up fs to LDT, local data segment, user DS
	mov   %dx,%fs              # in linux 0.11, code and data segment is same, see fork.c 'copy_mem' function
	call  _sys_call_table(,%eax,4)   # call address = _sys_call_table + %eax * 4
	pushl %eax                 # push return value in stack
# check current task state, if state not in ready(state != 0), run schedule.
# and if in ready state, but counter = 0, then alse schedule
	movl  _current,%eax        # obtain current task struct's address to eax
	cmpl  $0,state(%eax)       # state
	jne   reschedule
	cmpl  $0,counter(%eax)     # counter
	je    reshcedule
/*
 * below program process signal when system return from system_call C function.
 * other interrupt save function also jmp here then exit interrupt
 */
ret_from_sys_call:
#  first judge if task0, if task 0 , then skip signal processing and return imediately.
    movl  _current,%eax        # current task control struct, task[0] can not have signals
	cmpl  _task,%eax
	je    3f                   # jmp forward to label 3
# by check cs to judge the caller wheather user task, if not, exit(task in kernel can not been grabbed.
# if caller is user task, then process signal.
# here is cmp if the cs is 0x000f(RPL = 3, LDT, first segment(cs)) to judge wheather user task.
# if not, jmp and exit. 
# if stack segment not equal 0x17(in user stack), then also quit.
	cmpw  $0x0f,CS(%esp)         # was old code segment supervisor ?
	jne   3f
	cmpw  $0x17,OLDSS(%esp)      # was stack segment = 0x17 ?
    jne   3f
# below program process current task's signal
# first take signal bitmap (32 bit), then use mask code, obtain smallest signal num
# and reset corresponds bit(clear 0), then run do_signal use the parameter.
# do_signal is in kernel/signal.c, parameters are 13 stack information.
	movl  signal(%eax),%ebx         # signal num -> ebx
	movl  blocked(%eax),%ecx      
	notl  %ecx                      # convert, !x
	andl  %ebx,%ecx                 # &, obtain permitted signal
	bsfl  %ecx,%ecx                 # scan from bit 0, check if has 1.
	                                # if has, the offset save to ecx.
	je    3f                        # if not, jmp forward and exit
	btrl  %ecx,%ebx                 # reset signal
	movl  %ebx,signal(%eax)         # save signal -> current -> signal
	incl  %ecx                      # adjust signal start form num 1
	pushl %ecx                      # push stack as parameter
	call  _do_signal                # call do_signal, in kernel/signal.c
	popl  %eax                      # pop signal from stack
3:  popl  %eax
    popl  %ebx
	popl  %ecx
	popl  %edx
	pop   %fs
	pop   %es
	pop   %ds
	iret

# int 16, coprocessor error, type: error, no error code
# kernel/math/math_emulate.c
.align 2
_coprocessor_errror:
    push  %ds
	push  %es
	push  %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl  $0x10,%eax             # kernel segment
	mov   %ax,%ds
	mov   %ax,%es
	movl  $0x17,%eax             # fs is LDT segment(the program which meet error)
	mov   %ax,%fs
	pushl $ret_from_sys_call     # push stack to call
	jmp   _math_error            # math_error() , math/math_emulate.c

# int 7 , type: error, no error code
.align 2
_device_not_available:
    push  %ds
	push  %es
	push  %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl  $0x10,%eax
	mov   %ax,%ds
	mov   %ax,%es
	movl  $0x17,%eax
	mov   %ax,%fs
	pushl $ret_from_sys_call
	clts                             # clear TS so that we can use math
	movl  %cr0,%eax                  # get CR0, if EM = 0, then can restore
	testl $0x4,%eax                  # EM(math emulation bit)
	je    _math_state_restore        # math_state_restore(), in kernel/sched.c
# else, run math_emulate()
	pushl %ebp
	pushl %esi
	pushl %edi
	call  _math_emulate
	popl  %edi
	popl  %esi
	popl  %ebp
	ret

# int 32 (int 0x20) timer-interrupt handler
# interrupt frequency is 100Hz(include/linux/sched.h)
# 8253/8254 is init in kernel/sched.c
# jiffies inc in 10ms
.align 2
_timer_interrupt:
    push %ds                 # save ds,es and put kernel data space into them
	push %es              
	push %fs                 # fs i used by _system_call
	pushl %edx
	pushl %ecx
	pushl %ebx               # ebx is saved as we use that in ret_sys_call
	pushl %eax               # we save these regs because GCC doesn't save them from function calls
	movl  $0x10,%eax         # kernel segment
	mov   %ax,%ds
	mov   %ax,%es
	movl  $0x17,%eax         # LDT, user cs
	mov   %ax,%fs       
	incl  _jiffies           # inc in 10ms
	movb  $0x20,%al          
	outb  %al,$0x20          # clear interrupt flag
	movl  CS(%esp),%eax
	andl  $3,%eax            # obtain privilege in cs, (0 or 3) ,CPL(0 or 3, 0 = supervisor)
	pushl %eax               
	call  _to_timer          # do_timer(long CPL)' does everything from task switching to accounting
	                         # in kernel/sched.c
	addl  $4,%esp
	jmp   ret_from_sys_call

# sys_execve, do_execve() in fs/exec.c
.align 2
_sys_execve:
    lea   EIP(%esp),%eax      
	pushl %eax
	call  _do_execve
	addl  $4,%esp            
	ret
# sys_fork, create subprocess, system_call function 2
# see include/linux/sys.h
# call find_empty_process to obtain pid, if lower than 0, indicate task buffer has fulled.
# call copy_process 
.align 2
_sys_fork:
    call _find_empty_process     # kernel/fork.c
	testl %eax,%eax              # return pid to eax, if negtive,then quit
	js    1f                     # jmp forward
	push  %gs                    
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call  _copy_process          # copy_process(), lkernel/fork.c
	addl  $20,%esp
1:	ret

# int 46 (int 0x2E) harddisk interrupt handler, IRQ14
# kernel/blk_drv/hd.c, when harddisk operate done, then trigger interrupt
_hd_interrupt:
    pushl  %eax
	pushl  %ecx
	pushl  %edx
	push   %ds
	push   %es
	push   %fs
	movl   $0x10,%eax
	mov    %ax,%ds
	mov    %ax,%es
	movl   $0x17,%eax
	mov    %ax,%fs
	movb   %0x20,%al
	outb   %al,$0xA0     # output EOI command to 8259A, end hard interrupt
	jmp    1f            # delay
1:	jmp    1f
# do_hd define a function pointer, point to read_intr() or write_intr()
# mov to edx, then set do_hd pointer to NULL
# test, if pointer == NULL, then point to unexpected_hd_interrupt()
1:  xorl   %edx,%edx                      # clear
    xchgl  _do_hd,%edx                    # do_hd -> edx
	testl   %edx,%edx                     # test if == NULL
	jne    1f
	movl   $_unexpected_hd_interrut,%edx  # if == NULL,point to unexpected_hd_interrupt()
1:  outb   %al,$0x20                      # output EOI command
    call   *%edx                          # call function
	pop    %fs
	pop    %es
	pop    %ds
	popl   %edx
	popl   %ecx
	popl   %eax
	iret

# int 38 (int  0x26) floppy interrupt, IRQ6
# kernel/blk_drv/flopy.c
# do_floppy's function pointer -> eax
# call eax's pointed function:rw_interrupt,seek_interrupt,recal_interrupt,reset_interrupt
# or unexpected_floppy_interrupt.
_floppy_interrupt:
    pushl  %eax
	pushl  %ecx
	pushl  %edx
	push   %ds
	push   %es
	push   %fs
	movl   $0x10,%eax
	mov    %ax,%ds
	mov    %ax,%es
	movl   $0x17,%eax
	mov    %ax,%fs
	movb   $0x20,%al          
	outb   %al,$0x20                    # output EOI to 8259A
    xorl   %eax,%eax
	xchgl  _do_floppy,%eax
	testl  %eax,%eax
	jne    1f
	movl   $_unexpected_floppy_interrupt,%eax    # if NULL, pointer -> eax
1:  call   *%eax                        # call function
    pop    %fs
	pop    %es
	pop    %ds
	popl   %edx
	popl   %ecx
	popl   %eax
	iret

# int 39 (int 0x27), IRQ7
_parallel_interrupt:
    puhsl  %eax
	movb   $0x20,%al
	outb   %al,$0x20       # output EOI
	popl   %eax
	iret


