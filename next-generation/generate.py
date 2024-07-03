import sys

# INSTRUCTION(operand1, operand2, addressing_mode, operation, output, flag_update_mask)

"""
Would PC operations be special cases?
    JMP, Bxx
what is first operand
    a, x, y, sp, p, 0
Operand 2
    single bit
    0xFF
    memory
is operand 2 inverted?
Output
    dropped, a, x, y, sp, p, memory (, pc?)
which flags are updated?
What is the ALU operation?
    ADC, SBC, SET, CLEAR, AND, OR, EOR, TEST
Is there a PC update?
    add signed
    add
"""

addressing_mode_code = {
    "A" : None,

    "impl" : None,

    "rel" : None, # handled by branch instructions

    "zpg" : """
        address = read_pc_inc();
        """,

    "#" : """
        address = pc;
        pc++;
        """,

    "abs" : """
        uint8_t low = read_pc_inc();
        uint8_t high = read_pc_inc();
        address = low + high * 256;
        """,

    "abs,X" : """\
        uint8_t low = read_pc_inc();
        uint8_t high = read_pc_inc();
        uint16_t base = low + high * 256;
        address = base + x;
        if((base && 0xFF00) != (address && 0xFF00)) {
            clk.add_cpu_cycles(1);
        }
        """,

    "abs,Y" : """\
        uint8_t low = read_pc_inc();
        uint8_t high = read_pc_inc();
        uint16_t base = low + high * 256;
        address = base + y;
        if((base && 0xFF00) != (address && 0xFF00)) {
            clk.add_cpu_cycles(1);
        }
        """,

    "X,ind" : """\
        uint8_t zpg = (read_pc_inc() + x) & 0xFF;
        uint8_t low = bus.read(zpg);
        uint8_t high = bus.read((zpg + 1) & 0xFF);
        address addr = low + high * 256;
        """,
    
    "ind,Y" : """\
        uint8_t zpg = read_pc_inc();
        uint8_t low = bus.read(zpg);
        uint8_t high = bus.read((zpg + 1) & 0xFF);
        uint16_t base = low + high * 256;
        address = base + y;
        if((base && 0xFF00) != (address && 0xFF00)) {
            clk.add_cpu_cycles(1);
        }
        """,
    
    "zpg,X" : """\
        uint8_t zpg = read_pc_inc();
        address = (zpg + x) & 0xFF;
        """,
    
    "zpg,Y" : """\
        uint8_t zpg = read_pc_inc();
        address = (zpg + y) & 0xFF;
        """,
    
    "ind" : """\
        uint8_t low = read_pc_inc();
        uint8_t high = read_pc_inc();
        uint16_t addr = low + high * 256;
        low = bus.read(addr);
        high = bus.read(addr + 1);
        address = low + high * 256;
        """,
    
    "(zpg)" : """\
        uint8_t zpg = read_pc_inc();
        uint8_t low = bus.read(zpg);
        uint8_t high = bus.read((zpg + 1) & 0xFF);
        address = low + high * 256;
        """,
    
    "(abs,X)" : None, # handled by JMP indirect instruction

}

added_by_65C02 = {
    0x72: ["ADC", "(zpg)"],
    0x32: ["AND", "(zpg)"],
    0xD2: ["CMP", "(zpg)"],
    0x52: ["EOR", "(zpg)"],
    0xB2: ["LDA", "(zpg)"],
    0x12: ["ORA", "(zpg)"],
    0xF2: ["SBC", "(zpg)"],
    0x92: ["STA", "(zpg)"],
    0x89: ["BIT", "#"],
    0x34: ["BIT", "zpg,X"],
    0x3C: ["BIT", "abs,X"],
    0x3A: ["DEC", "A"],
    0x1A: ["INC", "A"],
    0x7C: ["JMP", "(abs,X)"],
    0x80: ["BRA", "rel"],
    0xDA: ["PHX", "impl"],
    0x5A: ["PHY", "impl"],
    0xFA: ["PLX", "impl"],
    0x7A: ["PLY", "impl"],
    0x64: ["STZ", "zpg"],
    0x74: ["STZ", "zpg,X"],
    0x9C: ["STZ", "abs"],
    0x9E: ["STZ", "abs,X"],
    0x14: ["TRB", "zpg"],
    0x1C: ["TRB", "abs"],
    0x04: ["TSB", "zpg"],
    0x0C: ["TSB", "abs"],
}

operations = {
    # (kind, op0, op1, op, result)
    'LDA' : {
        "kind" : "alu", 
        "operand1" : "mem",
        "operand2" : "none",
        "operation" : "none",
        "result" : "a"
    },
    'STA' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "none",
        "operation" : "none",
        "result" : "mem"
    },
    'LDX' : {
        "kind" : "alu", 
        "operand1" : "mem",
        "operand2" : "none",
        "operation" : "none",
        "result" : "x"
    },
    'STX' : {
        "kind" : "alu", 
        "operand1" : "x",
        "operand2" : "none",
        "operation" : "none",
        "result" : "mem"
    },
    'LDY' : {
        "kind" : "alu", 
        "operand1" : "mem",
        "operand2" : "none",
        "operation" : "none",
        "result" : "y"
    },
    'STY' : {
        "kind" : "alu", 
        "operand1" : "y",
        "operand2" : "none",
        "operation" : "none",
        "result" : "mem"
    },
    'TXA' : {
        "kind" : "alu", 
        "operand1" : "x",
        "operand2" : "none",
        "operation" : "none",
        "result" : "a"
    },
    'TAX' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "none",
        "operation" : "none",
        "result" : "x"
    },
    'TYA' : {
        "kind" : "alu", 
        "operand1" : "y",
        "operand2" : "none",
        "operation" : "none",
        "result" : "a"
    },
    'TAY' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "none",
        "operation" : "none",
        "result" : "y"
    },
    'TXS' : {
        "kind" : "alu", 
        "operand1" : "x",
        "operand2" : "none",
        "operation" : "none",
        "result" : "sp"
    },
    'TSX' : {
        "kind" : "alu", 
        "operand1" : "sp",
        "operand2" : "none",
        "operation" : "none",
        "result" : "x"
    },
    'CLV' : {
        "kind" : "alu", 
        "operand1" : "p",
        "operand2" : "~V",
        "operation" : "and",
        "result" : "p"
    },
    'SEC' : {
        "kind" : "alu", 
        "operand1" : "p",
        "operand2" : "C",
        "operation" : "or",
        "result" : "p"
    },
    'CLC' : {
        "kind" : "alu", 
        "operand1" : "p",
        "operand2" : "~C",
        "operation" : "and",
        "result" : "p"
    },
    'SED' : {
        "kind" : "alu", 
        "operand1" : "p",
        "operand2" : "D",
        "operation" : "or",
        "result" : "p"
    },
    'CLD' : {
        "kind" : "alu", 
        "operand1" : "p",
        "operand2" : "~D",
        "operation" : "and",
        "result" : "p"
    },
    'SEI' : {
        "kind" : "alu", 
        "operand1" : "p",
        "operand2" : "I",
        "operation" : "or",
        "result" : "p"
    },
    'CLI' : {
        "kind" : "alu", 
        "operand1" : "p",
        "operand2" : "~I",
        "operation" : "and",
        "result" : "p"
    },
# XXX shifts can be on A or on memory, so express it as one using "oper":
    'ASL' : {
        "kind" : "alu", 
        "operand1" : "oper",
        "operand2" : "none",
        "operation" : "asl",
        "result" : "oper"
    },
    'ROR' : {
        "kind" : "alu", 
        "operand1" : "oper",
        "operand2" : "none",
        "operation" : "ror",
        "result" : "oper"
    },
    'ROL' : {
        "kind" : "alu", 
        "operand1" : "oper",
        "operand2" : "none",
        "operation" : "rol",
        "result" : "oper"
    },
    'LSR' : {
        "kind" : "alu", 
        "operand1" : "oper",
        "operand2" : "none",
        "operation" : "lsr",
        "result" : "oper"
    },
    'CMP' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "mem",
        "operation" : "sbc",
        "result" : "none"
    },
    'CPX' : {
        "kind" : "alu", 
        "operand1" : "x",
        "operand2" : "mem",
        "operation" : "sbc",
        "result" : "none"
    },
    'CPY' : {
        "kind" : "alu", 
        "operand1" : "y",
        "operand2" : "mem",
        "operation" : "sbc",
        "result" : "none"
    },
    'DEC' : {
        "kind" : "alu", 
        "operand1" : "mem",
        "operand2" : "1",
        "operation" : "sub",
        "result" : "mem"
    },
    'INC' : {
        "kind" : "alu", 
        "operand1" : "mem",
        "operand2" : "1",
        "operation" : "add",
        "result" : "mem"
    },
    'DEX' : {
        "kind" : "alu", 
        "operand1" : "x",
        "operand2" : "1",
        "operation" : "sub",
        "result" : "x"
    },
    'INX' : {
        "kind" : "alu", 
        "operand1" : "x",
        "operand2" : "1",
        "operation" : "add",
        "result" : "x"
    },
    'DEY' : {
        "kind" : "alu", 
        "operand1" : "y",
        "operand2" : "1",
        "operation" : "sub",
        "result" : "y"
    },
    'INY' : {
        "kind" : "alu", 
        "operand1" : "y",
        "operand2" : "1",
        "operation" : "add",
        "result" : "y"
    },
    'ADC' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "mem",
        "operation" : "adc",
        "result" : "a"
    },
    'SBC' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "mem",
        "operation" : "sbc",
        "result" : "a"
    },
    'EOR' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "mem",
        "operation" : "eor",
        "result" : "a"
    },
    'ORA' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "mem",
        "operation" : "or",
        "result" : "a"
    },
    'AND' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "mem",
        "operation" : "and",
        "result" : "a"
    },
    'BIT' : {
        "kind" : "alu", 
        "operand1" : "a",
        "operand2" : "mem",
        "operation" : "and",
        "result" : "none"
    }, # this is a complicated instruction in terms of flags
    'NOP' : {
        "kind" : "alu", 
        "operand1" : "none",
        "operand2" : "none",
        "operation" : "none",
        "result" : "none"
    },
}


all_instructions = {}

def instruction(i, mnemonic, addressing, in_65C02):
    if addressing not in addressing_mode_code: # ["A", "#", "impl", "zpg", "abs", "rel", "X,ind", "ind,Y", "zpg,X", "zpg,Y", "abs,X", "abs,Y", "ind", "(zpg)", "(abs,X)"]:
        print("unknown addressing mode \"%s\"" % addressing)
        sys.exit(1)
    print("%02X : %s %s" % (i, mnemonic, addressing))
    all_instructions[i] = (in_65C02, mnemonic, addressing)

base = 0
for l in sys.stdin:
    words = l.strip().split()
    del words[0]
    for i in range(16):
        if words[0] == "---":
            # unimplemented
            del words[0]
        else:
            mnemonic = words[0]
            del words[0]
            addressing = words[0]
            if addressing == "---":
                del words[0]
                continue
            instruction(base + i, mnemonic, addressing, False)
            del words[0]
    base += 16

for (byte, params) in added_by_65C02.items():
    (mnemonic, addressing) = params
    instruction(byte, mnemonic, addressing, True)

instruction_template = r'''\
            case 0x%02X: {
                clk.add_cpu_cycles(1);
                {instruction_code}
                break;
            }
'''

for byte in range(256):
    if byte in all_instructions:
        (in_65C02, mnemonic, addressing) = all_instructions[byte]
        if in_65C02:
            print("#if EMULATE_65C02")
        # print("%02X: %s %s (65C02 only)" % (byte, mnemonic, addressing))
        op = operations[mnemonic]
        if op["kind"] == alu:
            print("""\
""")
        if in_65C02:
            print("#endif /* EMULATE_65C02 */")
