# Plans
- [ ] Figure out how to load files without packing .dat/.dtt
- [x] Fix Mod Loader not loading cpk properly and create binder for each cpk
- [x] Read every cpk and bind them right away
- [x] Add RMM compatability(mod.ini is available, I'm not going to make special enumeration where you can select from which directory files should be loaded)
# "Fuck around and find out"
As [Aura](https://github.com/Aura39) said, to add or replace files of the dat file we need to read dat file at once, add entries, and rebuild the dat

Probably should add directories to the profiles

Hw::cHeap probably replace memory allocation function