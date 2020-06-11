# park-lang
Park programming language

1. Install docker
2. Clone this repo
3. ```cd park-lang```
4. ```./park examples/hello.prk```

## Hello World 
```./park examples/hello.prk```
```javascript
function main()
{
    print("Hello World!")
}
```

## Basics
```./park examples/basics.prk```
```javascript
function main()
{
    let a = 1 /* integer */
    let b = 2 
    let c = true /* bool */
    let d = false 
    let e = "Hello" /* string */
    let f = "World!"

    print(a + b)
    print(c == d)
    print(e + " " + f)
}
```

## Functions
```./park examples/functions.prk```
```javascript
function sum(a, b) /* defining a function */
{
    return a + b
}

function main() /* main entry function */
{
    print(sum(10, 20)) /* calling a function */

    times(10, (n) => { /* anonymous function passed as argument */
        print(n)
    })
}
```

## Loops and Recursion
There is currently direct support for loops (for, do, while etc) in the language.
The ```recur``` statement can be used to make tail recursive calls to create
loops
```./park examples/loops.prk```
```
function loop(n)
{
    if(n == 0) {
        return 0
    }
    else {
        print(n)
        recurs (n - 1) /* tail recursive call to current function */
    }
}

function main()
{
    loop(10) 
}
```
