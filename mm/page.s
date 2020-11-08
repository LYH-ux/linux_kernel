/*
 * linux/mm/page.s
 */

/*
 * page.s contains the low-level page-exception code.
 * the real work i sdone in memory.c
 */

.globl _page_fault   # global variable, set descriptor in traps.c

_page_fault:
    xchgl  %eax,(%esp)     # get error code -> eax
	pushl  %ecx
	pushl  %edx
	push   %ds
	push   %es
	push   %fs
	movl   $0x10,%edx      # set kernel segment selector
	mov    %dx,%ds
	mov    %dx,%es
	mov    %dx,%fs
	movl   %cr2,%edx       # get liner adddress which cause page-exception
	pushl  %edx            # push address and error code in stack
	pushl  %eax
	testl  $1,%eax         # test exist flag P(bit 0), if not no_page exception, jmp
	jne    1f              
	call   _do_no_page     # call do_no_page , mm/memory.c
	jmp    2f
1:  call   _do_wp_page     # call write protection function, mm/memory.c
2:  addl   $8,%esp         # drop 2 parameters,pop and exit
    pop    %fs
	pop    %es
	pop    %ds
	popl   %edx
	popl   %ecx
	popl   %eax
	iret
