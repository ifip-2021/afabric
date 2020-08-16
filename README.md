# SRPT-based Congestion Control for Flows with Unknown Sizes

This repository contains the code necessary to reproduce the results presented
in the _"SRPT-based Congestion Control for Flows with Unknown Sizes"_
paper; specifically, Figures 4 and 5.

## Depencencies

  * Recent [Clang](https://clang.llvm.org/) C++ compiler

  * The [CMake](https://cmake.org/) build tool

  * [Tcl/Tk](https://www.tcl.tk/)

  * [OTcl](http://otcl-tclcl.sourceforge.net/otcl/)

  * [TclCL](http://otcl-tclcl.sourceforge.net/tclcl/)

  * [spdlog](https://github.com/gabime/spdlog)

  * [fmt](https://fmt.dev/latest/index.html)

  * [Python](https://www.python.org/)

  * [Click](https://palletsprojects.com/p/click/) python library

  * [TOML](https://github.com/uiri/toml) python library

## Building

 1. NS2-simulator

    ```bash
    cd ns2
    mkdir cmake-build-release
    cd cmake-build-release
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make
    cd ../..
    ```

 2. Auxiliary oracle-fct executable

    ```bash
    cd scripts/plot_generation
    mkdir cmake-build-release
    cd cmake-build-release
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make
    cd ../../../
    ```
## Running simulations

Preparation:

```bash
export DEV_NS_DIR=$PWD/ns2
cd scripts # change to scripts dir
```

Running actual simulations:

```bash
python -m congestion_runner.run --scale final --control ascc\
    --buffer ascc --input web_search --slacks default --load 0.8 --alpha 2.0
# see the description of CLI arguments below
```

The simulation results would be available in the `results` folder.

## Simulation CLI description

Below we provide a detailed description of the CLI arguments.

  * __Algorithms:__

     * _PIAS:_ `--buffer pias --control pias`

     * _aSCC:_ `--buffer ascc --control ascc`

     * _pFabric:_ `--buffer pfabric --control pfabric_clairvoyant`

     * _LAS:_ `--buffer las --control pias`

  * __Inputs:__

     * _Web Search:_ `--input web_search`

     * _Data Mining:_ `--input data_mining`

  * __Parameter specification:__

     * _Load:_ `--load 0.8`
     * _Alpha:_ `--alpha 1.0`

  * __Other mandatory arguments:__

     * `--scale final --slacks default`

## Statistic extraction

Preparation

```bash
mkdir plots
export PLOT_PATH=$PWD/plots
cd scripts
```

Generating plot data in `.csv` format:

```bash
# use same --control --buffer --input and --alpha parameters as for 
# running simulations
python -m plot_generation.generate_plots --aspect load --value 0.8\
    --scale final --control ascc --buffer ascc --input web_search\
    --slacks default --alpha 2.0
```

The above command would create a `.csv` fail in the `PLOT_PATH` dir.

_It is possible to generate csv for a range of parameters, which only requires
to specify `--value` multiple times. Set `--aspect` to either `load` or `alpha`
to control which parameter to vary_

