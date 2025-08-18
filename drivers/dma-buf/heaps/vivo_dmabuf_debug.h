#ifndef _VIVO_DMABUF_DEBUG_H
#define _VIVO_DMABUF_DEBUG_H

#define MAX_PID_PER_DMABUF 64

#include <linux/dma-buf.h>

/**
 * struct ion_stats_pid - ion statistics of a specified pid
 * @pid:						the pid
 * @num_of_buffers:				the number of buffers
 * @num_of_orphaned_buffers:	the number of orphaned buffers
 * @all_buffer_size:			the sum of sizes of all allocated buffers
 * @orphaned_buffer_size:		the sum of sizes of all orphaned buffers
 */
struct ion_stats_pid {
	struct list_head node;

	pid_t pid;

	u64 num_of_buffers;
	u64 num_of_orphaned_buffers;

	size_t all_buffer_size;
};

/**
 * struct ion_stats_list - a list of ion_stats_pid
 * @head:	list head
 * @lock:	list lock
 */
struct ion_stats_list {
	struct list_head *head;
	spinlock_t lock; // List lock
};

struct dmabuf_stats_pid_info {
	pid_t pid;
	size_t mmap_size;
};

/**
 * struct dmabuf_stats - record every pid that uses a piece of dmabuf
 * @buffer:			the dma-buf
 * @pid:			an array of pids, containing MAX_PID_PER_DMABUF elements
 * @pid_count:		how many pids use this dmabuf
 */
struct dmabuf_stats {
	struct list_head node;

	struct dma_buf *buffer;

	struct dmabuf_stats_pid_info **pid_infos; // Array of dmabuf_stats_pid_info, containing MAX_PID_PER_DMABUF elements
	int pid_count;

	size_t mmap_size;
};

/**
 * struct dmabuf_pid_stats - record how many pss and rss a pid uses
 */
struct dmabuf_pid_stats {
	struct list_head node;

	pid_t pid;
	size_t rss;
	size_t pss;
};

/**
 * struct process_file_data - data structure passed into process_file
 */
struct process_file_data {
	struct list_head *dmabuf_stats_list;
	pid_t pid;
};

/* Add buffer to a specified pid */
void add_buffer(struct ion_stats_list *list, pid_t pid, size_t size);

/* Remove buffer from a specified pid */
void remove_buffer(struct ion_stats_list *list, pid_t pid, size_t size);

/* Add orphaned buffer to a specified pid */
void add_orphaned_buffer(struct ion_stats_list *list, pid_t pid, size_t size);

/* Remove orphaned buffer from a specified pid */
void remove_orphaned_buffer(struct ion_stats_list *list, pid_t pid, size_t size);

/* Find specified pid in list */
struct ion_stats_pid *find_pid(struct ion_stats_list *list, pid_t pid);

/* Initialization */
extern void rms_ion_init(void);

#endif /* _VIVO_DMABUF_DEBUG_H */