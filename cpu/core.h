#ifndef CORE_H
#define CORE_H

#include "../common.h"

#ifndef PP_STRING
#define PP_STRING "COR"
#include "../pretty_print.h"
#endif

union memmap_fn {
	bool (*R_fn32)(uint32_t, uint32_t *);
	void (*W_fn32)(uint32_t, uint32_t);
	bool (*R_fn8)(uint32_t, uint8_t *);
	void (*W_fn8)(uint32_t, uint8_t);
};

void register_memmap(
		const char *name,
		bool write,
		short alignment,
		union memmap_fn mem_fn,
		uint32_t bot,
		uint32_t top
	);

// These functions you must implement in core.c 
void		reset(void);

uint32_t	read_word(uint32_t addr);
void		write_word(uint32_t addr, uint32_t val);
uint16_t	read_halfword(uint32_t addr);
void		write_halfword(uint32_t addr, uint16_t val);
uint8_t		read_byte(uint32_t addr);
void		write_byte(uint32_t addr, uint8_t val);

// Exported for gdb
bool		try_read_byte(uint32_t addr, uint8_t *val)
			__attribute__ ((nonnull));

#endif // CORE_H
