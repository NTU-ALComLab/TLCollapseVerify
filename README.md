# TLCollapseVerify
## Introduction
- Threshold logic collapse operation and verification within ABC
- Primary developer: Nian-Ze Lee from National Taiwan University
- C implementation of algorithms proposed in [Analytic Approaches to the Collapse Operation and Equivalence Verification of Threshold Logic Circuits](https://ieeexplore.ieee.org/document/7827582/)
- Algorithms and codes for threshold logic technology mapping proposed in [Threshold Logic Synthesis Based on Cut Pruning](https://ieeexplore.ieee.org/document/7372610/) by Augusto Neutzling et al.
## Contents
1. [Installation](#installation)
2. [Commands](#commands)
3. [Examples](#examples)
4. [Contact](#contact)
## Installation
Type `make` to complie and the executable file is `bin/abc`
```
make
```
It has been compiled successfully with GCC\_VERSION=8.2.0 under CentOS 7.3.1611
## Commands:
### I/O:
- read_th (rt): read a .th file (POs must be buffered)
- write_th (wt): write current_TList out as a .th file
- print_th (pt): print network statistics of current_TList
### Synthesis:
- aig2th (a2t): convert an AIG to a TLC by replacing an AND gate with a TLG [1,1;2]
- merge_th (mt): the collapsing-based TLC synthesis
### Verification:
- th2mux (t2m): convert a TLC to an AIG by expanding a TLG to a MUX tree
- thverify (tvr): write a CNF/PB file for the equivalence checking of two TLCs
- thpg (tp): write a PB file for the output satisfiability of a TLC with PG encoding
## Usage examples
1. Collapse an AIG circuit iteratively with a fanout bound = 100
```
abc 01> r benchmark/iscas/s38417.blif
abc 02> comb
abc 03> fraig
abc 04> mt -B 100
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
3. Verify equivalence using TL-to-MUX conversion and cec
(Continued from the above example)
```
abc 01> rt s38417_before_clp.th
abc 02> t2m
abc 03> w s38417_before_clp.aig
abc 04> rt s38417_after_clp.th
abc 05> t2m
abc 06> w s38417_after_clp.aig
abc 07> cec s38417_before_clp.aig s38417_after_clp.aig
```
4. Verify equivalence using TL-to-PB conversion and minisat+
(Continued from the above example)
```
abc 01> tvr s38417_before_clp.th s38417_after_clp.th
abc 02> quit
bin/minisat+ compTH.opb
```
5. Verify equivalence using TL-to-CNF conversion and minisat
(Continued from the above example)
```
abc 01> tvr -V 1 s38417_before_clp.th s38417_after_clp.th
abc 02> quit
bin/minisat compTH.dimacs
```
## Contact:
Please let us know if you have any problem using the code.  
Nian-Ze Lee: d04943019@ntu.edu.tw, nianzelee@gmail.com
