/* runtime16.h - host surface the lifted OutNumbered! code calls into. */
#ifndef SSO_RUNTIME16_H
#define SSO_RUNTIME16_H
#include "cpu.h"

void    int_handler(CPU *cpu, int vec);
void    dos_int21(CPU *cpu);
void    bios_int10(CPU *cpu);
void    bios_int16(CPU *cpu);
void    mouse_int33(CPU *cpu);
uint8_t port_in8(CPU *cpu, uint16_t port);
void    port_out8(CPU *cpu, uint16_t port, uint8_t val);
uint16_t port_in16(CPU *cpu, uint16_t port);
void    port_out16(CPU *cpu, uint16_t port, uint16_t val);

int  recomp_dispatch(CPU *cpu, unsigned seg, unsigned off);   /* generated */
void dispatch_near(CPU *cpu, unsigned seg, unsigned off);
void dispatch_far(CPU *cpu, unsigned seg, unsigned off);
void recomp_dump_misses(const char *path);

void recomp_div0(const char *kind);

/* boot */
int  sso_load_image(CPU *cpu, const char *exe_path);
void sso_set_game_dir(const char *dir);
void sso_run(CPU *cpu);                 /* runs the startup; returns at exit */
extern int sso_exit_code;
extern volatile unsigned long g_stat_isrs, g_stat_calls;   /* timer ISRs run / recomp_tick calls so far */
void sso_dump_frame(CPU *cpu, const char *ppm_path);

/* sound device the game is told to use: 0 = PC speaker, 1 = Tandy 3-voice (SN76496) */
extern int sso_sound_device;
/* guest speed cap in millions of instructions per second (0 = unlimited) */
extern unsigned sso_mhz;
uint8_t sso_sound_machine_byte(CPU *cpu);   /* [4C62h] as seen by the sound bank choice and driver init */
#endif
