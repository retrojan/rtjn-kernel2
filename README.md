<div align=center>
<h1>rtjn-kernel2</h1>
<p ><b>BIOS/Legacy kernel</b></p>
<img width="700" height="412" alt="image" src="https://github.com/user-attachments/assets/ab8f405d-2397-4b2a-8400-5e3d0a3ed46f" />


</div>


> [!WARNING]
> **BIOS/Legacy only (QEMU). Won't boot on UEFI without CSM.**

## Real pc issues
- shutdown
- file system
- ethernet

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
