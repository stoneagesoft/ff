-- Lua transcription of b4 (variable r/m/w, 50 M iterations).
-- Run: lua5.4 b4.lua
-- Uses a 1-element table to force table-load and table-store per
-- iteration, matching the Forth `v @ 1 + v !` heap r/m/w pattern.
local v = {0}
for i = 1, 50000000 do v[1] = v[1] + 1 end
