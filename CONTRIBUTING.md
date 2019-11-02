# How to contribute to airspy-fmradion

This document outlines how to contribute to airspy-fmradion.

## Use English language with American spelling

All documents and source code in the repository should be written in English, with American spelling.

## Feedback

* Use [airspy-fmradion issues](https://github.com/jj1bdx/airspy-fmradion/issues) for general feedbacks, including the bug reports.

## Getting started on modifying the repository

* Fork the repository on GitHub
* Read the README.md for build instructions

## Contribution flow

* Create a topic branch from where you want to base your work. 
* Use the tagged branches as the starting point.
* Make commits separated by logical units.
* Apply the proper C++ coding style.
* Make sure your commit messages are in the proper format.
* Push your changes to a topic branch in your fork of the repository.
* Submit a pull request to jj1bdx/airspy-fmradion.

## Commit message format convention

First line of the commit messages usually follows the following formats:

* Verb + object (e.g., "Update README.md")
* Target file/branch/function/class + action (e.g., "MultipathFilter: remove redundant spaces")

Keep the first line terse and short. Describe the details after the third line.

## C++ coding style

airspy-fmradion is based on C++11. If you propose a modification of a different version of C++, nofity so explicitly in the pull request.

The coding style is defined in the file `.clang-format`. To follow this style, do the following:

* Install clang-format if you don't have it.
* Run the following command at the top directory of the repository:

    clang-format -i main.cpp include/*.h sfmbase/*.cpp

## License

airspy-fmradion is GPLv3 licensed. Do not include the code which is not following the license.
