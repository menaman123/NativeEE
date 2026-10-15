"""Reviewed DT-12 model counter catalog, not candidate-supplied zero aliases.

Each entry is a narrow executed operation. It does NOT assert retirement of an
entire model. Add new counters here only after reviewing their increment and
run.meta publication sites. Wrapper fallback/error/unsupported counters are not
execution counters and deliberately do not qualify.
"""
import json
from pathlib import Path

# A title may bind this reviewed core counting primitive to its own entry and
# atomic cell. Its descriptor's source/increment then identifies that binding;
# the verifier also requires the actual increment/delegation implementation.
# Catalog data cannot supply arbitrary core paths or define a new provider.
COUNT_PROVIDERS = {
    "DispatchTable.CountCalls": {
        "source": "engine/ee/cpu/dispatch_table.cpp",
        "needles": (
            "DispatchTable::Body DispatchTable::CountCalls(",
            "counter->fetch_add(1, std::memory_order_relaxed);",
            "original(ctx, entry);",
        ),
    },
}

COUNTERS = {}
for command in ("bclr", "idec", "bdec", "vdec", "fdec", "setiq", "setvq", "csc", "pack", "setth", "unknown"):
    COUNTERS["IPU.command." + command] = {
        "field": "ipu_commands", "member": command,
        "scope": "IPU command dispatch: " + command,
        "source": "engine/ipu/ipu_device.cpp",
        "increment": "++command_counts_[cmd];",
        "publisher": "app/host/subsystem_census.cpp",
        "publish": 'meta.Set("ipu_commands", commands);',
    }
for operation in ("init", "sync", "stop", "restart"):
    COUNTERS["IPU.operation." + operation] = {
        "field": "ipu_operations", "member": operation,
        "scope": "actual libipu model entry: " + operation,
        "source": "engine/ipu/hle/libipu_api.cpp",
        "increment": "++operations_." + operation + ";",
        "publisher": "app/host/subsystem_census.cpp",
        "publish": 'meta.Set("ipu_operations", ipu_operations);',
    }
COUNTERS["SPU.movie_input.pop"] = {
    "field": "movie_audio", "member": "model_pops",
    "scope": "actual retained Movie PCM input consumption attempt",
    "source": "engine/sound/spu_core.cpp",
    "increment": "++fmv_model_pops_;",
    "publisher": "app/host/movie_sound.cpp",
    "publish": 'meta.Set("movie_audio", "selected=" + std::to_string(selected_) +',
}
COUNTERS["GS.syncv_callback"] = {
    "field": "libgraph_calls", "member": "syncvcb",
    "scope": "actual LibgraphBindings SyncVCallback registration/unregistration",
    "source": "engine/gs/hle/sce/libgraph_bindings.cpp",
    "increment": "++syncv_callback_calls_;",
    "publisher": "app/host/ee_bootstrap.cpp",
    "publish": "ps2r::gs::LibgraphBindings::SyncVCallbackCalls()",
}
for direction in ("from", "to"):
    for mode in ("normal", "chain", "interleave", "reserved"):
        member = direction + "_" + mode
        COUNTERS["DMA.SPR." + member] = {
            "field": "dma_spr", "member": member,
            "scope": "scratchpad DMA starts: " + member,
            "source": "engine/dmac/dmac_spr_transfer.cpp",
            "increment": "++count.starts;",
            "publisher": "app/host/subsystem_census.cpp",
            "publish": 'meta.Set("dma_spr", text);',
        }
# The IOP kernel model under the title's streaming module (M11 service-only
# startup retires it in play: hle.startup=native never boots the IOP; the
# deterministic profile's retained arm keeps it for the oracle legs). The
# module's own RPC-server call counter is the title's (stream_rpc_calls, published
# by the root next to these two).
COUNTERS["IOP.kernel.timer_beat"] = {
    "field": "iop_hb_beats",
    "scope": "actual IOP kernel periodic hard-timer beat (the streaming module's heartbeat)",
    "source": "engine/iop/iop_kernel.cpp",
    "increment": "timer_beats_++;",
    "publisher": "app/host/ee_bootstrap_census.cpp",
    "publish": 'meta->Set("iop_hb_beats", std::to_string(iop_kernel.TimerBeats()));',
}
COUNTERS["IOP.libsd.call"] = {
    "field": "libsd_calls",
    "scope": "actual libsd IOP module entry called by the recompiled streaming module (LibsdModule::Call, per ordinal)",
    "source": "engine/iop/hle/libsd/libsd_module.cpp",
    "increment": "calls_[ordinal]++;",
    "publisher": "app/host/ee_bootstrap_census.cpp",
    "publish": 'meta->Set("libsd_calls", stream_irx.Libsd().CallsCensus());',
}
for member in ("priority_calls", "delay_kernel", "delay_host"):
    COUNTERS["IOP.thread." + member] = {
        "field": "iop_thread_ops", "member": member,
        "scope": "IOP Thbase operation: " + member,
        "source": "engine/iop/hle/thbase/thbase_module.cpp",
        "increment": member + "_.fetch_add(1, std::memory_order_relaxed);",
        "publisher": "app/host/subsystem_census.cpp",
        "publish": 'meta.Set("iop_thread_ops", text);',
    }


def _unique(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("duplicate reviewed counter member: " + key)
        result[key] = value
    return result


def load_title_catalogs(root, counters):
    """Load reviewed repository data, never a candidate-selected catalog."""
    result = dict(counters)
    for path in sorted((root / "games").glob("*/verify/retirement_counters.json")):
        if path.is_symlink() or path.resolve() != path.absolute():
            raise ValueError("reviewed counter catalog must be an owned regular path")
        catalog = json.loads(path.read_text(), object_pairs_hook=_unique)
        if set(catalog) != {"schema", "counters"} or catalog["schema"] != "dt12-reviewed-counters-v1":
            raise ValueError("invalid reviewed counter catalog schema")
        entries = catalog["counters"]
        if not isinstance(entries, dict) or not entries:
            raise ValueError("empty/invalid reviewed counter catalog")
        title = path.parent.parent.relative_to(root)
        required = {"field", "scope", "source", "increment", "publisher", "publish"}
        for name, descriptor in entries.items():
            if not name or name in result:
                raise ValueError("duplicate/invalid reviewed counter: " + name)
            if not isinstance(descriptor, dict) or not required <= descriptor.keys() or \
                    descriptor.keys() - (required | {"member", "provider"}):
                raise ValueError("invalid reviewed counter descriptor: " + name)
            if any(not isinstance(value, str) or not value.strip() or value != value.strip()
                   for value in descriptor.values()):
                raise ValueError("empty/invalid reviewed counter field: " + name)
            if "provider" in descriptor and descriptor["provider"] not in COUNT_PROVIDERS:
                raise ValueError("unreviewed counter provider: " + name)
            for key in ("source", "publisher"):
                relative = Path(descriptor[key])
                if relative.is_absolute() or ".." in relative.parts or not relative.is_relative_to(title):
                    raise ValueError("counter source outside its title: " + name)
            result[name] = dict(descriptor, catalog=str(path.relative_to(root)))
    return result


COUNTERS = load_title_catalogs(Path(__file__).resolve().parent.parent, COUNTERS)
