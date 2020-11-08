/*
 * linux/mm/memory.c
 */

#include <signal.h>                 // signal header file, signal const, struct and op function
#include <asm/system.h>             // set to modify decriptor/trap_gate ..
#include <linux/sched.h>            // schedule, task_struct, init task0's data
#include <linux/head.h>             // GDT and segment selector
#include <linux/kernel.h>           

//volatile tell cpp no return, avoid some warining
volatile void do_exit(long code);       // kernel/exit.c, process exit

/*
 * show memory no empty's error,and exit
 */
static inline volatile void oom(void)
{
	printk("out of memory \n\r");
	do_exit(SIGSEGV);          // SIGSEGV(11) resoure can not used
}

/*
 * the micro of update page change and fast buffer
 * cpu save latese page to fast buffer, when it been modified, then update
 * here by reload cr3 (base address of page directory)
 */
#define invalidate() \
	_asm_("movl %%eax,%%cr3"::"a"(0))  // eax = 0 is the base address


/*
 * these need to be changed with change head.s
 * linux suport not more than 16MB memory
 * if need more, change these defines
 */
#define LOW_MEM  0x100000                         // the low_start 1MB
#define PAGING_MEMORY (15*1024*1024)              // main_memory 15MB
#define PAGING_PAGES  (PAGING_MEMORY>>12)         // the page nums after paging(4Kb,>>12)
#define MAP_NR(addr)  (((addr) - LOW_MEM) >> 12)  // memory address map to page_num
#define USED  100                                 // used flag


/*
 * check given address wheather in current code segment
 * (((addr)+4095)&~4095) to get liner address 's current page's end address
 * ((((addr)+0xfff)&~0xfff) < current-> start_code + current -> end_code)
 */
#define CODE_SPACE(addr)  ((((addr)+4095)&~4095) < \
		current->start_code+current->end_code)

static long HIGH_MEMORY = 0;  //global variable, save physical memory's highest address


/*
 * copy one page ,from -> to (4K)
 */
#define copy_page(from,to) \
	_asm_("cld; rep; movsl"::"s"(from),"D"(to),"c"(1024):"cx","di","si")  // rep is repeat, 1024 times

/*
* physical map, one byte repreent one page
* every byte used to indicate the reference times of current page
* max num is 15MB, in mem_init, pre set USED(100) to can used as main memory
*/
static unsigned char mem_map [PAGING_PAGES] = {0,};

/*
 * Get physical addres of firt free page(actually is last), and mark it used.
 * if no free pages left, return 0.
 * input: %1(ax = 0) -- 0; %2(LOW_MEM)the start of map_bytes; %3(cx=PAGING_PAGES);
 * %4(edi = mem_map+PAGING_PAGES-1)
 * output: %0(ax = start address of physical page 
 *
 * %4 reg actually point to mem_map[] 's last byte,this function scan from last to forward
 * if has free page (mem_map byte == 0), then return page address
 * it not map to process's address space,use 'put_page" to map.
 * kernel and it's data don't use put_page. it has mapped(16MB)
 * the variable _res saved in eax, for quickly visited and operate.
 */
unsigned long get_free_page(void)
{
	register unsigned long _res asm("ax");

	_asm_("std ; repne ; scasb\n\t"       // set direction, al(0) cmp with di(every page contents)
		  "jne 1f\n\t"                    // if != 0,jmp to 1:(end)
		  "movb $1,1(%%edi)\n\t"          // 1 -> (1+edi),set bit location of page map to  1.
		  "sall $12,%%ecx\n\t"            // <<12, page_num * 4K = relative start address
		  "addl %2,%%ecx,%%edx\n\t"       // add start address(LOW_MEM) -> actually physical start address
		  "movl %%ecx,%%edx\n\t"          // address from ecx -> edx
		  "movl $1024,%%ecx\n\t"          // 1024 -> ecx
		  "leal 4092(%%edx),%%edi\n\t"    // 4092 + edx -> edi, page end
		  "rep; stosl\n\t"                // clear current page, (revertly,from edi -> low)
		  "movl %%edx,%%eax\n"            // get page start address
		  "1:"
     	  :"=a"(_res)
		  :""(0),"i"(LOW_MEM),"c"(PAGING_PAGES),
		  "D"(mem_map+PAGING_PAGES-1)
		  :"di","cx","dx");
	return _res;                          // return free page, if not, return 0
}


/*
 * free a page of memory at physical addre 'addr'
 * used by free_page_tables()
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) return;           // the address should bigger than 1MB
	if (addr >= HIGH_MEMORY)              // upper_bound, can not bigger than it
	{
		panic("trying to free nonexistent page");
	}
	addr -= LOW_MEM;                      
	addr >>= 12;                          // cal page_num, (addr - LOW_MEM) / 4096, start from 0
	if (mem_map[addr]--) return;          // if map byte != 0, -1, then return. map byte will be 0,free
	mem_map[addr] = 0;                    // the page is free, meet error, terminate
	panic("trying to free free pages");
}


/*
 * This functoin frees a continues block of page tables, as needed by 'exit()'
 * as does copy_page_tables(),this handles only 4Mb blocks
 * from: address, size: free byte length
 * 4K * 4K(page_table) * 1024, every page table can map 4Mb memory
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long *dir, nr;

	if (from & 0x3fffff)                   // if from address is bound of 4Mb, the low 22 bit need to be 0
		panic("free_page_tables called with wrong alignment");
	if (!from)                             // if from start from 0, free kernel, terminate
		panic("tring to free up swapper memory space");
	size = (size + 0x3fffff) >> 22;        // cal size contains page_directory nums
	                                       // if size = 4.01Mb, then size = 2.
										   // here is to save carry bit
	dir = (unsigned long *) ((from>>20) & 0xffc); // the page_directory term num is from >> 22
	                                       // because every term is 4 byte
										   // so actually address is term num << 2, from >> 20
										   // mask is 0xffc,10 bit, the last 2 bit is page_table term 

	// size is the dir num, dir is the start address
	for (; size-->0;dir++)  
	{
		if (!(1 & *dir))                   // if P = 0, indicate not used, continue
			continue;
		pg_gable = (unsigned long *) (0xfffff000 & *dir);  // get pg_table' address
		for (nr = 0; nr < 1024; nr++)              // free page_table's term,1024
		{
			if (1 & *pg_table)                     // if P, free
				free_page(0xfffff000 & *pg_table); 
			*pg_table = 0;                         // pointer to NULL
			pg_table++;
		}
		free_page(0xfffff000 & *dir);              // free the page_table's page
		*dir = 0;                                  // dir pointer to NULL
	}
	invalidate();                                  // update buffer
	return 0;
}


/*
 * here is one of the most complicated functions in mm.
 * it copies a range of linerar address by copying only the pages.
 *
 * NOTE:
 * when from=0, we are copying kernel space for the first fork().
 * then we don't want to copy a full page-directory entry, as that would lead to serious memory waste.
 * we just copy the first 160 page(640 kB).
 * we don't copy-on-write in the low 1Mb range, so the pages can be shared with the kernel.
 * this is the special case for nr = xxx
 *
 * copy page_dir terms and page_table term.
 * the original physical memory is shared by two page_table.
 * when copying, allocate new page to save new page_table,but share physical memory.
 * when one process write, then kernel distribute new page (copy-on-write)
 */
int copy_page_tables(unsigned long from, unsigned long to, long size)
{
	unsigned long *from_page_table;
	unsigned long *to_page_table;
	unsigned long this_page;
	unsigned long *from_dir, *to_dir;
	unsigned long nr;

	if ((from & 0x3fffff) || (to&0x3ffff))            // validate address,the low 22 bit need be 0
		panic("copy_page_tables called with wrong alilgnment");
	from_dir = (unsigned long *) ((from >> 20) & 0xffc); // get dir, 
	to_dir = (unsigned long *) ((to >> 20) & 0xffc);
	size = ((unsigned) (size + 0x3fffff)) >> 22;      // cal size (dir term nums)
	for ( ; size-- > 0; form_dir++, to_dir++)
	{
		if (1 & *to_dir)
			panic("copy_page_table:already exist");
		if (!(1&*from_dir))                           // from dir not valid, continue
			continue;

		from_page_table = (unsigned long *) (0xfffff000 & from_dir); // page_table address
		if (!(to_page_table = (unisgned long *)get_free_page()))     // allocate free page
			return -1;
		*to_dir = ((unsigned long) to_page_table) | 7;      // set last 3 bits,(Usr, R/W,Present)
		                                                    // if U/S = 0, R/W no use.
															// if U/S = 1, R/W = 0, read only
		nr = (from == 0)?0xA0:1024;                         // set page nums needed to copy
		                                                    // if kernel, nr = 160,640KB
		for (; nr-- >0; form_page_table++,to_page_table++)  
		{
			this_page = *from_page_table;
			if (!(1&this_page))                          // not present
				continue;
			this_page &= ~2;                             // clear bit 1, read only, copy to to_page_table
			*to_page_table = this_page;
			// when the task0 call fork create task1(init), current page is in kernel, not execute this
			// when the process call fork who in main_memory(>1MB), then execute.
			// this case will occur when process call execve(),and load new code.
			if (this_page > LOW_MEM)                    // if page > 1MB, set mem_map
			{
				*from_page_table = this_page;           // set from_page_table read only, 
				                                        // if one process write, copy-on-write ues write protectoin exception
				this_page -= LOW_MEM;                  
				this_page >>= 12;                       // cal page num
				mem_map[this_page]++;                   // set map
			}
		}
	}
	invalidate();           // update fast buffer
	return 0;
}

/*
 * this function puts a pgae in memory at the wanted address.
 * it returns the physical address of the page gotten, 
 * return 0 if out of memory(either when trying to access page-table or page.
 * in no page handle function do_no_page() also call this function
 * no page function not need update TLB, even if P from 0 to 1,because not Present page would not buffer.
 * 'page' is the page pointer, 'address' is linear address.
 */
unsigned long put_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;

/* This uses the fact that _pg_dir = 0 */
	if (page < LOW_MEM || page >= HIGH_MEMORY)               // invalidate
		printk("Trying to put page %p at %p \n",page,addres);
	if (mem_map[(page-LOW_MEM)>>12]!=1)                      // page not present
		printk("mem_map disagress with %p at %p\n",page,address);  
	page_table = (unsigned long *) ((address>>20) & 0xffc);  // cal dir pointer  
	if ((*page_table)&1)                                     // present, get page_table address
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else
	{
		if ( !(tmp = get_free_page()))
			return 0;
		*page_table = tmp | 7;             // allocate new free page,set flag User,R/W,P
		page_table = (unsigned long *)tmp;
	}
	page_table[(address>>12) & 0x3ff] = page | 7;  // put physical page 'page' in page_table term, set flag
	return page;                           // no need for invalidate(), return page address.
}

/*
 * un_wp_page  --- Un Write Protect Page
 * cancel write protection function, used in page excetion interrupt's write protection handler
 * when process fork new subprocess, shared page, all be set read only.
 * when write, trigger write protection, if not be shared , set write and exit.
 * if be shared, allocate a new page and copy the page, used for write process only.
 * cancel shared.
 * this function is used in do_wp_page()
 * input is page_table pointer, physical address. 
 */
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;
	old_page = 0xfffff000 & *table_entry;       // get page address 
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) // not be shared
	{
		*table_entry |= 2;     // set write flag
		invalidate();          // update fast buffer
		return ;
	}
	if (!(new_page = get_free_page()))  // allocate page failed
		oom();                 // out of memory handle
	if (old_page >= LOW_MEM)   
		mem_map[MAP_NR(old_page)]--;   // calcel shared
	*table_entry = new_page | 7;       // update page address
	invalidate();                      // update buffer
	copy_page(old_page,new_page);      // copy page
}

/*
 * This routine handles present pages, when users try to write to a shared page.
 * It is done by copying the page to a new address and decrementing the shared-page counter for the old pagte
 */
void do_wp_page(unsigned long error_code, unsigned long address)
{
#if 0
	/* we can not do this yet: the estdio libraty writes to code space*/
	if (CODE_PACE(address)) 
		do_exit(SIGEGV);
#endif
/*
 * 1.offset of page_table term in page_table.
 *   ((address>>10) & 0xffc)  address>> 12 is index, term is 4 byte, so address >> 12 << 2, &0xffc.
 *   (((address >> 12) & 0x3ff) << 2)
 * 2.page_table address in page_dir
 */
	un_wp_page((unsigned long *)   // cancel write protection
			(((address>>10)& 0xffc) + (0xfffff000 & *((unsigned long *) ((address >> 20) & 0xffc)))));
}


/*
 * write page verify, if can't write, copy.
 * called in fork.c 'verify_area'
 * address is linear address
 */
void write_verify(unsigned long address)
{
	unsigned long page;
	if (!((page = *((unsigned long *) ((addres>>20) & 0xffc)))&1))  // if dir not P, return
		return ;
	page &= 0xfffff000;  // page_dir address                       
	page += ((addres>>10) & 0xffc);   // get page_table index
	if ((3 & (unsigned long *) page) == 1)  // judge bit 1 and bit 0, if R/W = 0 and P, copy-on-write
		un_wp_page((unsigned long *)page);
    return ;
}


/*
 * get a free page and map to given address
 * get_free_page only allocate a free page
 * this get a free page and map use put_page.
 */
void get_empty_page(unsigned long address)
{
	unsigned long tmp;
	if (!(tmp = get_free_page()) || !put_page(tmp,addres))  // can not get free page or can not put_page
	{
		free_page(tmp); /* 0 is Ok, ignored*/
		oom();  // out of memory
	}
}

/*
 * try_to_share() checks the page at address 'address' in the task 'p'
 * to see if it exists, and if it is clean.If so, share it with the current task.
 *
 * NOTE:
 * This assumes we have checked that p != current, and that they share the same executable.
 */

static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_pate;
	unsigned long to_page;
	unsigned long phys_addr;
	from_page = to_page = ((address>>20) & 0xffc);   // get dir address
	                                                 // here we first get logic dir index, (0-64MB) 
	from_page += ((p -> start_code >> 20) & 0xffc);  // task p 's dir term , logic term + start_code
	to_page += ((current -> start_code >> 20) & 0xffc); // current process dir term
	/* is there a page-directory at from?*/
	from = *(unsigned  long *) from_page;   // the contents of from_page dir
	if (!(from & 1))       // if not present, P = 0, return
			return 0; 
	from &= 0xfffff000;    // get page_table address
	from_page = from + ((addres>>10) & 0xffc);    // page_table term pointer
	phys_addr = *(unsigned long *) from_page;     // contents of page pointer, contain physical address and flag
	/* is the page clean and preent? */
	if ((phys_addr & 0x41) != 0x01)      // 0x41 is the dirty and Present flag          
		return 0;
	phys_addr &= 0xfffff000;     // get physical address
	if (phys_addr >= HIGH_MEMORY | phys_addr < LOW_MEM)   // validate
		return 0;
	// handle to page
	to = *(unsigned long *)to_page;     // get contents of to_page dir   
	if (!(to & 1))       // if not present
		if (to = get_free_page())       // get a new free page
			*(unsigned long *)to_page = to | 7;    // set to_page 's content, User R/W P
	    else
			oom();      // out of memory
	to &= 0xfffff000;   // get page_table address
	to_page = to + ((addres>>10) & 0xffc);      // to_page page_table term pointer
	if (1& *(unsigned long *) to_page)          // Present
		panic("try_to_share: to_page already exists");

	/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;      // read only         
	*(unsigned long *) to_page = *(unsigned long *)from_page;   // map
	invalidate();             // update fast buffer
	phys_addr -= LOW_MEM;
	phys_addr >> = 12;        // cal phys_addr's map byte (page_num)
	mem_map[phys_addr]++;     // counter ++
	return 1;
}

/*
 * share_page() tries to find a process that could hare a page with the current one.
 * Address is the addres of the wanted page relative to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be > 1 if there are other taks sharing this inode.
 *
 * When occur no_page exception, first look if other process also execute same file.
 * if has, look for these task.
 * if find, try to share. (clean, Present.  &0x41)
 * if not find, exit.
 *
 * Here use task struct's executable to judge if other process also execute same file.
 * the executable point to iNode, check the counter of iNode, i_count.
 * then cmp the executable in task struct array to find same executable.
 *
 * 'address' is current process's logical address
 */
static int share_page(unsigned long address)
{
	struct task_struct **p;
	if (!current -> executable)    // if has executable file
		return 0;
	if (current -> executable -> i_count < 2)   // i_count = 1, only this process, exit
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK; --p)  // search
	{
		if (! *p)         // idle process
			continue;
		if (current == *p)  // current process
			continue;
		if ((*p) -> executable != current -> executable)  // not equal
			continue;
		if (try_to_share(address,*p))    // try to share. if success, return. else, continue
			return 1;
	}
	return 0;
}

/*
 * handle no_page exception
 * this function will be called when no_page excepton triggered, in page.s
 * error_code and address will create automatically in no_page exception.
 * address is in CR2.
 * this function first try_share with other process which load same file.
 * or map to a physical address.
 * else, load needed page to given address.
 */
void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block, i;

	address &= 0xfffff000;    // linear page address
	tmp = address - current->start_code;   // logical address

	// if execuble empty, or address exceed length, get_empty_page ( get free page and put_page)
	// task0 and task1 is in kernel, all task that not call execve() 's executable is 0.
	// executable==0 or tmp > code + data length, need get new page to save heap or stack data.
	if (!current -> executable || tmp  >= current->end_data)  // end_data = code + data length
	{
		get_empty_page(address);
		return;
	}
	// try to share page
	if (share_page(tmp))
	{
		return;
	}
	// get a new free page
	if (!(page = get_free_page()))
		oom();      // out of memory
	/* remember that 1 block is used for header */
	// the block device save header struct in first block, so need to skip first block
	block = 1+ tmp/BLOCK_SIZE;       // cal the page's start block_num, BLOCK_SIZE = 1KB
	for (i = 0; i < 4; block++,i++)
		nr[i] = bmap(current-> executable,block);    // save logical block_num in bmap
	bread_page(page,current->executable->i_dev,nr);  // read four logical block to page.
	i = tmp + 4096 - current -> end_data;            // the size of over length   
	tmp = page +4096;                                // tmp point to page end
	while (i-- > 0)                                  // clear the end i bytes
	{
		tmp --;
		*(char *)tmp = 0;
	}
	// map the physical address that cause excetipn to given linear address, return 
	if (put_page(page,address))
		return;
	free_page(page); // free memory page.
	oom();           // out of memory.
}


/*
 * physical memory init.
 * set memory up to 1MB, a page is 4KB, use mem_map[] to map page
 * for 16 MB machine, 3840, ((16MB - 1MB)/4KB)
 * when a physical memory used, mem_map[] 's byte ++
 * free a page, -- byte
 *
 * in linux 0.11, manage 16MB phyiscal memory
 * if not set RAMDISK, start_mem is 4MB, end_mem is 16MB, total 3072 page
 * 0-1 MB is kernel space (use 0-640 kb, others for fast buffer and device memory
 * 'start_mem' is main memory start, end_mem is max physical address. 
 */
void mem_init(long start_mem, long end_mem)
{
	int i;
	HIGH_MEMORY = end_mem;  
	for (i = 0; i < PAGING_PAGES; i++)
		mem_map[i] = USED;            // all set USED(100)
	i = MAP_NR(start_mem);            // the map num of start_mem
	end_mem -= start_mem;
	end_mem >>= 12;                   // main memory total pages
	while (end_mem -> 0)
		mem_map[i++]=0;               // clear map byte
}

/*
 * calculate free pages and show
 * this is used when Linus debug
 */
void calc_mem(void)
{
	int i,j,k,free = 0;
	long *pg_tbl;
	for (i = 0; i < PAGING_PAGES; i++)
		if(!mem_map[i])
			free++;
	printk("%d pages freee (of %d) \n\r",free,PAGING_PAGES);
	for(i = 2; i < 1024; i++)
	{
		if (1&pg_dir[i])   // present
		{
			pg_tbl = (long *) (0xfffff000 & pg_dir[i]);
			for (j =k = 0; j < 1024; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] ues %d pages\n",i,k);
		}
	}
}




