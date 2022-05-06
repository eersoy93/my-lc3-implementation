#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];

enum
{
    REGISTER_R0 = 0,
    REGISTER_R1,
    REGISTER_R2,
    REGISTER_R3,
    REGISTER_R4,
    REGISTER_R5,
    REGISTER_R6,
    REGISTER_R7,
    REGISTER_PC,
    REGISTER_COND,
    REGISTER_COUNT
};

enum
{
    MEMORY_MAPPED_REGISTER_KEYBOARD_STATUS = 0xFE00,
    MEMORY_MAPPED_REGISTER_KEYBOARD_DATA = 0xFE02
};

uint16_t registers[REGISTER_COUNT];

enum
{
    OPCODE_BR = 0,
    OPCODE_ADD,
    OPCODE_LD,
    OPCODE_ST,
    OPCODE_JSR,
    OPCODE_AND,
    OPCODE_LDR,
    OPCODE_STR,
    OPCODE_RTI,
    OPCODE_NOT,
    OPCODE_LDI,
    OPCODE_STI,
    OPCODE_JMP,
    OPCODE_RES,
    OPCODE_LEA,
    OPCODE_TRAP
};

enum
{
    FLAG_POS_P = 1 << 0,
    FLAG_ZRO_Z = 1 << 1,
    FLAG_NEG_N = 1 << 2,
};

enum
{
    TRAP_GETC = 0x20,
    TRAP_OUT = 0x21,
    TRAP_PUTS = 0x22,
    TRAP_IN = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT = 0x25
};

uint16_t sign_extend(uint16_t thing, int bit_count)
{
    if ((thing >> (bit_count - 1)) & 1)
    {
        thing |= (0xFFFF << bit_count);
    }

    return thing;
}

void update_flags(uint16_t r)
{
    if (registers[r] == 0)
    {
        registers[REGISTER_COND] = FLAG_ZRO_Z;
    }
    else if (registers[r] >> 15)
    {
        registers[REGISTER_COND] = FLAG_NEG_N;
    }
    else
    {
        registers[REGISTER_COND] = FLAG_POS_P;
    }
}

uint16_t swap16(uint16_t thing)
{
    return ((thing << 8) | (thing >> 8));
}

void image_file_read(FILE * file)
{
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    uint16_t read_max = MEMORY_MAX - origin;
    uint16_t * p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), read_max, file);

    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

int image_read(const char * image_path)
{
    FILE * file = fopen(image_path, "rb");
    if (!file)
    {
        return 0;
    }

    image_file_read(file);

    fclose(file);

    return 1;
}

void memory_write(uint16_t address, uint16_t value)
{
    memory[address] = value;
}

uint16_t check_key()
{
    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return (select(1, &readfds, NULL, NULL, &timeout) != 0);
}

uint16_t memory_read(uint16_t address)
{
    if (address == MEMORY_MAPPED_REGISTER_KEYBOARD_STATUS)
    {
        if (check_key())
        {
            memory[MEMORY_MAPPED_REGISTER_KEYBOARD_STATUS] = (1 << 15);
            memory[MEMORY_MAPPED_REGISTER_KEYBOARD_DATA] = getchar();
        }

        else
        {
            memory[MEMORY_MAPPED_REGISTER_KEYBOARD_STATUS] = 0;
        }
    }

    return memory[address];
}

struct termios original_terminal_io;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_terminal_io);
    struct termios new_terminal_io = original_terminal_io;
    new_terminal_io.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_terminal_io);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal_io);
}

void handle_interrupt()
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

int main(int argc, const char * argv[])
{
    if (argc < 2)
    {
        puts("Usage: lc3 [image-file1] ... \n");
        return EXIT_FAILURE;
    }

    for (int j = 1; j < argc; ++j)
    {
        if (!image_read(argv[j]))  // UNIMPLEMENTED
        {
            printf("%s: %s!\n", "Failed to load image", argv[j]);
        }
    }

    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    registers[REGISTER_COND] = FLAG_ZRO_Z;
    enum { PC_START = 0x3000 };
    registers[REGISTER_PC] = PC_START;

    int running = 1;
    while (running)
    {
        uint16_t instruction = memory_read(registers[REGISTER_PC]++);  // UNIMPLEMENTED
        uint16_t opcode = instruction >> 12;

        switch (opcode)
        {
            case OPCODE_ADD:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t register_1 = (instruction >> 6) & 0x7;
                uint16_t immediate_flag = (instruction >> 5) & 0x1;

                if(immediate_flag)
                {
                    uint16_t immediate_flag_extended = sign_extend(instruction & 0x1F, 5);
                    registers[register_0] = registers[register_1] + immediate_flag_extended;
                }
                else
                {
                    uint16_t register_2 = instruction & 0x7;
                    registers[register_0] = registers[register_1] + registers[register_2];
                }

                update_flags(register_0);
            }
                break;
            case OPCODE_AND:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t register_1 = (instruction >> 6) & 0x7;
                uint16_t immediate_flag = (instruction >> 5) & 0x1;

                if (immediate_flag)
                {
                    uint16_t immediate_flag = sign_extend(instruction & 0x1F, 5);
                    registers[register_0] = registers[register_1] & immediate_flag;
                }
                else
                {
                    uint16_t register_2 = instruction & 0x7;
                    registers[register_0] = registers[register_1] & registers[register_2];
                }

                update_flags(register_0);
            }
                break;
            case OPCODE_NOT:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t register_1 = (instruction >> 6) & 0x7;
                
                registers[register_0] = ~registers[register_1];
                
                update_flags(register_0);
            }
                break;
            case OPCODE_BR:
            {
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                uint16_t condition_flag = (instruction >> 9) & 0x7;

                if (condition_flag & registers[REGISTER_COND])
                {
                    registers[REGISTER_PC] += pc_offset;
                }
            }
                break;
            case OPCODE_JMP:
            {
                uint16_t register_1 = (instruction >> 6) & 0x7;
                registers[REGISTER_PC] = registers[register_1];
            }
                break;
            case OPCODE_JSR:
            {
                uint16_t long_flag = (instruction >> 11) & 1;
                registers[REGISTER_R7] = registers[REGISTER_PC];

                if (long_flag)
                {
                    uint16_t long_pc_offset = sign_extend(instruction & 0x7FF, 11);
                    registers[REGISTER_PC] += long_pc_offset;
                }
                else
                {
                    uint16_t register_1 = (instruction >> 6) & 0x7;
                    registers[REGISTER_PC] = registers[register_1];
                }
            }
                break;
            case OPCODE_LD:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);

                registers[register_0] = memory_read(registers[REGISTER_PC] + pc_offset);

                update_flags(register_0);
            }
                break;
            case OPCODE_LDI:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);

                registers[register_0] = memory_read(memory_read(registers[REGISTER_PC] + pc_offset));

                update_flags(register_0);
            }
                break;
            case OPCODE_LDR:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t register_1 = (instruction >> 6) & 0x7;
                uint16_t offset = sign_extend(instruction & 0x3F, 6);

                registers[register_0] = memory_read(registers[register_1] + offset);

                update_flags(register_0);
            }
                break;
            case OPCODE_LEA:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);

                registers[register_0] = registers[REGISTER_PC] + pc_offset;

                update_flags(register_0);
            }
                break;
            case OPCODE_ST:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);

                memory_write(registers[REGISTER_PC] + pc_offset, registers[register_0]);
            }
                break;
            case OPCODE_STI:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);

                memory_write(memory_read(registers[REGISTER_PC] + pc_offset), registers[register_0]);
            }
                break;
            case OPCODE_STR:
            {
                uint16_t register_0 = (instruction >> 9) & 0x7;
                uint16_t register_1 = (instruction >> 6) & 0x7;
                uint16_t offset = sign_extend(instruction & 0x3F, 6);

                memory_write(registers[register_1] + offset, registers[register_0]);
            }
                break;
            case OPCODE_TRAP:
            {
                switch (instruction & 0xFF)
                {
                    case TRAP_GETC:
                    {
                        registers[REGISTER_R0] = (uint16_t)getchar();
                        update_flags(REGISTER_R0);
                    }
                        break;
                    case TRAP_OUT:
                    {
                        putc((char)registers[REGISTER_R0], stdout);
                        fflush(stdout);
                    }
                        break;
                    case TRAP_IN:
                    {
                        printf("%s", "Enter a character: ");
                        char character = getchar();

                        putc(character, stdout);
                        fflush(stdout);

                        registers[REGISTER_R0] = (uint16_t)character;
                        update_flags(REGISTER_R0);
                    }
                        break;
                    case TRAP_PUTS:
                    {
                        uint16_t * character = memory + registers[REGISTER_R0];

                        while (*character)
                        {
                            putc((char)*character, stdout);
                            ++character;
                        }

                        fflush(stdout);
                    }
                        break;
                    case TRAP_PUTSP:
                    {
                        uint16_t * character = memory + registers[REGISTER_R0];

                        while (*character)
                        {
                            char character1 = (*character) & 0xFF;
                            putc(character1, stdout);

                            char character2 = (*character) >> 8;
                            if (character2) putc(character2, stdout);

                            ++character;
                        }

                        fflush(stdout);
                    }
                        break;
                    case TRAP_HALT:
                    {
                        puts("Machine halted!");
                        fflush(stdout);
                        running = 0;
                    }
                        break;
                }
            }
                break;
            case OPCODE_RES:
            case OPCODE_RTI:
            default:
                puts("Invalid opcode!\n");

                return EXIT_FAILURE;
        }
    }

    restore_input_buffering();

    return EXIT_SUCCESS;
}
