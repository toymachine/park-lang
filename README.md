# park-lang
This page describes the Park programming language. 

It is a dynamically typed language that is inspired by Clojure (Immutability), Javascript (Syntax) and various languages that support lightweight threads (Erlang, Go, Stackless Python). 
Most of my focus has been on the runtime implementation and not so much on the syntax.
Initially I had considered a syntax more similar to Python but I found the Javascript/C style syntax easier to parse. 
Also it allows more naturally for multi-line lambda/closure syntax that fits better with the mostly functional style of the language.

1. Install docker
2. Clone this repo
3. ```cd park-lang```

From there you can play some of the examples:

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


## Container types

```./park examples/containers.prk```
```javascript
function main()
{
    /* containers types are immutable */

    let d = {} /* empty map (associative container) */

    let d1 = assoc(d, "a", 10)   /* put stuff in the map */
    let d2 = assoc(d1, "b", 20)

    print(d2) /* {a: 10, b: 20} */

    print(d2["a"]) /* 10 */
    print(get(d2, "b")) /* 20 */

    let v = [] /* empty vector */
    let v1 = conj(v, 10) /* add to the vector */
    let v2 = conj(v1, 20)

    print(v2) /* [10, 20] */
    print(v2[0]) /* 10 */
    print(get(v2, 1)) /* 20 */
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
There is currently no syntax for loops (for, do, while etc) in the language.
Instead the ```recur``` statement is used to perform loops. The ```recur``` statement
makes a recursive call to the current function without growing the stack.
The ```recur``` statement can only be used in a tail position.


```./park examples/loops.prk```
```javascript
function loop(n)
{
    print(n)

    if(n == 0) {
        return 0
    }
    else {
        recurs (n - 1) /* tail recursive call to current function */
    }
}

function main()
{
    loop(10) 
}
```


## Fibers
The language supports lightweight threads named Fibers. A Channel object can be used to communicate values between Fibers.
Fibers are scheduled M:N on a limited number actual OS threads. \
Fibers are small enough (currently around 2KB) so that you can have millions of them on a single machine.


```./park examples/channel.prk```
```javascript

function child(chan)
{
    print("child: i am child")
    sleep(1000)   /* sleep for a second */
    print("child: send to parent 1")
    send(chan, 1) /* would block if there is no receiver */
    sleep(1000)
    print("child: send to parent 2")
    send(chan, 2)
    sleep(1000)
    print("child: send to parent 3")
    send(chan, 3)
    print("child: done")
    exit() /* exits current fiber */
}

function main() {
    let chan = channel() /* create a channel to communicate between fibers */

    spawn(() => {    /* creates a new fiber that will execute the given function */
        child(chan)
    })

    print("parent: i am parent")
    print(recv(chan)) /* blocks on recv till somebody sends a value */
    print(recv(chan))
    print(recv(chan))
    print("parent: done")
}


```


Another way to share data between fibers is to use an ```atom```. An atom is a value that can be atomically changed by a fiber using
the ```swap``` function. 


```./park examples/atom.prk```
```javascript
const a = atom(0)

function main() {
    runpar(10, () => { /* Run the given (anonymous) function on 10 fibers concurrently */
        times(100000, () => { /* Execute the given function 10.000 times */
            swap(a, (v) => { /* Update the atom using given function. The current value is given as argument */
                return v + 1
              })  	
        })
    })
    print(deref(a)) /* 1 million (e.g. 10 fibers each concurrently incremented the atom 100.000 times */
}
```


An example of starting 100.000 fibers 


```./park examples/fiber.prk```
```javascript
function create_receiver(ch)
{
    let receiver = (acc) => {
        let i = recv(ch) /* block to receive msg */
        recurs(assoc(acc, i, i)) /* add it to my collection and loop */
    }

    spawn(() => {
        receiver({})
    })
}

function main()
{
    let n = 100000
    print("createing ", n, "fibers")
    let ch = channel()
    times(n, (n) => {
        create_receiver(ch)
    })
    print("sending ", n, " messages")
    times(n, (n) => {
        send(ch, n)
        sleep(0)
    })
    print("send done")
}
```



## Structs and Keywords
A ```struct``` is a user defined container type that has fields indexed by ```keyword```. A keyword is created by using the literal syntax ```$<identifier>```.


```./park examples/struct.prk```
```javascript
/* Define a struct with fields and their default values */
struct Foo {
  $a = 10  /* An identifier starting with a $ is a keyword. */
  $b = 20  
  $c = 10
}

function main()
{
  print(Foo) /* The struct itself */
  let foo = Foo(1, 2, 3) /* A struct is callable and yields an instance of the struct */
  print(foo)
  print(foo[$a], foo[$b], foo[$c]) /* Structs are indexed by using keywords */
  print($a) /* A keywords evaluates to itself */
  print($a(foo))  /* A keyword is also a callable that takes a struct and returns the field value */
}

/* TODO add 'dot' syntax a.b.c to mean c(b(a)). combined with 
keywords would allow field access like this foo.$a, foo.$b etc
as foo.$a would turn in to $a(foo) */
```

