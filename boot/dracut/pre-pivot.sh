#!/bin/sh
# dracut sources hooks; isolate the shared engine in a child so exit never exits init.
/usr/lib/ubuntu-reset/restore.sh /sysroot
