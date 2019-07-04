# TLCollapseVerify
## Introduction
- Threshold logic collapse operation and verification within ABC
- Primary developer: Nian-Ze Lee from National Taiwan University
- C implementation of algorithms proposed in [Analytic Approaches to the Collapse Operation and Equivalence Verification of Threshold Logic Circuits](https://ieeexplore.ieee.org/document/7827582/)
- Algorithms and codes for threshold logic technology mapping proposed in [Threshold Logic Synthesis Based on Cut Pruning](https://ieeexplore.ieee.org/document/7372610/) by Augusto Neutzling et al.
## Contents
1. [Installation](#installation)
2. [Commands](#commands)
3. [Benchmark](#benchmark)
4. [Examples](#examples)
5. [Contact](#contact)
## Installation
Type `make` to complie and the executable file is `bin/abc`
```
make
```
It has been compiled successfully with GCC\_VERSION=8.2.0 under CentOS 7.3.1611
## Commands
### I/O
- `read_th` (alias `rt`): read a TLC file in the `.th` format (POs must be buffered)
- `write_th` (alias `wt`): write the current TLC out in the `.th` format
- `print_th` (alias `pt`): print the network statistics of the current TLC
### Synthesis
- `aig2th` (alias `a2t`): convert an AIG circuit to a TLC by replacing AIG nodes with TLGs
- `merge_th` (alias `mt`): the proposed collapsing-based TLC synthesis
### Verification
- `th2mux` (alias `t2m`): convert a TLC to an AIG circuit by expanding a TLG to a MUX tree
- `thverify` (alias `tvr`): write a CNF/PB file for the equivalence checking of two TLCs
- `thpg` (alias `tp`): write a PB file for the output satisfiability of a TLC with PG encoding
## Benchmark
### iscas/itc/iwls
- Selected circuits from iscas, itc, and iwls benchmark suites used in the paper 
### bnn
- The dense layers of activation-binarized neural networks trained on the MNIST dataset
## Examples
1. Collapse an AIG circuit iteratively with a fanout bound = 100
```
abc 01> r benchmark/iscas/s38417.blif
abc 02> comb
abc 03> fraig
abc 04> a2t
abc 05> mt -B 100
```
2. Collapse a synthesized TLC iteratively with a fanout bound = 100
```
abc 01> r benchmark/iscas/s38417.blif
abc 02> comb
abc 02> fraig
abc 03> synth1
abc 04> wt s38417_before_clp.th
abc 05> mt -B 100
abc 06> wt s38417_after_clp.th
```
3. Verify equivalence using TL-to-MUX conversion and `cec` (continued from the above example)
```
abc 01> rt s38417_before_clp.th
abc 02> t2m
abc 03> w s38417_before_clp.aig
abc 04> rt s38417_after_clp.th
abc 05> t2m
abc 06> w s38417_after_clp.aig
abc 07> cec s38417_before_clp.aig s38417_after_clp.aig
```
4. Verify equivalence using TL-to-PB conversion and `minisat+` (continued from the above example)
```
abc 01> tvr s38417_before_clp.th s38417_after_clp.th
abc 02> quit
bin/minisat+ compTH.opb
```
5. Verify equivalence using TL-to-CNF conversion and `minisat` (continued from the above example)
```
abc 01> tvr -V 1 s38417_before_clp.th s38417_after_clp.th
abc 02> quit
bin/minisat compTH.dimacs
```
6. Check the output satisfiability of a TLC with PG encoding
```
abc 01> r benchmark/iscas/s38417.blif
abc 02> comb
abc 03> st
abc 04> andpos
abc 05> a2t
abc 06> mt -B 100
abc 07> tp -p
bin/minisat+ pg.opb
```
## Contact
Please send an email to Nian-Ze Lee (d04943019@ntu.edu.tw, nianzelee@gmail.com) if there is any problem
