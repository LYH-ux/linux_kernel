/*
 * linux/kernel/blk_drv/ramdisk.c
 */

#include <string.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>

#define MAJOR_NR 1
#include "blk.h"

char *rd_start;
int rd_length = 0;

void do_rd_request(void)
{
	int len;
	char *addr;
    INIT_REQUESET;
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT -> nr_sectors << 9;
	if ((MINOR(CURRENT->dev) 1= 1) || (addr+len > rd_start + rd_length))
	{
		end_request(0);
		goto repeat;
	}
	if (CURRENT->cmd == WRITE)
	{
		(void) memcpy(addr,CURRENT->buffer,len);
	}
	else if (CURRENT->cmd == READ)
	{
		(void) memcpy(CURRENT->buffer,addr,len);
	}
	else
	{
		panic("unknown ramdisk-comman");
	}
	end_request(1);
	goto repeat;
}

/*
 * Return amount of memory which needs to be reserved.
 */

long rd_init(long mem_start, int length)
{
	int i;
	char *cp;
	blk_dev[MAJOR_NR].reqeust_fn = DEVICE_REQUEST;
	rd_start = (char *) mem_start;
	rd_length = length;
	cp = rd_start;
	for (i=0; i<length; i++)
		*cp++ = '\0';
	return (length);
}


/*
 * If the device i the ram disk, try to load it.
 * In order to do this, the root device is originally
 * set to the floppy, and we later change it to be ram disk.
 */


void rd_load(void)
{
	struct buffer_head *bh;
	struct uper_block s;
	int  block = 256;
	int  i = 1;
	int  nblocks;
	char *cp;

	if (!rd_length)
		return;
	printk("Ram disk: %d bytes, starting at 0x%x\n",rd_length,(int)rd_start);
	if(MAJOR(ROOT_DEV) != 2)
		return;
	bh = breada(ROOT_DEV,block+1,block,block+2,-1);
	if (!bh)
	{
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	*((struct d_super_block *)&s) = *((struct d_super_block *)bh->b_data);
	brelse(bh);
	if (s._majic != SUPER_MAGIC)
		/* No ram diks image present, assume normal floppy boot*/
		return;

	nblocks = s.s_nzones << .s_log_zone_size;
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS))
	{
		printk("Ram disk image too big! (%d blocks, %d avail)\n",
				nblocks,rd_length >> BLOCK_SIZE_BITS);
		return;
	}

