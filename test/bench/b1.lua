-- Lua transcription of b1 (empty loop, 100 M iterations).
-- Run: lua5.4 b1.lua
-- Matches the Forth `1 drop` body with a trivial local store; Lua's
-- `for ... do end` would otherwise be a single VM op with no body.
for i = 1, 100000000 do local x = 1 end
