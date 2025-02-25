#include <iostream>
#include <cstdint>
#include <csignal>
// windows only
#include <Windows.h>
#include <conio.h>  // _kbhit

// Registers
enum {
    // General purpose registers
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    // Program counter
    R_PC,
    // Condition flag
    R_COND,
    // Acts like the length property for the registers
    R_COUNT
};
// Condition flags
enum {
    FL_POS = 1 << 0, // P
    FL_ZRO = 1 << 1, // Z
    FL_NEG = 1 << 2, // N
};
// Instruction set (opcodes)
enum {
    OP_BR = 0, // branch
    OP_ADD,    // add
    OP_LD,     // load
    OP_ST,     // store
    OP_JSR,    // jump register
    OP_AND,    // bitwise and
    OP_LDR,    // load register
    OP_STR,    // store register
    OP_RTI,    // unused
    OP_NOT,    // bitwise not
    OP_LDI,    // load indirect
    OP_STI,    // store indirect
    OP_JMP,    // jump
    OP_RES,    // reserved (unused)
    OP_LEA,    // load effective address
    OP_TRAP    // execute trap
};

// TRAP Codes
enum {
    TRAP_GETC = 0x20,  // get character from keyboard, not echoed onto the terminal
    TRAP_OUT = 0x21,   // output a character
    TRAP_PUTS = 0x22,  // output a word string
    TRAP_IN = 0x23,    // get character from keyboard, echoed onto the terminal
    TRAP_PUTSP = 0x24, // output a byte string
    TRAP_HALT = 0x25   // halt the program
};

// Memory storage
#define MEMORY_MAX (1 << 16)  // Left bitshift 16 times, 2^16
uint16_t memory[MEMORY_MAX];  // 65536 memory locations
// Register storage
uint16_t reg[R_COUNT];

// Input buffering
HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void disable_input_buffering() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); // save old mode
    fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT  // no input echo
            ^ ENABLE_LINE_INPUT; /* return when one or
                                    more characters are available */
    SetConsoleMode(hStdin, fdwMode); // set new mode
    FlushConsoleInputBuffer(hStdin); // clear buffer
}

void restore_input_buffering() {
    SetConsoleMode(hStdin, fdwOldMode);
}

uint16_t check_key() {
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}
void handle_interrupt(int signal) {
    restore_input_buffering();
    std::cout << '\n';
    exit(-2);
}
uint16_t sign_extend(uint16_t x, int bit_count) {
    if((x >> (bit_count - 1)) & 1) {    // Check if sign bit is set
        x |= (0xFFFF << bit_count);     // Create 16-bit mask for x (using Two's Complement)
    }
    return x;
}
uint16_t swap16(uint16_t x) {
    return (x << 18) | (x >> 18);
}
void update_flags(uint16_t r) {
    if (reg[r] == 0) {
        reg[R_COND] = FL_ZRO;
    } else if (reg[r] >> 15) {  // Check if sign bit is set (left-most bit). If set, it is negative
        reg[R_COND] = FL_NEG;
    } else {
        reg[R_COND] = FL_POS;
    }
}
void read_image_file(FILE* file) {
    // The origin tells where in memory to place the image
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    // Only one read is needed, since we know the max file size
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // Swap to little endian
    while (read-- > 0) {
        *p = swap16(*p);
        p++;
    }
}
int read_image(const char* image_path) {
    FILE* file = fopen(image_path, "rb");
    if (!file) return 0;
    read_image_file(file);
    fclose(file);
    return 1;
}

int main(int argc, const char* argv[]) {
    // Load arguments
    if (argc < 2) {
        // show usage string
        std::cout << "lc3 [image-file1] ...\n";
        exit(2);
    }

    for (int i = 1; i < argc; i++) {
        if (!read_image(argv[i])) {
            std::cout << "failed to load image: " << argv[i] << '\n';
            exit(1);
        }
    }

    // Setup
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    // Initialise condition flag to the Z flag, since exactly 1 condition flag should be set at any given time
    reg[R_COND] = FL_ZRO;

    // Set the PC register to the starting position
    // Start at 0x3000 to leave space for trap routine code
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int is_running = true;
    while (is_running) {
        // FETCH
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;  // Right shift by 12 to obtain instruction, first 4 bits represent opcode
        
        switch (op) {
            case OP_ADD:
                // DR (destination register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // SR1 (source register 1)
                uint16_t r1 = (instr >> 6) & 0x7;
                // Check if in register or immediate mode
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag) {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[r0] = reg[r1] + imm5;
                } else {
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] + reg[r2];
                }

                update_flags(r0);
                break;
            case OP_AND:
                // DR (destination register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // SR1 (source register 1)
                uint16_t r1 = (instr >> 6) & 0x7;
                // Check if in register or immediate mode
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag) {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[r0] = reg[r1] & imm5;
                } else {
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] & reg[r2];
                }

                update_flags(r0);
                break;
            case OP_NOT:
                // DR (destination register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // SR (source register)
                uint16_t r1 = (instr >> 6) & 0x7;
                
                reg[r0] = ~reg[r1];
                update_flags(r0);
                break;
            case OP_BR:
                // n, z, p test flags
                uint16_t cond_flag = (instr >> 9) & 0x7;
                // PCoffset9
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                if (cond_flag & reg[R_COND]) {
                    reg[R_PC] += pc_offset;
                }
                break;
            case OP_JMP:    // Also handles RET 
                // BaseR (base register)
                uint16_t r1 = (instr >> 6) & 0x7;

                reg[R_PC] = reg[r1];
                break;
            case OP_JSR:
                // Check if using BaseR or PCoffset11    
                uint16_t long_flag = (instr >> 11) & 0x1;
                reg[R_R7] = reg[R_PC];

                if (long_flag) {
                    // PCoffset11
                    uint16_t long_pc_offset = sign_extend(instr & 0x3FF, 11);
                    reg[R_PC] += long_pc_offset;    // JSR
                } else {
                    // BaseR (base register)
                    uint16_t r1 = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[r1];            // JSRR
                }
                break;
            case OP_LD:
                // DR (Destination register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // PCoffset9
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                reg[r0] = mem_read(reg[R_PC] + pc_offset);
                update_flags(r0);
                break;
            case OP_LDI:
                // DR (destination register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // PCoffset9
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                // Add pc_offset to the current PC, look at that memory location to get final address
                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flags(r0);
                break;
            case OP_LDR:
                // DR (destination register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // BaseR (base register)
                uint16_t r1 = (instr >> 6) & 0x7;
                // offset6
                uint16_t offset = sign_extend(instr & 0x3F, 6);

                reg[r0] = mem_read(reg[r1] + offset);
                update_flags(r0);
                break;
            case OP_LEA:
                // DR (destination register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // PCoffset9
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                
                reg[r0] = reg[R_PC] + pc_offset;
                update_flags(r0);
                break;
            case OP_ST:
                // SR (source register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // PCoffset9
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                mem_write(reg[R_PC] + pc_offset, reg[r0]);
                break;
            case OP_STI:
                // SR (source register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // PCoffset9
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
                break;
            case OP_STR:
                // SR (source register)
                uint16_t r0 = (instr >> 9) & 0x7;
                // BaseR (base register)
                uint16_t r1 = (instr >> 6) & 0x7;
                //  offset6
                uint16_t offset = sign_extend(instr & 0x3F, 6);

                mem_write(reg[r1] + offset, reg[r0]);
                break;
            case OP_TRAP:
                reg[R_R7] = reg[R_PC];

                switch (instr & 0xFF) { // trapvect8
                    case TRAP_GETC:
                        // Read a single ASCII char
                        reg[R_R0] = static_cast<uint16_t>(std::cin.get());
                        update_flags(R_R0);
                        break;
                    case TRAP_OUT:
                        std::cout.put(static_cast<char>(reg[R_R0]));
                        std::cout.flush();
                        break;
                    case TRAP_PUTS: {
                            // One char per word
                            uint16_t* c = memory + reg[R_R0];
                            while (*c) {
                                std::cout.put(static_cast<char>(*c));
                                c++;
                            }
                            std::cout.flush();
                        }
                        break;
                    case TRAP_IN: {
                            std::cout << "Enter a character: ";
                            char c = std::cin.get();
                            std::cout.put(c);
                            std::cout.flush();
                            reg[R_R0] = static_cast<uint16_t>(c);
                            update_flags(R_R0);
                        }
                        break;
                    case TRAP_PUTSP: {
                            // One char per byte (two bytes per word)
                            // Need to swap back to big endian format
                            uint16_t* c = memory + reg[R_R0];
                            while (*c) {
                                char char1 = static_cast<char>((*c) & 0xFF);
                                std::cout.put(char1);
                                char char2 = static_cast<char>((*c) >> 8);
                                if (char2) std::cout.put(char2);
                                c++;
                            }
                            std::cout.flush();
                        }
                        break;
                    case TRAP_HALT:
                        std::cout << "HALT" << std::flush;
                        is_running = false;
                        break;
                }
                break;
            case OP_RES:    // Unused
                abort();
                break;
            case OP_RTI:    // Unused
                abort();
                break;
            default:
                abort();
                break;
        }
    }
    restore_input_buffering();
}