<div align=center>
<h1>rtjn-kernel2</h1>
<p ><b>BIOS/Legacy kernel</b></p>
<img width="717" height="522" alt="image" src="https://github.com/user-attachments/assets/daffeef8-35a2-4782-b078-e53ddb25b26d" />


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
