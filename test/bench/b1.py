# Python 3 transcription of b1 (empty loop, 100 M iterations).
# Run: python3 b1.py
# Matches the Forth `1 drop` body with a trivial local store so the
# loop is not optimised to a no-op.
for i in range(100000000):
    x = 1
