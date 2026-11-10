Vendored from [MD4C v0.6.0](https://github.com/mity/md4c/tree/v0.6.0),
commit `7fc1815a5eeba2af7d6120a76202bf59f3b6e6e4`. Linewren compiles the parser
only; the license is in [LICENSE.md](LICENSE.md).

The local changes make the existing UTF-16 path work with C11 types and
literals outside Windows, and report source ranges for recognized Markdown
delimiters that the renderer API normally omits. Linewren uses those UTF-16
offsets directly with Apple's MLTE text engine. The optional
`MD_FLAG_LINEWREN_EDIT_LISTS` flag also keeps an empty nested `- ` at its
parent's content column from being parsed as a Setext heading underline.
