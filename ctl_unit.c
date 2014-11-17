#include <stdbool.h>	// bool
#include <stdint.h>	// integer types
#include <stdlib.h>	// NULL

#include "memory.h"	// mem_[read|write][8|16]
#include "params.h"	// system_types
#include "print.h"	// fatal


#define WAIT_CYCLE(cycles, handler)  handler

/*! actual loaded ROM data */
extern unsigned char *data;

/*! registers */
uint16_t af, bc, de, hl, sp, pc;

char *a = ((char *)&af) + 1;
char *f = (char *)&af;
char *b = ((char *)&bc) + 1;
char *c = (char *)&bc;
char *d = ((char *)&de) + 1;
char *e = (char *)&de;
char *h = ((char *)&hl) + 1;
char *l = (char *)&hl;

char flag_reg;

bool toggle_int_on_next = false;
bool interrupts = true;

/*! Zero Flag */
#define FLAG_Z 0x80
/*! Subtract Flag */
#define FLAG_N 0x40
/*! Half-Carry Flag */
#define FLAG_H 0x20
/*! Carry Flag */
#define FLAG_C 0x10

/*!
 * @brief NOP (0x00)
 * @result Nothing.
 */
void nop(void)
{
	pc++;
}

/*!
 * @brief JR n (0x18)
 * @result add n to pc
 */
void jr_imm8(void)
{
	unsigned char to_add = mem_read8(pc + 1);

	pc += to_add;
}

/*!
 * @brief JR NZ,n (0x20)
 * @result add n to pc if Z (zero) flag clear
 */
void jr_nz_imm8(void)
{
	unsigned char to_add = mem_read8(pc + 1);

	pc += (flag_reg & FLAG_Z) ? 2 : to_add;
}

/*!
 * @brief JR Z,n (0x28)
 * @result add n to pc if Z (zero) flag set
 */
void jr_z_imm8(void)
{
	unsigned char to_add = mem_read8(pc + 1);

	pc += (flag_reg & FLAG_Z) ? to_add : 2;
}

/*!
 * @brief LD A,n (0x3E)
 * @result A = n
 */
void ld_a_imm8(void)
{
	*a = mem_read8(++pc);
	pc++;
}

/*!
 * @brief LD B,A (0x47)
 * @result B = A
 */
void ld_b_a(void)
{
	*b = *a;
	pc++;
}

/*!
 * @brief LD C,A (0x4F)
 * @result C = A
 */
void ld_c_a(void)
{
	*c = *a;
	pc++;
}

/*!
 * @brief LD D,A (0x57)
 * @result D = A
 */
void ld_d_a(void)
{
	*d = *a;
	pc++;
}

/*!
 * @brief LD E,A (0x5F)
 * @result E = A
 */
void ld_e_a(void)
{
	*e = *a;
	pc++;
}

/*!
 * @brief LD H,A (0x67)
 * @result H = A
 */
void ld_h_a(void)
{
	*h = *a;
	pc++;
}

/*!
 * @brief LD L,A (0x6F)
 * @result L = A
 */
void ld_l_a(void)
{
	*l = *a;
	pc++;
}

void xor_common(char to_xor)
{
	*a ^= to_xor;

	flag_reg = 0;
	if(*a == 0) flag_reg |= FLAG_Z;

	pc++;
}

/*!
 * @brief XOR B (0xA8)
 * @result A ^= B; Z flag set if A is now zero
 */
void xor_b(void)
{
	xor_common(*b);
}

/*!
 * @brief XOR C (0xA9)
 * @result A ^= C; Z flag set if A is now zero
 */
void xor_c(void)
{
	xor_common(*c);
}

/*!
 * @brief XOR D (0xAA)
 * @result A ^= D; Z flag set if A is now zero
 */
void xor_d(void)
{
	xor_common(*d);
}

/*!
 * @brief XOR E (0xAB)
 * @result A ^= E; Z flag set if A is now zero
 */
void xor_e(void)
{
	xor_common(*e);
}

/*!
 * @brief XOR H (0xAC)
 * @result A ^= H; Z flag set if A is now zero
 */
void xor_h(void)
{
	xor_common(*h);
}

/*!
 * @brief XOR L (0xAD)
 * @result A ^= H; Z flag set if A is now zero
 */
void xor_l(void)
{
	xor_common(*l);
}

/*!
 * @brief XOR A (0xAF)
 * @result A = 0; Z flag set
 */
void xor_a(void)
{
	*a = 0;
	flag_reg = FLAG_Z;
	pc++;
}

/*!
 * @brief JP nn (0xC3)
 * @result pc is set to 16-bit immediate value (LSB, MSB)
 */
void jp_imm16(void)
{
	char lsb = mem_read8(++pc);
	char msb = mem_read8(++pc);

	pc = (msb<<8 | lsb);
}

/*!
 * @brief CB ..
 * @note this is just a dispatch function for SWAP/BIT/etc
 */
void cb_dispatch(void)
{
	char opcode = mem_read8(++pc);

	pc++;
}

/*!
 * @brief CALL nn (0xCD)
 * @result next pc stored in stack; jump to nn
 */
void call_imm16(void)
{
	char lsb = mem_read8(++pc);
	char msb = mem_read8(++pc);

	sp -= 2;
	mem_write16(sp, ++pc);

	pc = (msb<<8 | lsb);
}

/*!
 * @brief LDH n,A (0xE0) - write A to 0xff00+n
 * @result the I/O register n will contain the value of A
 */
void ldh_imm8_a(void)
{
	uint16_t write = mem_read8(++pc);
	write += 0xFF00;

	mem_write8(write, *a);

	pc++;
}

/*!
 * @brief LD (nn),A (0xEA) - write A to *nn
 * @result the memory at address nn will contain the value of A
 */
void ld_d16_a(void)
{
	char lsb = mem_read8(++pc);
	char msb = mem_read8(++pc);

	uint16_t loc = (msb<<8) | lsb;

	mem_write8(loc, *a);

	pc++;
}

/*!
 * @brief LDH A,nn (0xF0) - read 0xff00+n to A
 * @result A will contain the value of the I/O register n
 */
void ldh_a_imm8(void)
{
	unsigned char loc = mem_read8(++pc);
	*a = mem_read8(0xFF00 + loc);

	pc++;
}

/*!
 * @brief DI (0xF3) - disable interrupts
 * @result interrupts will be disabled the instruction AFTER this one
 */
void di(void)
{
	toggle_int_on_next = true;

	pc++;
}

/*!
 * @brief EI (0xFB) - enable interrupts
 * @result interrupts will be enabled the instruction AFTER this one
 */
void ei(void)
{
	toggle_int_on_next = true;

	pc++;
}

/*!
 * @brief CP n (0xFE) - compare A with 8-bit immediate value
 * @result flags register modified based on result
 */
void cp_imm8(void)
{
	char cmp;

	cmp = mem_read8(++pc);
	debug("flags = %s%s%s%s; cmp = %d; A = %d",
	       (flag_reg & FLAG_Z) ? "Z":"z",
	       (flag_reg & FLAG_N) ? "N":"n",
	       (flag_reg & FLAG_H) ? "H":"h",
	       (flag_reg & FLAG_C) ? "C":"c", cmp, *a);
	flag_reg |= FLAG_N;
	flag_reg &= ~FLAG_H | ~FLAG_C;
	if(*a == cmp)
	{
		flag_reg |= FLAG_Z;
	} else {
		flag_reg &= ~FLAG_Z;
		if(*a < cmp)
		{
			flag_reg |= FLAG_C;
		} else {
			flag_reg |= FLAG_H;
		}
	}
	debug("flags = %s%s%s%s",
	       (flag_reg & FLAG_Z) ? "Z":"z",
	       (flag_reg & FLAG_N) ? "N":"n",
	       (flag_reg & FLAG_H) ? "H":"h",
	       (flag_reg & FLAG_C) ? "C":"c");

	pc++;
}

typedef void (*opcode_t)(void);

opcode_t handlers[0x100] = {
	/* 0x00 */ nop, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x08 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x10 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x18 */ jr_imm8, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x20 */ jr_nz_imm8, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x28 */ jr_z_imm8, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x30 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x38 */ NULL, NULL, NULL, NULL, NULL, NULL, ld_a_imm8, NULL,
	/* 0x40 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, ld_b_a,
	/* 0x48 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, ld_c_a,
	/* 0x50 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, ld_d_a,
	/* 0x58 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, ld_e_a,
	/* 0x60 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, ld_h_a,
	/* 0x68 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, ld_l_a,
	/* 0x70 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x78 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x80 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x88 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x90 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x98 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0xA0 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0xA8 */ xor_b, xor_c, xor_d, xor_e, xor_h, xor_l, NULL, xor_a,
	/* 0xB0 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0xB8 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0xC0 */ NULL, NULL, NULL, jp_imm16, NULL, NULL, NULL, NULL,
	/* 0xC8 */ NULL, NULL, NULL, cb_dispatch, NULL, call_imm16, NULL, NULL,
	/* 0xD0 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0xD8 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0xE0 */ ldh_imm8_a, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0xE8 */ NULL, NULL, ld_d16_a, NULL, NULL, NULL, NULL, NULL,
	/* 0xF0 */ ldh_a_imm8, NULL, NULL, di, NULL, NULL, NULL, NULL,
	/* 0xF8 */ NULL, NULL, NULL, ei, NULL, NULL, cp_imm8, NULL
};

char cycles[0x100] = {
	/* 0x00 */ 4, 12, 8, 8, 4, 4, 8, 4,
	/* 0x08 */ 20, 8, 8, 8, 4, 4, 8, 4,
	/* 0x10 */ 4, 12, 8, 8, 4, 4, 8, 4,
	/* 0x18 */ 12, 8, 8, 8, 4, 4, 8, 4,
	/* 0x20 */ 8, 12, 8, 8, 4, 4, 8, 4,
	/* 0x28 */ 8, 8, 8, 8, 4, 4, 8, 4,
	/* 0x30 */ 8, 12, 8, 8, 12, 12, 4, 4,
	/* 0x38 */ 8, 8, 8, 8, 4, 4, 8, 4,
	/* 0x40 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x48 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x50 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x58 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x60 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x68 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x70 */ 8, 8, 8, 8, 8, 8, 4, 8,
	/* 0x78 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x80 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x88 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x90 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0x98 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0xA0 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0xA8 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0xB0 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0xB8 */ 4, 4, 4, 4, 4, 4, 8, 4,
	/* 0xC0 */ 8, 12, 12, 16, 12, 16, 8, 16,
	/* 0xC8 */ 8, 16, 12, 4, 12, 24, 8, 16,
	/* 0xD0 */ 8, 12, 12, 0000, 12, 16, 8, 16,
	/* 0xD8 */ 8, 16, 12, 0000, 12, 0000, 8, 16,
	/* 0xE0 */ 12, 12, 8, 0000, 0000, 16, 8, 16,
	/* 0xE8 */ 16, 4, 16, 0000, 0000, 0000, 8, 16,
	/* 0xF0 */ 12, 12, 8, 4, 0000, 16, 8, 16,
	/* 0xF8 */ 12, 8, 16, 4, 0000, 0000, 8, 16
};


/*! boot up */
void init_ctl(char type)
{
	pc = 0x0100;
	switch(type)
	{
	case SYSTEM_SGB:
		debug("Super Game Boy emulation");
		*a = 0x01;
		break;
	case SYSTEM_GB:
	default:
		debug("original Game Boy emulation");
		*a = 0x01;
		break;
	case SYSTEM_GBC:
		debug("Game Boy Color emulation");
		*a = 0x11;
		break;
	case SYSTEM_GBP:
		debug("Game Boy Portable emulation");
		*a = 0xFF;
		break;
	}
	*f = 0xB0;
	*b = 0x00;
	*c = 0x13;
	*d = 0x00;
	*e = 0xD8;
	*h = 0x01;
	*l = 0x4D;
	sp = 0xFFFE;
}


/*! the emulated CU for the 'z80-ish' CPU */
bool execute(void)
{
	unsigned char opcode = mem_read8(pc);
	opcode_t handler = handlers[opcode];
	bool toggle = toggle_int_on_next;

	if(handler == NULL)
	{
		fatal("invalid opcode %02X at %04X", opcode, pc);
	}

	WAIT_CYCLE(cycles[opcode], handler());

	if(toggle)
	{
		toggle_int_on_next = false;
		interrupts = !interrupts;
	}

	return true;
}