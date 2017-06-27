## Bake-off/benchmark problems with Nek5000

### Required tools

* bash
* make
* git
* wget or curl

### Building Nek5000 tools

First, you may need to create a configuration for your machine, e.g. by 
copying and modifying one of the existing `machine-configs/*.sh` files 
if you have not already done so. Then you can build Nek5000 tools using 
the following command:

```sh
../../go.sh --config vulcan --compiler gcc --build "nek5000"
```

or the equivalent shorter version:

```sh
../../go.sh -c vulcan -m gcc -b "nek5000"
```

To see a list of the available configs use `./go.sh` or generally use
`./go.sh --help` for help. These configs correspond to the scripts
`machine-configs/<name>.sh`.

To see the available compilers for a config use `./go.sh --config <name>`.

## Running the benchmarks

Once the tools are built, you can run Nek5000 benchmarksd using the 
following command:

```sh
../../go.sh --config vulcan --compiler gcc --run bp1/bp1.sh -n 16 -p 16
```

or the equivalent shorter version:

```sh
../../go.sh -c linux -m gcc -r bp1/bp1.sh -n 16 -p 16
```

where `-n 16` is the total number of processors and `-p 16` is the 
number of processors per node.

Multiple processor configurations can be run with:

```sh
../../go.sh -c linux -m gcc -r bp1/bp1.sh -n "16 32 64" -p "16 32 64"
```

## Postprocessing the results

First, save the output of the run to a file:

```sh
../../go.sh -c linux -m gcc -r bp1/bp1.sh -n 16 -p 16 > run_001.txt
```

and then use one of the `postprocess-plot-*.py` scripts (which require
the python package matplotlib) or the `postprocess-table.py` script, e.g.:

```sh
python postprocess-plot-1.py run_001.txt
```

Note that the `postprocess-*.py` scripts can read multiple files at a 
time just by listing them on the command line and also read the standard 
input if no files were specified on the command line.