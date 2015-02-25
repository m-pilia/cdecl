cdecl - C gibberish into gibbered English
=========================================

This simple program is the implementation of a C declaration parser
suggested at the end of Chapter 5 in Kernighan and Ritchie's 
<i>The C Programming Language</i>, with some addings for C99 declarations and
more care for syntax errors.

The declarations are formalized in a simplified way, with a recursive
grammar composed by:
 - fdecl: a complete declarator (it may be a function, a variable etc.)
 - fdecll: a comma separated list of fdecl
 - decl: a declarator with no returning type (maybe a pointer)
 - ddecl: a direct declarator, with no returning type and no pointers

A BNF description of the grammar is the following:
````{.bnf}
    fdecl  ::= return_type decl

    fdecll ::= fdecl [fdecll]

    decl   ::= ["*"] decl |
               [qualifier] decl | 
               ddecl

    ddecl  ::= "(" decl ")" |
               ddecl "()" |
               ddecl "[" [size] "]" |
               identifier ddecl |
               ddecl "(" fdecll ")"
````

The recognition of these four nonterminal symbols is implemented in the
four homonymous functions.

Other facts are considered to detect syntax errors:
 - a parentheses opened before any identifier must be for gouping
    + and it cannot be followed by a type name
 - a parentheses opened after an identifier must be for function delclaration
    + and it must be void or followed by a type name
 - a square bracket cannot appear if there's no identifier before
 - a function cannot return an array nor a function

License
=======
The project is licensed under GPL 3. See [LICENSE](/LICENSE) file for the full
license. 
