#if !defined(_TRACE_VIVO_SYSTEM_HEAP_CHARGE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VIVO_SYSTEM_HEAP_CHARGE_TRACE_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dmaheap
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/dma-buf/heaps/vivo/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vivo_system_heap_charge_trace

#include <linux/tracepoint.h>

TRACE_EVENT(system_heap_charge,

	TP_PROTO(unsigned long set_size, unsigned long charged_pages, unsigned long cost,
		unsigned long exe, unsigned long nr_ion, unsigned long free_ion),

	TP_ARGS(set_size, charged_pages, cost, exe, nr_ion, free_ion),

	TP_STRUCT__entry(
		__field(	unsigned long,	set_size			)
		__field(	unsigned long,	charged_pages			)
		__field(	unsigned long,		cost		)
		__field(	unsigned long,		exe		)
		__field(	unsigned long,		nr_ion	)
		__field(	unsigned long,		free_ion	)
	),

	TP_fast_assign(
		__entry->set_size			= set_size;
		__entry->charged_pages			= charged_pages;
		__entry->cost		= cost;
		__entry->exe		= exe;
		__entry->nr_ion	= nr_ion;
		__entry->free_ion	= free_ion;
	),

	TP_printk("set_size=%luMB charged_size=%luKB cost=%luus exe=%luus NR_ION=%luKB free_ion=%luKB",
		__entry->set_size,
		__entry->charged_pages,
		__entry->cost,
		__entry->exe,
		__entry->nr_ion,
		__entry->free_ion)
);

#endif /*  _TRACE_VIVO_SYSTEM_HEAP_CHARGE_TRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
