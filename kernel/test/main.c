/**
 * @version		$Id$
 * @author		Nils Asmussen <nils@script-solution.de>
 * @copyright	2008 Nils Asmussen
 */

#include "../h/common.h"
#include "../h/video.h"
#include "../h/multiboot.h"
#include "../h/gdt.h"
#include "../h/mm.h"
#include "../h/util.h"
#include "../h/paging.h"
#include "../h/proc.h"
#include "../h/intrpt.h"
#include "../h/debug.h"
#include "../h/cpu.h"
#include "../h/elf.h"
#include "../h/kheap.h"

#include "test.h"
#include "kheap.h"
#include "paging.h"
#include "proc.h"
#include "mm.h"

/* TODO just temporary! */
u32 entryPoint;
bool procsReady = false;

u32 main(tMultiBoot *mbp,u32 magic) {
	/* the first thing we've to do is set up the page-dir and page-table for the kernel and so on
	 * and "correct" the GDT */
	paging_init();
	gdt_init();
	mboot_init(mbp);

	/* init video */
	vid_init();

	vid_printf("GDT exchanged, paging enabled, video initialized");
	vid_toLineEnd(vid_getswidth("DONE"));
	vid_printf("%:02s\n","DONE");

	printMultiBootInfo();

	/* mm && kheap */
	dbg_startTimer();
	vid_printf("Initializing memory-management...");
	mm_init();
	kheap_init();
	vid_toLineEnd(vid_getswidth("DONE"));
	vid_printf("%:02s","DONE");
	dbg_stopTimer();

	vid_printf("Free frames=%d, pages mapped=%d\n",mm_getNumberOfFreeFrames(MM_DMA | MM_DEF),
			paging_getPageCount());

	/* processes */
	dbg_startTimer();
	vid_printf("Initializing process-management...");
	proc_init();
	vid_toLineEnd(vid_getswidth("DONE"));
	vid_printf("%:02s","DONE");
	dbg_stopTimer();

	/* idt */
	dbg_startTimer();
	vid_printf("Initializing IDT...");
	intrpt_init();
	vid_toLineEnd(vid_getswidth("DONE"));
	vid_printf("%:02s","DONE");
	dbg_stopTimer();


	/* start tests */
	test_register(&tModMM);
	test_register(&tModPaging);
	test_register(&tModProc);
	test_register(&tModKHeap);
	test_start();


	/* stay here */
	while(1);

	return 0;
}
