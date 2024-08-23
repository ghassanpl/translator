# translator

Simple **translation/string interpolation library** and scripting language, similar to [Fluent](https://projectfluent.org/), but in **C++**, **fully embeddable**, and implementing a full-on (but simple to use) **scripting language**/syntax.

## Quick Example

```cpp
#include "translator.h"
translator::context ctx;
open_core_lib(ctx);
ctx.set_user_var("kills", 2);
auto result = ctx.interpolate("Killed [.kills] [ [.kills == 1] ? monster. : monsters. ]");
```
`result` will contain `"Killed 2 monsters."`

Calling `open_core_lib` is optional in general, but necessary for the `[] == []` and `[] ? [] : []` functions. 

There is no built-in syntax or operator precedence; everything is considered either a value or a function call.

## Other Examples

<table>
<thead>
<tr><th> 

[`Fluent` example](https://projectfluent.org/)

</th><th>

`translator` equivalent

</th></tr>
</thead>
<tr>
<td style="vertical-align: top">

```fluent
hello-user = Hello, {$userName}!
```

```fluent
shared-photos = {$userName} {$photoCount ->
	[one] added a new photo
	*[other] added {$photoCount} new photos
} to {$userGender ->
	[male] his stream
	[female] her stream
	*[other] their stream
}.
```

</td>
<td>

```st
Hello, [.userName]!
```

```zil
[.userName] [.photoCount 
	1? "added a new photo" 
	else ["added ", .photoCount, " new photos"]
] to [match .userGender
	with [male "his stream"]
	with [female "her stream"]
	default "their stream"
].
```

where `[] 1? [] else []` and `match [] with [+] default []` (and even `[] , [*]`) are user-provided/library functions.

</td>
</tr>
</table>

## Features

- Simple C++ API
- Simple C API that's easy to bind to other languages
- Fully extensible
- Easily embeddable in any C++17+ or C codebase
- Small codebase, easily modifiable for your own needs
- Can store any JSON values as variables
- Simple syntax understandable by non-programmers
- ... while still implementing a full Turing-complete scripting language
- And probably more, TODO fill me :)

## Function Call Syntax

`translator` uses a syntax that is a mix of Lips's lists and Smalltalk's infix/prefix notation. This means two basic function call forms are available:

```
[] infix [] notation []
```

and

```
prefix [] notation []
```

where `[]` represent call arguments.

Tokens are separated by spaces, except the delimiters: `[`, `]` and `,`.

`[` and `]` are list (JSON array) delimiters, everything else is space-delimited and either a string, number, true/false/null literal, or word, except for `,` which is always interpreted as a standalone word, even if right next to other word (i.e. `[a,b c]` is interpreted as an array of 4 elements: `a`, `,`, `b` and `c`). **"Words" are stored as regular JSON strings**.

If a word/string starts with `.` it's treated by the interpreter as a variable name. Evaluating such a word will try to retrieve a user-set variable.

### EBNF Syntax (approximate)
```ebnf
word   = /[^\s,\[\]]+/ | ','
value  = list | string | "true" | "false" | "null" | number | word
list   = '[' [value*] ']'
string = '"' /[^\"]*/ '"'
       | '\'' /[^\']*/ '\''
number = /* as parsed by std::from_chars */
```

## C++ API

### TODO

## C API

### TODO

## Roadmap

- [ ] Implement more parameter modifiers ('?' specifically)
- [ ] Consider switching to `boost::json` for better performance (needs measuring)
- [ ] The API needs more extensive querying functionality (should be trivial to add)
- [ ] Add more built-in functions
- [ ] More tests
- [ ] Caching of functions based on parameter names (to avoid searching the trees for the same function multiple times); probably as an option since it's going to be a trade-off between memory and speed
- [ ] Better error handling (currently, errors are just strings)
- [ ] Ability to fully opt-out of exceptions for error handling
- [ ] Consider making `bind_function` and `bind_macro` separate functions with different behaviors (`bind_function`-callbacks should be given already-evaluated args)
- [ ] Consider using a per-context `symbol` table to ease off on some memory pressures (strings everywhere)
- [ ] Consider moving away from JSON entirely and add a VERY SIMPLE value system (string, symbol, array, object, number, bool, null, error), perhaps even with GC-based memory management

## Rationale

The main goal of this project was to create a simple, embeddable, and extensible translation library that could be used in any C++ project. The idea was to create a system that could be used by non-programmers to write translations, but also to allow for more complex scripting if needed.

### Why a scripting language for translation?

See https://projectfluent.org/ for lots of reasons why translations are complicated. Sure, you could use a bespoke markup/description language like Fluent, but `translator`'s full scripting language allows for much more flexibility and power, while still providing basically the same syntax.

### Why JSON?

Code simplicity. `nlohmann::json` is a very simple and easy-to-use library, but can store 90% of what's needed to implement a generalized translation system.

### Why Simple and not Fast?

If you want speed, there are plenty of other libraries that are faster and more feature-rich. This library is meant to be simple to understand and modify, so that you can easily adapt it to your own needs.

## Notes on Performance

TLDR: Assume it's slow.

There is no (pre)compilation step - functions are searched every time a function call is evaluated. A tree-like structure is used to match them, so it should be relatively fast, but it's still a fully interpreted language (no bytecode or anything like that).

The system uses JSON values as the internal representation of its values. This makes the codebase very simple, but means that we're not using any sort of reference semantics, so all code has value semantics; you cannot pass any values around as reference, except by exploiting the variable system and passing around variable names.

This makes extensive use of the scripting system quite expensive both in time and memory usage. Again, the design goal was simplicity of implementation, so it was a trade-off.

Technically, evaluation could be made MUCH faster (there is nothing in the syntax/semantics preventing it), so you can treat this codebase as a proof-of-concept to build your own speedy version if you want to.

Easy places to gain performance:
- Treat some functions as special cases (e.g. `[] ? [] : []` or `[] == []`)
- Use a different internal representation of values (with better disambiguation between different value types (e.g. words vs strings, calls vs arrays, simple strings vs interpolated strings, strings vs errors, etc.))
- Use a garbage-collected memory allocation scheme (ala lisp)
- Store words differently than strings
