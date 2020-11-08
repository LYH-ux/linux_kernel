!
! SYS_SIZE is the number of clicks to be loaded
! 0x3000 i 0x3000 bytes = 196 KB 

SYSSIZE = 0x3000

! bootsect.s  
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves itself
! out of the way to address 0x90000, and jumps there.
!
! It then loads 'setup' directly after itself (0x90200), and the system at 0x10000
! 
!
! global symbol
! .text .data .bss define cs ds bss
.global begtext, begdata, begbss, endtext, enddata, endbss 
.text
begtext:
.data
begdata:
.bss
begbss:
.text

SETUPLEN = 4        !setup-sectors
BOOTSEG = 0x07c0    !original address of boot-sector
INITSEG = 0x9000    !bootsect's original address
DETUPSEG = 0x9020   !setup starts here
SYSSEG = 0x1000     !system loaded at 0x10000
ENDSEG = SYSSEG + SYSSIZE  !whree to stop loading ,SYSSIZE is defined at start

! ROOT_DEV:   0x000 same type of floppy as boot    ,soft driver disk
!             0x301 first partition on first drive  ,located at first harddisk's first segment
!
ROOT_DEV = 0x306    !second harddisk's first segment, this can be redefine

! command entry let a.out include the symbol it defined, here is start
entry start  
start: 
        mov    ax,#BOOTSEG   
        mov    ds,ax         ! ds, 0x7c0        
        mov    ax,#INITSEG
        mov    es,ax         ! es, 0x9000
        mov    cx,#256       ! cx, offset,256
        sub    si,si         ! src address: ds:si 0x07c0:0x0000  
        sub    di,di         ! dst address: es:di 0x9000:0x0000
        rep                  ! sub cx, repeat execution until cx = 0
        movw                 ! movs mov cx bytes from si to di
        jmpi   go,INITSEG    ! jump interegment, go is offset, INITSEG is 
		                     ! segment address
! below code will execute in address 0x90000
! this code is set segment reg include ss and sp
go:     mov    ax,cs         ! ax is 0x9000(cs:INITSEG)
        mov    ds,ax        
		mov    es,ax         ! set ds es
		mov    ss,ax
		mov    sp,#0xFF00    ! es:sp = 0x9000:0xff00

! load the setup-sectors directly after the bootblock
! Note that es is already set up.
! use BIOS 13 interrupt to read setup module to 0x90200, 4 sectors
! if error, reset, try again
! 0x13: 
! ah = 0x02  read sectors to memory
! al = nums of sectors need to read
! ch = tracks low 8 bit, cl = start sector(bit0-5) track(bit 6-7)
! dh = head num    dl = drive num
! es:bx  point to data buffer, if error then reset CF flag, ah is error code
load_setup:
        mov    dx,#0x0000    ! drive 0, head 0
		mov    cx,#0x0002    ! sector 2, track 0
		mov    bx,#0x0200    ! address = 512, in INITSEG
		mov    ax,#0x0200+SETUPLEN  ! service 2, nr of sectors
		int    0x13            
		jnc    ok_load_setup ! ok -continue  
		mov    dx,#0x0000    ! read driver 0
		mov    ax,#0x0000    ! reset the diskette
		int    0x13
		j      load_setup    ! jmp command
! Get disk drive parameters, specifically nr of sectors/track
ok_load_setup:
        mov    dl,#0x00
		mov    ax,#0x0800    ! ah = 0x08  dl = drive num
		int    0x13
		mov    ch,#0x00
		seg    cs            ! only influence one statement below it
		mov    sectors,cx
		mov    ax,#INITSEG
		mov    es,ax
		mov    ah,#0x03
		xor    bh,bh
		int    0x10
		mov    cx,#24
		mov    bx,#0x0007
		mov    bp,#msg1
		mov    ax,#0x1301
		int    0x10
		mov    ax,#SYSSEG
		mov    e,ax
		call   read_it
		call   kill_motor
		seg    cs
		mov    ax,root_dev
		cmp    ax,#0
		jne    root_defined
		seg    cs
		mov    bx,sectors
		mov    ax,#0x0208
		cmp    bx,#15
		je     root_defined
		mov    ax,#0x021c
		cmp    bx,#18
		je     root_defined
undef_root:
        jmp    undef_root
root_defined:
        seg    cs
		mov    root_dev,ax
        
        jmpi    0,SETUPSEG

! al need read nums, es:bx -buffer start
read_track:
        push   ax
		push   bx
		push   cx
		puhs   dx             ! push stack to save ax,bx,cx,dx
		mov    dx,track       ! obtain track num
		mov    cx,sread       ! obtain readed nums
		inc    cx             ! cl = start track
		mov    ch,dl          ! ch = current track
		mov    dx,head        ! current head
		mov    dh,dl          ! dh = head num
		mov    dl,#0          ! dl = drive num, here 0 is A drive
		and    dx,#0x0100     
		mov    ah,#2          ! ah = 2, function num
		int    0x13
		jc     bad_rt         ! if error, jmp bad_rt
		pop    dx             ! pop stack to recover ax,bx,cx,dx
		pop    cx
		pop    bx
		pop    ax
		ret 
! read track error, execute drive reset, jump read_track
bad_rt:
        mov    ax,#0
		mov    dx,#0
		int    0x13
		pop    dx
		pop    cx
		pop    bx
		pop    ax
		jmp    read_track

/*this procedure turns off the floppy drive motor*/
! 0x3f2 is an port of DOR, it is an 8 bit reg, bit 7 - 4 used to controll soft drive(D-A) open or off
! bit 3-2 used to allow DMA and interrupt request  and start/reset controller FDC
! bit 1-0 used to select soft drive which to be operated.
! reference kernel/blk_drv/floppy.c 
kill_motor:
        push    dx
		mov     dx,#0x3f2       ! DOR port, write only
		mov     al,#0           ! select soft drive A, closed FDC, prohibit DMA and interrupt
		                        ! close motor
		outb                    ! output to selected port 
		pop     dx
		ret

sectors:
        .word    0
msg1:
        .byte    13,10     ! ASCII  of enter and \n
	    .ascii    "Loading system ..."
	    .byte    13,10,13,10   ! total 24 ASCII bytes
! address start from  508 (0x1FC), so root_dev is in bootsect's  508 address
.org   508
root_dev:
        .word    ROOT_DEV     ! save root file system's device number
	                          ! this will used in init/main.c
boot_flag:
        .word    0xAA55       ! the flag of bootsect's validity
.text
endtext:
.data
enddata:
.bss
endbss:
