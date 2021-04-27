# Map-Reduce library
This is a library that executes a multi-threaded map reduce process.
## Technologies And Tools
This library was written in C++.
The library was developed and tested on a Linux computer.
## Overview
### Map-Reduce process
1. The input is given as a sequence of input elements.
2. Map phase - The _map_ function is applied to each input element, producing a sequence of intermediary elements.
3. Shuffle phase - The intermediary elements are sorted into new sequences.
4. Reduce phase - The _reduce_ function is applied to each of the sorted sequences of intermediary elements, producing a sequence of output elements.
5. The output is a concatenation of all sequences of output elements.
### MapReduceFramework.h
The class that implements the partition into phases, distribution of work between threads, synchronisation etc.<br/>
This will be identical for different tasks.<br/>
<br/>
### MapReduceClient.h
An abstract class that defines the api the user of the framework should implement in order for the library to work.<br/>
This part will be different for every tasks.<br/>
The methods needed to be implement are<br/>
![client_api.png](https://github.com/IdoSagiv/map-reduce-library/blob/main/images/client_api.png?raw=true)<br/>
## Usage Example - FileWordCounter.cpp
### Description
In the given example the library is used in order to count the number of times each word appears in a given directory of files.</br>
* In order to run the example, run it from the command line with the arguments _[dirPath] [num of threads]_
The input is a vector of strings containing the filenames of the files in the directory

#### Map stage
In this stage an input file is mapped to pairs of the form _(word,count)_ where word is some word in the file and count is the number of times it appeared in the file. Such pair is emitted for every word in the file.
#### Shuffle stage
In this stage, the emitted pairs from the first stage are re-arrange to the form of _(word,[c1,...,cn])_ where each _ci_ is the number of times the word appeared in a specific file.
#### Reduce stage
In this stage a pair _(word,[c1,...,cn])_ is reduced to a pair _(word,count)_ where count is the total number of appearances of the word in the directory files.<br/>
The output is a vector of pairs _(word,count)_ as described before.<br/>
