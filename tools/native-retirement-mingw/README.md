# Native-retirement MinGW support

`vadefs.h` is the project-owned MinGW varargs adapter used by the frozen
native-retirement dependency closure. Keep its byte identity synchronized with
the checked-in dependency descriptor and receipt; do not replace it with an
ambient host SDK header.

## Acceptance scheduling

Both the native-retirement contract and complete evidence workflows include
this directory in their pull-request path filters. A change here therefore
requires both gates.

Final acceptance after compiler- or contract-affecting base drift must validate
GitHub's exact pull-request integration revision, or an explicitly selected
workflow-dispatch revision. The retained evidence must bind the candidate
commit, tree, compiler binary, complete census partition, and all six strict
native differential lanes. Ordinary push CI, an earlier branch-head run, or a
clean-candidate result without clean acceptance is not a substitute.
