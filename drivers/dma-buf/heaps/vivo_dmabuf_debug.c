#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/fdtable.h>

#include "vivo_dmabuf_debug.h"

struct proc_dir_entry *proc_entry;
struct ion_stats_list *stats_list;
EXPORT_SYMBOL(stats_list);

struct ion_stats_pid *find_pid(struct ion_stats_list *list, pid_t pid)
{
	struct ion_stats_pid *stats;

	list_for_each_entry(stats, list->head, node) {
		if (stats->pid == pid)
			return stats;
	}
	return 0;
}

void add_buffer(struct ion_stats_list *list, pid_t pid, size_t size)
{
	struct ion_stats_pid *stats;
	struct ion_stats_pid *new_node;

	spin_lock(&list->lock);
	stats = find_pid(list, pid);
	if (stats) {
		stats->num_of_buffers++;
		stats->all_buffer_size += size;
	} else {
		new_node = kzalloc(sizeof(*new_node), GFP_ATOMIC);
		if (new_node) {
			new_node->pid = pid;
			new_node->num_of_buffers = 1;
			new_node->num_of_orphaned_buffers = 0;
			new_node->all_buffer_size = size;
			list_add_tail(&new_node->node, list->head);
		}
	}
	spin_unlock(&list->lock);
} EXPORT_SYMBOL(add_buffer);

void remove_buffer(struct ion_stats_list *list, pid_t pid, size_t size)
{
	struct ion_stats_pid *stats;

	spin_lock(&list->lock);
	stats = find_pid(list, pid);
	if (stats) {
		stats->num_of_buffers--;
		if (stats->num_of_buffers == 0 || size >= stats->all_buffer_size) {
			list_del(&stats->node);
			kfree(stats);
		} else {
			stats->all_buffer_size -= size;
		}
	}
	spin_unlock(&list->lock);
} EXPORT_SYMBOL(remove_buffer);

int ion_proc_stat_show(struct seq_file *s, void *unused)
{
	size_t total_all_buffer = 0;
	size_t total_orphaned_all_buffer = 0;
	struct ion_stats_pid *stats;

	spin_lock(&stats_list->lock);

	list_for_each_entry(stats, stats_list->head, node) {
		total_all_buffer += stats->all_buffer_size;
	}

	seq_printf(s, "%zu %zu", total_all_buffer, total_orphaned_all_buffer);
	list_for_each_entry(stats, stats_list->head, node) {
		seq_printf(s, " %d %zu %zu", stats->pid, stats->all_buffer_size, 0);
	}
	seq_puts(s, "\n");

	spin_unlock(&stats_list->lock);

	return 0;
}

int ion_proc_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, ion_proc_stat_show, inode->i_private);
}

const struct proc_ops ion_proc_stat_ops = {
	.proc_open = ion_proc_stat_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

void rms_ion_init(void)
{
	// Init stats_list
	stats_list = kzalloc(sizeof(*stats_list), GFP_KERNEL);
	spin_lock_init(&stats_list->lock);
	stats_list->head = kzalloc(sizeof(*stats_list->head), GFP_KERNEL);
	INIT_LIST_HEAD(stats_list->head);

	// Init procfs nodes
	proc_entry = proc_create("meminfo_ion", 0444, NULL, &ion_proc_stat_ops);
	if (!proc_entry)
		pr_err("ion: failed to create procfs meminfo_ion node.\n");
} EXPORT_SYMBOL(rms_ion_init);