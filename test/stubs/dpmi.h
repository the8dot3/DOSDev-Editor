/* Test stand-in: DJGPP's __dpmi_regs and the calls DE uses from <dpmi.h>. */
typedef union {
  struct { unsigned long edi, esi, ebp, res, ebx, edx, ecx, eax; } d;
  struct { unsigned short di, di_hi, si, si_hi, bp, bp_hi, res, res_hi, bx, bx_hi, dx, dx_hi,
           cx, cx_hi, ax, ax_hi, flags, es, ds, fs, gs, ip, cs, sp, ss; } x;
  struct { unsigned char edi[4], esi[4], ebp[4], res[4], bl, bh, ebx_b2, ebx_b3, dl, dh, edx_b2,
           edx_b3, cl, ch, ecx_b2, ecx_b3, al, ah, eax_b2, eax_b3; } h;
} __dpmi_regs;
int __dpmi_int(int _vector, __dpmi_regs *_regs);
void __dpmi_yield(void);
