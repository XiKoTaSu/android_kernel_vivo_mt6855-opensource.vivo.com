/*
 * drivers/dma-buf/heaps/vivo_system_heap_charge.c
 *
 * VIVO memory management
 *
 * <rongqianfeng@vivo.com>
 *
*/

#define pr_fmt(fmt) "dma_heap_charge: " fmt

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/sched/signal.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched/clock.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/vmstat.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/oom.h>

#include "../mtk_heap.h"
#include "../mtk_page_pool.h"
#include "vivo_system_heap_charge.h"
#include "vivo_system_heap_opt.h"

#include <linux/trace.h>
/* Instantiate tracepoints */
#define CREATE_TRACE_POINTS
#include "vivo_system_heap_charge_trace.h"

const char *const charge_mode_text[] = {
	"charge_none",
	"charge_size",
	"charge_unlock",
};

static int charge_enabled = 1;
static DEFINE_RATELIMIT_STATE(charge_mem, 5 * HZ, 2);
static DEFINE_SPINLOCK(oom_wait_block);
struct task_struct *charge_kworker;
struct charge_info *charge_info;
static struct kobject *charge_root_dir;
static int charged_memory_lock;

inline bool charge_is_enabled(void)
{
	if (charge_enabled)
		return true;
	else
		return false;
}

inline bool charged_memory_locked(void)
{
	if (charged_memory_lock)
		return true;
	else
		return false;
}

inline struct charge_info *get_charge_info(void)
{
	return charge_info;
}

/*
 * 1. charge chached&uncached and lock charged memory:
 * (1)echo size > /sys/rsc/ion_sys_heap_pools_charge
 *    mode is CHARGE
 *
 *  2. unlock charged memory:
 * echo 0 > /sys/rsc/ion_sys_heap_pools_charge
 *   mode is CHARGE_UNLOCK
 *
 *  3.read info is the last charged memory size unit MB.
 *
*/
static ssize_t sys_heap_pools_charge_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
	struct charge_info *cinfo = charge_info;

	if (!cinfo) {
		if (__ratelimit(&charge_mem))
			pr_err(" %s %d rsc_system_heap->cinfo is NULL\n",
			       __func__, __LINE__);
		return -ENOMEM;
	}

	if (cinfo->mode == CHARGE_MEMORY) {
		return sprintf(buf, "%ld\n", cinfo->charged_size_mb);
	} else {
		return sprintf(buf, "%ld\n", 0);
	}
}

static ssize_t sys_heap_pools_charge_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	int ret;
	struct charge_info cinfo;
	unsigned int cache_size, uncache_size;
	unsigned int half_memory_MB =
		(totalram_pages() >> (20 - PAGE_SHIFT)) / 2;

	if (IS_ERR(charge_kworker) || !charge_kworker || !charge_enabled) {
		if (__ratelimit(&charge_mem))
			pr_err("%s %d charge_kworker is not init, charge set is failed\n",
			       __func__, __LINE__);
		return -ENODEV;
	}

	if (!charge_info) {
		if (__ratelimit(&charge_mem))
			pr_err(" %s %d charge_info is NULL\n", __func__,
			       __LINE__);
		return -ENOMEM;
	}

	if (atomic_read(&charge_info->in_charge) &&
	    !atomic_read(&charge_info->oom_wait)) {
		if (__ratelimit(&charge_mem))
			pr_info(" %s %d in charge, please wait\n", __func__,
				__LINE__);
		return -EBUSY;
	}

	ret = sscanf(buf, "%u %u", &cache_size, &uncache_size);
	if (ret == 1) {
		cinfo.charged_size_mb = cache_size;
	} else if (ret == 2) {
		cinfo.charged_size_mb = cache_size + uncache_size;
	} else {
		pr_info(" %s unknow mode charge  %d failed \n", __func__,
			cinfo.charged_size_mb);
		return -EINVAL;
	}

	if (cinfo.charged_size_mb >= half_memory_MB) {
		pr_info("unlikely need charge %u MB is above half totalram %u MB, forbidden\n",
			cinfo.charged_size_mb, half_memory_MB);
		return -EINVAL;
	}

	if (cinfo.charged_size_mb == 0) {
		/* unlock charged memory */
		cinfo.mode = CHARGE_UNLOCK;
	} else {
		/*charge chached&uncached and lock charged memory*/
		cinfo.mode = CHARGE_MEMORY;
	}

	atomic_set(&cinfo.in_charge, 1);
	if (atomic_read(&charge_info->oom_wait)) {
		atomic_set(&cinfo.oom_wait, 1);
		spin_lock(&oom_wait_block);
		memcpy(charge_info, &cinfo, sizeof(cinfo));
		spin_unlock(&oom_wait_block);
		pr_info(" %s free_ion %llu, "
			"set charge&lock: %dMB mode: %s, in oom delay, will charge soon\n",
			__func__, get_dmabuf_free_pages(),
			cinfo.charged_size_mb, charge_mode_text[cinfo.mode]);
	} else {
		memcpy(charge_info, &cinfo, sizeof(cinfo));
		pr_info(" %s free_ion %llu, "
			"set charge&lock: %dMB mode: %s\n",
			__func__, get_dmabuf_free_pages(),
			cinfo.charged_size_mb, charge_mode_text[cinfo.mode]);
	}
	// if in oom wait, this can wakeup and check
	wake_up_process(charge_kworker);

	return count;
}

static struct kobj_attribute sys_heap_pools_charge_attr =
	__ATTR_RW(sys_heap_pools_charge);

static ssize_t charge_enable_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", charge_enabled);
}

static ssize_t charge_enable_store(struct kobject *kobj,
				   struct kobj_attribute *attr, const char *buf,
				   size_t count)
{
	int ret;
	int charge_enabled_old = charge_enabled;

	ret = kstrtoint(buf, 10, &charge_enabled);
	if (ret < 0) {
		pr_info("%s set reserve_enable_type %d failed \n", __func__,
			charge_enabled);
		charge_enabled = charge_enabled_old;
		return -EINVAL;
	}

	if (charge_enabled == 0) {
		/*disable charge need unlock charged memory*/
		charged_memory_lock = 0;
		atomic_set(&charge_info->in_charge, 0);
	}

	return count;
}

static struct kobj_attribute charge_enable_attr = __ATTR_RW(charge_enable);

static int (*rsc_chown)(struct kobject *kobj, const struct attribute *attr);

static int charge_init_sysfs(struct kobject *system_heap_root_dir)
{
	int ret;

	charge_root_dir =
		kobject_create_and_add("charge", system_heap_root_dir);

	if (!charge_root_dir) {
		pr_err("creat sysfs dir /sys/rsc/system_heap/charge failed\n");
		return -ENOMEM;
	}

	ret = sysfs_create_file(charge_root_dir,
				&sys_heap_pools_charge_attr.attr);
	if (ret) {
		pr_err("creat sysfs file /sys/rsc/system_heap/charge/sys_heap_pools_charge failed\n");
		return ret;
	}
	rsc_chown(charge_root_dir, &sys_heap_pools_charge_attr.attr);

	ret = sysfs_create_file(charge_root_dir, &charge_enable_attr.attr);
	if (ret) {
		pr_err("creat sysfs file /sys/rsc/system_heap/charge/charge_enable failed\n");
		return ret;
	}
	rsc_chown(charge_root_dir, &charge_enable_attr.attr);

	return 0;
}

static void caculate_charged_mem(struct charge_info *cinfo, bool *charge,
				 int *charged_pages, int *uncharge_pages)
{
	u64 total_pages = get_dmabuf_free_pages();
	int need_charge = cinfo->charged_size_mb << (20 - PAGE_SHIFT);

	switch (cinfo->mode) {
	case CHARGE_MEMORY:
		if (need_charge <= total_pages) {
			*uncharge_pages = total_pages - need_charge;
			*charge = false;
		} else {
			*charged_pages = need_charge - total_pages;
			*charge = true;
		}
		break;
	case CHARGE_UNLOCK:
		charged_memory_lock = 0;
		*charged_pages = 0;
		return;
	default:
		pr_info(" %s %d unknown charge mode, no charged", __func__,
			__LINE__);
		*charged_pages = 0;
		return;
	}

	/*lock the charged memory*/
	charged_memory_lock = 1;
}

int mtk_page_pool_uncharge(struct mtk_page_pool *pool, int nr_uncharge)
{
	struct page *page;
	int count = 0;

	while (count < nr_uncharge) {
		if (!pool->count[MTK_POOL_LOWPAGE])
			return count;
		page = mtk_page_pool_alloc(pool);
		if (!page)
			return count;

		__free_pages(page, pool->order);
		count += (1 << pool->order);
	}

	return count;
}

static void page_pool_uncharge_pages(int need_uncharged)
{
	int i;
	int num_orders = get_mtk_page_pool_orders_num();
	struct mtk_page_pool **pools = get_dmabuf_page_pools();
	for (i = num_orders - 1; i >= 0; i--) {
		int nr_freed = 0;

		if (i == 0 || i == 1)
			nr_freed += mtk_page_pool_uncharge(pools[i],
							   need_uncharged / 2);
		else
			nr_freed += mtk_page_pool_uncharge(pools[i],
							   need_uncharged);

		need_uncharged -= nr_freed;
		if (need_uncharged <= 0)
			break;
	}
}

static int dmabuf_charge_oom_notify(struct notifier_block *self,
				    unsigned long notused, void *nfreed)
{
	int free_ion = get_dmabuf_free_pages();
	unsigned long *nf = (unsigned long *)nfreed;

	if (!charged_memory_lock || !free_ion)
		return NOTIFY_OK;

	charged_memory_lock = 0;
	atomic_set(&charge_info->in_charge, 1); //block other charge request
	page_pool_uncharge_pages(free_ion);

	*nf += (unsigned long)free_ion;

	atomic_set(&charge_info->oom_wait, 1);
	wake_up_process(charge_kworker);

	return NOTIFY_OK;
}

int mtk_page_pool_charge(struct mtk_page_pool *pool, int nr_charge,
			 int *result)
{
	struct page *page;
	int count = 0;
	int now_free_pages;
	int total_need_pages =
		(charge_info->charged_size_mb << (20 - PAGE_SHIFT));
	*result = 0;

	while (count < nr_charge) {
		now_free_pages = get_dmabuf_free_pages();
		if (now_free_pages >= total_need_pages) {
			*result = -1;
			return count;
		}

		page = alloc_pages(pool->gfp_mask, pool->order);
		if (!page)
			return count;

		mtk_page_pool_free(pool, page);
		count += (1 << pool->order);
	}

	return count;
}

static void page_pool_charge_pages(int need_charge)
{
	u64 c1, c2;
	u64 c3, c4;
	int total_charged = 0;
	int i;
	int num_orders = get_mtk_page_pool_orders_num();
	struct mtk_page_pool **pools = get_dmabuf_page_pools();
	int result;
	u64 exetime = current->se.sum_exec_runtime;
	u64 exe = 0;

	c3 = local_clock();
	for (i = 0; i < num_orders; i++) {
		int charged = 0;

		if (need_charge <= 0)
			break;

		if (need_charge > 0) {
			c1 = local_clock();
			charged = mtk_page_pool_charge(pools[i], need_charge,
						       &result);
			c2 = local_clock();
			pr_info(" %s %d pool order %d need %d pages, charged pages %d cost %llu us",
				__func__, __LINE__, pools[i]->order,
				need_charge, charged, (c2 - c1) / 1000);
			total_charged += charged;
			need_charge -= charged;
		}

		if (result < 0) {
			pr_info("when charge, we meet free_ion by system free, no need to charge\n");
			break;
		}
	}
	c4 = local_clock();
	exe = (current->se.sum_exec_runtime - exetime) / 1000;

	trace_system_heap_charge(charge_info->charged_size_mb, total_charged << (PAGE_SHIFT - 10), (c4-c3)/1000, exe,
		get_total_dmabuf_heap_used_pages() << (PAGE_SHIFT - 10),
		get_dmabuf_free_pages() << (PAGE_SHIFT - 10));

	pr_debug(
		"%s %d NR_ION: %d, free pool %d, charged pages %d cost %llu us exe %llu us\n",
		__func__, __LINE__, get_total_dmabuf_heap_used_pages(),
		get_dmabuf_free_pages(), total_charged, (c4 - c3) / 1000, exe);
}

static struct zone *get_next_zone(struct zone *zone)
{
	struct pglist_data *pgdat = zone->zone_pgdat;

	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
		zone++;
	else
		zone = NULL;

	return zone;
}

static void dmabuf_oom_charge_delay(void)
{
	long avaliable;
	long zone_water_pages = 0;
	struct zone *zone;
	struct pglist_data *pgdat = NODE_DATA(0);
	pr_info("due to oom may trigger, charge feature need block until avaliable is recovery");

	for (zone = pgdat->node_zones; zone; zone = get_next_zone(zone)) {
		if (zone->present_pages)
			zone_water_pages += high_wmark_pages(zone);
	}

	while (true) {
		long need_charge;
		spin_lock(&oom_wait_block);

		need_charge =
			(charge_info->charged_size_mb << (20 - PAGE_SHIFT));
		spin_unlock(&oom_wait_block);
		avaliable = si_mem_available() - zone_water_pages;
		if (avaliable >= need_charge) {
			pr_info("memory level is ok, enable charge delay\n");
			break;
		}

		schedule_timeout_interruptible(HZ / 2);
	}
}

static int page_pool_charge_worker(void *data)
{
	struct cpumask mask;
	static u64 exetime;

	/* big cpu */
	cpumask_clear(&mask);
	cpumask_set_cpu(4, &mask);
	cpumask_set_cpu(5, &mask);
	cpumask_set_cpu(6, &mask);
	cpumask_set_cpu(7, &mask);
	set_cpus_allowed_ptr(current, &mask);

	for (;;) {
		int uncharge_pages = 0, charge_pages = 0;
		bool charge = true;

		if (charge_info->mode == CHARGE_NONE)
			goto sleep;

		if (atomic_read(&charge_info->oom_wait)) {
			dmabuf_oom_charge_delay();
			atomic_set(&charge_info->oom_wait, 0);
		}

		caculate_charged_mem(charge_info, &charge, &charge_pages,
				     &uncharge_pages);

		if ((charge && charge_pages == 0) ||
		    (!charge && uncharge_pages == 0))
			goto sleep;

		exetime = current->se.sum_exec_runtime;
		if (charge)
			page_pool_charge_pages(charge_pages);

		pr_info("this current exe %llu us\n",
			(current->se.sum_exec_runtime - exetime) / 1000);

	sleep:
		atomic_set(&charge_info->in_charge, 0);
		set_current_state(TASK_INTERRUPTIBLE);
		if (unlikely(kthread_should_stop())) {
			set_current_state(TASK_RUNNING);
			break;
		}
		schedule();

		set_current_state(TASK_RUNNING);
	}

	return 0;
}

struct task_struct *create_charge_kworker(void)
{
	struct sched_attr attr = { 0 };
	struct task_struct *thread;
	int ret;

	attr.sched_nice = MIN_NICE;

	thread = kthread_run(page_pool_charge_worker, NULL,
			     "sys-heap-charge-worker");
	if (IS_ERR(thread)) {
		pr_err("%s: failed to create sys-heap-charge-worker thread: %ld\n",
		       __func__, PTR_ERR(thread));
		return thread;
	}
	ret = sched_setattr(thread, &attr);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set task priority for sys-heap-charge-worker thread: ret = %d\n",
			__func__, ret);
		return ERR_PTR(ret);
	}

	return thread;
}

static struct notifier_block dmabuf_charge_oom_nb = {
	.notifier_call = dmabuf_charge_oom_notify,
};

int dmabuf_heap_charge_init(struct kobject *root_dir, unsigned long chown_addr)
{
	charge_info = kzalloc(sizeof(struct charge_info), GFP_KERNEL);
	if (!charge_info) {
		pr_err("alloc charge_info failed may low memory issue\n");
		return -ENOMEM;
	}
	atomic_set(&charge_info->in_charge, 0);
	atomic_set(&charge_info->oom_wait, 0);
	charge_kworker = create_charge_kworker();

	rsc_chown =
		(int (*)(struct kobject * kobj, const struct attribute *attr))
			chown_addr;
	charge_init_sysfs(root_dir);

	register_oom_notifier(&dmabuf_charge_oom_nb);

	return 0;
}
