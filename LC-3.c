#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>

/* 内存映射寄存器 */
enum {
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};

/* LC-3 预定义的函数 Trap Routines*/
enum {
    TRAP_GETC = 0x20,
    TRAP_OUT = 0x21,
    TRAP_PUTS = 0x22,
    TRAP_IN = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT = 0x25
};

/* 寄存器 */
enum {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND, /* program counter */
    R_COUNT
};

/* 精简指令集 */
enum {
    OP_BR = 0,  /* branch */
    OP_ADD,     /* add */
    OP_LD,      /* load */
    OP_ST,      /* store */
    OP_JSR,     /* jump register */
    OP_AND,     /* bitwise and */
    OP_LDR,     /* load register */
    OP_STR,     /* store register */
    OP_RTI,     /* unused */
    OP_NOT,     /* bitwise not */
    OP_LDI,     /* load indirect */
    OP_STI,     /* store indirect */
    OP_JMP,     /* jump */
    OP_RES,     /* reserved(unused) */
    OP_LEA,     /* load effective address */
    OP_TRAP     /* execute trap */
};

/* 条件标志位 */
enum {
    FL_POS = 1 << 0,    /* P */
    FL_ZRO = 1 << 1,    /* Z */
    FL_NEG = 1 << 2     /* N */
};

/* 内存 */
#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];

/* 寄存器 */
uint16_t reg[R_COUNT];

// windows
#if _WIN32
    #include <windows.h>
    #include <conio.h>
    HANDLE hStdin = INVALID_HANDLE_VALUE;
    DWORD fdwMode, fdwOldMode;

    void disable_input_buffering()
    {
        hStdin = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(hStdin, &fdwOldMode);    // save old mode
        fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT
            ^ ENABLE_LINE_INPUT;
        SetConsoleMode(hStdin, fdwMode);
        FlushConsoleInputBuffer(hStdin);
    }

    void restore_input_buffering()
    {
        SetConsoleMode(hStdin, fdwOldMode);
    }

    uint16_t check_key()
    {
        return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 
            && _kbhit();
    }

// linux
#elif __linux__
    #include <termios.h>
    #include <unistd.h>
    #include <sys/select.h>
    struct termios original_tio;

    void disable_input_buffering()
    {
        tcgetattr(STDIN_FILENO, &original_tio);
        struct termios new_tio = original_tio;
        new_tio.c_lflag &= ~ICANON & ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    }

    void restore_input_buffering()
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
    }

    uint16_t check_key()
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        return select(1, &readfds, NULL, NULL, &timeout) != 0;
    }
#endif

/* 结束后复原 */
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(2);
}

/* 有符号扩展 */
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) 
    {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

/* 大端转小端 */
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

/* 更新标记 */
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15)
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

/* 将LC-3程序加载到内存 */
void read_image_file(FILE* file)
{
    // origin 声明程序从哪个内存开始
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // 转为小端
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

/* 通过字符加载 */
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}

/* 内存映射寄存器 */
void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

/* main loop */
int main(int argc, const char* argv[]) 
{
    // 加载程序
    if (argc < 2) 
    {
        // 显示使用字符串
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; j++) 
    {
        if (!read_image(argv[j])) 
        {
            printf("failed to read image: %s\n", argv[j]);
            exit(1);
        }
    }
    // 平台设置
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    // 设置默认标志 
    reg[R_COND] = FL_ZRO;

    // 开始位置
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        // FETCH 获取
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
        case OP_ADD:
        {
            uint16_t r0 = (instr >> 9) & 0x7;       // 输出的位置 (DR)
            uint16_t r1 = (instr >> 6) & 0x7;       // 第一个值 (SR1)
            uint16_t imm_flag = (instr >> 5) & 0x1; // 是否立即模式

            if (imm_flag)
            {
                uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                reg[r0] = reg[r1] + imm5;
            }
            else
            {
                uint16_t r2 = instr & 0x7;
                reg[r0] = reg[r1] + reg[r2];
            }

            update_flags(r0);
        } break;
        case OP_AND:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t r1 = (instr >> 6) & 0x7; // 第一个值 (SR1)
            uint16_t imm_flag = (instr >> 5) & 0x1; // 是否立即模式

            if (imm_flag)
            {
                uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                reg[r0] = reg[r1] & imm5;
            }
            else
            {
                uint16_t r2 = instr & 0x7;
                reg[r0] = reg[r1] & reg[r2];
            }

            update_flags(r0);
        } break;
        case OP_NOT:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t r1 = (instr >> 6) & 0x7; // 第一个值 (SR1)

            reg[r0] = ~reg[r1];
            update_flags(r0);
        } break;
        case OP_BR:
        {
            uint16_t pc_offset = sign_extend(instr & 0x1ff, 9); // PCoffset 9
            uint16_t cond_flag = (instr >> 9) & 0x7; // Cond 位置

            if (cond_flag & reg[R_COND])
            {
                reg[R_PC] += pc_offset;
            }
        } break;
        case OP_JMP:
        {
            // RET 一样
            uint16_t r1 = (instr >> 6) & 0x7; // 第一个值 (SR1)
            reg[R_PC] = reg[r1];
        } break;
        case OP_JSR:
        {
            uint16_t r1 = (instr >> 6) & 0x7; // 第一个值 (SR1)
            uint16_t long_pc_offset = sign_extend(instr& 0x7ff, 11); // PCoffset 11
            uint16_t long_flag = (instr >> 11) & 1; // long 位置

            reg[R_R7] = reg[R_PC];
            if (long_flag)
            {
                reg[R_PC] += long_pc_offset;  // JSR
            }
            else
            {
                reg[R_PC] = reg[r1];    // JSRR
            }
        } break;
        case OP_LD:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t pc_offset = sign_extend(instr & 0x1ff, 9); // PCoffset 9

            reg[r0] = mem_read(reg[R_PC] + pc_offset);
            update_flags(r0);
        } break;
        case OP_LDI:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t pc_offset = sign_extend(instr & 0x1ff, 9); // PCoffset 9

            // 把 pc_offset 加到 pc ，查看那个内存指向的位置获取最终值
            reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
            update_flags(r0);
        } break;
        case OP_LDR:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t r1 = (instr >> 6) & 0x7; // 第一个值 (SR1)
            uint16_t offset = sign_extend(instr & 0x3F, 6); // PCoffset 6

            reg[r0] = mem_read(reg[r1] + offset);
            update_flags(r0);
        } break;
        case OP_LEA:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t pc_offset = sign_extend(instr & 0x1ff, 9); // PCoffset 9

            reg[r0] = reg[R_PC] + pc_offset;
            update_flags(r0);
        } break;
        case OP_ST:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t pc_offset = sign_extend(instr & 0x1ff, 9); // PCoffset 9

            mem_write(reg[R_PC] + pc_offset, reg[r0]);
        } break;
        case OP_STI:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t pc_offset = sign_extend(instr & 0x1ff, 9); // PCoffset 9

            mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
        } break;
        case OP_STR:
        {
            uint16_t r0 = (instr >> 9) & 0x7; // 输出的位置 (DR)
            uint16_t r1 = (instr >> 6) & 0x7; // 第一个值 (SR1)
            uint16_t offset = sign_extend(instr & 0x3F, 6); // PCoffset 6

            mem_write(reg[r1] + offset, reg[r0]);
        } break;
        case OP_TRAP:
        {
            reg[R_R7] = reg[R_PC];

            switch (instr & 0xFF)
            {
            case TRAP_GETC:
            {
                // 读取一个ASCII字符
                reg[R_R0] = (uint16_t)getchar();
                update_flags(R_R0);
            } break;
            case TRAP_OUT:
            {
                putc((char)reg[R_R0], stdout);
                fflush(stdout);
            } break;
            case TRAP_PUTS:
            {
                // 一个一个字符打印
                uint16_t* c = memory + reg[R_R0];
                while (*c)
                {
                    putc((char)*c, stdout);
                    ++c;
                }
                fflush(stdout);
            } break;
            case TRAP_IN:
            {
                printf("Enter a character: ");
                char c = getchar();
                putc(c, stdout);
                fflush(stdout);
                reg[R_R0] = (uint16_t)c;
                update_flags(R_R0);
            } break;
            case TRAP_PUTSP:
            {
                // 一个字符一个字符打印，需要转换为大端
                uint16_t* c = memory + reg[R_R0];
                while (*c)
                {
                    char char1 = (*c) & 0xFF;
                    putc(char1, stdout);
                    char char2 = (*c) >> 8;
                    if (char2) putc(char2, stdout);
                    ++c;
                }
                fflush(stdout);
            } break;
            case TRAP_HALT:
            {
                puts("HALT");
                fflush(stdout);
                running = 0;
            } break;
            }
        } break;
        case OP_RES:
        case OP_RTI:
        default:
            abort();
            break;
        }
    }
    
    restore_input_buffering();
    return 0;
}