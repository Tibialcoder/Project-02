#pragma once
#include <stdint.h>
#include <stdbool.h>

// Tamanho padrão da MEM1 do GameCube (24 MB)
#define GC_RAM_SIZE (24 * 1024 * 1024)
#define GC_RAM_MASK 0x01FFFFFF

// Contexto dos registradores do processador Gekko (PowerPC 750CXe)
typedef struct {
    uint32_t r[32];      // Registradores de uso geral
    double   f[32];      // Registradores de ponto flutuante (64-bit / Paired Singles)
    uint32_t lr;         // Link Register
    uint32_t ctr;        // Count Register
    uint32_t cr;         // Condition Register
    uint32_t xer;        // Fixed-Point Exception Register
    uint32_t gqr[8];     // Graphics Quantization Registers (usados para vértices)
} PPCContext;

// Ponteiro global para a RAM virtual alocada no Android
extern uint8_t* g_gc_ram;

// Macros de acesso à memória traduzindo Endereço Virtual GC (0x80000000+) -> RAM Android
// O __builtin_bswap é compilado para uma única instrução REV no ARM64 (custo zero!)
static inline uint32_t mem_read_u32(uint32_t addr) {
    uint32_t offset = addr & GC_RAM_MASK;
    uint32_t val = *(uint32_t*)(g_gc_ram + offset);
    return __builtin_bswap32(val);
}

static inline void mem_write_u32(uint32_t addr, uint32_t val) {
    uint32_t offset = addr & GC_RAM_MASK;
    *(uint32_t*)(g_gc_ram + offset) = __builtin_bswap32(val);
}

static inline uint16_t mem_read_u16(uint32_t addr) {
    uint32_t offset = addr & GC_RAM_MASK;
    uint16_t val = *(uint16_t*)(g_gc_ram + offset);
    return __builtin_bswap16(val);
}

static inline void mem_write_u16(uint32_t addr, uint16_t val) {
    uint32_t offset = addr & GC_RAM_MASK;
    *(uint16_t*)(g_gc_ram + offset) = __builtin_bswap16(val);
}

static inline uint8_t mem_read_u8(uint32_t addr) {
    return g_gc_ram[addr & GC_RAM_MASK];
}

static inline void mem_write_u8(uint32_t addr, uint8_t val) {
    g_gc_ram[addr & GC_RAM_MASK] = val;
}