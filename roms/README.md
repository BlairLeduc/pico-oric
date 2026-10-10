# roms/

A staging area for ROM images on the workstation. **Everything in here except
this file is gitignored**, because the Oric's ROMs are copyrighted and this
project does not redistribute them. See [“ROM images”](../README.md#rom-images)
for the files, their hashes and where they go.

The emulator loads ROMs from the SD card at `/oric/roms/` (design document
§10.2). The host tests look for them here, by SHA-1, and skip the tests that
need a ROM that is absent:

```sh
shasum roms/*.rom                      # check against README.md before trusting
cp roms/{basic10,basic11b,microdis}.rom /Volumes/PICOCALC/oric/roms/
```

`test_disc` (M14) also looks here for the Sedoric 3.006 distribution disc,
Ray McLaughlin's of 1996 as TOSEC has it, under any name ending `.dsk`, by
SHA-1 `4cc7c19ecf04300e49fb0b84ce64a2899632b3ab`; without it the Sedoric
cases skip. `tools/fetch-corpus.sh` fetches it with the rest of the archive:

```sh
cp out/corpus/dsk/Sedoric3*.dsk roms/sedoric3.dsk
```
