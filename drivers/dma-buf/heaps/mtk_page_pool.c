// SPDX-License-Identifier: GPL-2.0
/*
 * DMA BUF page pool system
 *
 * Copyright (C) 2020 Linaro Ltd.
 *
 * Based on the ION page pool code
 * Copyright (C) 2011 Google, Inc.
 */

#include <linux/freezer.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/sched/signal.h>
#include "mtk_page_pool.h"

#ifdef CONFIG_DMABUF_HEAPS_VIVO_OPT
#include "vivo/vivo_system_heap_opt.h"
#endif

#include <linux/sched/clock.h>
#include <linux/trace.h>
/* Instantiate tracepoints */
#define CREATE_TRACE_POINTS
#include "vivo/vivo_page_alloc_trace.h"

static LIST_HEAD(pool_list);
static DEFINE_MUTEX(pool_list_lock);

static inline
struct page *mtk_page_pool_alloc_pages(struct mtk_page_pool *pool)
{
	if (fatal_signal_pending(current))
		return NULL;

	return alloc_pages(pool->gfp_mask, pool->order);
}

static inline void mtk_page_pool_free_pages(struct mtk_page_pool *pool,
					       struct page *page)
{
	__free_pages(page, pool->order);
}

static void mtk_page_pool_add(struct mtk_page_pool *pool, struct page *page)
{
	int index;

	if (PageHighMem(page))
		index = MTK_POOL_HIGHPAGE;
	else
		index = MTK_POOL_LOWPAGE;

	mutex_lock(&pool->mutex);
	list_add_tail(&page->lru, &pool->items[index]);
	pool->count[index]++;
	mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
			    1 << pool->order);
#ifdef CONFIG_DMABUF_HEAPS_VIVO_OPT
	count_dmabuf_free_pages(1 << pool->order);
#endif
	mutex_unlock(&pool->mutex);
}

static struct page *mtk_page_pool_remove(struct mtk_page_pool *pool, int index)
{
	struct page *page;

	mutex_lock(&pool->mutex);
	page = list_first_entry_or_null(&pool->items[index], struct page, lru);
	if (page) {
		pool->count[index]--;
		list_del(&page->lru);
		mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
				    -(1 << pool->order));
#ifdef CONFIG_DMABUF_HEAPS_VIVO_OPT
		count_dmabuf_free_pages(-(1 << pool->order));
#endif
	}
	mutex_unlock(&pool->mutex);

	return page;
}

static struct page *mtk_page_pool_fetch(struct mtk_page_pool *pool)
{
	struct page *page = NULL;

	page = mtk_page_pool_remove(pool, MTK_POOL_HIGHPAGE);
	if (!page)
		page = mtk_page_pool_remove(pool, MTK_POOL_LOWPAGE);

	return page;
}

struct page *mtk_page_pool_alloc(struct mtk_page_pool *pool)
{
	struct page *page = NULL;
	u64 c1, c2;
	u64 exetime = current->se.sum_exec_runtime;

	if (WARN_ON(!pool))
		return NULL;

	page = mtk_page_pool_fetch(pool);

	if (!page) {
		c1 = local_clock();
		page = mtk_page_pool_alloc_pages(pool);
		c2 = local_clock();
		trace_system_heap_alloc_pages(pool->order,
				global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10),
				(c2 - c1) / 1000, (current->se.sum_exec_runtime - exetime) / 1000);
	}
	return page;
}
EXPORT_SYMBOL_GPL(mtk_page_pool_alloc);

void mtk_page_pool_free(struct mtk_page_pool *pool, struct page *page)
{
	if (WARN_ON(pool->order != compound_order(page)))
		return;

	mtk_page_pool_add(pool, page);
}
EXPORT_SYMBOL_GPL(mtk_page_pool_free);

static int mtk_page_pool_total(struct mtk_page_pool *pool, bool high)
{
	int count = pool->count[MTK_POOL_LOWPAGE];

	if (high)
		count += pool->count[MTK_POOL_HIGHPAGE];

	return count << pool->order;
}

struct mtk_page_pool *mtk_page_pool_create(gfp_t gfp_mask, unsigned int order)
{
	struct mtk_page_pool *pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	int i;

	if (!pool)
		return NULL;

	for (i = 0; i < MTK_POOL_TYPE_SIZE; i++) {
		pool->count[i] = 0;
		INIT_LIST_HEAD(&pool->items[i]);
	}
	pool->gfp_mask = gfp_mask | __GFP_COMP;
	pool->order = order;
	mutex_init(&pool->mutex);

	mutex_lock(&pool_list_lock);
	list_add(&pool->list, &pool_list);
	mutex_unlock(&pool_list_lock);

	return pool;
}
EXPORT_SYMBOL_GPL(mtk_page_pool_create);

void mtk_page_pool_destroy(struct mtk_page_pool *pool)
{
	struct page *page;
	int i;

	/* Remove us from the pool list */
	mutex_lock(&pool_list_lock);
	list_del(&pool->list);
	mutex_unlock(&pool_list_lock);

	/* Free any remaining pages in the pool */
	for (i = 0; i < MTK_POOL_TYPE_SIZE; i++) {
		while ((page = mtk_page_pool_remove(pool, i)))
			mtk_page_pool_free_pages(pool, page);
	}

	kfree(pool);
}
EXPORT_SYMBOL_GPL(mtk_page_pool_destroy);

static int mtk_page_pool_do_shrink(struct mtk_page_pool *pool, gfp_t gfp_mask,
				      int nr_to_scan)
{
	int freed = 0;
	bool high;

	if (current_is_kswapd())
		high = true;
	else
		high = !!(gfp_mask & __GFP_HIGHMEM);

	if (nr_to_scan == 0)
		return mtk_page_pool_total(pool, high);

#ifdef CONFIG_DMABUF_HEAPS_VIVO_OPT
	nr_to_scan = mtk_page_pool_shrink_count_adjust(nr_to_scan);
	if (!nr_to_scan)
		return 0;
#endif

	while (freed < nr_to_scan) {
		struct page *page;

		/* Try to free low pages first */
		page = mtk_page_pool_remove(pool, MTK_POOL_LOWPAGE);
		if (!page)
			page = mtk_page_pool_remove(pool, MTK_POOL_HIGHPAGE);

		if (!page)
			break;

		mtk_page_pool_free_pages(pool, page);
		freed += (1 << pool->order);
	}

	return freed;
}

static int mtk_page_pool_shrink(gfp_t gfp_mask, int nr_to_scan)
{
	struct mtk_page_pool *pool;
	int nr_total = 0;
	int nr_freed;
	int only_scan = 0;

	if (!nr_to_scan)
		only_scan = 1;

	mutex_lock(&pool_list_lock);
	list_for_each_entry(pool, &pool_list, list) {
		if (only_scan) {
			nr_total += mtk_page_pool_do_shrink(pool,
							       gfp_mask,
							       nr_to_scan);
		} else {
			nr_freed = mtk_page_pool_do_shrink(pool,
							      gfp_mask,
							      nr_to_scan);
			nr_to_scan -= nr_freed;
			nr_total += nr_freed;
			if (nr_to_scan <= 0)
				break;
		}
	}
	mutex_unlock(&pool_list_lock);

#ifdef CONFIG_DMABUF_HEAPS_VIVO_OPT
	if (only_scan)
		nr_total = mtk_page_pool_shrink_count_adjust(nr_total);
#endif

	return nr_total;
}

static unsigned long mtk_page_pool_shrink_count(struct shrinker *shrinker,
						   struct shrink_control *sc)
{
	return mtk_page_pool_shrink(sc->gfp_mask, 0);
}

static unsigned long mtk_page_pool_shrink_scan(struct shrinker *shrinker,
						  struct shrink_control *sc)
{
	if (sc->nr_to_scan == 0)
		return 0;
	return mtk_page_pool_shrink(sc->gfp_mask, sc->nr_to_scan);
}

struct shrinker pool_shrinker = {
	.count_objects = mtk_page_pool_shrink_count,
	.scan_objects = mtk_page_pool_shrink_scan,
	.seeks = DEFAULT_SEEKS,
	.batch = 0,
};

int mtk_page_pool_init_shrinker(void)
{
	return register_shrinker(&pool_shrinker);
}
