/*
 * Loader Implementation
 *
 * 2018, Operating Systems
 *
 * Ciupitu Andrei-Valentin, 332CC
 */

#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>

#include "exec_parser.h"
#include "utils.h"

static int fd;
static int page_size;
static struct sigaction old_handler;
static so_exec_t *exec;
struct page_map {
	int *pages;
	int count;
};

/*
 * Checks if a page has been mapped for a segment
 */
static int find_page(struct page_map *map, int page)
{
	int i;

	for (i = 0; i < map->count; i++)
		if (map->pages[i] == page)
			return 1;
	return 0;
}

/*
 * Handler for SIGSEGV signals that provides pages on demand.
 */
static void segv_handler(int signum, siginfo_t *info, void *context)
{
	void *p;
	uintptr_t addr;
	uintptr_t seg_start;
	int i, rc;
	unsigned int page, page_offset, page_mem;
	unsigned int mem_size, file_size;
	struct page_map *map;

	/*
	 * Old handler for any other signals
	 */
	if (signum != SIGSEGV) {
		old_handler.sa_sigaction(signum, info, context);
		return;
	}

	/*
	 * Get the address that caused the signal
	 */
	addr = (uintptr_t)info->si_addr;

	for (i = 0; i < exec->segments_no; i++) {
		seg_start = exec->segments[i].vaddr;
		map = exec->segments[i].data;
		mem_size = exec->segments[i].mem_size;
		file_size = exec->segments[i].file_size;

		/*
		 * Check if the current segment produced the page fault
		 */
		if (addr >= seg_start + mem_size || addr < seg_start)
			continue;

		/*
		 * Obtain the index of the page
		 */
		page = (addr - seg_start) / page_size;

		/*
		 * Allocate space for the page vector
		 */
		if (map->pages == NULL) {
			map->pages = calloc(mem_size / page_size + 1,
				sizeof(int));
			DIE(map->pages == NULL, "alloc error");
		}

		/*
		 * Check if the page was already mapped and use the old handler
		 */
		if (find_page(map, page)) {
			old_handler.sa_sigaction(signum, info, context);
			return;
		}

		/*
		 * Map a new memory page
		 */
		page_mem = page * page_size;
		page_offset = exec->segments[i].offset + page_mem;
		p = mmap((void *)(seg_start + page_mem), page_size, PROT_WRITE,
			MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, 0, 0);
		DIE(p == MAP_FAILED, "Error on mmap");

		/*
		 * Copy data from exec to the new page
		 */
		if (file_size > page_mem) {
			lseek(fd, page_offset, SEEK_SET);
			if (file_size < (page + 1) * page_size) {
				rc = read(fd, p, file_size - page_mem);
				DIE(rc < 0, "Read error");
			} else {
				rc = read(fd, p, page_size);
				DIE(rc < 0, "Read error");
			}
		}

		/*
		 * Add permissions to the page
		 */
		rc = mprotect(p, page_size, exec->segments[i].perm);
		DIE(rc < 0, "Error on mprotect");

		/*
		 * Add the new page to the list of mapped pages
		 */
		map->pages[map->count++] = page;
		return;
	}

	/*
	 * Address was not in any segment => use old handler
	 */
	old_handler.sa_sigaction(signum, info, context);
}

/*
 * Sets the custom handler for the SIGSEGV signal
 */
static void set_signal(void)
{
	struct sigaction handler;
	int rc;

	/*
	 * Add handler
	 */
	handler.sa_sigaction = segv_handler;

	/*
	 * Add SIGSEGV signal to the set of handled signals
	 */
	rc = sigemptyset(&handler.sa_mask);
	DIE(rc < 0, "sigemptyset error");
	rc = sigaddset(&handler.sa_mask, SIGSEGV);
	DIE(rc < 0, "sigaddset error");
	handler.sa_flags = SA_SIGINFO;

	/*
	 * Register handler and obtain the old handler(default)
	 */
	rc = sigaction(SIGSEGV, &handler, &old_handler);
	DIE(rc < 0, "sigaction error");
}

int so_init_loader(void)
{
	page_size = getpagesize();
	set_signal();
	return 0;
}

int so_execute(char *path, char *argv[])
{
	int i;

	exec = so_parse_exec(path);
	DIE(exec == NULL, "Can't parse exec");

	/*
	 * Open the file to copy the data in memory
	 */
	fd = open(path, O_RDONLY);
	DIE(fd < 0, "Can't open exec for read");

	/*
	 * Init aux structure for each segment
	 */
	for (i = 0; i < exec->segments_no; i++) {
		exec->segments[i].data = calloc(1, sizeof(struct page_map));
		DIE(exec->segments[i].data == NULL, "alloc error");
	}

	so_start_exec(exec, argv);

	return -1;
}
