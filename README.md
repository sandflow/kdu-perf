# kdu_perf

## Overview

_kdu_perf_ tests the in-memory performance of the
`kdu_supp::kdu_stripe_decompressor` class exposed by the [Kakadu
SDK](https://kakadusoftware.com/).

_NOTE_: The `#define _DECODER_WANTS_STREAMING_STORES` macro found in the
`coresys/coding/avx2_coder_local.cpp` source file of the Kakadu SDK may need to
be commented out to achieve best performance on low latency HTJ2K codestreams.

_IMPORTANT_: While _kdu_perf_ is published under an [open-source
license](./LICENSE.txt), the Kakadu SDK is a commercial library licensed under a
restrictive license. The _kdu_perf_ license does not extend to the Kakadu SDK
and a separate license for Kakadu SDK must be obained.

## Prerequisites

* Kakadu SDK library files (version 9.4.1+)
* C++11 toolchains
* CMake

## Quick start

    git clone --recurse-submodules https://github.com/sandflow/kdu-perf.git
    cd kdu-perf
    mkdir build
    cd build
    cmake -DKDU_LIBRARY=<path to libkdu_a84R.so> \
          -DKDU_INCLUDE_DIR=<path to Kakadu SDK include headers> \
          ..
    ctest
    ./kdu_perf ../src/test/resources/codestreams/ht.j2c

## Usage

    kdu_perf [OPTION...] <path to j2c codestream>

    -r, --repetitions arg  Number of repetitions per thread (default: 100)
    -t, --threads arg      Number of threads (default: 1)