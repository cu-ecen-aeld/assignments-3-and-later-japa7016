# Faulty Driver Crash in QEMU

After booting through QEMU and loading the `faulty` driver, the command:
```bash
echo "hello_world" > /dev/faulty
```
triggered a kernel crash and subsequent reboot. The kernel log showed a NULL pointer dereference at virtual address 0000000000000000, which is not allowed. Here is the relevant output:

```bash
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b44000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 152 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
...
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 ...
```
From the call trace, I saw that the final function before the crash is faulty_write. The relevant code from the driver:

```bash
 ssize_t faulty_write(struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
    /* make a simple fault by dereferencing a NULL pointer */
    *(int *)0 = 0;
    return 0;
}
```
Because the driver writes to *(int *)0, it attempts to store a value at the NULL address 0x0, causing the kernel to throw a NULL pointer dereference exception. That leads to the crash and subsequent reboot.
