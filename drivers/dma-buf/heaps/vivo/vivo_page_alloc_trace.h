#if !defined(_TRACE_VIVO_SYSTEM_HEAP_ALLOC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VIVO_PAGE_ALLOC_TRACE_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dmaheap
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/dma-buf/heaps/vivo/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vivo_page_alloc_trace

#include <linux/tracepoint.h>

TRACE_EVENT(system_heap_alloc_pages,

	TP_PROTO(int order, unsigned long free_mem,
		unsigned long cost, unsigned long exe),

	TP_ARGS(order,
		free_mem, cost, exe),

	TP_STRUCT__entry(
		__field(	unsigned long,	order			)
		__field(	unsigned long,		free_mem		)
		__field(	unsigned long,		cost		)
		__field(	unsigned long,		exe	)
	),

	TP_fast_assign(
		__entry->order			= order;
		__entry->free_mem		= free_mem;
		__entry->cost		= cost;
		__entry->exe	= exe;
	),

	TP_printk("order=%lu free_mem=%luKB cost=%luus exe=%luus",
		__entry->order,
		__entry->free_mem,
		__entry->cost,
		__entry->exe)
);

#endif /*  _TRACE_VIVO_PAGE_ALLOC_TRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>