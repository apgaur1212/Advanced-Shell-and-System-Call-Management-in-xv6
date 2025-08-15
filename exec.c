#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#define MAX_HISTORY 16

struct history_entry {
    int pid;
    char name[16];
    uint memory_usage;
};

struct history {
    struct history_entry entries[MAX_HISTORY];
    int count;
};

struct history cmd_history = { .count = 0 };

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  if (!(ip->mode & 0x4)) { // Check execute permission
    iunlock(ip);
    end_op();
    cprintf("Operation execute failed\n");
    return -1;
  }

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  uint memory_usage = curproc->sz;


if ((path[0] == 's' && path[1] == 'h' && path[2] == '\0') ||
(path[0] == '/' && path[1] == 'i' && path[2] == 'n' && path[3] == 'i' && path[4] == 't' && path[5] == '\0')) { 
} else {
// Update history.

if (cmd_history.count < MAX_HISTORY) {
  int i = cmd_history.count;  // Append at the end
  cmd_history.entries[i].pid = curproc->pid;
  safestrcpy(cmd_history.entries[i].name, path, sizeof(cmd_history.entries[i].name));
  // safestrcpy(cmd_history.entries[i].name, last, sizeof(cmd_history.entries[i].name));
  cmd_history.entries[i].memory_usage = memory_usage;
  cmd_history.count++;
} else {
  int j;
  for (j = 0; j < MAX_HISTORY - 1; j++) {
      cmd_history.entries[j] = cmd_history.entries[j+1];
  }
  int i = MAX_HISTORY - 1;
  cmd_history.entries[i].pid = curproc->pid;
  safestrcpy(cmd_history.entries[i].name, path, sizeof(cmd_history.entries[i].name));
  cmd_history.entries[i].memory_usage = memory_usage;
}
}
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
