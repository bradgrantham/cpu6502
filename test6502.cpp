#include <vector>
#include <array>
#include <set>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cinttypes>
#include <iostream>
#include "dis6502.h"

#include "cpu6502.h"

struct dummyclock
{
    uint64_t cycles = 0;
    void add_cpu_cycles(int N) {
        cycles += N;
    }
};

typedef std::array<unsigned int, 6> cpu_state_vector;

enum {
    CPU_STATE_VECTOR_A = 0,
    CPU_STATE_VECTOR_X = 1,
    CPU_STATE_VECTOR_Y = 2,
    CPU_STATE_VECTOR_STATUS = 3,
    CPU_STATE_VECTOR_SP = 4,
    CPU_STATE_VECTOR_PC = 5,

    CPU_STATE_VECTOR_STATUS_N = 0x80,
    CPU_STATE_VECTOR_STATUS_V = 0x40,
    CPU_STATE_VECTOR_STATUS_B2 = 0x20,
    CPU_STATE_VECTOR_STATUS_B = 0x10,
    CPU_STATE_VECTOR_STATUS_D = 0x08,
    CPU_STATE_VECTOR_STATUS_I = 0x04,
    CPU_STATE_VECTOR_STATUS_Z = 0x02,
    CPU_STATE_VECTOR_STATUS_C = 0x01,
};

template<class CPU>
cpu_state_vector get_cpu_state_vector(CPU& cpu)
{
    return {cpu.a, cpu.x, cpu.y, cpu.p, cpu.s, cpu.pc};
}

struct bus
{
    typedef std::array<uint8_t, 64 * 1024> memory_type;
    memory_type memory;

    std::map<uint16_t, uint8_t> write_history;

    bus()
    {
        std::fill(memory.begin(), memory.end(), 0xA5);
    }
    uint8_t read(uint16_t addr) const
    {
        return memory[addr];
    }
    void write(uint16_t addr, uint8_t data)
    {
        memory[addr] = data;
        write_history[addr] = data;
    }
};

extern "C" {
#define CHIPS_IMPL
#include "m6502.h"
};

template<class CLK, class BUS>
struct Reference6502
{
    CLK &clk;
    BUS &bus;

    m6502_t cpu;
    uint64_t pins;

    Reference6502(CLK& clk_, BUS& bus_) :
        clk(clk_),
        bus(bus_)
    {
        m6502_desc_t init { 0 };
        pins = m6502_init(&cpu, &init);
        cycle();
        cycle();
        cycle();
        cycle();
        cycle();
        cycle();
        cycle();
    }

    void cycle()
    {
        uint64_t cycles = 0;
        do {
            pins = m6502_tick(&cpu, pins);
            const uint16_t addr = M6502_GET_ADDR(pins);
            if (pins & M6502_RW) {
                // a memory read
                M6502_SET_DATA(pins, bus.read(addr));
            }
            else {
                // a memory write
                bus.write(addr, M6502_GET_DATA(pins));
            }
            cycles++;
        } while (!(pins & M6502_SYNC));
        clk.add_cpu_cycles(cycles);
    }

    void set_pc(uint16_t addr)
    {
        pins = M6502_SYNC;
        M6502_SET_ADDR(pins, addr);
        M6502_SET_DATA(pins, bus.read(addr));
        m6502_set_pc(&cpu, addr);
    }
};

template<class CLK, class BUS>
cpu_state_vector get_cpu_state_vector(Reference6502<CLK, BUS>& cpu)
{
    uint8_t a = m6502_a(&cpu.cpu);
    uint8_t x = m6502_x(&cpu.cpu);
    uint8_t y = m6502_y(&cpu.cpu);
    uint8_t status = m6502_p(&cpu.cpu);
    uint8_t sp = m6502_s(&cpu.cpu);
    uint16_t pc = m6502_pc(&cpu.cpu);
    return {a, x, y, status, sp, pc};
}

void print_cpu_state(const cpu_state_vector& state)
{
    printf("6502: A:%02X X:%02X Y:%02X P:", state[CPU_STATE_VECTOR_A], state[CPU_STATE_VECTOR_X], state[CPU_STATE_VECTOR_Y]);
    printf("%s", (state[CPU_STATE_VECTOR_STATUS] & CPU_STATE_VECTOR_STATUS_N) ? "N" : "n");
    printf("%s", (state[CPU_STATE_VECTOR_STATUS] & CPU_STATE_VECTOR_STATUS_V) ? "V" : "v");
    printf("-");
    printf("%s", (state[CPU_STATE_VECTOR_STATUS] & CPU_STATE_VECTOR_STATUS_B) ? "B" : "b");
    printf("%s", (state[CPU_STATE_VECTOR_STATUS] & CPU_STATE_VECTOR_STATUS_D) ? "D" : "d");
    printf("%s", (state[CPU_STATE_VECTOR_STATUS] & CPU_STATE_VECTOR_STATUS_I) ? "I" : "i");
    printf("%s", (state[CPU_STATE_VECTOR_STATUS] & CPU_STATE_VECTOR_STATUS_Z) ? "Z" : "z");
    printf("%s ", (state[CPU_STATE_VECTOR_STATUS] & CPU_STATE_VECTOR_STATUS_C) ? "C" : "c");
    printf("S:%02X ", state[CPU_STATE_VECTOR_SP]);
    printf("PC:%04X\n", state[CPU_STATE_VECTOR_PC]);
}

template<class BUS>
std::string read_bus_and_disassemble(const BUS &bus, int pc)
{
    uint8_t buf[4];
    buf[0] = bus.read(pc + 0);
    buf[1] = bus.read(pc + 1);
    buf[2] = bus.read(pc + 2);
    buf[3] = bus.read(pc + 3);
    auto [bytes, dis] = disassemble_6502(pc, buf);
    return dis;
}

int main(int argc, const char **argv)
{
    if(argc < 2) {
        fprintf(stderr, "usage: %s testfile.bin\n", argv[0]);
    }

    bus machine;
    uint16_t start;
    bool validate = true;

    FILE *testbin = fopen(argv[1], "rb");
    if(!testbin) {
        printf("couldn't open \"%s\" for reading\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    fseek(testbin, 0, SEEK_END);
    long length = ftell(testbin);
    fseek(testbin, 0, SEEK_SET);
    std::vector<uint8_t> rom(length);
    fread(rom.data(), 1, length, testbin);
    fclose(testbin);

    if(true) {

        // Assume https://github.com/amb5l/6502_65C02_functional_tests.git
        // Binary file is 64K and fills memory
        // Tests start at 0x400 (1024)

        uint16_t addr = 0;
        for(const auto &data: rom) {
            machine.write(addr, data);
            addr += 1;
        }
        start = 0x400;

    } else {

        // Placeholder for write from handcoded ROM

        uint16_t addr = 0x600;
        for(const auto &data: { 0xa9, 0x00, 0x8d, 0x19, 0x06, 0x8d, 0x1a, 0x06, 0xf8, 0xa9, 0x7a, 0x48, 0x28, 0xad, 0x19, 0x06, 0xed, 0x1a, 0x06, 0x8d, 0x1b, 0x06, 0x4c, 0x16, 0x06, 0x00, 0x00, 0x00 }) {
            machine.write(addr, data);
            addr += 1;
        }
        start = addr;

    }

    dummyclock clock, clock2;

    bus machine2 = machine;

    CPU6502<dummyclock, bus> cpu(clock, machine);
    Reference6502<dummyclock, bus> refcpu(clock2, machine2);

    cpu.set_pc(start);
    refcpu.set_pc(start);

    std::set<std::pair<cpu_state_vector, bus::memory_type>> seen_states;

    uint16_t oldpc;
    uint64_t oldclock = 0;
    uint64_t oldclock2 = 0;
    do {
        oldpc = get_cpu_state_vector(cpu)[CPU_STATE_VECTOR_PC];

        [[maybe_unused]] static uint64_t count = 0;
        // if((count++) % 1000 == 0) {
            printf("%08" PRIu64 ", ", clock.cycles - oldclock);
            print_cpu_state(get_cpu_state_vector(cpu));
            printf("%s\n", read_bus_and_disassemble(machine, oldpc).c_str());
        // }

        if(false) {
            auto current_state = std::make_pair(get_cpu_state_vector(cpu), machine.memory);
            if(seen_states.count(current_state) > 0) {
                printf("saw this state before, bail\n");
                print_cpu_state(get_cpu_state_vector(cpu));
                printf("%s\n", read_bus_and_disassemble(machine, oldpc).c_str());
                exit(0);
            }
            seen_states.insert(current_state);
        }

        machine.write_history.clear();
        oldclock = clock.cycles;
        cpu.cycle();

        if(validate) {
            machine2.write_history.clear();
            oldclock2 = clock2.cycles;
            refcpu.cycle();

            uint64_t cycles = clock.cycles - oldclock;
            uint64_t cycles2 = clock2.cycles - oldclock2;

            auto cpu_state = get_cpu_state_vector(cpu);
            auto refcpu_state = get_cpu_state_vector(refcpu);

            if(cycles != cycles2) {
                printf("CPU instruction timing differed\n");
                printf("CPU:    %" PRIu64 " cycles\n", cycles);
                printf("REFCPU: %" PRIu64 " cycles\n", cycles2);
                exit(1);
            }

            // Ignore B2 - I think I'm handling it correctly
            cpu_state[3] &= ~CPU_STATE_VECTOR_STATUS_B2;
            refcpu_state[3] &= ~CPU_STATE_VECTOR_STATUS_B2;

            if(cpu_state != refcpu_state) {
                printf("CPU vectors differ\n");
                printf("CPU:     ");
                print_cpu_state(cpu_state);
                for(auto i: cpu_state) {
                    printf("%08X ", i);
                }
                puts("");
                printf("REFCPU:  ");
                print_cpu_state(refcpu_state);
                for(auto i: refcpu_state) {
                    printf("%08X ", i);
                }
                puts("");
                exit(1);
            }

            if(machine.write_history != machine2.write_history) {
                printf("memory writes differed\n");
                printf("CPU:");
                for(auto& write: machine.write_history) {
                    printf(" (0x%04X, 0x%02X)", write.first, write.second);
                }
                printf("\n");
                printf("REFCPU:");
                for(auto& write: machine2.write_history) {
                    printf(" (0x%04X, 0x%02X)", write.first, write.second);
                }
                printf("\n");
                exit(1);
            }
        }

    } while(get_cpu_state_vector(cpu)[CPU_STATE_VECTOR_PC] != oldpc);

    printf("%08" PRIu64 ", ", clock.cycles - oldclock);
    print_cpu_state(get_cpu_state_vector(cpu));
    printf("%s\n", read_bus_and_disassemble(machine, oldpc).c_str());

    exit(EXIT_SUCCESS);
}
