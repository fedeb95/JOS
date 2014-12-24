// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if( !(err & FEC_WR) ) panic("Not a write operation");
	pte_t pte = uvpt[PGNUM((uint32_t)addr)];
	if ( !(pte & PTE_COW)) panic("page Not copy on write");


	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	void *tmpva = (void *) ROUNDDOWN((uint32_t) addr, PGSIZE);
	if ((r = sys_page_alloc(0, (void *) PFTEMP, PTE_P|PTE_W|PTE_U)) < 0) panic("sys_page_alloc: error %e\n", r);
	memmove((void *)PFTEMP, tmpva, PGSIZE);
	if ((r = sys_page_map(0, (void *)PFTEMP, 0, tmpva, PTE_P|PTE_W|PTE_U)) < 0) panic("sys_page_map: error %e\n", r);	
	if ((r = sys_page_unmap(0, (void *) PFTEMP)) < 0) panic("sys_page_unmap");;
	// LAB 4: Your code here.
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	unsigned va = (pn << PGSHIFT);
	if (!(uvpt[pn] & PTE_P)) 	panic("page not present\n");
	if (pn >= PGNUM(UTOP) || va >= UTOP)	panic("page out of UTOP\n");
	if (!(uvpt[pn] & PTE_U))	panic("page must user accessible\n");

	//map pages that are neither writable nor copy on write
	if (!((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW))) {
		if ((r = sys_page_map(0, (void *) va, envid, (void *) va, PGOFF(uvpt[pn]) )) < 0) 
		panic("sys_page_map: error %e\n", r);
	}
	else {
	//map page copy-on-write in child's address space
		if ((r = sys_page_map(0, (void *) va, envid, (void *) va, (PTE_P | PTE_U | PTE_COW) & ~PTE_W)) < 0)
		panic("sys_page_map: error %e\n", r);

	//map page copy-on-write in its own address space
		if ((r = sys_page_map(0, (void *) va, 0, (void *) va, (PTE_P | PTE_U | PTE_COW) & ~PTE_W)) < 0)
		panic("sys_page_map: error %e\n", r);
	}
	// LAB 4: Your code here.
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	extern void _pgfault_upcall(void);
	set_pgfault_handler(pgfault);
	envid_t envid;
	if((envid = sys_exofork()) < 0) panic("exofork failed");
	if(envid == 0) { 
		thisenv = &envs[ENVX(sys_getenvid())];	//that is child process is running
		return envid;
	}
	int pgn, r;
	uint32_t UXSTACKBASE = UXSTACKTOP - PGSIZE;
	for(pgn = PGNUM(UTEXT); pgn < PGNUM(USTACKTOP); pgn++)    {
        	if(pgn < PGNUM(UXSTACKBASE) 
                && ( (PTE_P | PTE_U) == (uvpd[pgn >> 10] & (PTE_P | PTE_U)))
                && ((PTE_P | PTE_U) == (uvpt[pgn] & (PTE_P | PTE_U))) )
        	{       duppage(envid, pgn);        }
        }

	if ((r = sys_page_alloc(envid, (void*)(UXSTACKTOP-PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
		panic("sys_page_alloc: error %e\n", r);
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		panic("sys_env_set_pgfault_upcall: error %e\n", r);
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0) 
		panic("sys_env_set_status: error %e\n", r);

	return envid;	
	
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}