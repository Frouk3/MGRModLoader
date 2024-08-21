# Plans
- [ ] Figure out how to load files without packing .dat/.dtt
- [ ] Fix Mod Loader not loading cpk properly and create binder for each cpk
- [ ] Read every cpk and bind them right away
- [ ] Add RMM compatability
# "Fuck around and find out"
As [Aura](https://github.com/Aura39) said, to add or replace files of the dat file we need to read dat file at once, add entries, and rebuild the dat

RMM Compatability - Find out how to RMM handles the ini files and operates with them and do the same IF the ini file exists, if not, return the loading behavior to default

Probably should add directories to the profiles

Hw::cHeap probably replace memory allocation function