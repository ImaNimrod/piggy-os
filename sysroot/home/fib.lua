function fibonacci(n) 
    return n < 2 and n or fibonacci(n - 1) + fibonacci(n - 2) 
end

for n = 1, 20 do
    io.write(fibonacci(n), "\n")
end
