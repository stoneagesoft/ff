-- Lua transcription of b3 (recursive fib(36)).
-- Run: lua5.4 b3.lua
local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end
fib(36)
