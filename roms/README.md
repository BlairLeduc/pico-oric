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
