#pragma once
#include <stddef.h>
#include <stdint.h>
#define RIX_TTY_COUNT 4
#define RIX_TTY_INPUT 4096
typedef struct {uint8_t input[RIX_TTY_INPUT];size_t head,tail,count;uint8_t canonical,echo;uint32_t foreground_pgrp;} rix_tty_t;
void tty_init(void);
rix_tty_t *tty_get(unsigned id);
int tty_input(unsigned id,uint8_t ch);
int tty_read(unsigned id,void *buf,size_t n,size_t *out);
int tty_set_canonical(unsigned id,int enabled);
int tty_set_echo(unsigned id,int enabled);
