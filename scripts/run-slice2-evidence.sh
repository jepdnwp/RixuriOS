#!/bin/bash
# Slice-2 evidence driver (survives via Start-Process on Windows side):
#   1) second qemu_sched_test.py confirmation  -> build/sched-slice2-run2.log
#   2) full 27-suite matrix                    -> build/test-logs/all-tests-*.log
cd /mnt/c/Users/vey/Desktop/RixuriOS || exit 99
export CROSS=x86_64-linux-gnu- HOST_CC=gcc
echo "DRIVER: sched run 2 starting at $(date -u)"
python3 ./scripts/qemu_sched_test.py > build/sched-slice2-run2.log 2>&1
echo "DRIVER: sched run 2 rc=$?" >> build/sched-slice2-run2.log
echo "DRIVER: full suite starting at $(date -u)"
bash ./scripts/run-all-tests.sh > build/slice2-full.log 2>&1
rc=$?
echo "DRIVER: full suite rc=${rc}" >> build/slice2-full.log
echo "DRIVER: all done at $(date -u)"
exit 0