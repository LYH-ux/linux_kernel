/*
 * linux/kernel/asm.s
 */

/*
 * aasm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so thar isn't here.
 */

# this file only handle int0-int16, int17-int31 is reserved.
# below is some statements of global functions, define is in 'traps.c'
#
.global _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.global _double_falult,_coprocessor_segment_overrun
.global _invalid_TSS,_segment_not_present,_stack_segment
.global _general_protection,_coprocessor_error,_irq13,_reserved

# below program handle no error code.
# int0, handle devide by 0, type: fault  code:none
# '_do_divide_error' is name of compiled module of 'do_divide_error()',in traps.c

_divide_error:
    pushl $_do_divide_error  # push the address of call function in stack
_no_error_code:
    xchgl %eax,(%esp)        # address of _do_divide_error -> eax, eax exchange to stack
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push  %ds                # also take 4 bytes in stack
	push  %es
	push  %fs
	pushl $0                 # 'error code', here push 0 in stack
	lea   44(%esp),%edx      # 44 + (%esp) -> edx
	pushl %edx               # pushl, return address, esp0
	movl  $0x10,%edx         # init segment reg, 0x10 is segment descriptor
	mov   %dx,%ds
	mov   %dx,%es
	mov   %dx,%fs
	call  *%eax              # call function, address is in %eax
	addl  $8,%esp            # esp + 8, equal pop 2 parameters
	pop   %fs
	pop   %es
	pop   %ds
	popl  %ebp
	popl  %esp
	popl  %esi
	popl  %edi
	popl  %edx
	popl  %ecx
	popl  %ebx
	popl  %eax
	iret


_debug:                     
    pushl  $_do_int3
	jmp    no_error_code
_nmi:                         # int 2
    pushl  $ _do_nmi
	jmp    no_error_code
_int3:  
    pushl  $_do_int3
	jmp    no_error_code
_overflow:                    # int 4 
    pushl  $_do_overflow
	jmp    no_error_code
_bounds:                      # int 5
    pushl  $_do_bounds
	jmp    no_error_code
_invalid_op:                  # int 6
    pushl  $_do_invalid_op
	jmp    no_error_code
_coprocessor_segment_overrun:                    # int 9
    pushl  $_do_coprocessor_segment_overrun
	jmp    no_error_code
_reserved:                    # int 15
    pushl  $_do_reserved
	jmp    no_error_code

# 0x20 + 13, math coprocessor
_irq13:                       #int 45
    pushl  %eax
	xorb   %al,%al
	outb   %al,$0xF0
	movb   $0x20,%al
	outb   %al,$0x20
	jmp    1f
1:	jmp    1f
	outb   %al,$0xA0
	popl   %eax
	jmp    _coprocessor_error  #system_call.s  

_double_fault:
    pushl  $_do_double_fault
error_code:
    xchgl  %eax,4(%esp)       # error code <-> %eax, %eax 's orign value save in stack
	xchgl  %ebx,(%esp)        # &function <-> %ebx, %ebx 's orign value save in stack
    pushl  %ecx
	pushl  %edx
	pushl  %edi
	pushl  %esi
	pushl  %ebp
	push   %ds
	push   %es
	push   %fs
	pushl  %eax               # error code push to stack
	lea    44(%esp),%eax
	pushl  %eax
	movl   $0x10,%eax
	mov    %ax,%ds
	mov    %ax,%es
	mov    %ax,%fs
    call   *%ebx              # call function
	addl   $8,%esp            # drop 2 parameters
	pop    %fs
	pop    %es
	pop    %ds
	popl   %ebp
	popl   %esi
	popl   %edi
	popl   %edx
	popl   %ecx
	popl   %ebx
	popl   %eax
	iret

_invalid_TSS:                          # int 10
    pushl  $_do_invalid_TSS
	jmp    error_code
_segment_not_present:                  # int 11
    pushl  $_do_segment_not_present
	jmp    error_code
_stack_segment:                        # int 12
    pushl  $_do_stack_segment
	jmp    error_code

_general_protection:                   # int 13
    pushl  $do_general_protection
	jmp    error_code

# int 7, _device_not_available, in kernel/ystem_call.s
# int 14, _page_fault, in mm/page.s
# int 16, _coprocessor_error, in kernel/system_call.s
# int 0x20, _timer_interrupt, in kernel/system_call.s
# int 0x80, _system_call, in kernel/system_call.s
