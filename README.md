# Virtual Block Device

## Running
1. Create directories "fusedata" and "mnt"
2. Compile with "make all"
3. "vblkdev -f mnt" will mount the filesystem in the mnt folder
4. When finished, "sudo umount mnt" will unmount the file system

## Filesystem Checker
Run ./csefsck in the project directory