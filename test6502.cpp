#include <vector>
#include <array>
#include <set>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cinttypes>
#include "dis6502.h"

#ifndef USE_FAKE_6502
#error must set USE_FAKE_6502
#endif

#if !USE_FAKE_6502
#include "cpu6502.h"
#endif

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

#if 0

struct bus
{
    typedef std::map<uint16_t, uint8_t> memory_type;
    memory_type memory;

    bus() { }

    uint8_t read(uint16_t addr) const
    {
        auto found = memory.find(addr);
        if(found == memory.end()) {
            // printf("read 0x%04X yields 0xFF\n", addr);
            return 0xff;
        };
        // printf("read 0x%04X yields 0x%02X\n", addr, memory.at(addr));
        return found->second;
    }

    void write(uint16_t addr, uint8_t data)
    {
        // printf("write 0x%02X to 0x%04X\n", data, addr);
        if(data == 0xff) {
            memory.erase(addr);
        } else {
            memory.insert_or_assign(addr, data);
        }
    }
};

#else

struct bus
{
    typedef std::array<uint8_t, 64 * 1024> memory_type;
    memory_type memory;
    bus()
    {
        std::fill(memory.begin(), memory.end(), 0xA5);
    }
    uint8_t read(uint16_t addr) const
    {
        printf("read 0x%04X yields 0x%02X\n", addr, memory[addr]);
        return memory[addr];
    }
    void write(uint16_t addr, uint8_t data)
    {
        printf("write 0x%02X to 0x%04X\n", data, addr);
        memory[addr] = data;
    }
};

#endif


#if USE_FAKE_6502

#include "fake6502.h"

bus* fake_6502_bus;

extern "C" {

uint8_t read6502(uint16_t address)
{
    return fake_6502_bus->read(address);
}

void write6502(uint16_t address, uint8_t value)
{
    fake_6502_bus->write(address, value);
}

extern uint16_t pc;
extern uint8_t sp;
extern uint8_t a;
extern uint8_t x;
extern uint8_t y;
extern uint8_t status;
extern uint32_t clockticks6502;

};

template<class CLK, class BUS>
struct CPU6502
{
    CLK &clk;
    BUS &bus;

    CPU6502(CLK& clk_, BUS& bus_) :
        clk(clk_),
        bus(bus_)
    {
        fake_6502_bus = &bus;
        reset6502();
    }

    void reset()
    {
        reset6502();
    }

    void cycle()
    {
        uint32_t previous_clocks = clockticks6502;
        step6502();
        uint32_t current_clocks = clockticks6502;
        clk.add_cpu_cycles(current_clocks - previous_clocks);
    }

    void set_pc(uint16_t addr)
    {
        pc = addr;
    }
};

template<class CLK, class BUS>
cpu_state_vector get_cpu_state_vector(const CPU6502<CLK, BUS>& cpu)
{
    return {a, x, y, status, sp, pc};
}

#else

template<class CLK, class BUS>
cpu_state_vector get_cpu_state_vector(const CPU6502<CLK, BUS>& cpu)
{
    return {cpu.a, cpu.x, cpu.y, cpu.p, cpu.s, cpu.pc};
}

#endif

template<class CLK, class BUS>
void print_cpu_state(const CPU6502<CLK, BUS>& cpu)
{
    cpu_state_vector state = get_cpu_state_vector(cpu);
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

    dummyclock clock;

    CPU6502<dummyclock, bus> cpu(clock, machine);

    cpu.reset();

    cpu.set_pc(start);

    uint16_t oldpc;
    std::set<std::pair<cpu_state_vector, bus::memory_type>> seen_states;
    do {
        oldpc = get_cpu_state_vector(cpu)[CPU_STATE_VECTOR_PC];

        [[maybe_unused]] static uint64_t count = 0;
        // if((count++) % 1000 == 0) {
            printf("%08" PRIu64 ", ", clock.cycles);
            print_cpu_state(cpu);
            printf("%s\n", read_bus_and_disassemble(machine, oldpc).c_str());
        // }

        if(false) {
            auto current_state = std::make_pair(get_cpu_state_vector(cpu), machine.memory);
            if(seen_states.count(current_state) > 0) {
                printf("saw this state before, bail\n");
                print_cpu_state(cpu);
                printf("%s\n", read_bus_and_disassemble(machine, oldpc).c_str());
                exit(0);
            }
            seen_states.insert(current_state);
        }

        cpu.cycle();
    } while(get_cpu_state_vector(cpu)[CPU_STATE_VECTOR_PC] != oldpc);

    printf("%08" PRIu64 ", ", clock.cycles);
    print_cpu_state(cpu);
    printf("%s\n", read_bus_and_disassemble(machine, oldpc).c_str());

    exit(EXIT_SUCCESS);
}
