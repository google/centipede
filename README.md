# Centipede - a distributed fuzzing engine. Work-in-progress.

## Why Centipede

Why not? We are currently trying to fuzz some very large and very slow targets
for which [libFuzzer](https://llvm.org/docs/LibFuzzer.html),
[AFL](https://lcamtuf.coredump.cx/afl/), and the like do not necessarily scale
well. For one of our motivating examples
see [SiliFuzz](https://arxiv.org/abs/2110.11519). While working on Centipede we
plan to experiment with new approaches to massive-scale differential fuzzing,
that the existing fuzzing engines don't try to do.

Notable features:

* Early **work-in-progress**. We test centipede within a small team on a couple
  of targets. Unless you are part of the Centipede project, or want to help us,
  **you probably don't want to read further just yet**.

* Scale. The intent is to be able to run any number of jobs concurrently, with
  very little communication overhead. We currently test with 100 local jobs and
  with 10k jobs on a cluster.

* Out of process. The target runs in a separate process. Any crashes in it will
  not affect the fuzzer. Centipede can be used in-process as well, but this mode
  is not the main goal. If your target is small and fast you probably still want
  libFuzzer.

* Integration with the sanitizers is achieved via separate builds. If during
  fuzzing you want to find bugs with
  [ASAN](https://github.com/google/sanitizers/wiki/AddressSanitizer),
  [MSAN](https://github.com/google/sanitizers/wiki/MemorySanitizer), or
  [TSAN](https://github.com/google/sanitizers/wiki/ThreadSanitizer), you will
  need to provide separate binaries for every sanitizer as well as one main
  binary for Centipede itself. The main binary should not use any of the
  sanitizers.

* No part of interface is stable. Anything may change at this stage.

## Terminology

* **Fuzz target**, or **target**:
  A binary, a library, an API, or rather anything that can consume bytes for
  input and produce some sort of coverage data as an output.
  A [libFuzzer](https://llvm.org/docs/LibFuzzer.html)'s target can be
  Centipede's target.

* **Fuzzing engine**: a program that produces an infinite stream of inputs for a
  target and orchestrates the execution.

* **Input**: a sequence of bytes that we can feed to a target. The input can be
  an arbitrary bag of bytes, or some structured data, e.g. serialized proto.

* **Coverage**: some information about the behaviour of the target when it
  executes a given input. See e.g.
  [SanitizerCoverage](https://clang.llvm.org/docs/SanitizerCoverage.html)

* **Mutator**: a function that takes bytes as input and outputs a small random
  mutation of the input. See also:
  [Structure-aware-fuzzing](https://github.com/google/fuzzing/blob/master/docs/structure-aware-fuzzing.md)
  .

* **Executor**: a function that knows how to feed an input into a target and get
  coverage in return (i.e. to **execute**).

* **Centipede**: is a customizable fuzzing engine. It allows the user to
  substitute the Mutator and the Executor.

* **Workdir** or **WD**: a local or remote directory that contains data produced
  or consumed by a fuzzer.

* **Corpus** (plural: **corpora**): a set of inputs.

* **Feature**: A number that represents some unique behavior of the target. E.g.
  a feature 1234567 may represent the fact that a basic block number 987 in the
  target has been executed 7 times. When executing an input with the target, the
  fuzzer collects the features that were observed during execution. **Feature
  set** is a set of features associated with one specific input.

* **Distillation** (creating a **distilled corpus**): a process of choosing a
  subset of a larger corpus, such that the subset has the same coverage features
  as the original corpus.

* **Shard**: A file representing a subset of the corpus and another file
  representing feature sets for that same subset of the corpus.

* **Merge** shard B into shard A:
  for every input in shard B that has features missing in shard A, add that
  input to A.

* **Job**: a single fuzzer process. One job writes only to one shard, but may
  read multiple shards.

## Build

```
% bazel build -c opt :centipede
% bazel build -c opt :target_example
```

`centipede` and `target_example` are two independent binaries that may not
necessarily know about each other. But the executor built into the
fuzzer (`centipede`) needs to know how to properly execute the target
(`target_example`).

`centipede` is a regular C++ binary built with usual build options.

The target could be anything that the fuzzer knows how to execute. In this
example, `target_example` is a
[fuzz target](https://github.com/google/fuzzing/blob/master/docs/good-fuzz-target.md)
built with [sancov](https://clang.llvm.org/docs/SanitizerCoverage.html)
via [bazel transitions](https://bazel.build/rules/lib/transition).

## Run locally

Running locally will not give the full scale, but it could be useful during the
fuzzer development stage. We recommend that both the fuzzer and the target are
copied to a local directory before running in order to avoid stressing a network
file system.

```
DIR=$HOME/centipede_example_dir
rm -rf $DIR # Careful!
mkdir $DIR
cp bazel-bin/centipede/testing/target_example $DIR
cp bazel-bin/centipede/centipede $DIR
cd $DIR
```

NOTE: You may need to add
[`llvm-symbolizer`](https://llvm.org/docs/CommandGuide/llvm-symbolizer.html)
to your `$PATH` for some of the Centipede functionality to work. The symbolizer
can be installed as part of the [LLVM](https://releases.llvm.org) distribution.

Create a workdir:

```
mkdir WD
```

Run one fuzzing job. Will create a single shard.

```
./centipede --alsologtostderr --workdir=WD --binary=./target_example --num_runs=100
```

See what's in workdir:

```
% ls WD/*
WD/corpus.0  WD/coverage-report-target_example.0.txt

WD/target_example-467422156588a87805669f8334cb88889ab8958d:
features.0
```

Run 5 concurrent fuzzing jobs. Don't run more than the number of cores on your
machine.

```
for((shard=0;shard<5;shard++)); do
  ./centipede --alsologtostderr --workdir=WD --binary=./target_example --num_runs=100 \
    --first_shard_index=$shard --total_shards=5 2> $shard.log &
done ; wait

```

See what's in workdir:

```
% ls WD/*
WD/corpus.0  WD/corpus.1  WD/corpus.2  WD/corpus.3  WD/corpus.4
WD/coverage-report-target_example.0.txt

WD/target_example-467422156588a87805669f8334cb88889ab8958d:
features.0  features.1  features.2  features.3  features.4
```

## Corpus distillation

Each Centipede shard typically does not cover all features that the entire
corpus covers. In order to distill the corpus, a Centipede process will need to
read all shards. Currently, distillation works like this:

* Run fuzzing as described above, so that all shards have their feature sets
  computed. Stop fuzzing.
* Then, run the same fuzzing jobs, but with `--distill_shards=N`. This will
  cause the first `N` jobs to produce `N` independent distilled corpus files
  (one per job). Each of the distilled corpora should have the same features as
  the full corpus, but the inputs might be very different between these
  distilled corpora.

If you need to also export the distilled corpus to a libFuzzer-style directory
(local dir with one file per input), add `--corpus_dir=DIR`.

## Coverage report

Centipede generates a simple coverage report in a form of a text file. The shard
0 generates a file `workdir/coverage-report-BINARY.0.txt`
before the actual fuzzing begins, i.e. the report reflects the coverage as
observed by the shard 0 after loading the corpus.

The report shows functions that are fully covered (all control flow edges are
observed at least once), not covered, or partially covered. For partially
covered functions the report contains symbolic information for all covered and
uncovered edges.

The report will look something like this:

```
FULL: FUNCTION_A a.cc:1:0
NONE: FUNCTION_BB bb.cc:1:0
PARTIAL: FUNCTION_CCC ccc.cc:1:0
+ FUNCTION_CCC ccc.cc:1:0
- FUNCTION_CCC ccc.cc:2:0
- FUNCTION_CCC ccc.cc:3:0
```

## Customization

Centipede.

## Related Reading

* [Centipede Design](doc/DESIGN.md)
