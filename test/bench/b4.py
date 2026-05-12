# Python 3 transcription of b4 (variable r/m/w, 50 M iterations).
# Run: python3 b4.py
# Uses a 1-element list to force list-load and list-store per
# iteration, matching the Forth `v @ 1 + v !` heap r/m/w pattern and
# the Lua 1-element-table transcription.
v = [0]
for i in range(50000000):
    v[0] = v[0] + 1
