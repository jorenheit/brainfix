# Brainfix v2.0
This is a reboot of a little project I did as a student. BrainFix is a compiler that takes a C-style
language (although the syntax slowly evolved to something very close to Javascript for some reason) and compiles this
into [BrainF*ck](https://esolangs.org/wiki/Brainfuck), an esoteric programming language consisting of only 8 operations.

To run the resulting Brainf*ck code, you can use any third party utility that does the job. However, a simple
interpreter `bfint` is included in the project, which will be built along with the `bfx` compiler.

## Bisonc++ and Flexc++
The lexical scanner and parser that are used to parse the Brainfix language are generated by [Bisonc++](https://fbb-git.gitlab.io/bisoncpp/)
and [Flexc++](https://fbb-git.gitlab.io/flexcpp/) (not to be confused with bison++ and flex++). This software is readily available in the
Debian repositories, but not necessarily needed to build this project. The sourcefiles generated by these programs, based on the current grammar and lexer specifications, are included in the source-folder.

## Building
To build the project, simply cd into the src folder and call make:

```
cd src
make
```

To let bisonc++ and flexc++ regenerate the sourcecode for the scanner and parser, run

```
make regenerate
make
```

To copy the resulting binaries to `/usr/local/bin`, run as root:

```
make install
```

## Usage
### Using the `bfx` executable
Building the project results will produce the compiler executable `bfx`. The syntax for invoking the compiler can be inspected by running `bfx -h`:

```
$ bfx -h
Usage: ./bfx [options] [target(.bfx)]
Options:
-h                  Display this text.
-t [Type]           Specify the number of bytes per BF-cell, where [Type] is one of
                    int8, int16 and int32 (int8 by default).
-I [path to folder] Specify additional include-path.
                    This option may appear multiple times to specify multiple folders.
-o [file, stdout]   Specify where the generate BF is output to.

Example: ./bfx -o program.bf -I ../std/ -t int16 program.bfx
```

### Using the `bfint` executable
To run the resulting BF, call the included interpreter or any other utility that was designed to run or compile BF. When using the included `bfint` interpreter, its syntax can be inspected by running `bfint -h`:

```
$ bfint -h
Usage: bfint [options] [target(.bf)]
Options:
-h                  Display this text.
-t [Type]           Specify the number of bytes per BF-cell, where [Type] is one of
                    int8, int16 and int32 (int8 by default).
-n [N]              Specify the number of cells (30,000 by default).
-o [file, stdout]   Specify where the generate BF is output to (defaults to stdout).

Example: bfint -t int16 -o stdout program.bf
```
### The type of a BrainF\*ck cell
The type of the BF cell that is assumed during compilation with `bfx` can be specified using the `-t` option and will specify the size of the integers on the BF tape. By default, this is a single byte (8-bits). Other options are `int16` and `int32`. All generated BF-algorithms work with any of these architectures, so changing the type will not result in different BF-code. It will, however, allow the compiler to issue a warning if numbers are used throughout the program that exceed the maximum value of a cell. The same flag can be specified to `bfint`. This will change the size of the integers that the interpreter is operating on. For example, executing the `+` operation on a cell with value 255 will result in overflow (and wrap around to 0) when the interpreter is invoked with `-t int8` but not when it's invoked with `-t int16`. 

### Example: Hello World
Every programming language tutorial starts with a "Hello, World!" program of some sort. This is no exception:

```javascript
// File: hello.bfx
include "std/io.bfx"

function main()
{
    println("Hello, World!");
}
```

The program starts with an end-of-line comment (C-comment blocks between `/*` and `*/` are also allowed) and then
includes the IO-library which is included with this project. This exposes some basic IO-facilities the sourcefile.

Next, the main-function is defined. Every valid BFX-program should contain a `main` function which takes no arguments
and does not return anything. The order in which the functions are defined in a BFX-file does not matter; the compiler
will always try to find main and use this as the entrypoint.

In `main()`, the function `println()` from the IO library is called to print the argument and a newline to the console.
Let's try:

```
$ bfx -o hello.bf hello.bfx
$ bfint hello.bf
$ Hello, World!
```

## Target Architecture
The compiler targets the canonical BrainF*ck machine, where cells are unsigned integers of the type specified as the argument to the `-t` flag. At the start of the program, it is assumed that all cells are zero-initialized and the pointer is pointing at cell 0. Furthermore, it is assumed that the tape-size is, for all intents and purposes, infinitely long. The produced BF consists of only the 8 classic BF-commands (no [extensions](https://esolangs.org/wiki/Extended_Brainfuck)):

| Command | Effect |
| --- | --- |
| `>` | Move pointer to the right. |
| `<` | Move pointer to the left. |
| `+` | Increase value pointed to by 1. |
| `-` | Decrease value pointed to by 1. |
| `[` | If current value is nonzero, continue. Otherwise, skip to matching `]` |
| `]` | If current value is zero, continue. Otherwise, go back to matching `[` |
| `.` | Output current byte to stdout |
| `,` | Read byte from stdin and store it in the current cell |


## Language
### Functions
A BrainFix program consists of a series of functions (one of which is called `main()`). Apart from global variable declarations, `const` declarations and file inclusion (more on those later), no other syntax is allowed at global scope. In other words: BF code is only generated in function-bodies.

A function without a return-value is defined like we saw in the Hello World example and may take any number of parameters. For example:

```javascript
function foo(x, y, z)
{
    // body
}
```

When a function has a return-value, the syntax becomes:

```javascript
function ret = bar(x, y, z)
{
    // body --> must contain instantiation of 'ret' !
}
```

It does not matter where a function is defined with respect to the call:
```javascript
function foo()
{
    let x = 31;
    let y = 38;

    let nice = bar(x, y); // works, even if bar is defined below
}

function z = bar(x, y)
{
    let z = x + y; // return variable is instantiated here
}
```

#### Value and Reference Semantics
By default, all arguments are passed by value to a function: every argument is copied into the local scope of the function. Modifications to the arguments will therefore have no effect on the corresponding variables in the calling scope.

```javascript
function foo()
{
    let x = 2;
    bar(x);

    // x == 2, still
}

function bar(x)
{
    ++x;
}
```

However, BrainFix supports reference semantics as well! Parameters prefixed by `&` (like in C++) are passed by reference to the function. This will prevent the copy from taking place and will therefore be faster than passing it by value. However, it also introduces the possibility of subtle bugs, which is why it's not the default mode of operation.

```javascript

function foo()
{
    let x = 2;
    bar(x);

    // x == 3 now
}

function bar(&x)
{
    ++x;
}

```


#### Passing array-elements by reference
When an array-element is accessed through the index-operator (more on arrays in the dedicated section below), the result of this expression is actually a temporary copy of the actual element. This is because the position of the BF-pointer has to be known at all times, even when the index is a runtime variable (for example determined by user-input). Therefore, when passing an array-element by reference to a function, a reference to the temporary copy is passed rather than the actual element. This limitation is easily side-stepped by passing both the array and the index seperately:

```javascript
function modify1(&x)
{
    ++x;
}

function modify2(&arr, i)
{
    ++arr[i];
}

function main()
{
    let [] str = "Hello";
    modify1(str[2]);   // str is still "Hello"
    modify2(str, 2);   // str is now "Hemlo"
}
```

#### Recursion
Unfortunately, recursion is not allowed in BrainFix. Most compilers implement function calls as jumps. However, this is not possible in BF code because there is no JMP instruction that allows us to jump to arbitrary places in the code. It should be possible in principle, but would be very hard to implement (and would probably require a lot more memory to accomodate the algorithms that could make it happen). Therefore, the compiler will throw an error when recursion is detected.

### Variable Declarations
New variables are declared using the `let` keyword and can from that point on only be accessed in the same scope; this includes the scope of `if`, `for` and `while` statements. At the declaration, the size of the variable can be specified using square brackets. Variables without a size specifier are allocated as having size 1. It's also possible to let the compiler deduce the size of the variable by adding empty brackets to the declaration. In this case, the variable must be initialized in the same statement in order for the compiler to know its size. After the declaration, only same-sized variables can be assigned to eachother, in which case the elements of the right-hand-side will be copied into the corresponding left-hand-side elements. There is one exception to this rule: an single value (size 1) can be assigned to an array as a means to initialize or refill the entire array with this value;

```javascript
function main()
{
    let x;                // size 1, not initialized
    let y = 2;            // size 1, initialized to 2
    let [10] array1;      // array of size 10, not initialized
    let [] str = "Hello"; // the size of str is deduced as 5 and initialized to "Hello"

    let [y] array2;       // ERROR: size of the array must be a compiletime constant
    let [10] str2 = str;  // ERROR: size mismatch in assignment
    let [10] str3 = '0';  // OK: str3 is now a string of ten '0'-characters 
}
```

In the example above, we see how a string is used to initialize an array-variable. Other ways to initialize arrays all involve the `#` symbol to indicate an array-literal. In each of these cases, the size-specification can be empty, as the compiler is able to figure out the resulting size from its initializer.

```javascript
function main()
{
    let v1 = 1;
    let v2 = 2;
    let zVal = 42;

    let []x = #(v1, v2, 3, 4, 5); // initializer-list
    let []y = #[5];               // 5 elements, all initialized to 0
    let []z = #[5, zVal];         // 5 elements, all initialized to 42

    let [zVal] arr;               // ERROR: size-specifier is runtime variable
}
```

Size specifications must be known at compiletime; see the section on the `const` keyword below on how to define named compile-time constants.

#### Numbers
All numbers are treated as unsigned 8-bit integers. The compiler will throw an error on the use of the unary minus sign. A warning is issued when the program contains numbers that exceed the range of a single byte (255).

#### Indexing
Once an variable is declared (as an array), it can be indexed using the familiar index-operator `[]`. Elements can be both accessed and changed using this operator. BrainFix does not do any bounds-checking, so be careful to make sure that the indices are within the bounds of the array. Anything may happen when going out of bounds...

```javascript
function main()
{
    let [] arr = #(42, 69, 123);

    ++arr[0];     // 42  -> 43
    --arr[1];     // 69  -> 68
    arr[2] = 0;   // 123 -> 0

    arr[5] = 'x'; // WHOOPS! index out of bounds. Modifying other memory
}

```

#### `sizeof()`
The `sizeof()` operator (it's not really a function, as it's a compiler intrinsic and not defined in terms of the BrainFix language itself) returns the size of a variable and can be used, for example, to loop over an array (more on control-flow in the relevant sections below). 

```javascript
function looper(arr)
{
    for (let i = 0; i != sizeof(arr); ++i)
        printd(arr[i]);
}
```

#### Constants
BrainFix provides a simple way to define constants in your program, using the `const` keyword. `const` declarations can only appear at global scope. Throughout the program, occurrences of the variable are replaced at compiletime by the literal value they've been assigned. This means that `const` variables can be used as array-sizes (which is their most common usecase):

```javascript
const SIZE = 10;

function main()
{
    let [] arr1 = #[SIZE, 42]; 
    let [] arr2 = #[SIZE, 69];

    arr1 = arr2; // guaranteed to work, sizes will always match
}
```

### Operators
The following operators are supported by BrainFix:

| Operator | Description |
| --- | --- |
| `++`  |  post- and pre-increment |
| `--`   |  post- and pre-decrement |
| `+`    |  add |
| `-`    |  subtract |
| `*`    |  multiply |
| `/`    |  divide |
| `%`    |  modulo |
| `+=`   |  add to left-hand-side (lhs), returns lhs |
| `-=`   |  subtract from lhs, returns lhs |
| `*=`   |  multiply lhs by rhs, returns lhs |
| `/=`   |  divide lhs by rhs, returns lhs |
| `%=`   |  assigns the remainder of lhs / rhs and assigns it to lhs |
| `/=%`  |  divides lhs by rhs, returns the remainder |
| `%=/`  |  assigns the remainder to lhs, returns the result of the division |
| `&&`   |  logical AND |
| `\|\|` |  logical OR |
| `!`    |  (unary) logical NOT |
| `==`   |  equal to |
| `!=`   |  not equal to |
| `<`    |  less than |
| `>`    |  greater than |
| `<=`   |  less than or equal to |
| `>=`   |  greater than or equal to |

#### The div-mod and mod-div operators
Most of these operators are commonplace and use well known notation. The exception might be the div-mod and mod-div operators, which were added as a small optimizing feature. The BF-algorithm that is implemented to execute a division, calculates the remainder in the process. These operators reflect this fact, and let you collect both results in a single operation.

```javascript
function divModExample()
{
    let x = 42;
    let y = 5;

    let z = (x /=% y);

    // x -> x / y (8) and
    // z -> x % y (2)
}

function modDivExample()
{
    let x = 42;
    let y = 5;

    let z = (x %=/ y);
	
    // x -> x % y (2) and
    // z -> x / y (8)
}
```

### Flow
There are 4 ways to control flow in a BrainFix-program: `if` (-`else`), `switch`, `for` and `while`. Each of these uses similar syntax as to what we're familiar with from other C-like programming languages. Each of the flow-control mechanisms is illustrated in the example below:

```javascript
include "../std/io.bfx"

function main()
{
    let n = scand();
	
    // Print 'xoxoxo...' using a for-loop
    for (let i = 0; i < n; ++i)
    {
        if (i % 2 == 0)
            printc('x');  // defined in the std/io library
        else
            printc('o');
    }
    endl();               // newline (also from std/io)

    // Let n go to zero
    while (n > 0)
    {
        switch (--n)
        {
            case 0:
                 println("Done!");
            case 1:
                 println("Almost done!");
            default:
            {
                prints("Working on it: ");
                printd(n);
                endl();
            }
        }
    }
}
```

#### Switch Statements
In BrainFix, a `switch` statement is simply a syntactic alternative to an `if-else` ladder. Most compiled languages like C and C++ will generate code that jumps to the appropriate case-label (which therefore has to be constant expression), which in many cases is faster than the equivalent `if-else` ladder. In BrainF*ck, this is difficult to implement due to the lack of jump-instructions. For the same reason, a `break` statement is not required in the body of a case and it's therefore not possible to 'fall through' cases: only one case will ever be executed.

#### No return, break or continue?
BrainF*ck does not contain an opcode that let's us jump to arbitrary places in the code, which makes it very hard to implement `goto`-like features like `return`, `break` and `continue`. Instead, it is up to the programmer to implement conditional blocks using `if`, `if`-`else` or `switch` to emulate these jumps.

### File Inclusion
The compiler accepts only 1 sourcefile, but the `include` keyword can be used to organize your code among different  files. Even though the inner workings are not exactly the same as the C-preprocessor, the semantics pretty much are. When an include directive is encountered, the lexical scanner is simply redirected to that file and continues scanning the original file when it has finished scanning the included one. Currently, circular inclusions are not detected, and will simply crash the compiler.

### Standard Library
The standard library provides some useful functions in two categories: IO and mathematics. Below is a list of provided functions that you can use to make your programs interactive and mathematical. Also, in the "bool.bfx" headerfile, the constants `true` and `false` are defined to `1` and `0` respectively.

#### IO
All functions below are defined in `std/io.bfx`:

|     function     | description  |
| ---------------- | ------------ |
|   `printc(x)`	   | Print `x` as ASCII character |
|   `printd(x)`	   | Print `x` as decimal (at most 3 digits) |
|   `printd_4(x)`  | Print `x` as decimal (at most 4 digits) |
|   `prints(str)`  | Print string (stop at `\0` or end of the string) |
|   `println(str)` | Same as `prints()` but including newline |
|   `print_vec(v)` | Print formatted vector, including newline: `(v1, v2, v3, ..., vN)` |
|   `endl()`	   | Print a newline (same as `printc('\n')`) |
|   `scanc()`	   | Read a single byte from stdin |
|   `scand()`	   | Read at most 3 bytes from stdin and convert to decimal |
|   `scand_4()`	   | Read at most 4 bytes from stdin and convert to decimal |
|   `scans(buf)`   | Read string from stdin. `sizeof(buf)` determines maximum number of bytes to read |
|   `to_int(str)`  | Converts string to int (at most 3 digits) |
|   `to_int_4(str)`  | Converts string to int (at most 4 digits) |
|   `to_string(x)` | Converts int to string (at most 3 digits) |
|   `to_string_4(x)` | Converts int to string (at most 4 digits) |
|   `to_binary_str(x)` | Returns binary representation of `x` as a string |
|   `to_hex_str(x)` | Returns hexadecimal representation of `x` as a string |

##### Big Numbers
On the default architecture, where the cells are only 1 byte long, values can never grow beyond 255. It is therefore sufficient to assume that number will never grow beyond 3 digits. However, when the target architecture contains larger cells, the functions suffixed with `_4` can be used to extend some facilities to 4 digits. Printing and scanning even larger digits is also possible, but functions to the end are not provided by the standard library for the simple reason that these functions would be terribly slow and impractical.

#### Math
All functions below are defined in `std/math.bfx`:

|     function     | description  |
| ---------------- | ------------ |
|   `pow(x,y)`	   | Calculate x raised to the power y |
|   `square(x)`    | Calculate the square of x |
|   `sqrt(x)`	   | Calculate the square root of x, rounded down |
|   `factorial(n)` | Calculate n! (overflows for n > 5) |
|   `min(x,y)`     | Returns the minimum of x and y |
|   `max(x,y)`     | Returns the maximum of x and y |

### Direct Control With `__bf()` and `__movePtr()`
For even more control, the intrinsics `__bf(code)` and `__movePtr(identifier)` are provided. The first injects BF code directly, provided that this code has no net effect on the pointer position (which must be known to the compiler at all times). The latter moves the pointer to the address of the provided identifier.

The standard library uses the inline BF intrinsic in its definition of the `printc()` and `scanc()` functions. The language has no built-in facilities or operators that allow you to write or read bytes, so inline BF is used to make this happen:

```javascript
function printc(c)
{
    __movePtr(c);
    __bf(".");
}

function c = scanc()
{
    /* The argument to __movePtr() is not evaluated,
    so the return value has to be instantiated explicitly. */

    c; __movePtr(c);
    __bf(",");
}
```



