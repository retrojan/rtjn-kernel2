<div align=center>
<h1>rtjn-kernel2</h1>
<p ><b>BIOS/Legacy kernel</b></p>
<img width="875" height="610" alt="image" src="https://github.com/user-attachments/assets/c62845da-4c5b-49c1-9ebb-3790929e1ae4" />


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
