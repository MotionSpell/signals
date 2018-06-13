<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
**Table of Contents**  *generated with [DocToc](https://github.com/thlorenz/doctoc)*

- [Signals](#signals)
- [Introduction](#introduction)
- [Applications](#applications)
- [Build](#build)
- [Tests](#tests)
- [Documentation](#documentation)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

Signals
=======

# Introduction

Signals is a modern C++ framework to build modular applications. It is currently used for building multimedia applications. Its architecture allows to extend to any domain. Signals is used by companies from the multimedia industry (audio, video and broadcast).

Signals is designed with the following goals:
 - Writing new modules must be easy and require minimal boilerplate. Especially for multimedia systems, you should not have to know about complex matters (types, internals, clocks, locking, ...) unless you need to ; according to our experience 90% of the applications use the same mechanisms.
 - Writing an application using these modules must be easy.

# Applications

Signals comes with several multimedia applications:
 - player: a generic multimedia player.
 - dashcastx: a rewrite of the GPAC dashcast application (any input to MPEG-DASH live) in less than 300 lines of code.

# Build

Please read [build.md](doc/build.md).

# Tests

Signals is built using TDD. There are plenty of tests. If you contribute please commit your tests first.
Unit tests should be self-validating, which means they shouldn't need the intervention of a human operator to validate the result (e.g, they shouldn't print anything on the console).
They should also be isolated. At best, they should depend upon no third-party code (except of course when testing the code making the direct calls to a third-party component).

You can check the code coverage of the test suite using ./scripts/cov.sh. This will generate an HTML report using gcov+lcov in 'cov-html'.

# Documentation

Documentation is both a set of markdown files and a doxygen. See in the [doc subdirectory](doc/).
