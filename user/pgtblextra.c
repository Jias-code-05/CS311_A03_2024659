#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "user/user.h"

static void
check(int ok, char *what)
{
  if (!ok) {
    printf("pgtblextra: FAIL %s\n", what);
    exit(1);
  }
}

int
main(int argc, char **argv)
{
  uchar bits[512];
  uint64 ns, nr;
  char *raw = sbrk(4097 * PGSIZE);
  check(raw != SBRK_ERROR, "allocate scan range");
  volatile char *buf = (char *)PGROUNDUP((uint64)raw);

  check(pgaccess((void *)buf, 4096, bits) == 0, "4096-page limit");
  buf[0] = 1;
  buf[31 * PGSIZE] = 1;
  buf[32 * PGSIZE] = 1;
  buf[255 * PGSIZE] = 1;
  buf[4095 * PGSIZE] = 1;
  check(pgaccess((void *)buf, 4096, bits) == 0, "read bitmap");
  for (int i = 0; i < 4096; i++)
    check(((bits[i / 8] >> (i % 8)) & 1) ==
          (i == 0 || i == 31 || i == 32 || i == 255 || i == 4095),
          "bitmap boundary bits");
  check(pgaccess((void *)buf, 4096, bits) == 0, "second scan");
  for (int i = 0; i < sizeof(bits); i++)
    check(bits[i] == 0, "access bits cleared");

  check(pgaccess((void *)buf, -1, bits) == -1, "negative count");
  check(pgaccess((void *)buf, 0, bits) == -1, "zero count");
  check(pgaccess((void *)buf, 4097, bits) == -1, "excess count");
  check(pgaccess((void *)MAXVA, 1, bits) == -1, "MAXVA");
  check(pgaccess((void *)~0UL, 2, bits) == -1, "address overflow");
  check(pgaccess((void *)(MAXVA - PGSIZE), 2, bits) == -1,
        "range beyond MAXVA");
  check(pgaccess((void *)TRAPFRAME, 1, bits) == -1, "supervisor page");
  check(pgaccess(sbrk(0), 1, bits) == -1, "unmapped source");
  check(pgaccess((void *)buf, 1, (void *)USYSCALL) == -1,
        "read-only output");
  check(pgaccess((void *)buf, 1, (void *)0) == -1, "text output");
  check(pgaccess((void *)buf, 1, (void *)MAXVA) == -1, "invalid output");
  check(pgaccess((void *)buf, 32, (void *)(MAXVA - 1)) == -1,
        "output range overflow");

  // A failed copy must not consume access history or write a partial bitmap.
  char *end = sbrk(0);
  end[-1] = 0x55;
  buf[0] = 2;
  check(pgaccess((void *)buf, 32, end - 1) == -1, "unmapped output tail");
  check(end[-1] == 0x55, "output unchanged on failure");
  check(pgaccess((void *)buf, 1, bits) == 0 && (bits[0] & 1),
        "history retained on failure");
  buf[7] = 3;
  check(pgaccess((void *)(buf + 7), 1, bits) == 0 && bits[0] == 1,
        "unaligned starting address");

  // The bitmap may span two mapped pages, with its unused high bits zero.
  char *out = (char *)buf + 3 * PGSIZE - 1;
  check(pgaccess((void *)buf, 9, out) == 0, "cross-page output");
  check((out[1] & 0xfe) == 0, "unused mask bits");
  check(sbrk(-4097 * PGSIZE) != SBRK_ERROR, "release scan range");

  check(ugetpid() == getpid(), "shared PID");
  pte_t pte = pgpte((void *)USYSCALL);
  check((pte & (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U)) ==
        (PTE_V | PTE_R | PTE_U), "shared-page permissions");
  check(ksupernpte(&ns, &nr) == 0, "kernel mapping counts");
  printf("kernel mappings: %lu superpages, %lu ordinary leaves\n", ns, nr);
  printf("pgtblextra: all tests succeeded\n");
  exit(0);
}
