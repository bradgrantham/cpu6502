import sys
import re

# egrep -v 'PHP|PLP|BRK|0x40|0x60|0x0F|NOP|0x03|0x0B|0x54|0xDC' cpu6502.h | python3 verify-addressing.py 

addressing = None
instruction = None
for l in sys.stdin:
    if re.search("case.*:", l):
        if addressing:
            raise ValueError("didn't match %s, last was \"%s\"" % (addressing, instruction))
        case = l
        # print(re.search("case 0x.*", l))
        if re.search("case 0x.*", l):
            instruction = l.strip()
            print(instruction)
            if re.search("\(abs, X\)", l):
                addressing = "absolute indexed indirect"
            elif re.search("abs, X", l):
                addressing = "absolute indexed X"
            elif re.search("abs, Y", l):
                addressing = "absolute indexed Y"
            elif re.search("zpg, X", l):
                addressing = "zeropage indexed X"
            elif re.search("zpg, Y", l):
                addressing = "zeropage indexed Y"
            elif re.search("\(ind, X\)", l):
                addressing = "indexed indirect"
            elif re.search("\(ind\), Y", l):
                addressing = "indirect indexed"
            elif re.search("ind", l):
                addressing = "indirect"
            elif re.search("\(zpg\)", l):
                addressing = "zeropage indirect"
            elif re.search("A", l):
                addressing = None # "accumulator"
            elif re.search("#", l):
                addressing = None # "immediate"
            elif re.search("imm", l):
                addressing = None # "immediate"
            elif re.search("impl", l):
                addressing = None # "implicit"
            elif re.search("zpg", l):
                addressing = "zeropage"
            elif re.search("abs", l):
                addressing = "absolute"
            elif re.search("rel", l):
                addressing = None # "relative"
            else:
                print("unknown addressing mode: %s" % l);
            print(addressing)
            # case 0x0A: { // ASL A
        else:
            raise ValueError("case but no case 0x")
    else:
        if addressing == "indirect indexed" and re.search("indirect_indexed\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "indexed indirect" and re.search("indexed_indirect\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "absolute indexed X" and re.search("absolute_indexed_X\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "absolute indexed Y" and re.search("absolute_indexed_Y\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "indirect" and re.search("indirect\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "absolute" and re.search("absolute\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "zeropage" and re.search("zeropage\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "zeropage indexed X" and re.search("zeropage_indexed_X\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "zeropage indexed Y" and re.search("zeropage_indexed_Y\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "absolute indexed indirect" and re.search("absolute_indexed_indirect\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
        elif addressing == "zeropage indirect" and re.search("zeropage_indirect\(", l):
            print("matched %s with %s" % (addressing, l.strip()))
            addressing = None
