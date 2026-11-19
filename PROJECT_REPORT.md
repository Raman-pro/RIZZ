# RIZZ
## A mini Git implementation in C++

**Object-Oriented Programming Mini Project**  
Department of Computer Science and Engineering, Delhi Technological University

| Submission detail | Fill in before submission |
| --- | --- |
| Student 1 | Ramanjot Singh · Roll number __________ |
| Student 2 | ____________________ · Roll number __________ |
| Class and section | ____________________ |
| Submitted to | ____________________ |
| Submission date | ____________________ |

### Abstract

We chose RIZZ because we used Git commands without knowing exactly what happened to our files. One question got us started. If we stage a file and edit it again, which version does Git commit? We studied the supplied Python mini Git and adapted its 15 commands to C++. Our program stages files, saves snapshots, reads history, creates tags and recovers saved files. Building these operations gave us a practical reason to use classes, composition and virtual functions.

### 1. Problem and objectives

A folder full of copies such as report-final and report-final-2 keeps old work, but it is hard to tell which files belonged together on a particular day. A commit records a snapshot of the project and can link it to an earlier snapshot. We wanted to understand how that record fits together on disk.

Our objectives were to follow one file from the working directory into a commit, recover its exact contents and explain each step from our own C++ code. We also wanted to identify places where two commands could share code without forcing them to do the same thing.

### 2. Reference and tools

We used the supplied libwyag.py, from Thibault Polge's *Write yourself a Git!*, as our reference for the commands and file formats. We adapted the implementation to C++17 in one source file. The C++ standard library handles strings, maps, files and directories. Zlib compresses stored objects. CommonCrypto on macOS and OpenSSL on Linux provide SHA-1. The version-control operations themselves are implemented in RIZZ. We used the Git command-line program to check the files our program produced.

### 3. How a file becomes a commit

Suppose diary.txt contains Monday. When we run add, RIZZ reads those bytes and saves them as a blob. It writes the blob's ID next to diary.txt in the index. This is the staging area. Editing our working file afterward leaves the staged ID in place.

When we run commit, RIZZ groups the staged paths by directory and builds a tree for each directory. A tree records names, modes and object IDs. The new commit records the root tree ID, author, time, message and, when available, the previous commit ID. RIZZ then updates the current branch reference. Recovering the file works in the opposite direction. We choose a commit, read its tree, find the entry called diary.txt and write the blob contents to a destination folder.

### 4. Use of object-oriented programming

| Concept | Where we used it | Why it helped |
| --- | --- | --- |
| Encapsulation | ObjectStore provides save and load methods | Commands can work with objects without repeating the compression and path rules. |
| Composition | Repository contains an ObjectStore | The repository connects storage with its working directory and references. |
| Abstraction | TreeVisitor declares a visit operation | The tree traversal can hand an entry to a caller through one common interface. |
| Inheritance and polymorphism | Listing, Snapshot and Export override visit | The same traversal can print entries, collect a snapshot or recover files. |

We used simple structures for values such as a tree entry. A tree entry mainly needs a name, a mode and an ID. Giving it a separate inheritance hierarchy would have added work without helping the main operations.

### 5. Commands implemented

| Purpose | Commands |
| --- | --- |
| Create a repository and prepare changes | init, add, rm, ls-files, status, check-ignore |
| Save and recover snapshots | commit, ls-tree, checkout |
| Inspect objects and history | hash-object, cat-file, log, rev-parse |
| Name saved work and list references | tag, show-ref |

Checkout exports into an empty directory, following the behaviour of our Python reference. Log prints a Graphviz description of the commit history. A lightweight tag gives an object a name. An annotated tag adds a separate object containing information about the tag.

### 6. Testing and observations

We compiled the C++ version on macOS and exercised all 15 commands in a temporary repository. We checked the following outcomes.

| Test | Observed result |
| --- | --- |
| Stage a text file, edit it and then commit | The saved commit contained the first version. Status identified both the staged addition and the later edit. |
| Commit and recover a nested binary file containing all 256 byte values | The exported file matched the original byte for byte. |
| Make two commits and tag the first | The log contained a parent edge and the tag still resolved to the first snapshot. |
| Compare with Git | Git and RIZZ reported the same blob ID, staged filenames and recursive tree listing. Git's strict repository check completed successfully. |
| Try checkout into a nonempty directory | RIZZ returned an error. The existing export remained in place. |
| Stage ignore rules including an exception | check-ignore printed the ignored filename and left out the exception. |

### 7. Difficulties and limitations

The index was the hardest part to reason about because it mixes numbers, raw IDs, names and padding. We found it easier to think of it as a table first and then work out how one row is stored. Another important detail was the order of tree creation. A parent directory needs the ID of each child tree, so the children have to be saved first.

RIZZ is intended for classroom repositories containing regular files. It uses loose SHA-1 objects and a version 2 index. It does not implement network operations, packed storage, merge conflict handling or branch switching. Our checks covered the macOS build. The source also includes a Linux build path.

### 8. Conclusion

Our starting question had a concrete answer. A commit uses the version saved by add. We could show this by changing a file, making a commit and recovering the earlier text. We also learned why Git can keep many snapshots without making a fresh copy of every unchanged file. Several trees or filenames can refer to a blob that already exists.

The most useful OOP example was the tree traversal. Listing a snapshot and recovering it need the same reading logic, but each command does something different with an entry. Giving those actions a common interface allowed us to implement the traversal once.

### References

- Thibault Polge, *Write yourself a Git!*, and the supplied libwyag.py. The adaptation retains the GPL-3.0-or-later license. [Original tutorial](https://wyag.thb.lt/)
- RIZZ project source, rizz.cpp. The accompanying TEACHER.md explains its classes, methods and command flow.
