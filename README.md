<div align=center>
<h1>rtjn-kernel2</h1>
<p ><b>BIOS/Legacy kernel</b></p>
<img width="712" height="403" alt="image" src="https://github.com/user-attachments/assets/fac6ffa0-274a-4efd-9b8e-89f0accaa2b5" />
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
AUR:
```sh
yay -S i686-elf-gcc i686-elf-binutils
```
Or
```sh
paru -S i686-elf-gcc i686-elf-binutils
```
## Build
just build:
```sh
make
```
build and run:
```sh
make run
```
