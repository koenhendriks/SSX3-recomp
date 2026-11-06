/* Host register access in a signal ucontext, Linux and macOS (arm64 / x86-64).
 * Linux keeps the machine context inline (uc_mcontext.pc / .regs[]); Darwin
 * points at it (uc_mcontext->__ss.__pc / __x[0..28], __fp, __lr). */
#ifndef UC_REGS_H
#define UC_REGS_H
#include <stdint.h>
#include <signal.h>
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

#if defined(__aarch64__)
#if defined(__APPLE__)
#define UC_PC(uc) (*(uint64_t *)&(uc)->uc_mcontext->__ss.__pc)
static inline uint64_t *uc_xreg(ucontext_t *uc, int r)
{
    if (r < 29) return (uint64_t *)&uc->uc_mcontext->__ss.__x[r];
    if (r == 29) return (uint64_t *)&uc->uc_mcontext->__ss.__fp;
    return (uint64_t *)&uc->uc_mcontext->__ss.__lr;
}
#else
#define UC_PC(uc) ((uc)->uc_mcontext.pc)
static inline uint64_t *uc_xreg(ucontext_t *uc, int r)
{
    return (uint64_t *)&uc->uc_mcontext.regs[r];
}
#endif
#define UC_XREG(uc, r) (*uc_xreg((uc), (r)))
#elif defined(__x86_64__)
#if defined(__APPLE__)
#define UC_PC(uc) ((uc)->uc_mcontext->__ss.__rip)
#else
#define UC_PC(uc) ((uc)->uc_mcontext.gregs[REG_RIP])
#endif
#endif

#endif
