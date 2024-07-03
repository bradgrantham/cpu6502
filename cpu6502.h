/*
    Public methods:
        CPU6502(CLK& clk, BUS& bus); - construct using clk and bus
        cycle() - issue one instruction and add necessary cycles to clk
        reset() - reset CPU state
        irq() - put CPU in IRQ
        nmi() - put CPU in NMI

    CLK template parameter must provide methods:
        void add_cpu_cycles(int N); - add N CPU cycles to the clock

    BUS template parameter must provide methods:
        uint8_t read(uint16_t addr);
        void write(uint16_t addr, uint8_t data);
*/

/*
  Candidates for refactor
      effective address
      arithmetic operation including BCD
*/

#ifndef CPU6502_H
#define CPU6502_H

#include <stdlib.h>
#include <assert.h>
#include <vector>

#ifndef EMULATE_65C02
#define EMULATE_65C02 1
#endif /* EMULATE_65C02 */

template<class CLK, class BUS>
struct CPU6502
{
    CLK &clk;
    BUS &bus;

    const static int32_t cycles[256];

    static constexpr uint8_t N = 0x80;
    static constexpr uint8_t V = 0x40;
    static constexpr uint8_t B2 = 0x20;
    static constexpr uint8_t B = 0x10;
    static constexpr uint8_t D = 0x08;
    static constexpr uint8_t I = 0x04;
    static constexpr uint8_t Z = 0x02;
    static constexpr uint8_t C = 0x01;
    uint8_t a, x, y, s, p;
    uint16_t pc = 0;

    enum Exception {
        NONE,
        RESET,
        NMI,
        BRK,
        INT,
    } exception;

    std::vector<std::pair<uint16_t, uint8_t>> writes;

    // XXX For debugging, normally couldn't set CPU PC directly
    void set_pc(uint16_t addr)
    {
        pc = addr;
    }

    void stack_push(uint8_t d)
    {
        writes.push_back(std::make_pair(0x100 + s--, d));
    }

    uint8_t stack_pull()
    {
        return bus.read(0x100 + ++s);
    }

    uint8_t read_pc_inc()
    {
        return bus.read(pc++);
    }

    void flag_change(uint8_t flag, bool v)
    {
        if(v) {
            p |= flag;
        } else {
            p = (p & ~flag) | B2 | B;
        }
    }

    void flag_set(uint8_t flag)
    {
        p |= flag;
    }

    void flag_clear(uint8_t flag)
    {
        p = (p & ~flag) | B2 | B;
    }

    uint8_t carry()
    {
        return (p & C) ? 1 : 0;
    }

    bool isset(uint8_t flag)
    {
        return (p | B | B2) & flag;
    }

    void set_flags(uint8_t flags, uint8_t v)
    {
        if(flags & Z) {
            flag_change(Z, v == 0x00);
        }
        if(flags & N) {
            flag_change(N, v & 0x80);
        }
    }

    static bool sbc_overflow_d(uint8_t a, uint8_t b, uint8_t borrow)
    {
        int8_t a_ = a;
        int8_t b_ = b;
        int16_t c = a_ - (b_ + borrow);
        return (c < 0) || (c > 99);
    }

    static bool adc_overflow_d(uint8_t a, uint8_t b, uint8_t carry)
    {
        int8_t a_ = a;
        int8_t b_ = b;
        int16_t c = a_ + b_ + carry;
        return (c < 0) || (c > 99);
    }

    static bool sbc_overflow(uint8_t a, uint8_t b, uint8_t borrow)
    {
        int8_t a_ = a;
        int8_t b_ = b;
        int16_t c = a_ - (b_ + borrow);
        return (c < -128) || (c > 127);
    }

    static bool adc_overflow(uint8_t a, uint8_t b, uint8_t carry)
    {
        int8_t a_ = a;
        int8_t b_ = b;
        int16_t c = a_ + b_ + carry;
        return (c < -128) || (c > 127);
    }

    CPU6502(CLK& clk_, BUS& bus_) :
        clk(clk_),
        bus(bus_),
        a(0),
        x(0),
        y(0),
        s(0xFD),
        p(I | B | B2),
        exception(RESET)
    {
    }

    void reset()
    {
        s = 0xFD;
        uint8_t low = bus.read(0xFFFC);
        uint8_t high = bus.read(0xFFFD);
        pc = low + high * 256;
        exception = NONE;
    }

    void irq()
    {
        stack_push((pc - 1) >> 8);
        stack_push((pc - 1) & 0xFF);
        stack_push((p | B2) & ~B);
        uint8_t low = bus.read(0xFFFE);
        uint8_t high = bus.read(0xFFFF);
        pc = low + high * 256;
        exception = NONE;
    }

    void nmi()
    {
        stack_push((pc - 1) >> 8);
        stack_push((pc - 1) & 0xFF);
        stack_push((p | B2) & ~B);
        uint8_t low = bus.read(0xFFFA);
        uint8_t high = bus.read(0xFFFB);
        pc = low + high * 256;
        exception = NONE;
    }

    void adc_bcd(uint8_t m, uint8_t carry)
    {
        uint8_t bcd_a = a / 16 * 10 + a % 16;
        uint8_t bcd_m = m / 16 * 10 + m % 16;
        flag_change(C, ((uint16_t)bcd_a + (uint16_t)bcd_m + carry) > 99);
        flag_change(V, adc_overflow_d(bcd_a, bcd_m, carry));
        set_flags(N | Z, bcd_a = bcd_a + bcd_m + carry);
        a = (bcd_a % 100) / 10 * 16 + bcd_a % 10;
    }

    void sbc_bcd(uint8_t m, uint8_t borrow)
    {
        uint8_t bcd_a = a / 16 * 10 + a % 16;
        uint8_t bcd_m = m / 16 * 10 + m % 16;
        flag_change(C, !(bcd_a < bcd_m + borrow));
        flag_change(V, sbc_overflow_d(bcd_a, bcd_m, borrow));
        if(bcd_m + borrow <= bcd_a) { 
            set_flags(N | Z, bcd_a = (bcd_a - (bcd_m + borrow)) % 100);
        } else {
            set_flags(N | Z, bcd_a = (bcd_a + 100 - (bcd_m + borrow)));
        }
        a = (bcd_a % 100) / 10 * 16 + bcd_a % 10;
    }

    void branch(bool condition) 
    {
        int32_t rel = (read_pc_inc() + 128) % 256 - 128;
        if(condition) {
            clk.add_cpu_cycles(1);
            if((pc + rel) / 256 != pc / 256) {
                clk.add_cpu_cycles(1);
            }
            pc += rel;
        }
    }

    void cycle()
    {
        if(exception == RESET) {
            reset();
        } if(exception == NMI) {
            nmi();
        } if(exception == INT) {
            irq();
        }
        // BRK is a special case caused directly by an instruction

        uint8_t inst = read_pc_inc();

        uint8_t m;

        switch(inst) {
            case 0x00: { // BRK
                stack_push((pc + 1) >> 8);
                stack_push((pc + 1) & 0xFF);
                stack_push(p | B2 | B); // | B says the Synertek 6502 reference
                p |= I;
#if EMULATE_65C02
                p &= ~D;
#endif /* EMULATE_65C02 */
                uint8_t low = bus.read(0xFFFE);
                uint8_t high = bus.read(0xFFFF);
                pc = low + high * 256;
                exception = NONE;
                break;
            }

            case 0x20: { // JSR
                stack_push((pc + 1) >> 8);
                stack_push((pc + 1) & 0xFF);
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                pc = low + high * 256;
                break;
            }

            case 0xEA: { // NOP
                break;
            }

            case 0x8A: { // TXA
                set_flags(N | Z, a = x);
                break;
            }

            case 0xAA: { // TAX
                set_flags(N | Z, x = a);
                break;
            }

            case 0xBA: { // TSX
                set_flags(N | Z, x = s);
                break;
            }

            case 0x9A: { // TXS
                s = x;
                break;
            }

            case 0xA8: { // TAY
                set_flags(N | Z, y = a);
                break;
            }

            case 0x98: { // TYA
                set_flags(N | Z, a = y);
                break;
            }

            case 0x18: { // CLC
                flag_clear(C);
                break;
            }

            case 0x38: { // SEC
                flag_set(C);
                break;
            }

            case 0xF8: { // SED
                flag_set(D);
                break;
            }

            case 0xD8: { // CLD
                flag_clear(D);
                break;
            }

            case 0x58: { // CLI
                flag_clear(I);
                break;
            }

            case 0x78: { // SEI
                flag_set(I);
                break;
            }

            case 0xB8: { // CLV
                flag_clear(V);
                break;
            }

            case 0xC6: { // DEC zpg
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, m = bus.read(zpg) - 1);
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0xD6: { // DEC zpg, X
                uint8_t zpg = (read_pc_inc() + x) % 0xFF;
                set_flags(N | Z, m = bus.read(zpg) - 1);
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0xDE: { // DEC abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                set_flags(N | Z, m = bus.read(addr) - 1);
                writes.push_back(std::make_pair(addr, m));
                break;
            }

            case 0xCE: { // DEC abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, m = bus.read(addr) - 1);
                writes.push_back(std::make_pair(addr, m));
                break;
            }

            case 0xCA: { // DEX
                set_flags(N | Z, x = x - 1);
                break;
            }

            case 0xFE: { // INC abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                if((addr - x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, m = bus.read(addr) + 1);
                writes.push_back(std::make_pair(addr, m));
                break;
            }

            case 0xEE: { // INC abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, m = bus.read(addr) + 1);
                writes.push_back(std::make_pair(addr, m));
                break;
            }

            case 0xE6: { // INC zpg
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, m = bus.read(zpg) + 1);
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0xF6: { // INC zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                set_flags(N | Z, m = bus.read(zpg) + 1);
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0xE8: { // INX
                set_flags(N | Z, x = x + 1);
                break;
            }

            case 0xC8: { // INY
                set_flags(N | Z, y = y + 1);
                break;
            }

            case 0x10: { // BPL
                branch(!isset(N));
                break;
            }

            case 0x50: { // BVC
                branch(!isset(V));
                break;
            }

            case 0x70: { // BVS
                branch(isset(V));
                break;
            }

            case 0x30: { // BMI
                branch(isset(N));
                break;
            }

            case 0x90: { // BCC
                branch(!isset(C));
                break;
            }

            case 0xB0: { // BCS
                branch(isset(C));
                break;
            }

            case 0xD0: { // BNE
                branch(!isset(Z));
                break;
            }

            case 0xF0: { // BEQ
                branch(isset(Z));
                break;
            }

            case 0xA1: { // LDA (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xB5: { // LDA zpg, X
                uint8_t zpg = read_pc_inc();
                uint16_t addr = zpg + x;
                set_flags(N | Z, a = bus.read(addr & 0xFF));
                break;
            }

            case 0xB1: { // LDA ind, Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xA5: { // LDA zpg
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, a = bus.read(zpg));
                break;
            }

            case 0xDD: { // CMP abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC1: { // CMP (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xD9: { // CMP abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + y;
                m = bus.read(addr);
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xB9: { // LDA abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + y;
                set_flags(N | Z, a = bus.read(addr));
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                break;
            }

            case 0xBC: { // LDY abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                set_flags(N | Z, y = bus.read(addr));
                if((addr - x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                break;
            }

            case 0xBD: { // LDA abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                set_flags(N | Z, a = bus.read(addr));
                if((addr - x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                break;
            }

            case 0xF5: { // SBC zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    sbc_bcd(m, borrow);
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xE5: { // SBC zpg
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    sbc_bcd(m, borrow);
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xE1: { // SBC ind, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    sbc_bcd(m, borrow);
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xF1: { // SBC ind, Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    sbc_bcd(m, borrow);
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xF9: { // SBC abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                uint8_t m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    sbc_bcd(m, borrow);
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xFD: { // SBC abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                if((addr - x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                uint8_t m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    sbc_bcd(m, borrow);
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xED: { // SBC abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                uint8_t m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    sbc_bcd(m, borrow);
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xE9: { // SBC imm
                uint8_t m = read_pc_inc();
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    sbc_bcd(m, borrow);
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0x71: { // ADC (ind), Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x61: { // ADC (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x6D: { // ADC abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x65: { // ADC
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x7D: { // ADC abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                if((addr - x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x79: { // ADC abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x69: { // ADC
                m = read_pc_inc();
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x0E: { // ASL abs
                uint16_t addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                writes.push_back(std::make_pair(addr, m));
                break;
            }

            case 0x1E: { // ASL abs, X
                uint16_t addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + x);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
#if EMULATE_65C02
                if((addr + x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
#endif
                writes.push_back(std::make_pair(addr + x, m));
                break;
            }

            case 0x06: { // ASL
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0x16: { // ASL
                uint8_t zpg = read_pc_inc();
                m = bus.read((zpg + x) & 0xFF);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                bus.write((zpg + x) & 0xFF, m);
                break;
            }

            case 0x0A: { // ASL
                flag_change(C, a & 0x80);
                set_flags(N | Z, a = a << 1);
                break;
            }

            case 0x5E: { // LSR abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                if((m + 1) / 256 != m / 256) {
                    clk.add_cpu_cycles(1);
                }
                writes.push_back(std::make_pair(addr + x, m));
                break;
            }

            case 0x46: { // LSR
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0x56: { // LSR zpg, X
                uint8_t zpg = read_pc_inc() + x;
                m = bus.read(zpg & 0xFF);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0x4E: { // LSR
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                writes.push_back(std::make_pair(addr, m));
                break;
            }

            case 0x4A: { // LSR
                flag_change(C, a & 0x01);
                set_flags(N | Z, a = a >> 1);
                break;
            }

            case 0x68: { // PLA
                set_flags(N | Z, a = stack_pull());
                break;
            }

            case 0x48: { // PHA
                stack_push(a);
                break;
            }

            case 0x01: { // ORA (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x15: { // ORA zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x0D: { // ORA abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x19: { // ORA abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x1D: { // ORA abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x11: { // ORA (ind), Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x05: { // ORA zpg
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x09: { // ORA imm
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, a = a | imm);
                break;
            }

            case 0x35: { // AND zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                set_flags(N | Z, a = a & bus.read(zpg));
                break;
            }

            case 0x21: { // AND (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, a = a & bus.read(addr));
                break;
            }

            case 0x31: { // AND (ind), y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, a = a & bus.read(addr));
                break;
            }

            case 0x3D: { // AND abs, x
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = a & bus.read(addr + x));
                if((addr + x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                break;
            }

            case 0x39: { // AND abs, y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = a & bus.read(addr + y));
                if((addr + y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                break;
            }

            case 0x2D: { // AND abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = a & bus.read(addr));
                break;
            }

            case 0x25: { // AND zpg
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, a = a & bus.read(zpg));
                break;
            }

            case 0x29: { // AND imm
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, a = a & imm);
                break;
            }

            case 0x88: { // DEY
                set_flags(N | Z, y = y - 1);
                break;
            }

            case 0x7E: { // ROR abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                writes.push_back(std::make_pair(addr + x, m));
                break;
            }

            case 0x36: { // ROL zpg,X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                writes.push_back(std::make_pair(zpg, m));
                break;
            }


            case 0x3E: { // ROL abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                bool c = isset(C);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                writes.push_back(std::make_pair(addr + x, m));
                break;
            }

            case 0x2A: { // ROL
                bool c = isset(C);
                flag_change(C, a & 0x80);
                set_flags(N | Z, a = (c ? 0x01 : 0x00) | (a << 1));
                break;
            }

            case 0x6A: { // ROR
                bool c = isset(C);
                flag_change(C, a & 0x01);
                set_flags(N | Z, a = (c ? 0x80 : 0x00) | (a >> 1));
                break;
            }

            case 0x6E: { // ROR abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                writes.push_back(std::make_pair(addr, m));
                break;
            }

            case 0x66: { // ROR
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0x76: { // ROR
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0x2E: { // ROL abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                bool c = isset(C);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                writes.push_back(std::make_pair(addr, m));
                break;
            }


            case 0x26: { // ROL
                uint8_t zpg = read_pc_inc();
                bool c = isset(C);
                m = bus.read(zpg);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                writes.push_back(std::make_pair(zpg, m));
                break;
            }

            case 0x4C: { // JMP
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                pc = addr;
                break;
            }

            case 0x6C: { // JMP indirect
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                uint8_t addrl = bus.read(addr);
                uint8_t addrh = bus.read(addr + 1);
                addr = addrl + addrh * 256;
                pc = addr;
                break;
            }

            case 0x9D: { // STA abs, x
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr + x, a));
                break;
            }

            case 0x99: { // STA
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr + y, a));
                break;
            }

            case 0x91: { // STA (ind), Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                writes.push_back(std::make_pair(addr, a));
                break;
            }

            case 0x81: { // STA (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr, a));
                break;
            }

            case 0x8D: { // STA
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr, a));
                break;
            }

            case 0x08: { // PHP
                stack_push(p | B2 | B);
                break;
            }

            case 0x28: { // PLP
                p = stack_pull() | B2 | B;
                break;
            }

            case 0x24: { // BIT zpg
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0x34: { // BIT abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                m = bus.read(addr);
                if((addr - x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0x3C: { // BIT abs, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0x2C: { // BIT abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0xB4: { // LDY zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                set_flags(N | Z, y = bus.read(zpg));
                break;
            }

            case 0xAE: { // LDX abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, x = bus.read(addr));
                break;
            }

            case 0xBE: { // LDX
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, x = bus.read(addr));
                break;
            }

            case 0xA6: { // LDX
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, x = bus.read(zpg));
                break;
            }

            case 0xB6: { // LDX zpg, Y
                uint8_t zpg = (read_pc_inc() + y) & 0xFF;
                set_flags(N | Z, x = bus.read(zpg));
                break;
            }

            case 0xA4: { // LDY
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, y = bus.read(zpg));
                break;
            }

            case 0xAC: { // LDY
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, y = bus.read(addr));
                break;
            }

            case 0xA2: { // LDX
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, x = imm);
                break;
            }

            case 0xA0: { // LDY
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, y = imm);
                break;
            }

            case 0xA9: { // LDA
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, a = imm);
                break;
            }

            case 0xAD: { // LDA
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xCC: { // CPY abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
                break;
            }

            case 0xEC: { // CPX abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= x);
                set_flags(N | Z, m = x - m);
                break;
            }

            case 0xE0: { // CPX
                uint8_t imm = read_pc_inc();
                flag_change(C, imm <= x);
                set_flags(N | Z, imm = x - imm);
                break;
            }

            case 0xC0: { // CPY
                uint8_t imm = read_pc_inc();
                flag_change(C, imm <= y);
                set_flags(N | Z, imm = y - imm);
                break;
            }

            case 0x55: { // EOR zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x41: { // EOR (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x4D: { // EOR abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x5D: { // EOR abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                m = bus.read(addr);
                if((addr - x) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x59: { // EOR abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x45: { // EOR
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, a = a ^ bus.read(zpg));
                break;
            }

            case 0x49: { // EOR
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, a = a ^ imm);
                break;
            }

            case 0x51: { // EOR
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0xD1: { // CMP
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256) {
                    clk.add_cpu_cycles(1);
                }
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC5: { // CMP
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xCD: { // CMP
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC9: { // CMP
                uint8_t imm = read_pc_inc();
                flag_change(C, imm <= a);
                set_flags(N | Z, imm = a - imm);
                break;
            }

            case 0xD5: { // CMP
                uint8_t zpg = read_pc_inc() + x;
                m = bus.read(zpg & 0xFF);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xE4: { // CPX
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= x);
                set_flags(N | Z, m = x - m);
                break;
            }

            case 0xC4: { // CPY
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
                break;
            }

            case 0x85: { // STA
                uint8_t zpg = read_pc_inc();
                writes.push_back(std::make_pair(zpg, a));
                break;
            }

            case 0x40: { // RTI
                p = stack_pull() | B2 | B;
                uint8_t pcl = stack_pull();
                uint8_t pch = stack_pull();
                pc = pcl + pch * 256;
                break;
            }

            case 0x60: { // RTS
                uint8_t pcl = stack_pull();
                uint8_t pch = stack_pull();
                pc = pcl + pch * 256 + 1;
                break;
            }

            case 0x95: { // STA
                uint8_t zpg = read_pc_inc();
                bus.write((zpg + x) & 0xFF, a);
                break;
            }

            case 0x94: { // STY
                uint8_t zpg = read_pc_inc();
                bus.write((zpg + x) & 0xFF, y);
                break;
            }

            case 0x8E: { // STX abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr, x));
                break;
            }

            case 0x86: { // STX
                uint8_t zpg = read_pc_inc();
                writes.push_back(std::make_pair(zpg, x));
                break;
            }

            case 0x96: { // STX zpg, Y
                uint8_t zpg = read_pc_inc();
                uint16_t addr = (zpg + y) & 0xFF;
                writes.push_back(std::make_pair(addr, x));
                break;
            }

            case 0x84: { // STY
                uint8_t zpg = read_pc_inc();
                writes.push_back(std::make_pair(zpg, y));
                break;
            }

            case 0x8C: { // STY
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr, y));
                break;
            }

            case 0x75: { // ADC zpg, X
                uint8_t zpg = read_pc_inc();
                uint16_t addr = (zpg + x)& 0xFF;
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }


#if EMULATE_65C02
            // 65C02 instructions

            case 0x0F: case 0x1F: case 0x2F: case 0x3F:
            case 0x4F: case 0x5F: case 0x6F: case 0x7F: { // BBRn zpg, rel, 65C02
                int whichbit = (inst >> 4) & 0x7;
                uint8_t zpg = read_pc_inc();
                uint8_t m = bus.read(zpg);
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(!(m & (1 << whichbit))) {
                    // if((pc + rel) / 256 != pc / 256)
                        // clk.add_cpu_cycles(1); // XXX ???
                    pc += rel;
                }
                break;
            }
            
            case 0x8F: case 0x9F: case 0xAF: case 0xBF:
            case 0xCF: case 0xDF: case 0xEF: case 0xFF: { // BBSn zpg, rel, 65C02
                int whichbit = (inst >> 4) & 0x7;
                uint8_t zpg = read_pc_inc();
                uint8_t m = bus.read(zpg);
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(m & (1 << whichbit)) {
                    // if((pc + rel) / 256 != pc / 256)
                        // clk.add_cpu_cycles(1); // XXX ???
                    pc += rel;
                }
                break;
            }
            
            case 0x5A: { // PHY, 65C02
                stack_push(y);
                break;
            }

            case 0x7A: { // PLY, 65C02
                set_flags(N | Z, y = stack_pull());
                break;
            }

            case 0xFA: { // PLX, 65C02
                set_flags(N | Z, x = stack_pull());
                break;
            }

            case 0x80: { // BRA imm, 65C02
                branch(true);
                break;
            }

            case 0x64: { // STZ zpg, 65C02
                uint8_t zpg = read_pc_inc();
                writes.push_back(std::make_pair(zpg, 0));
                break;
            }

            case 0x74: { // STZ zpg, X, 65C02
                uint8_t zpg = read_pc_inc();
                writes.push_back(std::make_pair((zpg + x) & 0xFF, 0));
                break;
            }

            case 0x9C: { // STZ abs, 65C02
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr, 0x0));
                break;
            }

            case 0xDA: { // PHX, 65C02
                stack_push(x);
                break;
            }

            case 0xB2: { // LDA (zpg), 65C02
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0x92: { // STA (zpg), 65C02
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr, a));
                break;
            }

            case 0x72: { // ADC (zpg), 65C02
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    adc_bcd(m, carry);
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x3A: { // DEC, 65C02
                set_flags(N | Z, a = a - 1);
                break;
            }

            case 0x1A: { // INC, 65C02
                set_flags(N | Z, a = a + 1);
                break;
            }

            case 0x12: { // ORA (ind), 65C02
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0xD2: { // CMP (zpg), 65C02 instruction
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0x1C: { // TRB abs, 65C02 instruction
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(Z, m & a);
                writes.push_back(std::make_pair(addr, m & ~a));
                break;
            }

            case 0x14: { // TRB zpg, 65C02 instruction
                uint8_t zpgaddr = read_pc_inc();
                m = bus.read(zpgaddr);
                set_flags(Z, m & a);
                writes.push_back(std::make_pair(zpgaddr, m & ~a));
                break;
            }

            case 0x0C: { // TSB abs, 65C02 instruction
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(Z, m & a);
                writes.push_back(std::make_pair(addr, m | a));
                break;
            }

            case 0x04: { // TRB zpg, 65C02 instruction
                uint8_t zpgaddr = read_pc_inc();
                m = bus.read(zpgaddr);
                set_flags(Z, m & a);
                writes.push_back(std::make_pair(zpgaddr, m | a));
                break;
            }

            case 0x02: case 0x22: case 0x42: case 0x62: case 0x82: case 0xC2: case 0xE2: { // two-byte NOP, 2 cycles
                [[maybe_unused]] uint8_t ignored = read_pc_inc();
                break;
            }

            case 0x03: case 0x13: case 0x23: case 0x33: case 0x43: case 0x53: case 0x63: case 0x73:
            case 0x83: case 0x93: case 0xA3: case 0xB3: case 0xC3: case 0xD3: case 0xE3: case 0xF3: { // one-byte NOP, 1 cycle
                break;
            }

            case 0x0B: case 0x1B: case 0x2B: case 0x3B: case 0x4B: case 0x5B: case 0x6B: case 0x7B:
            case 0x8B: case 0x9B: case 0xAB: case 0xBB: case 0xCB: case 0xDB: case 0xEB: case 0xFB: { // one-byte NOP, 1 cycle
                break;
            }

            case 0x44: { // two-byte NOP, 3 cycles
                [[maybe_unused]] uint8_t ignored = read_pc_inc();
                break;
            }

            case 0x54: case 0xD4: case 0xF4: { // two-byte NOP, 4 cycles
                [[maybe_unused]] uint8_t ignored = read_pc_inc();
                break;
            }

            case 0x5C: { // three-byte NOP, 8 cycles
                [[maybe_unused]] uint8_t ignored1 = read_pc_inc();
                [[maybe_unused]] uint8_t ignored2 = read_pc_inc();
                break;
            }

            case 0xDC: case 0xFC: { // three-byte NOP, 4 cycles
                [[maybe_unused]] uint8_t ignored1 = read_pc_inc();
                [[maybe_unused]] uint8_t ignored2 = read_pc_inc();
                break;
            }

            case 0x7C: { // JMP (ind, X), 65C02 instruction
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                uint8_t addrl = bus.read(addr);
                uint8_t addrh = bus.read(addr + 1);
                addr = addrl + addrh * 256;
                pc = addr;
                break;
            }

            case 0x89: { // BIT imm
                m = read_pc_inc();
                flag_change(Z, (a & m) == 0);
                break;
            }

            case 0x9E: { // STZ abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                writes.push_back(std::make_pair(addr + x, 0));
                break;
            }

#else /* ! EMULATE_65C02 */

            case 0x04: { // NOP zpg
                uint8_t zpgaddr = read_pc_inc();
                m = bus.read(zpgaddr);
                break;
            }


#endif /* EMULATE_65C02 */

            default: {
                printf("unhandled instruction %02X at %04X\n", inst, pc - 1);
                fflush(stdout);
                exit(1);
            }
        }
        assert(cycles[inst] > 0);
        // Hack for putting writes near the end of the instruction to hopefully
        // match real timing
        clk.add_cpu_cycles(cycles[inst] - writes.size());
        for(size_t i = 0; i < writes.size(); i++) {
            const auto& write = writes[i];
            clk.add_cpu_cycles(1);
            bus.write(write.first, write.second);
        }
        writes.clear();
    }
};

template<class CLK, class BUS>
const int32_t CPU6502<CLK, BUS>::cycles[256] =
{
#if ! EMULATE_65C02
    /*         0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
    /* 0x0- */ 7, 6, 2, 1, 3, 3, 5, 0, 3, 2, 2, 1, 6, 4, 6, 5,
    /* 0x1- */ 2, 5, 5, 1, 5, 4, 6, 0, 2, 4, 2, 1, 6, 4, 7, 5,
    /* 0x2- */ 6, 6, 2, 1, 3, 3, 5, 0, 4, 2, 2, 1, 4, 4, 6, 5,
    /* 0x3- */ 2, 5, 0, 1, 0, 4, 6, 0, 2, 4, 2, 1, 0, 4, 7, 5,
    /* 0x4- */ 6, 6, 2, 1, 3, 3, 5, 0, 3, 2, 2, 1, 3, 4, 6, 5,
    /* 0x5- */ 2, 5, 0, 1, 4, 4, 6, 0, 2, 4, 3, 1, 8, 4, 7, 5,
    /* 0x6- */ 6, 6, 2, 1, 3, 3, 5, 0, 4, 2, 2, 1, 5, 4, 6, 5,
    /* 0x7- */ 2, 5, 5, 1, 0, 4, 6, 0, 2, 4, 4, 1, 6, 4, 7, 5,
    /* 0x8- */ 2, 6, 2, 1, 3, 3, 3, 0, 2, 2, 2, 1, 4, 4, 4, 5,
    /* 0x9- */ 2, 6, 5, 1, 4, 4, 4, 0, 2, 5, 2, 1, 4, 5, 5, 5,
    /* 0xA- */ 2, 6, 2, 1, 3, 3, 3, 0, 2, 2, 2, 1, 4, 4, 4, 5,
    /* 0xB- */ 2, 5, 5, 1, 4, 4, 4, 0, 2, 4, 2, 1, 4, 4, 4, 5,
    /* 0xC- */ 2, 6, 2, 1, 3, 3, 5, 0, 2, 2, 2, 1, 4, 4, 3, 5,
    /* 0xD- */ 2, 5, 5, 1, 4, 4, 6, 0, 2, 4, 3, 1, 4, 4, 7, 5,
    /* 0xE- */ 2, 6, 2, 1, 3, 3, 5, 0, 2, 2, 2, 2, 4, 4, 6, 5,
    /* 0xF- */ 2, 5, 0, 1, 4, 4, 6, 0, 2, 4, 4, 1, 4, 4, 7, 5,
#else /* EMULATE_65C02 */
    /*         0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  */
    /* 0x0- */ 7, 6, 2, 1, 5, 3, 5, 0, 3, 2, 2, 1, 6, 4, 6, 5,
    /* 0x1- */ 2, 5, 5, 1, 5, 4, 6, 0, 2, 4, 2, 1, 6, 4, 6, 5,
    /* 0x2- */ 6, 6, 2, 1, 3, 3, 5, 0, 4, 2, 2, 1, 4, 4, 6, 5,
    /* 0x3- */ 2, 5, 0, 1, 4, 4, 6, 0, 2, 4, 2, 1, 0, 4, 6, 5,
    /* 0x4- */ 6, 6, 2, 1, 3, 3, 5, 0, 3, 2, 2, 1, 3, 4, 6, 5,
    /* 0x5- */ 2, 5, 0, 1, 4, 4, 6, 0, 2, 4, 3, 1, 8, 4, 6, 5,
    /* 0x6- */ 6, 6, 2, 1, 3, 3, 5, 0, 4, 2, 2, 1, 5, 4, 6, 5,
    /* 0x7- */ 2, 5, 5, 1, 4, 4, 6, 0, 2, 4, 4, 1, 6, 4, 6, 5,
    /* 0x8- */ 2, 6, 2, 1, 3, 3, 3, 0, 2, 2, 2, 1, 4, 4, 4, 5,
    /* 0x9- */ 2, 6, 5, 1, 4, 4, 4, 0, 2, 5, 2, 1, 4, 5, 6, 5,
    /* 0xA- */ 2, 6, 2, 1, 3, 3, 3, 0, 2, 2, 2, 1, 4, 4, 4, 5,
    /* 0xB- */ 2, 5, 5, 1, 4, 4, 4, 0, 2, 4, 2, 1, 4, 4, 4, 5,
    /* 0xC- */ 2, 6, 2, 1, 3, 3, 5, 0, 2, 2, 2, 1, 4, 4, 3, 5,
    /* 0xD- */ 2, 5, 5, 1, 4, 4, 6, 0, 2, 4, 3, 1, 4, 4, 7, 5,
    /* 0xE- */ 2, 6, 2, 1, 3, 3, 5, 0, 2, 2, 2, 2, 4, 4, 6, 5,
    /* 0xF- */ 2, 5, 0, 1, 4, 4, 6, 0, 2, 4, 4, 1, 4, 4, 7, 5,
#endif /* EMULATE_65C02 */
};

#endif // CPU6502_H

