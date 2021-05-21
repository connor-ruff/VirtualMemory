/*
CSE Operating Systems Project 4: Virtual Memory
Authors:
Jorge Daboub
Connor Ruff
Mauricio Interiano
Pablo Martinez

Main program for the virtual memory project.
Make all of your modifications to this file.
You may add or rearrange any code or data as you need.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

#include "page_table.h"
#include "disk.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Keep track of what algorythm we are going to use.
char *algorithm;

// Summary of Program execution
int page_faults = 0;
int disk_reads = 0;
int disk_writes = 0;

// Defined globaly to be managed by multiple functions including page_fault_handler which i dont call directly
char *virtmem = NULL;
char *physmem = NULL;
struct disk *disk = NULL;

int npages;
int nframes;

// He also mentioned in the FAQ we should use our own pageTable
// So i just used this
typedef struct
{
	int page;
	short int bits;
} frameTableEntry;

frameTableEntry *frameTable = NULL;

// Global variables needed for fifo
int *fifo;
int head = 0;
int tail = 0;



// Helper Functions
int get_available_frame();
void remove_page(struct page_table *pt, int frameNumber);

// Fault Handlers
void random_fault_handler(struct page_table *pt, int page);
void fifo_fault_handler(struct page_table *pt, int page);
void custom_fault_handler(struct page_table *pt, int page);
//void custom_fault_handler_new(struct page_table *pt, int page);

void page_fault_handler(struct page_table *pt, int page)
{
	page_faults++;

	// Since this function is sent to the page table we will handle separate algrythms here.
	if (!strcmp(algorithm, "rand"))
	{
		random_fault_handler(pt, page);
	}
	else if (!strcmp(algorithm, "fifo"))
	{
		fifo_fault_handler(pt, page);
	}
	else if (!strcmp(algorithm, "custom"))
	{
		custom_fault_handler(pt, page);
	}
	else // Cleaner to just check here
	{
		printf("use: virtmem <npages> <nframes> <rand|fifo|custom> <alpha|beta|gamma|delta>\n");
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	if (argc != 5)
	{
		printf("use: virtmem <npages> <nframes> <rand|fifo|custom> <alpha|beta|gamma|delta>\n");
		return 1;
	}

	// Set variables based on user input
	npages = atoi(argv[1]);
	nframes = atoi(argv[2]);
	algorithm = argv[3];
	const char *program = argv[4];

	// Create disk and page table
	disk = disk_open("myvirtualdisk", npages);
	if (!disk)
	{
		fprintf(stderr, "couldn't create virtual disk: %s\n", strerror(errno));
		return 1;
	}
	struct page_table *pt = page_table_create(npages, nframes, page_fault_handler);
	if (!pt)
	{
		fprintf(stderr, "couldn't create page table: %s\n", strerror(errno));
		return 1;
	}

	// Create the frame table and check to make sure allocated correctly
	frameTable = malloc(nframes * sizeof(frameTableEntry));
	if (frameTable == NULL)
	{
		fprintf(stderr, "couldn't create frameTable: %s\n", strerror(errno));
		exit(1);
	}

	//set virtual and physical memory
	virtmem = page_table_get_virtmem(pt);
	physmem = page_table_get_physmem(pt);

	// in case we need fifo array
	// the arrray will get filled as we add,
	// we use two ints to track the head and tail
	if (!strcmp(algorithm, "fifo"))
	{
		fifo = malloc(nframes * sizeof(int));
		if (fifo == NULL)
		{
			fprintf(stderr, "couldn't create fifo array: %s\n", strerror(errno));
			exit(1);
		}
	}

	if (!strcmp(program, "alpha"))
	{
		alpha_program(virtmem, npages * PAGE_SIZE);
	}
	else if (!strcmp(program, "beta"))
	{
		beta_program(virtmem, npages * PAGE_SIZE);
	}
	else if (!strcmp(program, "gamma"))
	{
		gamma_program(virtmem, npages * PAGE_SIZE);
	}
	else if (!strcmp(program, "delta"))
	{
		delta_program(virtmem, npages * PAGE_SIZE);
	}
	else
	{
		fprintf(stderr, "unknown program: %s\n", argv[3]);
		return 1;
	}

	// Cleanup Crew
	free(frameTable);
	free(fifo);
	page_table_delete(pt);
	disk_close(disk);

	// Display Results
	printf("Page Faults: %d\n", page_faults);
	printf("Disk Reads: %d\n", disk_reads);
	printf("Disk Writes: %d\n", disk_writes);

	return 0;
}

// Check if there is a free frame
int get_available_frame()
{
	int i;
	for (i = 0; i < nframes; i++)
	{
		if (frameTable[i].bits == 0)
			return i;
	}
	return -1;
}

// Remove a given page
void remove_page(struct page_table *pt, int frameNumber)
{
	// Check if we need to write back
	if (frameTable[frameNumber].bits & PROT_WRITE)
	{
		disk_write(disk, frameTable[frameNumber].page, &physmem[frameNumber * PAGE_SIZE]);
		disk_writes++;
	}
	// Update the frame table to not written so we can use it
	page_table_set_entry(pt, frameTable[frameNumber].page, frameNumber, 0);
	frameTable[frameNumber].bits = 0;
}

void custom_fault_handler(struct page_table *pt, int page){
	int frame, bits;
	page_table_get_entry(pt, page, &frame, &bits);

	int frame_index;
	int frameToEvict = nframes -1;
	int currMaxDiff = 0;

	if (!bits){
		bits = PROT_READ;
		frame_index = get_available_frame();
		if (frame_index < 0){ // no frame available
			// implement algorithm
			// Search for physical memory location that contains the virtual address
			// that is furthest away from the desired page
			int diff = 0;
			for (int i=0; i < nframes; ++i){
				diff = abs(page - frameTable[i].page);
				if (diff > currMaxDiff){
					frameToEvict = i;
					currMaxDiff = diff;
				}
			}
			remove_page(pt, frameToEvict);
			frame_index = frameToEvict;
		} 

		disk_read(disk, page, &physmem[frame_index * PAGE_SIZE]);
		disk_reads++;
	} else if (bits & PROT_READ){ // if read already set
		bits = PROT_READ | PROT_WRITE;
		frame_index = frame;
	} else {
		printf("Error on page fault\n");
		exit(1);
	}

	// update page table
	page_table_set_entry(pt, page, frame_index, bits);

	// Update frame table used to track
	frameTable[frame_index].page = page;
	frameTable[frame_index].bits = bits;
}

void random_fault_handler(struct page_table *pt, int page)
{
	// Get current values
	int frame, bits;
	page_table_get_entry(pt, page, &frame, &bits);

	int frame_index;

	// Check if bits set for given page
	if (!bits)
	{
		bits = PROT_READ;

		frame_index = get_available_frame();

		if (frame_index < 0) // No free frame
		{
			frame_index = (int)rand() % nframes;
			remove_page(pt, frame_index);
		}
		disk_read(disk, page, &physmem[frame_index * PAGE_SIZE]);
		disk_reads++;
	}
	else if (bits & PROT_READ) // If read already set
	{
		bits = PROT_READ | PROT_WRITE;
		frame_index = frame;
	}
	else
	{
		printf("Error on page fault.\n");
		exit(1);
	}

	// Update page table
	page_table_set_entry(pt, page, frame_index, bits);

	// Update frame table used to track
	frameTable[frame_index].page = page;
	frameTable[frame_index].bits = bits;
}

void fifo_fault_handler(struct page_table *pt, int page)
{
	// Get current values
	int frame, bits;
	page_table_get_entry(pt, page, &frame, &bits);

	int frame_index;

	if (!bits)
	{
		bits = PROT_READ;

		frame_index = get_available_frame();

		if (frame_index < 0) // No free frame
		{
			frame_index = fifo[head]; // We will remove the first in head
			remove_page(pt, frame_index);
			// Since we removed a page we have to update the head to next val
			head = (head + 1) % nframes;
		}
		disk_read(disk, page, &physmem[frame_index * PAGE_SIZE]);
		disk_reads++;

		// Update tail
		fifo[tail] = frame_index;
		tail = (tail + 1) % nframes;
	}
	else if (bits & PROT_READ) // If read already set
	{
		bits = PROT_READ | PROT_WRITE;
		frame_index = frame;
	}
	else
	{
		printf("Error on page fault.\n");
		exit(1);
	}

	// Update page table
	page_table_set_entry(pt, page, frame_index, bits);

	// Update frame table used to track
	frameTable[frame_index].page = page;
	frameTable[frame_index].bits = bits;
}
