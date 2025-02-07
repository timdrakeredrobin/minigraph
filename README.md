## <a name="started"></a>Getting Started

```sh
git clone https://github.com/lh3/minigraph
cd minigraph && make
# Map sequence to sequence, similar to minimap2 without base alignment
./minigraph test/MT-human.fa test/MT-orangA.fa > out.paf
# Map sequence to graph
./minigraph test/MT.gfa test/MT-orangA.fa > out.gaf
# Incremental graph generation (-l10k necessary for this toy example)
./minigraph -xggs -l10k test/MT.gfa test/MT-chimp.fa test/MT-orangA.fa > out.gfa
# The lossy FASTA representation (requring https://github.com/lh3/gfatools)
gfatools gfa2fa -s out.gfa > out.fa
```

## Table of Contents

- [Getting Started](#started)
- [Introduction](#intro)
- [Users' Guide](#uguide)
  - [Installation](#install)
  - [Sequence-to-graph mapping](#map)
  - [Graph generation](#ggen)
  - [Prebuilt graphs](#prebuilt)
  - [Algorithm overview](#algo)
- [Limitations](#limit)

## <a name="intro"></a>Introduction

<img align="right" width="278" src="doc/example1.png"/>

Minigraph is a *proof-of-concept* sequence-to-graph mapper and graph
constructor. It finds *approximate* locations of a query sequence in a sequence
graph and incrementally augments an existing graph with long query subsequences
diverged from the graph. The figure on the right briefly explains the procedure.

Minigraph borrows many ideas and code from [minimap2][minimap2]. It is fairly
efficient and can construct a graph from 15 human assemblies in an hour using
24 CPU cores. **However**, minigraph is at an early development stage. It lacks
important features and may produce suboptimal mappings. Please read the
[Limitations section](#limit) of this README before using minigraph.

## <a name="uguide"></a>Users' Guide

### <a name="install"></a>Installation

To install minigraph, type `make` in the source code directory. The only
non-standard dependency is [zlib][zlib].

### <a name="map"></a>Sequence-to-graph mapping

To map sequences against a graph, you should prepare the graph in the [GFA
format][gfa1], or preferrably the [rGFA format][rgfa]. If you don't have
a graph, you can generate a graph from multiple samples (see the [Graph
generation section](#ggen) below). The typical command line for mapping is
```sh
minigraph -x lr graph.gfa query.fa > out.gaf
```
You may choose the right preset option `-x` according to input. Minigraph
output mappings in the [GAF format][gaf], which is a strict superset of the
[PAF format][paf]. The only visual difference between GAF and PAF is that the
6th column in GAF may encode a graph path like
`>MT_human:0-4001<MT_orang:3426-3927` instead of a contig/chromosome name.

The minigraph GFA parser seamlessly parses FASTA and converts it to GFA
internally, so you can also provide sequences in FASTA as the reference. In
this case, minigraph will behave like minimap2 but without base-level
alignment.

### <a name="ggen"></a>Graph generation

The following command-line generates a graph in rGFA:
```sh
minigraph -xggs -t16 ref.fa sample1.fa sample2.fa > out.gfa
```
which is equivalent to
```sh
minigraph -xggs -t16 ref.fa sample1.fa > sample1.gfa
minigraph -xggs -t16 sample1.gfa sample2.fa > out.gfa
```
File `ref.fa` is typically the reference genome (e.g. GRCh38 for human).
It can also be replaced by a graph in rGFA. Minigraph assumes `sample1.fa` to
be the whole-genome assembly of an individual. This is an important assumption:
minigraph only considers 1-to-1 orthogonal regions between the graph and the
individual FASTA. If you use raw reads or put multiple individual genomes in
one file, minigraph will filter out most alignments as they cover the input
graph multiple times.

The output rGFA can be converted to a FASTA file with [gfatools][gfatools]:
```sh
gfatools gfa2fa -s graph.gfa > out.stable.fa
```
The output `out.stable.fa` will always include the initial reference `ref.fa`
and may additionally add new segments diverged from the initial reference.

### <a name="prebuilt"></a>Prebuilt graphs

Prebuilt human graphs in the rGFA format can be found at
`ftp://ftp.dfci.harvard.edu/pub/hli/minigraph`.

### <a name="algo"></a>Algorithm overview

In the following, minigraph command line options have a dash ahead and are
highlighted in bold. The description may help to tune minigraph parameters.

1. Read all reference bases, extract (**-k**,**-w**)-minimizers and index them
   in a hash table.

2. Read **-K** [=*500M*] query bases in the mapping mode, or read all query
   bases in the graph construction mode. For each query sequence, do step 3
   through 5:

3. Find colinear minimizer chains using the [minimap2][minimap2] algorithm,
   assuming segments in the graph are disconnected. These are called *linear
   chains*.

4. Perform another round of chaining, taking each linear chain as an anchor.
   For a pair of linear chains, minigraph finds up to 15 shortest paths between
   them and chooses the path of length closest to the distance on the query
   sequence. Importantly, sequences are ignored in this round of chaining.
   Chains found at this step are called *graph chains*.

5. Identify primary chains and estimate mapping quality with a method similar
   to the one used in minimap2.

6. In the graph construction mode, collect all mappings longer than **-d**
   [=*10k*] and keep their query and graph segment intervals in two lists,
   respectively.

7. For each mapping longer than **-l** [=*50k*], finds poorly aligned regions.
   A region is filtered if it overlaps two or more intervals collected at step
   6.

8. Insert the remaining poorly aligned regions into the input graph. This
   constructs a new graph.

## <a name="limit"></a>Limitations

* Minigraph needs to find strong colinear chains first. For a graph consisting
  of many short segments (e.g. one generated from rare SNPs in large
  populations), minigraph will fail to map query sequences.

* When connecting colinear chains on graphs, minigraph ignores sequences, and
  only considers the distances of top 15 shortest paths between colinear
  chains. It may miss the optimal alignments.

* Minigraph doesn't give base-level alignment.

* Minigraph only inserts segments contained in long graph chains. This
  conservative strategy helps to build relatively accurate graph, but may miss
  more complex events. Other strategies may be explored in future.



[zlib]: http://zlib.net/
[minimap2]: https://github.com/lh3/minimap2
[rgfa]: https://github.com/lh3/gfatools/blob/master/doc/rGFA.md
[gfa1]: https://github.com/GFA-spec/GFA-spec/blob/master/GFA1.md
[gaf]: https://github.com/lh3/gfatools/blob/master/doc/rGFA.md#the-graph-alignment-format-gaf
[paf]: https://github.com/lh3/miniasm/blob/master/PAF.md
[gfatools]: ttps://github.com/lh3/gfatools
