#
# if you want the ram-dist device,define this to be the size in blocks
# otherwise the gcc will use -DRAMDISK = 512
RAMDISK = #-DRAMDISK=512

AS86 = as86 -O -a   
LD86 = ld86 -O

AS = gas    # GNU as
LD = gld    # GNU ld
LDFLAGS = -s -x -M  # -s omit symbol, -x delete local symbol, -M print link map

CC = gcc $(RAMDISK)  # $() to quote user defined symbol
CFLAGS = -Wall -O -fstrength-reduce -fomit-frame-pointer \
         -fcobine-regs -mstring-insns

# -Wall print all warning information 
# -O ,optimize code
# -f ,appoint compile flag which unrelated with machine
# -fstrength-reduce   ,optimize circulate statements
# -fcombine-regs  ,tell CC to combine the instructions which copy one reg to another reg
# -fomit-frame-pointer  ,assign not save frame-pointer in reg for function which not use it
# -mstring-insns  ,can omit

CPP = cpp -nostdinc -Include
# cpp is pretreatment program,  -nostdinc -Iinclude ,do not serch in /usr/include, but use
# -I appoint dir, or in current dir 

ROOT_DEV = /dev/hd6   # default root file system

ARCHIVES = kernel/kernel.o mm/mm.o fs/fs.o  #this is target files, combine them for use

DRIVERS = kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a 
# block file, .a indicate them is bin lib file, which is created by GNU ar program
# ar is GNU's bin file processing file

MATH = kernel/math/math.a   # math lib
LIBS = lib/lib.a            # bin file lib in dir lib/ 

# below is suffix rules in make
# .c.s indicate cc .c file to .s file, .s.o  .c.o is same as it
# $*.s is equal to $@ , delegate all target files, $< is first prerequisite condition
.c.s:
    $(CC) $(CFLAGS) -nostdinc -Iinclude -S -o $*.s $<
.s.o:
    $(AS) -c -o $*.o $<
.c.o:
    $(CC) $(CFLAGS) -nostdinc -Iinclude -c -o $*.o $<


all: Image  
# this is target bootImage used to guide OS

# Image requirs four file as below, the next rows is the command which is executed
# > is the redirection symbol
Image: boot/bootsect boot/setup tools/system tools/build 
    tools/build boot/bootsect boot/setup tools/system $(ROOT_DEV) > Image
    sync
# target disk require Image, dd is UNIX standard command, copy a file and convert and format 
# according to selection
# bs:bytes read/write once, if= represent input file, of= represent output file
disk: Image
    dd b = 8192 if=Image of=/dev/PS0

tools/build: tools/build.c   
    $(CC) $(CFLAGS) -o tools/build tools/build.c

boot/head.o: boot/head.s  
# use .s.o rule to generate head.o

tools/system: boot/head.o init/main.o $(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
    $(LD) $(LDFLAGS) boot/head.o init/main.o \
    $(ARCHIVES) $(DRIVERS) $(MATH) $(LIB)\
    -o tools/system > System.map

kernel/math/math.a:
    (cd kernel/math; make)   # enter dir kernel/math ,run make
kernel/blk_drv/blk_drv.a:
    (cd kernel/blk_drv; make)
kernel/chr_drv/chr_drv.a:
    (cd kernel/chr_drv; make)
kernel /kernel.o:
    (cd kernel; make)
mm/mm.o:
    (cd  mm; make)
fs/fs.o:
    (cd fs; make)
lib/lib.a:
    (cd lib; make)

boot/setup: boot/setup.s
    $(AS86) -o boot/setup.o boot/setup.s
    $(LD86) -s -o boot/setup boot/setup.o #-s indicate remove symbols in target file

boot/bootsect: boot/bootsect.s
    $(AS86) -o boot/bootsect.o boot/bootsect.s
    $(LD86) -s -o boot/bootect boot/bootsect.o

# below command add a row of system size information in bootsect.s
# first generate aa tmp.s, then add bootsect.s after it
# obtain system size: command ls to show system module, use grep command acquire size information, save to tmp.s
tmp.s: boot/bootect.s tools/system
    (echo -n "SYSSIZE = (";ls -l tool/system | grep system | cut -c25-31 | tr '\012' ' '; \
                         echo " + 15) / 16") > tmp.s
    cat boot/bootsect.s >> tmp.s  #add last, not overlap

clean: 
    rm -f Image  System.map tmp_make core boot/bootsect boot/setup
    rm -f init/*.o tool/system tools/build boot/*.o
    (cd mm; make clean)
    (cd fs; make clean)
    (cd kernel; make clean)
    (cd lib; make clean)

backup: clean
    (cd ..; tar cf - linux | compres - > backup.Z) # clean ,then compress linux/ and generate backup.Z
    sync      # update
 
# generate prerequires between files, let make to ensure whether neet rebuild
# delete rows below ### Dependencies, generate a tmp_make
# then execute cpp  pre process, -M indicate output rules observe make
dep:
    sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
    (for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
    cp tmp_make Makefile
    (cd fs; make dep)
    (cd kernel; make dep)
    (cd mm; make dep)

### Dependencies:
init/main.o: init/main.c include/unistd.h include/sys/stat.h include/sys/types.h include/sys/times.h\
    include/sys/utsname.h include/utime.h/ include/time.h include/linux/tty.h include/temios.h\
    include/linux/sched.h include/linux/head.h include/linux/fs.h include/linux/mm.h include/signal.h\
    include/asm/system.h include/asm/io.h include/stddef.h include/stdarg.h include/fcntl.h
    
