import sst
import os
import argparse
import json
from datetime import datetime

# ======================================================================================================================
# Parse Command Line Arguments
# ======================================================================================================================

parser = argparse.ArgumentParser(
    prog=f'sst {os.path.basename(__file__)} --',
    usage=f'sst {os.path.basename(__file__)} -- [-h] -r [NRANKS] [-t [TRACERANK]] [-o [OUT_DIR]] [-c CONFIG] [-i INDEX] [binary] -- [args ...]',
    description='SST Simulation with configurable cache parameters.'
)
parser.add_argument('-r', '--nranks', nargs='?', type=int, help='Number of MPI ranks', default=1)
parser.add_argument('-t', '--tracerank', nargs='?', type=int, help='MPI rank to run the tracer on', default=0)
parser.add_argument('-o', '--out-dir', nargs='?', help='Output directory for tracer output files', default="")
parser.add_argument('-c', '--config-file', help='Path to JSON configuration file')
parser.add_argument('-i', '--config-index', type=int, help='Index of configuration to use from JSON file', default=0)
parser.add_argument('binary', type=str, help='Path to the MPI binary to run')
parser.add_argument('args', nargs='*', help='Arguments to pass to the MPI binary', default=[])
args = parser.parse_args()

print(args)

# ======================================================================================================================
# Load json configuration
# ======================================================================================================================

def load_cache_config(config_file, config_index):
    if config_file is None or config_file == "":
        return None

    if not os.path.isfile(config_file):
        raise ValueError(f"Configuration file not found: {config_file}")

    with open(config_file, 'r') as f:
        config_data = json.load(f)

    if 'runs' not in config_data:
        raise ValueError("JSON file must contain a 'runs' array")

    if config_index < 0 or config_index >= len(config_data['runs']):
        raise ValueError(f"Config index {config_index} out of range (0-{len(config_data['runs']) - 1})")

    return config_data['runs'][config_index]


# Load the selected configuration
cache_config = load_cache_config(args.config_file, args.config_index)
config_name = None
config_description = None

if cache_config is not None:
    config_name = cache_config.get('name', f'config_{args.config_index}')
    config_description = cache_config.get('description', 'No description provided')

    print(f"\n{'=' * 120}")
    print(f"Using cache configuration: {config_name}")
    print(f"{'=' * 120}\n")

# ======================================================================================================================
# Setup output dir
# ======================================================================================================================

# Set output directory with config name
if args.out_dir is None or args.out_dir == "":
    args.out_dir = './out' + '/' + datetime.now().strftime("%Y-%m-%d-%H%M%S")
    if config_name is not None:
         args.out_dir += '-' + config_name
    print(f"Output directory not specified, using default: {args.out_dir}")

# Throw error if binary does not exist
if args.binary == "" or not os.path.isfile(args.binary):
    raise ValueError("Binary path is not valid or does not exist")

# Create output directory if it doesn't exist
if not os.path.exists(args.out_dir):
    os.makedirs(args.out_dir)

# ======================================================================================================================
# SST components params
# ======================================================================================================================

corecount = 1  # As we only can trace one MPI rank at a time, we can leave corecount to 1
clock = "1.7GHz"

# Ariel params
ariel_params = {
    "verbose": "1",
    #"maxcorequeue": "256",
    #"maxissuepercycle": "2",
    #"pipetimeout": "0",
    "corecount": str(corecount),
    "clock": clock,
    "executable": args.binary,
    "appargcount": len(args.args),
    "arielmode": 0, # set to 1 to trace entire program (default), set to 0 to delay tracing until ariel_enable() call.
    "arielinterceptcalls": "0",  # Do not intercept malloc/free
    "launchparamcount": 1,
    "launchparam0": "-ifeellucky",  # For Pin2.14 on newer hardware
    "mpilauncher": "build/mpilauncher",
    "mpimode": 1,
    "mpiranks": args.nranks,
    "mpitracerank": args.tracerank,
}

# Add target binary arguments to ariel
for i, apparg in enumerate(args.args):
    ariel_params["apparg" + str(i)] = apparg

# Tracer Params
tracer_params = {
    "clock": clock,
    "mpi_trace_out": args.out_dir + "/data/mpi_traces.csv",
    "mem_trace_out": args.out_dir + "/data/samples.csv",
    "debug": "8",
    "corecount": str(corecount)
}

# Default parameters for L1
l1_params = {
    "cache_frequency": clock,
    "access_latency_cycles": 4,
    "cache_size": "32KB",
    "associativity": 8,
    "cache_line_size": 64,
    "L1": 1,
    "verbose": 1,
}

l1_prefetcher_params = {}

# Default parameters for L2
l2_params = {
    "cache_frequency": clock,
    "access_latency_cycles": 12,
    "cache_size": "1024KB",
    "associativity": 16,
    "cache_line_size": 64,
    "verbose": 1,
}

l2_prefetcher_params = {}

# Default parameters for L3
l3_params = {
    "cache_frequency": clock,
    "access_latency_cycles": 17,
    "cache_size": "11MB",
    "associativity": 16, # Should be 11
    "cache_line_size": 64,
    "verbose": 1,
}

l3_prefetcher_params = {}

# Memory controller params
memctrl_params = {
    "clock": clock
}

# Memory params
mem_params = {
    "access_time": "110ns",
    "mem_size": "78GB"
}

# ======================================================================================================================
# Override default parameters with values from JSON config
# ======================================================================================================================

if cache_config is not None:
    # Apply custom parameters for Ariel from JSON config
    if 'ariel' in cache_config:
        for p in cache_config['ariel']:
            ariel_params[p] = cache_config['ariel'][p]

    # Apply custom parameters for L1 from JSON config
    if 'l1' in cache_config:
        for p in cache_config['l1']:
            l1_params[p] = cache_config['l1'][p]

    if 'l1_prefetcher' in cache_config:
        for p in cache_config['l1_prefetcher']:
            l1_prefetcher_params[p] = cache_config['l1_prefetcher'][p]

    # Apply custom parameters for L2 from JSON config
    if 'l2' in cache_config:
        for p in cache_config['l2']:
            l2_params[p] = cache_config['l2'][p]

    if 'l2_prefetcher' in cache_config:
        for p in cache_config['l2_prefetcher']:
            l2_prefetcher_params[p] = cache_config['l2_prefetcher'][p]

    # Apply custom parameters for L3 from JSON config
    if 'l3' in cache_config:
        for p in cache_config['l3']:
            l3_params[p] = cache_config['l3'][p]

    if 'l3_prefetcher' in cache_config:
        for p in cache_config['l3_prefetcher']:
            l3_prefetcher_params[p] = cache_config['l3_prefetcher'][p]

    # Apply custom parameters for memory from JSON config
    if 'mem_params' in cache_config:
        for p in cache_config['mem_params']:
            mem_params[p] = cache_config['mem_params'][p]

# ======================================================================================================================
# SST model definition
# ======================================================================================================================

# CPU (Ariel) ----------------------------------------------------------------------------------------------------------
ariel = sst.Component("ariel0", "ariel.ariel")
ariel.addParams(ariel_params)

# Tracer ---------------------------------------------------------------------------------------------------------------
tracer = sst.Component("tracer", "customTracer.customTracer")
tracer.addParams(tracer_params)

# Bus between L2s and L3 -----------------------------------------------------------------------------------------------
membus = sst.Component("membus", "memHierarchy.Bus")
membus_params = {"bus_frequency": clock}
membus.addParams(membus_params)

for x in range(0, corecount):
    # Private L1s ------------------------------------------------------------------------------------------------------
    l1d = sst.Component("l1cache_" + str(x), "memHierarchy.Cache")
    l1d.addParams(l1_params)

    #l1d.setSubComponent("listener", "customTracer.perfCacheListener")

    if l1_prefetcher_params is not None and 'prefetcher_subcomponent' in l1_prefetcher_params and l1_prefetcher_params['prefetcher_subcomponent'] != "":
        l1d_prefetcher = l1d.setSubComponent("prefetcher", l1_prefetcher_params['prefetcher_subcomponent'])
        l1d_prefetcher.addParams(l1_prefetcher_params)

    l1d.addPortModule("highlink", "customTracer.portmodules.tracerPortModule", {"data_src": "L1"})

    ariel_core_tracer_link = sst.Link("ariel_core_tracer_link_" + str(x))
    ariel_core_tracer_link.connect((ariel, "cache_link_" + str(x), "50ps"), (tracer, "cpu_link_" + str(x), "50ps"))

    tracer_l1d_link = sst.Link("tracer_l1d_link_" + str(x))
    tracer_l1d_link.connect((tracer, "mem_link_" + str(x), "50ps"), (l1d, "highlink", "50ps"))

    # Private L2s ------------------------------------------------------------------------------------------------------
    l2 = sst.Component("l2cache_" + str(x), "memHierarchy.Cache")
    l2.addParams(l2_params)

    #l2.setSubComponent("listener", "customTracer.perfCacheListener")

    if l2_prefetcher_params is not None and 'prefetcher_subcomponent' in l2_prefetcher_params and l2_prefetcher_params['prefetcher_subcomponent'] != "":
        l2_prefetcher = l2.setSubComponent("prefetcher", l2_prefetcher_params['prefetcher_subcomponent'])
        l2_prefetcher.addParams(l2_prefetcher_params)

    l2.addPortModule("highlink", "customTracer.portmodules.tracerPortModule", {"data_src": "L2"})

    l1d_l2_link = sst.Link("l1_l2_link_" + str(x))
    l1d_l2_link.connect((l1d, "lowlink", "50ps"), (l2, "highlink", "50ps"))

    l2_bus_link = sst.Link("l2_bus_link_" + str(x))
    l2_bus_link.connect((l2, "lowlink", "10ps"), (membus, "highlink" + str(x), "10ps"))

# Shared L3 ------------------------------------------------------------------------------------------------------------
l3 = sst.Component("l3cache", "memHierarchy.Cache")
l3.addParams(l3_params)

#l3.setSubComponent("listener", "customTracer.perfCacheListener")

if l3_prefetcher_params is not None and 'prefetcher_subcomponent' in l3_prefetcher_params and l3_prefetcher_params['prefetcher_subcomponent'] != "":
    l3_prefetcher = l3.setSubComponent("prefetcher", l3_prefetcher_params['prefetcher_subcomponent'])
    l3_prefetcher.addParams(l3_prefetcher_params)

l3.addPortModule("highlink", "customTracer.portmodules.tracerPortModule", {"data_src": "L3"})

l3_bus_link = sst.Link("l3_bus_link")
l3_bus_link.connect((l3, "highlink", "10ps"), (membus, "lowlink0", "10ps"))

# Memory ---------------------------------------------------------------------------------------------------------------
memctrl = sst.Component("memory", "memHierarchy.MemController")
memctrl.addParams(memctrl_params)

memory = memctrl.setSubComponent("backend", "memHierarchy.simpleMem")
memory.addParams(mem_params)

memctrl.addPortModule("highlink", "customTracer.portmodules.tracerPortModule", {"data_src": "Mem"})

memory_link = sst.Link("memory_bus_link")
memory_link.connect((l3, "lowlink", "50ps"), (memctrl, "highlink", "50ps"))

# ======================================================================================================================
# Statistics Configuration
# ======================================================================================================================

sst.setStatisticLoadLevel(7)
sst.setStatisticOutput("sst.statOutputConsole")
sst.enableAllStatisticsForComponentType("customTracer.customTracer")
sst.enableAllStatisticsForComponentType("ariel.ariel")
sst.enableAllStatisticsForComponentType("memHierarchy.Cache")
sst.enableAllStatisticsForComponentType("cassini.StridePrefetcher")
sst.enableAllStatisticsForComponentType("cassini.NextBlockPrefetcher")
#sst.enableAllStatisticsForComponentType("customTracer.perfCacheListener")

# sst.enableStatisticsForComponentName("ariel0", ["read_requests", "write_requests"], {}, True)
#
# for x in range(0, corecount):
#     sst.enableStatisticsForComponentName("l1cache_" + str(x), ["CacheHits", "CacheMisses"], {}, True)
#     sst.enableStatisticsForComponentName("l2cache_" + str(x), ["CacheHits", "CacheMisses"], {}, True)
#
# sst.enableStatisticsForComponentName("l3cache", ["CacheHits", "CacheMisses"], {}, True)

sst.setStatisticOutput("sst.statOutputCSV", {"filepath": f"{args.out_dir}/sst-stats.csv", "separator": ","})

print("Done configuring SST model")

# ======================================================================================================================
# Storing the SST configuration in config.json
# ======================================================================================================================

config_to_store = {
    "simulation_metadata": {
        "configuration_name": config_name if config_name is not None else 'None',
        "configuration_description": config_description if config_description is not None else 'None',
        "number_of_mpi_ranks": args.nranks,
        "mpi_rank_for_tracer": args.tracerank,
        "output_directory": args.out_dir,
        "binary_path": args.binary,
        "binary_arguments": args.args,
        "full_command_line": ' '.join(os.sys.argv),
    },
    "sst_model": {
        "corecount": corecount,
        "clock": clock,
    },
    "components": {
        "ariel": ariel_params,
        "tracer": tracer_params,
        "l1": l1_params,
        "l1_prefetcher": l1_prefetcher_params,
        "l2": l2_params,
        "l2_prefetcher": l2_prefetcher_params,
        "l3": l3_params,
        "l3_prefetcher": l3_prefetcher_params,
        "membus": membus_params,
        "memctrl": memctrl_params,
        "memory": mem_params,
    },
}

with open(os.path.join(args.out_dir, "config.json"), "w") as f:
    json.dump(config_to_store, f, indent=4)
