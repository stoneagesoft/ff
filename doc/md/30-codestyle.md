# Coding Style Guidelines

## Language Standard

The codebase targets **ISO C17** (`-std=c17` on GCC/Clang, `/std:c17` on MSVC). Compiler-specific extensions are not used outside of clearly marked platform-shim files. CMake should enforce this:

```cmake
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
```

Consequences referenced elsewhere in this document:

  * `<stdbool.h>` is available and required for `bool` / `true` / `false` (see *NULL, false and 0*).
  * Use `_Static_assert` (or `static_assert` after `#include <assert.h>`) for compile-time invariants rather than negative-array tricks.
  * `_Generic`, anonymous struct/union members, and designated initializers are allowed.
  * Variable-length arrays (VLAs) are permitted by C17 but are **not** to be used in this codebase — prefer fixed-size buffers or heap allocation.


## Line Ends

All source code files should use Unix line ends.


## Indentation

Use spaces, not tabs. Tabs should only appear in files that require them for semantic meaning, like `Makefiles`. The indent size is 4 spaces.

Right

```c
int main()
{
    return 0;
}
```

Wrong

```c
int main()
{
  return 0;
}
```


A case label should be indented from its switch statement. The case statement is indented, too.
Add one empty line between cases.

Right

```c
switch (condition)
{
    case foo_condition:
    case bar_condition:
        i++;
        break;

    default:
        i--;
}
```

Wrong

```c
switch (condition)
{
case foo_condition:
case bar_condition:
    i++;
    break;
default:
    i--;
}
```

Boolean expressions at the same nesting level that span multiple lines should have their operators on the left side of the line instead of the right side.

Right

```c
if (attr->name() == src_attr
        || attr->name() == lowsrc_attr
        || (attr->name() == usemap_attr && attr->value().dom_string()[0] != '#'))
    return;
```

Wrong

```c
if (attr->name() == src_attr ||
    attr->name() == lowsrc_attr ||
    (attr->name() == usemap_attr && attr->value().dom_string()[0] != '#'))
    return;
```


## Spacing

Do not place spaces around unary operators.

Right

```c
i++;
```

Wrong

```c
i ++;
```

Do place spaces around binary and ternary operators.

Right

```c
y = m * x + b;
f(a, b);
c = a | b;
return condition ? 1 : 0;
```

Wrong

```c
y=m*x+b;
f(a,b);
c = a|b;
return condition ? 1:0;
```

Do not place spaces before comma and semicolon.

Right

```c
for (int i = 0; i < 10; ++i)
    do_something();

f(a, b);
```

Wrong

```c
for (int i = 0 ; i < 10 ; ++i)
    do_something();

f(a , b) ;
```

Place spaces between control statements and their parentheses.

Right

```c
if (condition)
    do_it();
```

Wrong

```c
if(condition)
    do_it();
```

Do not place spaces between a function and its parentheses, or between a parenthesis and its content.

Right

```c
f(a, b);
```

Wrong

```c
f (a, b);
f( a, b );
```


## Line Breaking

Each statement should get its own line.

Right

```c
x++;
y++;
if (condition)
    do_it();
```

Wrong

```c
    x++; y++;
    if (condition) do_it();
```

An `else` statement should go on the next line after a preceding close brace if one is present, else it should line up with the `if` statement.

Right

```c
if (condition)
{
    ...
}
else
{
    ...
}

if (condition)
    do_something();
else
    do_something_else();

if (condition)
    do_something();
else
{
    ...
}
```

Wrong

```c
if (condition)
{
    ...
} else {
    ...
}

if (condition) do_something(); else do_something_else();

if (condition) do_something(); else {
    ...
}
```

An `else if` statement should be written as an if statement when the prior if concludes with a return statement.

Right

```c
if (condition)
{
    ...
    return someValue;
}
if (condition)
{
    ...
}
```

Wrong

```c
if (condition)
{
    ...
    return someValue;
}
else if (condition)
{
    ...
}
```


## Braces


Place each brace on its own line.

Right

```c
int main()
{
    ...
}

struct my_struct
{
    ...
};

for (int i = 0; i < 10; ++i)
{
    ...
}
```

Wrong

```c
int main() {
    ...
}

struct my_class {
    ...
};
```

One-line control clauses should not use braces unless comments are included or a single statement spans multiple lines.

Right

```c
if (condition)
    do_it();

if (condition)
{
    // Some comment
    do_it();
}

if (condition)
{
    my_function(really_long_param1, really_long_param2, ...
                really_long_param5);
}
```

Wrong

```c
if (condition)
{
    do_it();
}

if (condition)
    // Some comment
    do_it();

if (condition)
    my_function(really_long_param1, really_long_param2, ...
                really_long_param5);
```

Control clauses without a body should use empty braces.

Right

```c
for ( ; current; current = current->next) {}
```

Wrong

```c
for ( ; current; current = current->next);
```


## NULL, false and 0

The null pointer value should be written as NULL.

Boolean values should be written as `true` and `false` with `stdbool.h` included.

Tests for true/false, null/non-null, and zero/non-zero should all be done without equality comparisons.

Right

```c
if (condition)
    do_it();

if (!ptr)
    return;

if (!count)
    return;
```

Wrong

```c
if (condition == true)
    do_it();

if (ptr == NULL)
    return;

if (count == 0)
    return;
```


## File Names

Every non-trivial entity represented with a `struct` should be stored in three files. If the struct's name is `entity`, the file with public declarations is `entity.h`. Internal declarations (private struct fields, file-local helper prototypes shared between translation units of the same module) go to `entity_p.h`. Implementations go to `entity.c`. Symbols local to a single translation unit use `static` and stay in the `.c` file.


## Names

Use capital SNAKE_CASE for a macro or enum's members. Use snake_case in a struct, function, or variable name.

Right

```c
struct data;
size_t buffer_size;
struct html_document;
const char *mime_type();
```

Wrong

```c
struct Data;
size_t buffer_size;
struct HTMLDocument;
String MIMEType();
```

Use full words, except in the cases where an abbreviation would be more canonical and easier to understand.

Right

```c
size_t char_size;
size_t len;
short tab_index; // More canonical
```

Wrong

```c
size_t ch_size;
size_t l;
short tabulation_index; // bizarre
```

enums, structs and other typedefs except function pointers should use `_t` suffix in their names.

Right

```c
typedef struct string
{
    ...
} string_t;
```

Wrong

```c
typedef struct string
{
    ...
} string;
```

Precede boolean values with words like "is" and "did".

Right

```c
bool is_valid;
bool did_send_data;
```

Wrong

```c
bool valid;
bool sent_data;
```

Precede setters with the word "set". Use bare words for getters. Setter and getter names should match the names of the variables being set/gotten.

Right

```c
void set_count(size_t count);
size_t count();
```

Wrong

```c
void set_count(size_t count);
size_t get_count();
```

Precede getters that return values through out arguments with the word "get".

Right

```c
void get_inline_box_and_offset(inline_box *box, int *caret_offset);
```

Wrong

```c
void inline_box_and_offset(inline_box *box, int *caret_offset);
```

Do not use descriptive verbs in function names.

Right

```c
bool to_ascii(short *a, size_t b);
```

Wrong

```c
bool convert_to_ascii(short *a, size_t b);
```

Never omit variable names in function declarations.

Right

```c
void set_count(size_t count);

void do_something(script_execution_context_t *context);
```

Wrong

```c
void set_count(size_t);

void do_something(script_execution_context_t *);
```

Prefer enums to bools on function parameters if callers are likely to be passing constants, since named constants are easier to read at the call site. An exception to this rule is a setter function, where the name of the function already makes clear what the boolean is.

Right

```c
do_something(something, ALLOW_FOO_BAR);
paint_text_with_shadows(context, ..., text_stroke_width > 0, is_horizontal());
set_resizable(false);
```

Wrong

```c
do_something(something, false);
set_resizable(NOT_RESIZABLE);
```

`#defined` constants should use all uppercase names with words separated by underscores.

Macros that expand to function calls or other non-constant computation: these should be named like functions, and should have parentheses at the end, even if they take no arguments (with the exception of some special macros). Note that usually it is preferable to use an inline function in such cases instead of a macro.

Right

```c
#define WB_STOP_BUTTON_TITLE() \
        localized_string("Stop", "Stop button title")
```

Wrong

```c
#define wb_stop_button_title \
        localized_string("Stop", "Stop button title")

#define WBStopButtontitle \
        localized_string("Stop", "Stop button title")
```

`#pragma once` should be used instead of `#define` -- `#ifdef` "header guards".

Right

```c
#pragma once
```

Wrong

```c
#ifndef __HTML_DOCUMENT_H__
#define __HTML_DOCUMENT_H__
...
#endif
```


## Pointers

Pointer types should be written with a space between the type and the `*` (so the `*` is adjacent to the following identifier if any).

Right

```c
image_t *svg_styled_element_do_something(svg_styled_element_t *self, paint_info_t *info)
{
    svg_styled_element_t *element = svg_styled_element_node(self);
    const kc_dash_array_t *dashes = svg_styled_element_dash_array(self);
    ...
}
```

Wrong

```c
image_t* svg_styled_element_do_something(svg_styled_element_t* self, paint_info_t* info)
{
    svg_styled_element_t* element = svg_styled_element_node(self);
    const kc_dash_array_t* dashes = svg_styled_element_dash_array(self);
    ...
}
```


## #include Statements

All implementation files must #include the primary header first. So for example, `node.c` should include `node.h` first, before other files. This guarantees that each header's completeness is tested. This also assures that each header can be compiled without requiring any other header files be included first.

Other `#include` statements should be in sorted order (case sensitive). Don't bother to organize them in a logical order.

Right

```c
// html_div_element.c
#include "html_div_element.h"

#include "attribute.h"
#include "html_element.h"
#include "qualified_name.h"
```

Wrong

```c
// html_div_element.c
#include "html_element.h"
#include "html_div_element.h"
#include "qualified_name.h"
#include "attribute.h"
```

Includes of system headers must come after includes of other headers.

Right

```c
// connection_qt.c
#include "argument_encoder.h"
#include "process_launcher.h"
#include "web_page_proxy_message_kinds.h"
#include "work_item.h"

#include <assert.h>
```

Wrong

```c
// connection_qt.c
#include "argument_encoder.h"
#include "process_launcher.h"
#include <assert.h>
#include "web_page_proxy_message_kinds.h"
#include "work_item.h"
```


## Types

You can omit `int` when using `unsigned` modifier. Do not use `signed` modifier. Use `int` by itself instead.

Right

```c
unsigned a;
int b;
```

Wrong

```c
unsigned int a; // Doesn't omit "int".
signed b; // Uses "signed" instead of "int".
signed int c; // Doesn't omit "signed".
```


## Comments

Use only one space before end of line comments and in between sentences in comments.

Right

```c
f(a, b); // This explains why the function call was done. This is another sentence.
```

Wrong

```c
int i;    // This is a comment with several spaces before it, which is a non-conforming style.
double f; // This is another comment.  There are two spaces before this sentence which is a non-conforming style.
```

Make comments look like sentences by starting with a capital letter and ending with a period (punctation). One exception may be end of line comments like this `if (x == y) // false for NaN`.

Use `\bug` to denote items that need to be addressed in the future.

Right

```c
draw_jpg(); ///< \bug Make this code handle jpg in addition to the png support.
```

Wrong

```c
draw_jpg(); // FIXME: Make this code handle jpg in addition to the png support.

draw_jpg(); // TODO: Make this code handle jpg in addition to the png support.
```

## ++ and --

Use prefixed `++` and `--` operators in stand-alone statements.

Right

```c
++i;
--j;
```

Wrong

```c
i++;
j--;
```
