#!/bin/sh
# NativeLink worker entrypoint: every remote action runs as
#   worker_action.sh <command> <args...>
# Commands sent from Windows hosts arrive without the executable bit, and the
# only remote commands are bash scripts (clang_remote_wrapper), so run them
# through bash.
exec /bin/bash "$@"
