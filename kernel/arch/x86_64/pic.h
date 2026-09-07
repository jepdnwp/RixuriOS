#pragma once
int pic_init(void);
void pic_disable(void);
void pic_eoi(unsigned irq);
int pic_active(void);
