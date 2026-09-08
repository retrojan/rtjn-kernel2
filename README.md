<div align=center>
<h1>rtjn-kernel2</h1>
<p ><b>BIOS/Legacy kernel</b></p>
<img width="702" height="388" alt="image" src="https://github.com/user-attachments/assets/a1a77cdf-edff-4180-9e4d-ff42a2d04f52" />
</div>


> [!WARNING]
> **This kernel is BIOS/Legacy ONLY and WILL NOT boot on UEFI systems without CSM**

## Known issues (all working in QEMU)
- shutdown on real pc
- file system on real pc
- ethernet on real pc

## Pkgs
### Arch Linux
pacman:
```sh
sudo pacman -S base-devel nasm qemu-system-x86 e2fsprogs
```
yay (AUR):
```sh
yay -S i686-elf-gcc i686-elf-binutils
```
Or use paru

paru (AUR):
```sh
paru -S i686-elf-gcc i686-elf-binutils
```
