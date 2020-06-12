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
There is currently no syntax for loops (for, do, while etc) in the language.
Instead the ```recur``` statement is used to perform loops. The ```recur``` statement
makes a recursive call to the current function without growing the stack.
The ```recur``` statement can only be used in a tail position.

```./park examples/loops.prk```
```javascript
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

## Fibers and Channels
The language supports lightweight threads named Fibers. Fibers are scheduled 
M:N on actual OS threads.

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
