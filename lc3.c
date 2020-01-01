/* 
 * File:   lc3.c
 * Author: Vidreven
 *
 * Created on December 28, 2019, 4:12 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

uint16_t memory[65536];

/* Registers */
enum{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};
uint16_t reg[R_COUNT];

/* Opcodes */
enum{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

/* Flags */
enum{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

enum{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

enum{
    MR_KBSR = 0xfe00, /* keyboard status */
    MR_KBDR = 0xfe02 /* keyboard data */
};

uint16_t sign_extend(uint16_t x, int bit_count){
    if((x >> (bit_count -1)) & 1){
        x |= (0xffff << bit_count);
    }
    return x;
}

void update_flags(uint16_t r){
    if(reg[r] == 0){
        reg[R_COND] = FL_ZRO;
    }
    else if(reg[r] >> 15){ /* a 1 in the left-most bit indicates negative */
        reg[R_COND] = FL_NEG;
    }
    else{
        reg[R_COND] = FL_POS;
    }
}

void mem_write(uint16_t address, uint16_t val){
    memory[address] = val;
}

uint16_t mem_read(uint16_t address){
    if(address == MR_KBSR){
        if(check_key()){
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else{
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

uint16_t check_key(){
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void add(uint16_t instr){
    /* Destination register (DR) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* First operand (SR1) */
    uint16_t r1 = (instr >> 6) & 0x7;
    /* whether we are in immediate mode */
    uint16_t imm_flag = (instr >> 5) & 0x1;
        
    if(imm_flag){
        uint16_t imm5 = sign_extend(instr & 0x1f, 5);
        reg[r0] = reg[r1] + imm5;
    }
    else{
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] + reg[r2];
    }
        
    update_flags(r0);
}

void and(uint16_t instr){
    /* Destination register (DR) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* First operand (SR1) */
    uint16_t r1 = (instr >> 6) & 0x7;
    /* whether we are in immediate mode */
    uint16_t imm_flag = (instr >> 5) & 0x1;
        
    if(imm_flag){
        uint16_t imm5 = sign_extend(instr & 0x1f, 5);
        reg[r0] = reg[r1] & imm5;
    }
    else{
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] & reg[r2];
    }
        
    update_flags(r0);
}

void br(uint16_t instr){
    uint16_t nzp = (instr >> 9) & 0x7;
    /* PCoffset 9*/
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    
    if(reg[R_COND] & nzp){
        reg[R_PC] += + pc_offset;
    }
    //else if((nzp == 0x7) | (nzp == 0)){
    //    reg[R_PC] = reg[R_PC] + pc_offset;
    //}
}

void jmp(uint16_t instr){
    uint16_t r0 = (instr >> 6) & 0x7;
    
    /* RET? */
    if((r0 & 0x7) == 0x7){
        reg[R_PC] = reg[R_R7];
    }
    else{
        reg[R_R7] = reg[R_PC];
        reg[R_PC] = reg[r0];
    }
}

void jsr(uint16_t instr){
    reg[R_R7] = reg[R_PC];
    if(instr[11] == 0){
        uint16_t r_base = (instr >> 6) & 0x7;
        reg[R_PC] = reg[r_base];
    }
    else{
        reg[R_PC] += sign_extend(instr & 0x7ff, 11);
    }
}

void ld(uint16_t instr){
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    reg[dr] = mem_read(reg[R_PC] + pc_offset);
    update_flags(dr);
}

void ldi(uint16_t instr){
    /* Destination register (DR) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* PCoffset 9*/
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
    update_flags(r0);
}

void ldr(uint16_t instr){
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t br = (instr >> 6) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x3f, 6);
    
    reg[dr] = mem_read(reg[br] + pc_offset);
    update_flags(dr);
}

void lea(uint16_t instr){
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    
    reg[dr] = reg[R_PC] + pc_offset;
    update_flags(dr);
}

void not(uint16_t instr){
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t sr = (instr >> 6) & 0x7;
    
    reg[dr] = ~reg[sr];
    update_flags(dr);
}

void st(uint16_t instr){
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    mem_write(reg[R_PC] + pc_offset, reg[sr]);
}

void sti(uint16_t instr){
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    mem_write(mem_read(reg[R_PC] + pc_offset), reg[sr]);
}

void str(uint16_t instr){
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t br = (instr >> 6) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x3f, 6);
    
    mem_write(reg[br] + pc_offset, reg[sr]);
}

void puts(){
    uint16_t* c = memory + reg[R_R0];
    
    while(*c){
        putc((char)* c, stdout);
        ++c;
    }
    fflush(stdout);
}

void getch(){
    reg[R_R0] = (uint16_t)getchar();
}

void out(){
    putc((char)reg[R_R0], stdout);
    fflush(stdout);
}

void in(){
    printf("Enter a character: ");
    char c = getchar();
    putc(c, stdout);
    reg[R_R0] = (uint16_t) c;
}

void putsp(){
    /* one char per byte (two bytes per word)
       here we need to swap back to
       big endian format */
    uint16_t* c = memory + reg[R_R0];
    
    while(*c){
        char char1 = (*c) & 0xff;
        putc(char1, stdout);
        char char2 = (*c) >> 8;
        if(char2) putc(char2, stdout);
        ++c;
    }
    fflush(stdout);
}

void halt(int running){
    puts("HALT");
    fflush(stdout);
    running = 0;
}

void read_image_file(FILE* file){
    /* the origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);
    
    /* we know the maximum file size so we only need one fread */
    uint16_t max_read = 65536 - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);
    
    /* swap to little endian */
    while(read-- > 0){
        *p = swap16(*p);
        ++p;
    }
}

uint16_t swap16(uint16_t x){
    return (x << 8) | (x >> 8);
}

int read_image(const char* image_path){
    FILE* file = fopen(image_path, "rb");
    if(!file) return 0;
    
    read_image_file(file);
    fclose(file);
    
    return 1;
}

/* Input Buffering */
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

/* Handle Interrupt */
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

int main(int argc, char** argv) {
    /* Load Arguments */
    if (argc < 2)
    {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }
    
    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    /* Setup */
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();
    
    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum{
        PC_START = 0x3000
    };
    reg[R_PC] = PC_START;
    
    int running = 1;
    
    while(running){
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        /* 16 bit instructions. First 4 bit are opcode. */
        uint16_t op = instr >> 12;
        
        switch(op){
            case OP_ADD:
                add(instr);
                break;
            case OP_AND:
                and(instr);
                break;
            case OP_BR:
                br(instr);
                break;
            case OP_JMP:
                jmp(instr);
                break;
            case OP_JSR:
                jsr(instr);
                break;
            case OP_LD:
                ld(instr);
                break;
            case OP_LDI:
                ldi(instr);
                break;
            case OP_LDR:
                ldr(instr);
                break;
            case OP_LEA:
                lea(instr);
                break;
            case OP_NOT:
                not(instr);
                break;
            case OP_ST:
                st(instr);
                break;
            case OP_STI:
                sti(instr);
                break;
            case OP_STR:
                str(instr);
                break;
            case OP_TRAP:
                switch(instr & 0xff){
                    case TRAP_GETC:
                        getch();
                        break;
                    case TRAP_OUT:
                        out();
                        break;
                    case TRAP_PUTS:
                        puts();
                        break;
                    case TRAP_IN:
                        in();
                        break;
                    case TRAP_PUTSP:
                        putsp();
                        break;
                    case TRAP_HALT:
                        halt(running);
                        break;
                }
                break;
            case OP_RES:
            case OP_RTI:
            default:
                /* BAD OPCODE */
                abort();
                break;
        }
    }
    /* Shutdown */
    restore_input_buffering();
}

