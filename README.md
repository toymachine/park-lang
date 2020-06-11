# park-lang
Park programming language

1. Install docker
2. Clone this repo
3. ```cd park-lang```
4. ```./park examples/hello.prk```

## Hello World
```javascript
function main()
{
    print("Hello World!")
}
```

## Basics
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
