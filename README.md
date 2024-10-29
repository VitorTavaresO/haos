# HAOS - Hypotetical Architecure Operating System

 - This is a speculative operating system under development for the Hypothetical Architecture devised by Professor Eduardo Molina Cruz within the framework of the Computer Architecture course.
 - The aforementioned architecture is identical to that employed in the HASS (Hypothetical Architecture Simulator System) simulator.


# Dependencies

 **Windows**
 -  NCURSES on MSYS2 using UCRT
```
pacman -S mingw-w64-ucrt-x86_64-ncurses
```
 **Linux**
 - Using APT
```
sudo apt install build-essential libncurses-dev git
```

**For Both**
 -  My Lib of Professor Eduardo.
```
git clone https://github.com/ehmcruz/my-lib.git
```

Directorie structure need to be:

--project-folder

 ----my-lib

 ----haos

**Compile**
```
cd haos
```
- Use the make for Windows or Linux based on your machine
```
make CONFIG_TARGET_WINDOWS=1
```
OR
```
make CONFIG_TARGET_LINUX=1
```


 

