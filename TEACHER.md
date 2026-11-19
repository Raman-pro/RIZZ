# Learning RIZZ from the source

Start with one question. You write “Monday” in a file, run `add`, change the word to “Tuesday”, then run `commit`. Which word goes into the commit? **Monday.** When you understand how RIZZ makes that happen, you understand the main idea of the project.

## Build and run

The whole program is in `rizz.cpp`. There are no project header files. A few system libraries handle compression, hashing and file information. Run one of these commands in the folder containing the source.

| System | Build command |
| --- | --- |
| macOS with Xcode Command Line Tools | `clang++ -std=c++17 -Wall -Wextra rizz.cpp -lz -o rizz` |
| Linux with a C++17 compiler and the zlib and OpenSSL development packages | `g++ -std=c++17 -Wall -Wextra rizz.cpp -lz -lcrypto -o rizz` |

Run `./rizz --help` to see the commands. The following exercise runs in a fresh temporary directory. Keep the same terminal open so the shell remembers the location of the executable.

```sh
RIZZ="$PWD/rizz"
cd "$(mktemp -d)"
"$RIZZ" init
printf 'Monday\n' > diary.txt
"$RIZZ" add diary.txt
printf 'Tuesday\n' > diary.txt
"$RIZZ" status
"$RIZZ" commit -m "Save Monday"
"$RIZZ" checkout HEAD recovered
cat diary.txt
cat recovered/diary.txt
```

You should see Tuesday in the working file and Monday in the recovered copy. Try predicting the result if you run `add diary.txt` a second time just before the commit.

## Where everything lives

| Name | What you find there | When it changes |
| --- | --- | --- |
| Working directory | Ordinary files such as `diary.txt` | You edit a file |
| `.git/index` | A binary table of staged paths, modes and object IDs | You add or remove a file |
| `.git/objects/` | Compressed blobs, trees, commits and annotated tags | RIZZ encounters a new object |
| `.git/HEAD` | Usually the text `ref: refs/heads/master` | It identifies the current branch or detached commit |
| `.git/refs/heads/master` | The latest commit ID on the initial branch | The first commit creates it. Later commits update it |
| `.git/refs/tags/` | One reference per tag | You create a tag |
| `.git/config` and `.git/description` | Local settings and a short description | Initialization creates them |
| `.git/info/exclude` | Optional local ignore patterns | You create or edit this file |

To find Monday after the commit, RIZZ follows HEAD to master, master to a commit, the commit to a tree, and the tree entry named diary.txt to a blob. Only the blob holds the six letters and the newline.

## Reading the C++ one section at a time

Read by symbol name. In your editor, search for the names in backticks. The numbered comments in the source mark the six main sections.

### Before section 1 — handling bytes and files

`Bytes` is another name for `std::string`. In C++ a string stores its length, so it can hold a zero byte in the middle. This matters for images, index entries and tree objects. `readFile` uses binary mode. `writeFile` writes to a temporary file in the same directory and renames it when the write succeeds. A reader therefore sees a complete old or new file.

`require` throws an exception when a condition fails. `main` prints the error in one place. `trim` removes surrounding whitespace from text such as a commit message. Blob data goes straight to storage with its whitespace intact.

`digest` computes SHA-1. `hexId` turns the 20 result bytes into 40 printable hexadecimal digits. `hashText` checks the alphabet and `rawId` converts the printable form back into bytes. One byte needs two hex digits. `safeName` rejects unsafe path components such as `..` before the program uses a stored name as a filesystem path.

### Section 1 — Object and ObjectStore

An `Object` holds a type and a payload. Its `encode()` method joins the type, a space, the decimal size, one zero byte and the payload. For a three-byte file containing `cat`, the bytes hashed are `blob 3`, followed by a zero byte, followed by `cat`. A newline in the file would make the size four.

`ObjectStore::save` hashes that sequence, splits the resulting ID after its first two digits, compresses the sequence with zlib and writes it under `.git/objects`. An existing ID can be reused. Two files with identical contents can share a blob even when their names differ. The tree records the two names.

`ObjectStore::load` does the reverse. It opens the path built by the private `location` method, inflates the data in chunks, checks the digest and splits the header from the payload. This class is an example of **encapsulation**. The commit code calls `save` without knowing the compression or directory rules.

### Section 2 — Repository and references

`Repository` groups a working directory, its metadata directory and its object store. Its constructor connects those three pieces. `init` creates the directory layout and points HEAD at master. An empty repository already knows its branch name even though that branch has no commit ID yet.

`discover` starts in the current directory and walks upward until it finds a directory named `.git`. That is why you can run commands inside a subfolder. `fileName` converts a user's path to a repository-relative path, checks the boundary and rejects symlink traversal.

`ref` reads a reference file. If the file names another reference, it follows the link. `resolve` turns a branch name, a tag, HEAD or an unambiguous hash prefix of at least four digits into a full ID. If two different IDs match, the user must give a more specific name.

### Section 3 — Entry and Index

Think of the index as a table with one row per staged file. `Entry` is a row. Its fields hold timestamps, device and inode numbers, mode, owner, group and size. Its ID identifies the exact blob staged. The path is the key in `Index::files`, a `map<string, Entry>`. Using the same key again replaces the old row.

`Index::stage` reads a regular file, saves a blob and fills in the row. `Index::save` writes the table to disk. The constructor reads it back for the next invocation. Without this step each terminal command would lose the previous command's staged information when it exited.

The first four bytes spell `DIRC`. Next come the version and entry count. Each row contains the fixed fields, 20 bytes of ID, two flag bytes, a filename and a zero terminator. Padding takes each row to a multiple of eight bytes. `putNumber` and `getNumber` write and read multi-byte integers in big-endian order. For example, a four-byte count of two occupies `00 00 00 02`. RIZZ appends a SHA-1 checksum. The supplied Python writer omitted that trailer, so the reader also accepts an index ending exactly after its last padded entry.

### Section 4 — Turning staged paths into history

`Leaf` represents one named tree entry. Its mode tells us whether it represents a directory, ordinary file or another supported listing type. `Tree::parse` reads each mode, filename and raw ID. `Tree::encode` sorts entries in Git's tree order and produces the bytes for an object of type tree. Each tree covers exactly one directory.

`Folder` helps build trees from flat index paths. Suppose the index contains `main.cpp` and `src/menu.cpp`. Inserting the first path adds a file to the root. Inserting the second creates a child folder called src and places menu.cpp there. `Folder::save` saves the child first, then places its ID beside the name src in the root tree. The parent needs the child's ID before it can calculate its own.

`headers` collects matching fields from a commit or annotated tag, stopping at the blank line before its message. A commit can have several parent fields. `header` expects exactly one match. `findType` follows a tag to its target, or a commit to its root tree when a command asks for a tree.

`configHome` locates user configuration. `author` reads simple name and email settings, applies a classroom fallback when needed and adds a timestamp. `commit` builds the root tree from the index, includes the previous HEAD as its parent when present, writes the commit, then updates the reference. On the first commit it leaves out the parent field. Editing diary.txt between add and commit changes none of those staged IDs.

`logHistory` follows parent links recursively. A set records the IDs printed so far. This avoids visiting shared history twice. Output is Graphviz DOT text. Each node is a commit and each arrow goes from a child to its parent.

### Section 4 continued — One walk, three behaviours

`walk` visits each entry in a tree and descends into child directories when requested. The parameter `TreeVisitor&` is a reference to an abstract base class. Its pure virtual method, `visit`, describes the operation each child class must implement.

| Class implementing visit | What happens to a file entry | Who uses it |
| --- | --- | --- |
| `Listing` | Prints its mode, type, ID and path | `ls-tree` |
| `Snapshot` | Adds its path and ID to a map | `status` |
| `Export` | Reads the blob and writes its contents to the destination | `checkout` |

This is **inheritance** because the three classes derive from TreeVisitor. It is **runtime polymorphism** because the same line, `visitor.visit(leaf, path)`, invokes the implementation belonging to the object passed to walk. The `override` keyword lets the compiler catch a child method whose signature does not match.

`checkout` checks the destination before constructing an Export object. It requires an empty or new directory. HEAD and the index still refer to your current work. This matches the export-style checkout in the supplied Python program.

### Section 5 — IgnoreRules and status

`IgnoreRules` loads staged .gitignore blobs, the repository's info/exclude file and the user's global Git ignore file when present. `ignores` searches from a file's nearest directory upward, then tries the fallback rules. `match` interprets comments, wildcard patterns and the leading ! used to include a matching path. Like the Python source, the matcher compares patterns with the repository-relative pathname. Stage .gitignore before demonstrating it.

`status` makes two different comparisons. First it compares the HEAD snapshot with the index to find staged additions, modifications and deletions. Then it compares each staged blob ID with a fresh hash of the working file, and walks the working directory to find untracked files. In our example diary.txt appears under both staged additions and unstaged modifications before the first commit. Those messages describe two different comparisons.

### Section 6 — Getting from a typed command to a method

| Symbol | Its job |
| --- | --- |
| `listRefs`, `tag` | Enumerate reference files or create one. A lightweight tag points directly to its target. An annotated tag points to a separate tag object. |
| `Arguments` | Owns the command's arguments. `flag` consumes a switch, `option` takes a named value and `paths` checks the number of remaining arguments. |
| `WriteLock` | Creates a lock directory for index and reference edits. Its destructor releases the lock when the command exits normally or throws. This is object lifetime managing a resource, also called RAII. |
| `editIndex` | Coordinates add and rm, checks the complete path list and saves the resulting index. rm removes the working copy as well as its staged row. |
| `help`, `run`, `main` | Display usage, route each command and convert exceptions into a readable message and a nonzero exit code. |

For an oral explanation of composition, point to `Repository::objects`. A repository **has an** ObjectStore. For inheritance, point to Listing. A Listing **is a** TreeVisitor. The choice expresses two different relationships.

## Python to C++ command map

| Command | C++ starting point after run | What to observe |
| --- | --- | --- |
| init | Repository::init | HEAD exists before the first commit |
| hash-object | Object::encode or ObjectStore::save | Without -w it can calculate an ID outside a repository |
| cat-file | findType and ObjectStore::load | A blob prints the original bytes |
| add | editIndex and Index::stage | Explicit regular file paths enter the index |
| ls-files | Index constructor | --verbose includes metadata and the staged ID |
| commit | commit and Folder::save | The current reference moves to the new commit |
| ls-tree | walk and Listing | -r includes paths inside child trees |
| checkout | checkout and Export | An empty destination receives the snapshot |
| log | logHistory | A second commit introduces a parent arrow |
| tag | tag or listRefs | -a creates an annotated tag |
| show-ref | listRefs | Prints loose reference IDs and names |
| rev-parse | Repository::resolve and findType | --wyag-type tree HEAD returns the root tree ID |
| check-ignore | IgnoreRules::ignores | Each matching argument is printed |
| status | status and Snapshot | Distinguishes staged, unstaged and untracked work |
| rm | editIndex | Removes a tracked file and stages its deletion |

## Scope and design choices

Use a fresh practice repository with regular files. RIZZ follows the reference's 15-command scope, including its simple ignore matching and loose SHA-1 objects. It stages explicit files, writes ordinary files with mode 100644 and exports blob contents as regular files. Packed objects and references, remote operations, conflicted index entries, branch creation and merge are outside the implementation. Use one tool at a time when writing to the repository. RIZZ's writer lock coordinates RIZZ processes. Run checkout and rm on disposable examples when practising.

The C++ port uses one Object value for the four on-disk types. The meaningful inheritance example is the tree visitor. It also handles first commits without a parent, writes valid detached HEAD updates, and adds the standard index checksum. These changes address concrete edge cases in the reference.

## Short viva practice

1. **Why can I delete a working file after committing and still recover it?** The commit reaches a tree that still points to the stored blob.
2. **If I rename a file without editing it, which ID can stay the same?** Its blob ID. The tree changes because it records the filename.
3. **Why can two commits share a tree?** If the same staged paths, modes and contents are committed twice, their trees are identical. The second commit still has a different parent field.
4. **What does a branch store?** Its loose reference file stores the ID of its current commit. The commit stores the root tree ID.
5. **Where would you set a breakpoint to see polymorphism?** At visitor.visit inside walk. Run ls-tree and checkout on the same commit and inspect which override executes.

The supplied libwyag.py is part of *Write yourself a Git!* by Thibault Polge, copyright 2018–2023. RIZZ adapts that work under GPL-3.0-or-later. The license text is in COPYING.
