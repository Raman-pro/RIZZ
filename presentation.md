# Presenting RIZZ

**Format:** Two speakers, seven minutes, one terminal and one source file. Keep the terminal large enough for the teacher to read the output.

## Before class

Build the executable on your Mac in the folder containing rizz.cpp. Keep this terminal open. Each rehearsal gets a fresh directory, so an earlier export or commit cannot affect the demo.

```sh
clang++ -std=c++17 rizz.cpp -lz -o rizz
RIZZ="$PWD/rizz"
cd "$(mktemp -d)"
```

Open rizz.cpp in an editor. Bookmark `Index::stage`, the free function `commit`, and the call to `visitor.visit` inside `walk`. Pick who will type and who will speak so that you can discuss the output while it is still visible.

| Time | Speaker | What to show |
| --- | --- | --- |
| 0:00–0:45 | Student 1 | The question about editing after add |
| 0:45–2:15 | Student 1 | The working file and staging area |
| 2:15–3:45 | Student 2 | Save a commit, inspect it and recover the staged text |
| 3:45–5:00 | Student 2 | Make a second commit and follow its parent |
| 5:00–6:15 | Both | Two places where OOP helped |
| 6:15–7:00 | Both | Summarise the result and take questions |

## 1. Ask the question

**Student 1:** “We use Git in our projects, but we wanted to know what actually gets saved when we type add and commit. So we studied a small Python implementation and built RIZZ in C++. Suppose I stage the word Monday, change it to Tuesday, and then commit. Which word do you think we can recover?”

Give the teacher a moment to answer. Leave the question open until the export.

## 2. Make two versions of one file

```sh
"$RIZZ" init
printf 'Monday\n' > diary.txt
"$RIZZ" add diary.txt
printf 'Tuesday\n' > diary.txt
"$RIZZ" status
```

**Student 1:** Point to diary.txt appearing twice. “The index has a new file ready for the next commit. The working file has another edit. Both lines can be true because RIZZ compares three places: the last commit, the index and the working directory.”

Hand over with “We now have two versions. Let's see which one the commit uses.”

## 3. Save the first snapshot and recover it

```sh
"$RIZZ" commit -m "Save Monday"
"$RIZZ" cat-file commit HEAD
"$RIZZ" ls-tree HEAD
"$RIZZ" tag monday
"$RIZZ" checkout monday recovered
cat recovered/diary.txt
cat diary.txt
```

**Student 2:** “The first cat prints Monday. The second prints Tuesday. The tree line above contains the name diary.txt and the ID of its blob. The commit contains the ID of that tree. Checkout followed those links and wrote the stored bytes into recovered.”

Explain why the first commit output has a tree line and no parent line. Leave recovered untracked and add only diary.txt in the next step.

## 4. Show what changes on the second commit

```sh
"$RIZZ" add diary.txt
"$RIZZ" commit -m "Save Tuesday"
"$RIZZ" cat-file commit HEAD
"$RIZZ" rev-parse monday
"$RIZZ" log
```

**Student 2:** Compare the parent ID in the new commit with the ID printed for monday. They should match. “The first snapshot is still there. The branch now points to the second commit, which records the first one as its parent.” In the log output, point to the arrow from the new commit to the old one. The IDs vary with your author settings and commit time.

## 5. Show OOP in the code

**Student 1:** Open Index::stage. “This method reads the file and asks the repository's ObjectStore to save a blob. Index looks after the staged entry. ObjectStore knows how to hash, compress and store the bytes. We can explain and test those jobs separately.” Point to the call to save.

**Student 2:** Open walk. “This line calls visit through a base-class reference. During ls-tree, Listing prints a name. During checkout, Export writes the same entry to disk. The tree-reading loop stays in one place.” Show the two override methods.

## 6. Close and answer questions

**Closing line:** “We started by asking which version gets committed. We could answer it by recovering Monday, tracing its blob back to the tree and reading the commit. Implementing those steps helped us understand both Git's storage and where C++ classes help.”

| If the teacher asks | A short answer |
| --- | --- |
| Did you call Git to make the commit? | RIZZ writes the blob, tree, index, commit and branch reference itself. We used Git separately to check the result. |
| Why did you choose this project for OOP? | A repository owns an object store, the index manages staged entries, and three visitors share a tree traversal while doing different work. |
| Does it merge branches? | The Python reference covers 15 local commands. We implemented those. A merge would also require choosing a common ancestor and handling conflicting edits. |
| Why are the two blob IDs different? | Monday and Tuesday have different bytes, so their encoded blob contents hash differently. |
| How would you demonstrate data reuse? | Stage a second filename containing Monday. Both entries will show the same blob ID in ls-files --verbose. |

**If time is cut to three minutes**, use sections 1–3, show the visit call, then give the closing line. If asked for independent verification, run `git show monday:diary.txt` in the same directory. It should print Monday from the repository RIZZ created.
