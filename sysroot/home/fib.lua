function fib(n)
    return n < 2 and n or fib(n - 1) + fib(n - 2)
end

for n = 0, 30 do
    print(fib(n))
end