# -*- bazel-starlark -*-
"""Siso backend config for Zitter's LAN NativeLink cluster.

Copied to build/config/siso/backend_config/backend.star by
tools/remote_build/setup_siso.py.
"""

load("@builtin//struct.star", "module")

def __platform_properties(ctx):
    linux = {
        "OSFamily": "Linux",
        "container-image": "docker://zitter-remote-build",
    }
    return {
        "default": linux,
        "large": linux,
    }

backend = module(
    "backend",
    platform_properties = __platform_properties,
)
