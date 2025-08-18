#if !defined(_TRACE_VIVO_SYSTEM_HEAP_ALLOC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VIVO_SYSTEM_HEAP_ALLOC_TRACE_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dmaheap
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/dma-buf/heaps/vivo/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vivo_system_heap_alloc_trace

#include <linux/tracepoint.h>

TRACE_EVENT(system_heap_alloc,

	TP_PROTO(unsigned long need_size, unsigned long size_remaining,
		unsigned long cost, unsigned long exe, unsigned long nr_ion,
		unsigned long free_ion, unsigned long free_mem),

	TP_ARGS(need_size, size_remaining, cost, exe, nr_ion, free_ion, free_mem),

	TP_STRUCT__entry(
		__field(	unsigned long,	need_size			)
		__field(	unsigned long,		size_remaining		)
		__field(	unsigned long,		cost		)
		__field(	unsigned long,		exe	)
		__field(	unsigned long,		nr_ion		)
		__field(	unsigned long,		free_ion		)
		__field(	unsigned long,		free_mem	)
	),

	TP_fast_assign(
		__entry->need_size			= need_size;
		__entry->size_remaining		= size_remaining;
		__entry->cost		= cost;
		__entry->exe	= exe;
		__entry->nr_ion		= nr_ion;
		__entry->free_ion		= free_ion;
		__entry->free_mem	= free_mem;
	),

	TP_printk("need_size=%lu size_remaining=%lu cost=%luus exe=%luus "
			  "NR_ION=%luKB free_ion=%luKB free_mem=%luKB",
		__entry->need_size,
		__entry->size_remaining,
		__entry->cost,
		__entry->exe,
		__entry->nr_ion,
		__entry->free_ion,
		__entry->free_mem)
);

#endif /*  _TRACE_VIVO_SYSTEM_HEAP_ALLOC_TRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
